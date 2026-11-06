#include "lwip/tcp.h"

#include "net_echo.h"

#ifdef ALLOW_DEBUG

#define NET_ECHO_PORT 7U

// Received data waiting to be echoed is kept as the pcb argument. The receive
// window is only reopened as data is queued for sending, so a slow reader
// throttles the sender instead of exhausting memory.

static void net_echo_flush(struct tcp_pcb *pcb, struct pbuf *pending) {
  struct pbuf *q = pending;
  u16_t queued = 0U;
  while (q != NULL) {
    u16_t room = tcp_sndbuf(pcb);
    u16_t n = (q->len < room) ? q->len : room;
    if ((n == 0U) || (tcp_write(pcb, q->payload, n, TCP_WRITE_FLAG_COPY) != ERR_OK)) {
      break;
    }
    queued += n;
    q = pbuf_free_header(q, n);
  }
  tcp_arg(pcb, q);
  if (queued > 0U) {
    tcp_recved(pcb, queued);
    (void)tcp_output(pcb);
  }
}

// Returns ERR_ABRT if the pcb had to be aborted, as lwIP callbacks must.
static err_t net_echo_close(struct tcp_pcb *pcb, struct pbuf *pending) {
  err_t ret = ERR_OK;
  if (pending != NULL) {
    (void)pbuf_free(pending);
  }
  tcp_arg(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_sent(pcb, NULL);
  tcp_err(pcb, NULL);
  if (tcp_close(pcb) != ERR_OK) {
    tcp_abort(pcb);
    ret = ERR_ABRT;
  }
  return ret;
}

static err_t net_echo_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
  struct pbuf *pending = (struct pbuf *)arg;
  err_t ret = ERR_OK;
  if (p == NULL) {
    ret = net_echo_close(pcb, pending);  // peer closed
  } else if (err != ERR_OK) {
    (void)pbuf_free(p);
    ret = err;
  } else {
    if (pending == NULL) {
      pending = p;
    } else {
      pbuf_cat(pending, p);
    }
    net_echo_flush(pcb, pending);
  }
  return ret;
}

static err_t net_echo_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
  (void)len;
  net_echo_flush(pcb, (struct pbuf *)arg);
  return ERR_OK;
}

static void net_echo_error(void *arg, err_t err) {
  (void)err;
  if (arg != NULL) {
    (void)pbuf_free((struct pbuf *)arg);
  }
}

static err_t net_echo_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
  (void)arg;
  (void)err;
  tcp_arg(pcb, NULL);
  tcp_recv(pcb, net_echo_recv);
  tcp_sent(pcb, net_echo_sent);
  tcp_err(pcb, net_echo_error);
  return ERR_OK;
}

void net_echo_init(void) {
  struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (pcb != NULL) {
    if (tcp_bind(pcb, IP_ANY_TYPE, NET_ECHO_PORT) == ERR_OK) {
      struct tcp_pcb *listen = tcp_listen(pcb);  // frees pcb on success
      if (listen != NULL) {
        tcp_accept(listen, net_echo_accept);
      } else {
        (void)tcp_close(pcb);
      }
    } else {
      (void)tcp_close(pcb);
    }
  }
}

#else

void net_echo_init(void) {}

#endif
