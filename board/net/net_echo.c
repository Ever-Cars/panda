#include <stdbool.h>

#include "lwip/tcp.h"

#include "net_echo.h"

#ifdef ALLOW_DEBUG

#define NET_ECHO_PORT 7U

// Received data waiting to be echoed is kept per connection. The receive
// window is only reopened as data is queued for sending, so a slow reader
// throttles the sender instead of exhausting memory.
//
// The peer's FIN is remembered explicitly (recv with p == NULL). pcb->state
// cannot be used for this: lwIP enters CLOSE_WAIT before it has delivered the
// last data and the FIN, and may call sent() in between.
typedef struct {
  bool in_use;
  bool peer_closed;
  struct pbuf *pending;
} net_echo_conn_t;

static net_echo_conn_t net_echo_conns[MEMP_NUM_TCP_PCB];

static void net_echo_release(net_echo_conn_t *conn) {
  if (conn->pending != NULL) {
    (void)pbuf_free(conn->pending);
  }
  conn->pending = NULL;
  conn->peer_closed = false;
  conn->in_use = false;
}

// Queue as much pending data as the send buffer takes. Once the peer has
// finished sending and everything is queued, close gracefully. tcp_close()
// fails with ERR_MEM while the FIN cannot be queued yet; the callbacks then
// stay registered and the next sent/poll callback tries again.
static void net_echo_flush(struct tcp_pcb *pcb, net_echo_conn_t *conn) {
  struct pbuf *q = conn->pending;
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
  conn->pending = q;
  if (queued > 0U) {
    tcp_recved(pcb, queued);
    (void)tcp_output(pcb);
  }

  if (conn->peer_closed && (conn->pending == NULL) && (tcp_close(pcb) == ERR_OK)) {
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_poll(pcb, NULL, 0U);
    net_echo_release(conn);
  }
}

static err_t net_echo_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
  net_echo_conn_t *conn = (net_echo_conn_t *)arg;
  err_t ret = ERR_OK;
  if (p == NULL) {
    conn->peer_closed = true;  // all data has been delivered before this
    net_echo_flush(pcb, conn);
  } else if (err != ERR_OK) {
    (void)pbuf_free(p);
    ret = err;
  } else {
    if (conn->pending == NULL) {
      conn->pending = p;
    } else {
      pbuf_cat(conn->pending, p);
    }
    net_echo_flush(pcb, conn);
  }
  return ret;
}

static err_t net_echo_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
  (void)len;
  net_echo_flush(pcb, (net_echo_conn_t *)arg);
  return ERR_OK;
}

// Retry every 500 ms: if tcp_write() or tcp_close() failed for lack of memory
// while nothing was in flight, no sent callback would come to try again.
static err_t net_echo_poll(void *arg, struct tcp_pcb *pcb) {
  net_echo_flush(pcb, (net_echo_conn_t *)arg);
  return ERR_OK;
}

// The pcb is already freed by lwIP (reset, abort or timeout)
static void net_echo_error(void *arg, err_t err) {
  (void)err;
  net_echo_release((net_echo_conn_t *)arg);
}

static err_t net_echo_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
  (void)arg;
  (void)err;
  net_echo_conn_t *conn = NULL;
  for (uint32_t i = 0U; (i < (uint32_t)MEMP_NUM_TCP_PCB) && (conn == NULL); i++) {
    if (!net_echo_conns[i].in_use) {
      conn = &net_echo_conns[i];
    }
  }

  err_t ret = ERR_OK;
  if (conn == NULL) {
    tcp_abort(pcb);
    ret = ERR_ABRT;
  } else {
    conn->in_use = true;
    conn->peer_closed = false;
    conn->pending = NULL;
    tcp_arg(pcb, conn);
    tcp_recv(pcb, net_echo_recv);
    tcp_sent(pcb, net_echo_sent);
    tcp_err(pcb, net_echo_error);
    tcp_poll(pcb, net_echo_poll, 1U);
  }
  return ret;
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
