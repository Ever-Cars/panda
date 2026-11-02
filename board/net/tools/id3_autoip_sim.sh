#!/usr/bin/env bash
#
# Bench stand-in for the VW ID.3 gateway's IPv4 link bring-up, replayed from
# id3_gateway_ipv4.pcapng, followed by a TCP echo test against the STM32 lwIP
# echo server.
#
# Timeline (seconds after the first DHCP Discover, as in the capture):
#    0.0 / 4.0 / 12.0   DHCP Discover (same xid, requests options 1 and 3 only)
#   15.3                ARP Probe for the chosen 169.254.x.x (sender IP 0.0.0.0)
#   16.35               ARP Announcement
# On a probe conflict a new random 169.254.x.x is chosen, like AutoIP does.
#
# The vehicle address is never configured on the host. The script answers ARP
# for it and runs a minimal TCP client over raw frames, so the host IP stack
# (and macOS's own link-local handling of the NIC) stays out of the test.
#
# Requires root (raw frames via BPF on macOS, AF_PACKET on Linux) and python3
# (standard library only).

set -euo pipefail

VEHICLE_IP=169.254.198.14
BOARD_IP=
PORT=7
WAIT=60
ROUNDS=1
FAST=0
IFACE=

usage() {
  cat <<EOF
Usage: $(basename "$0") -i IFACE [options]

  -i IFACE   host NIC wired to the board (e.g. en7 on macOS, eth0 on Linux)
  -a IP      vehicle link-local address (default $VEHICLE_IP, as in the capture)
  -b IP      board address; skips looking for the board
  -p PORT    board echo port (default $PORT)
  -t SECS    how long to look for the board (default $WAIT)
  -n COUNT   rounds of echo messages (default $ROUNDS)
  -f         fast: skip the ~15 s of DHCP Discovers before the ARP probe
  -h         show this help

The board is found from its own ARP traffic (e.g. its AutoIP announcement,
about 15 s after its link comes up) or by ARP-probing, every 2 s, the last
board address, link-local neighbours in the host ARP cache, and the lwIP
AutoIP address of any MAC heard on the link. So an already running board is
found too, as soon as it sends any frame or sits at a known address.
EOF
}

while getopts "i:a:b:p:t:n:fh" opt; do
  case $opt in
    i) IFACE=$OPTARG ;;
    a) VEHICLE_IP=$OPTARG ;;
    b) BOARD_IP=$OPTARG ;;
    p) PORT=$OPTARG ;;
    t) WAIT=$OPTARG ;;
    n) ROUNDS=$OPTARG ;;
    f) FAST=1 ;;
    h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done
[[ -n $IFACE ]] || { usage >&2; exit 2; }

if [[ $EUID -ne 0 ]]; then
  exec sudo PYTHON="${PYTHON:-$(command -v python3)}" "$0" "$@"
fi
PYTHON=${PYTHON:-python3}

log() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
die() { log "ERROR: $*" >&2; exit 1; }

case "$(uname -s)" in
  Darwin) OS=mac ;;
  Linux)  OS=linux ;;
  *)      die "unsupported OS $(uname -s)" ;;
esac

if [[ $OS == mac ]]; then
  MAC=$(ifconfig "$IFACE" 2>/dev/null | awk '/ether /{print $2; exit}')
else
  MAC=$(cat "/sys/class/net/$IFACE/address" 2>/dev/null || true)
  ip link set "$IFACE" up
fi
[[ -n $MAC ]] || die "no Ethernet interface named $IFACE"

# Link-local neighbours the host already knows on IFACE, as board candidates.
cached_neighbours() {
  if [[ $OS == mac ]]; then
    arp -an -i "$IFACE" 2>/dev/null | awk '/\(169\.254\./ && !/incomplete/ { gsub(/[()]/, "", $2); print $2 }'
  else
    ip -4 neigh show dev "$IFACE" 2>/dev/null | awk '$1 ~ /^169\.254\./ && !/FAILED|INCOMPLETE/ { print $1 }'
  fi
}

LAST_BOARD_FILE=/tmp/id3_autoip_sim.last_board
HELPER=$(mktemp "${TMPDIR:-/tmp}/id3_autoip_sim.XXXXXX")
BOARD_FILE=$(mktemp "${TMPDIR:-/tmp}/id3_autoip_board.XXXXXX")
WATCH_PID=

cleanup() {
  if [[ -n $WATCH_PID ]]; then kill "$WATCH_PID" 2>/dev/null || true; fi
  rm -f "$HELPER" "$BOARD_FILE"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

cat > "$HELPER" <<'PY'
import errno
import fcntl
import os
import random
import select
import socket
import struct
import sys
import time

BCAST = b'\xff' * 6
ZERO_MAC = b'\x00' * 6
ZERO_IP = b'\x00' * 4

# Payloads the tester sent in the capture, reused as realistic echo data.
DOIP_ROUTING_ACTIVATION_REQ = bytes.fromhex('02fd0005000000070e890000000000')
DOIP_DIAG_READ_F17C = bytes.fromhex('02fd8001000000070e89382822f17c')


def log(msg):
  print(time.strftime('[%H:%M:%S] ') + msg, file=sys.stderr, flush=True)


def parse_mac(text):
  return bytes(int(octet, 16) for octet in text.split(':'))


def fmt_mac(raw):
  return ':'.join('%02x' % b for b in raw)


class Link:
  """Raw Ethernet send/receive on one interface."""

  # <net/bpf.h> ioctls (macOS, 64-bit)
  BIOCGBLEN = 0x40044266
  BIOCSETIF = 0x8020426C
  BIOCIMMEDIATE = 0x80044270
  BIOCSHDRCMPLT = 0x80044275
  BIOCSSEESENT = 0x80044277

  def __init__(self, iface):
    self.bpf = not sys.platform.startswith('linux')
    if self.bpf:
      self.fd = self._open_bpf()
      fcntl.ioctl(self.fd, self.BIOCSETIF, struct.pack('16s16x', iface.encode()))
      fcntl.ioctl(self.fd, self.BIOCIMMEDIATE, struct.pack('I', 1))
      fcntl.ioctl(self.fd, self.BIOCSHDRCMPLT, struct.pack('I', 1))
      fcntl.ioctl(self.fd, self.BIOCSSEESENT, struct.pack('I', 0))
      self.blen = struct.unpack('I', fcntl.ioctl(self.fd, self.BIOCGBLEN, b'\0' * 4))[0]
    else:
      self.sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0003))
      self.sock.bind((iface, 0))
      self.fd = self.sock.fileno()

  @staticmethod
  def _open_bpf():
    for i in range(256):
      try:
        return os.open('/dev/bpf%d' % i, os.O_RDWR)
      except OSError as e:
        if e.errno != errno.EBUSY:
          break
    raise OSError('no free /dev/bpf device')

  def send(self, frame):
    frame = frame.ljust(60, b'\0')
    if self.bpf:
      os.write(self.fd, frame)
    else:
      self.sock.send(frame)

  def frames(self, timeout):
    deadline = time.monotonic() + timeout
    while True:
      left = deadline - time.monotonic()
      if left <= 0 or not select.select([self.fd], [], [], left)[0]:
        return
      if not self.bpf:
        yield self.sock.recv(65535)
        continue
      buf = os.read(self.fd, self.blen)
      off = 0
      while off + 18 <= len(buf):
        caplen, _, hdrlen = struct.unpack_from('=IIH', buf, off + 8)
        yield buf[off + hdrlen:off + hdrlen + caplen]
        off += (hdrlen + caplen + 3) & ~3


def arp_frame(sha, spa, tpa, op=1, tha=ZERO_MAC, dst=BCAST):
  return dst + sha + b'\x08\x06' + struct.pack('!HHBBH6s4s6s4s', 1, 0x0800, 6, 4, op, sha, spa, tha, tpa)


def parse_arp(frame):
  if len(frame) < 42 or frame[12:14] != b'\x08\x06':
    return None
  if struct.unpack_from('!HHBB', frame, 14) != (1, 0x0800, 6, 4):
    return None
  return struct.unpack_from('!H6s4s6s4s', frame, 20)


def checksum(data):
  if len(data) % 2:
    data += b'\0'
  s = sum(struct.unpack('!%dH' % (len(data) // 2), data))
  while s >> 16:
    s = (s & 0xffff) + (s >> 16)
  return ~s & 0xffff


def dhcp_discover_frame(mac, xid, ip_id):
  bootp = struct.pack('!BBBBIHH16x', 1, 1, 6, 0, xid, 0, 0x8000) + mac.ljust(16, b'\0') + bytes(192)
  bootp += b'\x63\x82\x53\x63' + bytes([53, 1, 1, 55, 2, 1, 3, 255])
  src, dst = ZERO_IP, b'\xff' * 4
  udp_len = 8 + len(bootp)
  udp = struct.pack('!HHHH', 68, 67, udp_len, 0) + bootp
  udp_csum = checksum(src + dst + struct.pack('!BBH', 0, 17, udp_len) + udp) or 0xffff
  udp = udp[:6] + struct.pack('!H', udp_csum) + udp[8:]
  ip = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + udp_len, ip_id, 0, 255, 17, 0, src, dst)
  ip = ip[:10] + struct.pack('!H', checksum(ip)) + ip[12:]
  return BCAST + mac + b'\x08\x00' + ip + udp


def cmd_dhcp(iface, mac, xid, ip_id):
  Link(iface).send(dhcp_discover_frame(parse_mac(mac), int(xid, 16), int(ip_id)))


def cmd_probe(iface, mac, ip, wait):
  """Send an ARP Probe; exit 3 (printing the other MAC) if someone claims or probes the address."""
  mac, ip = parse_mac(mac), socket.inet_aton(ip)
  link = Link(iface)
  link.send(arp_frame(mac, ZERO_IP, ip))
  for frame in link.frames(float(wait)):
    arp = parse_arp(frame)
    if arp is None or arp[1] == mac:
      continue
    _, sha, spa, _, tpa = arp
    if spa == ip or (spa == ZERO_IP and tpa == ip):
      print(fmt_mac(sha))
      sys.exit(3)


def cmd_announce(iface, mac, ip):
  ip = socket.inet_aton(ip)
  Link(iface).send(arp_frame(parse_mac(mac), ip, ip))


AUTOIP_RANGE_START = 0xA9FE0100  # 169.254.1.0
AUTOIP_RANGE_END = 0xA9FEFEFF    # 169.254.254.255
WATCH_PROBE_INTERVAL = 2.0


def lwip_autoip_addrs(mac, tries=2):
  """First addresses lwIP's AutoIP tries for a MAC (LWIP_AUTOIP_CREATE_SEED_ADDR, autoip_create_addr)."""
  span = AUTOIP_RANGE_END - AUTOIP_RANGE_START + 1
  addrs = []
  for tried in range(tries):
    addr = 0xA9FE0000 | ((AUTOIP_RANGE_START + (mac[4] | mac[5] << 8) + tried) & 0xffff)
    if addr < AUTOIP_RANGE_START:
      addr += span
    if addr > AUTOIP_RANGE_END:
      addr -= span
    addrs.append(struct.pack('!I', addr))
  return addrs


def cmd_watch(iface, mac, sender_ip, timeout, *candidates):
  """Print 'IP MAC HOW' for the first other host using a 169.254.x.x address.

  Besides listening for its ARP traffic, ARP-probe the candidate addresses, plus
  the AutoIP addresses of every MAC heard on the link, every WATCH_PROBE_INTERVAL.
  Probes carry a real sender address: an all-zero sender would look like an
  AutoIP conflict to a board that is still probing.
  """
  mac, sender = parse_mac(mac), socket.inet_aton(sender_ip)
  probe = {socket.inet_aton(ip) for ip in candidates}
  heard = set()
  link = Link(iface)
  deadline = time.monotonic() + float(timeout)
  next_probe = time.monotonic()
  while time.monotonic() < deadline:
    if time.monotonic() >= next_probe:
      for ip in probe:
        link.send(arp_frame(mac, sender, ip))
      next_probe = time.monotonic() + WATCH_PROBE_INTERVAL
    for frame in link.frames(min(deadline, next_probe) - time.monotonic()):
      if len(frame) < 14 or frame[6:12] == mac:
        continue
      arp = parse_arp(frame)
      if arp is not None and arp[2][:2] == b'\xa9\xfe':
        how = 'ARP reply' if arp[0] == 2 else 'announcement' if arp[2] == arp[4] else 'ARP request'
        print(socket.inet_ntoa(arp[2]), fmt_mac(arp[1]), how.replace(' ', '_'), flush=True)
        return
      src = frame[6:12]
      if src[0] & 1 or src in heard:
        continue
      heard.add(src)
      new = [ip for ip in lwip_autoip_addrs(src) if ip not in probe]
      if new:
        log('Heard %s, probing its AutoIP address %s' % (fmt_mac(src), ' / '.join(socket.inet_ntoa(ip) for ip in new)))
        probe.update(new)
        next_probe = time.monotonic()
        break
  sys.exit(1)


FIN, SYN, RST, PSH, ACK = 0x01, 0x02, 0x04, 0x08, 0x10
SEQ_MASK = 0xffffffff


def tcp_frame(src_mac, dst_mac, src_ip, dst_ip, sport, dport, seq, ack, flags, payload=b'', options=b''):
  tcp = struct.pack('!HHIIBBHHH', sport, dport, seq, ack, ((20 + len(options)) // 4) << 4, flags, 64240, 0, 0)
  tcp += options + payload
  csum = checksum(src_ip + dst_ip + struct.pack('!BBH', 0, 6, len(tcp)) + tcp)
  tcp = tcp[:16] + struct.pack('!H', csum) + tcp[18:]
  ip = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(tcp), random.getrandbits(16), 0x4000, 64, 6, 0, src_ip, dst_ip)
  ip = ip[:10] + struct.pack('!H', checksum(ip)) + ip[12:]
  return dst_mac + src_mac + b'\x08\x00' + ip + tcp


def parse_tcp(frame, src_ip, dst_ip):
  """Return (sport, dport, seq, ack, flags, payload) for a TCP frame src_ip -> dst_ip."""
  if len(frame) < 54 or frame[12:14] != b'\x08\x00' or frame[23] != 6:
    return None
  if frame[26:30] != src_ip or frame[30:34] != dst_ip:
    return None
  ihl = (frame[14] & 0x0f) * 4
  total = struct.unpack_from('!H', frame, 16)[0]
  tcp = frame[14 + ihl:14 + total]  # drops Ethernet padding
  sport, dport, seq, ack, off, flags = struct.unpack_from('!HHIIBB', tcp)
  return sport, dport, seq, ack, flags, tcp[(off >> 4) * 4:]


class Vehicle:
  """The vehicle's address in userspace: answers ARP for it and sends raw frames from it."""

  def __init__(self, iface, mac, ip):
    self.link = Link(iface)
    self.mac = parse_mac(mac)
    self.ip = socket.inet_aton(ip)

  def poll(self, timeout, match):
    """Answer ARP requests for our address; return the first non-None match(frame) or None on timeout."""
    for frame in self.link.frames(max(timeout, 0)):
      arp = parse_arp(frame)
      if arp is not None:
        op, sha, spa, _, tpa = arp
        if op == 1 and tpa == self.ip and sha != self.mac:
          self.link.send(arp_frame(self.mac, self.ip, spa, op=2, tha=sha, dst=sha))
      result = match(frame)
      if result is not None:
        return result
    return None

  def resolve(self, ip, tries=3):
    def reply(frame):
      arp = parse_arp(frame)
      if arp is not None and arp[0] == 2 and arp[2] == ip:
        return arp[1]
    for _ in range(tries):
      self.link.send(arp_frame(self.mac, self.ip, ip))
      mac = self.poll(1.0, reply)
      if mac is not None:
        return mac
    return None


class TcpClient:
  """Just enough TCP for a stop-and-wait echo exchange."""

  def __init__(self, vehicle, peer_ip, peer_mac, port):
    self.v = vehicle
    self.peer_ip = peer_ip
    self.peer_mac = peer_mac
    self.port = port
    self.sport = random.randint(49152, 65535)
    self.snd_nxt = random.getrandbits(32)
    self.rcv_nxt = 0

  def _send(self, flags, payload=b'', seq=None, options=b''):
    seq = self.snd_nxt if seq is None else seq
    self.v.link.send(tcp_frame(self.v.mac, self.peer_mac, self.v.ip, self.peer_ip, self.sport, self.port,
                               seq, self.rcv_nxt, flags, payload, options))

  def _segment(self, deadline):
    def match(frame):
      seg = parse_tcp(frame, self.peer_ip, self.v.ip)
      if seg is not None and seg[0] == self.port and seg[1] == self.sport:
        return seg
    seg = self.v.poll(deadline - time.monotonic(), match)
    if seg is not None and seg[4] & RST:
      raise ConnectionResetError('connection reset by peer')
    return seg

  def connect(self, tries=3):
    iss = self.snd_nxt
    for _ in range(tries):
      self._send(SYN, options=struct.pack('!BBH', 2, 4, 1460))
      deadline = time.monotonic() + 1.0
      while True:
        seg = self._segment(deadline)
        if seg is None:
          break
        _, _, seq, ack, flags, _ = seg
        if flags & SYN and flags & ACK and ack == (iss + 1) & SEQ_MASK:
          self.snd_nxt = ack
          self.rcv_nxt = (seq + 1) & SEQ_MASK
          self._send(ACK)
          return
    raise TimeoutError('no SYN-ACK after %d SYNs' % tries)

  def echo(self, data, timeout=2.0):
    """Send data and return what comes back within timeout (retransmitting unacknowledged data)."""
    start = self.snd_nxt
    self.snd_nxt = (start + len(data)) & SEQ_MASK
    self._send(PSH | ACK, data, seq=start)
    acked = False
    got = b''
    deadline = time.monotonic() + timeout
    rexmit_at = time.monotonic() + 0.5
    while len(got) < len(data) and time.monotonic() < deadline:
      seg = self._segment(min(deadline, rexmit_at))
      if seg is None:
        if not acked and time.monotonic() >= rexmit_at:
          self._send(PSH | ACK, data, seq=start)
          rexmit_at = time.monotonic() + 0.5
        continue
      _, _, seq, ack, flags, payload = seg
      if flags & ACK and ack == self.snd_nxt:
        acked = True
      if payload and seq == self.rcv_nxt:
        got += payload
        self.rcv_nxt = (self.rcv_nxt + len(payload)) & SEQ_MASK
      if payload or flags & FIN:
        if flags & FIN:
          self.rcv_nxt = (self.rcv_nxt + 1) & SEQ_MASK
        self._send(ACK)
      if flags & FIN:
        break
    return got

  def close(self, timeout=2.0):
    self._send(FIN | ACK)
    self.snd_nxt = (self.snd_nxt + 1) & SEQ_MASK
    deadline = time.monotonic() + timeout
    while True:
      seg = self._segment(deadline)
      if seg is None:
        self._send(RST)
        return False
      _, _, seq, _, flags, payload = seg
      if flags & FIN:
        self.rcv_nxt = (seq + len(payload) + 1) & SEQ_MASK
        self._send(ACK)
        return True


def cmd_echo(iface, mac, src, dst, port, rounds):
  vehicle = Vehicle(iface, mac, src)
  peer_ip = socket.inet_aton(dst)
  t0 = time.monotonic()
  peer_mac = vehicle.resolve(peer_ip)
  if peer_mac is None:
    print('  no ARP reply from %s' % dst)
    sys.exit(1)
  print('  %s is at %s (ARP %.1f ms)' % (dst, fmt_mac(peer_mac), (time.monotonic() - t0) * 1e3))

  conn = TcpClient(vehicle, peer_ip, peer_mac, int(port))
  t0 = time.monotonic()
  try:
    conn.connect()
  except OSError as e:
    print('  connect to %s:%s failed: %s' % (dst, port, e))
    sys.exit(1)
  print('  connected %s:%d -> %s:%s in %.1f ms' % (src, conn.sport, dst, port, (time.monotonic() - t0) * 1e3))

  failures = total = 0
  for r in range(int(rounds)):
    messages = [
      ('1 byte', b'\x00'),
      ('ascii', b'hi'),
      ('ascii', b'ping'),
      ('random', os.urandom(8)),
      ('DoIP routing activation request', DOIP_ROUTING_ACTIVATION_REQ),
      ('DoIP diag request 22 F1 7C', DOIP_DIAG_READ_F17C),
    ]
    for label, data in messages:
      t0 = time.monotonic()
      try:
        got = conn.echo(data)
      except ConnectionResetError as e:
        print('  FAIL %s' % e)
        sys.exit(1)
      ms = (time.monotonic() - t0) * 1e3
      ok = got == data
      total += 1
      failures += not ok
      print('  %s %-32s %2d B  %6.1f ms  tx %s' % ('OK  ' if ok else 'FAIL', label, len(data), ms, data.hex(' ')))
      if not ok:
        print('       %-32s           rx %s' % ('', got.hex(' ') if got else '(nothing)'))
  if not conn.close():
    print('  board did not close the connection; sent RST')
  print('  %d/%d messages echoed correctly' % (total - failures, total))
  sys.exit(1 if failures else 0)


COMMANDS = {
  'dhcp': cmd_dhcp,
  'probe': cmd_probe,
  'announce': cmd_announce,
  'watch': cmd_watch,
  'echo': cmd_echo,
}

if __name__ == '__main__':
  COMMANDS[sys.argv[1]](*sys.argv[2:])
PY

py() { "$PYTHON" "$HELPER" "$@"; }

log "Interface $IFACE ($MAC), vehicle address $VEHICLE_IP"

if [[ -z $BOARD_IP ]]; then
  candidates=$( { cat "$LAST_BOARD_FILE" 2>/dev/null; cached_neighbours; } | { grep -vxF -e "$VEHICLE_IP" -e 169.254.255.255 || true; } | sort -u | tr '\n' ' ')
  log "Watching for the board${candidates:+; ARP-probing $candidates every 2 s}"
  # shellcheck disable=SC2086
  py watch "$IFACE" "$MAC" "$VEHICLE_IP" "$(( WAIT + (FAST ? 0 : 17) ))" $candidates > "$BOARD_FILE" &
  WATCH_PID=$!
fi

if (( ! FAST )); then
  XID=$(printf '%04x%04x' "$RANDOM" "$RANDOM")
  log "DHCP Discover 1/3 (xid 0x$XID)"; py dhcp "$IFACE" "$MAC" "$XID" 1; sleep 4
  log "DHCP Discover 2/3";              py dhcp "$IFACE" "$MAC" "$XID" 2; sleep 8
  log "DHCP Discover 3/3";              py dhcp "$IFACE" "$MAC" "$XID" 3; sleep 3.3
fi

for attempt in 1 2 3 4 5 6 7 8 9 10; do
  log "ARP Probe for $VEHICLE_IP"
  set +e
  other=$(py probe "$IFACE" "$MAC" "$VEHICLE_IP" 1.05)
  rc=$?
  set -e
  if (( rc == 0 )); then
    break
  elif (( rc == 3 )); then
    log "$VEHICLE_IP is in use by $other"
    (( attempt < 10 )) || die "too many address conflicts"
    VEHICLE_IP=169.254.$(( RANDOM % 254 + 1 )).$(( RANDOM % 256 ))
  else
    die "ARP probe failed"
  fi
done

log "ARP Announcement for $VEHICLE_IP"
py announce "$IFACE" "$MAC" "$VEHICLE_IP"

if [[ -z $BOARD_IP ]]; then
  log "Waiting for the board (power the board or plug its cable now if it is not up yet)"
  wait "$WATCH_PID" || die "board not found; pass -b IP if you know its address"
  WATCH_PID=
  read -r BOARD_IP BOARD_MAC HOW < "$BOARD_FILE"
  log "Board is $BOARD_IP ($BOARD_MAC), found by ${HOW//_/ }"
fi
echo "$BOARD_IP" > "$LAST_BOARD_FILE"

log "TCP echo test $VEHICLE_IP -> $BOARD_IP:$PORT"
if py echo "$IFACE" "$MAC" "$VEHICLE_IP" "$BOARD_IP" "$PORT" "$ROUNDS"; then
  log "PASS"
else
  log "FAIL"
  exit 1
fi
