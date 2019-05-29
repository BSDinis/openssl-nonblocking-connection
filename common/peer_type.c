/**
 * implementation
 */

#include "peer_type.h"
#include "macros.h"
#include <string.h>

int peer_create(peer_t *peer, SSL_CTX *ctx, int fd, bool server)
{
  /* missing stuff */
  memset(peer, 0, sizeof(peer_t));

  peer->fd = fd;

  peer->rbio = BIO_new(BIO_s_mem());
  peer->wbio = BIO_new(BIO_s_mem());
  peer->ssl  = SSL_new(ctx);

  if (server)
    SSL_set_accept_state(peer->ssl);
  else
    SSL_set_connect_state(peer->ssl);

  SSL_set_bio(peer->ssl, peer->rbio, peer->wbio);
  return 0;
}

int peer_delete(peer_t * peer)
{
  SSL_free(peer->ssl);

  if (peer->write_buf) {
    free(peer->write_buf);
    peer->write_buf = NULL;
  }

  if (peer->encrypt_buf) {
    free(peer->encrypt_buf);
    peer->encrypt_buf = NULL;
  }

  return 0;
}

static int __queue(uint8_t ** dst_buf, ssize_t *dst_sz,
             const uint8_t * src_buf, ssize_t src_sz)
{
  *dst_buf = realloc(*dst_buf, *dst_sz + src_sz);
  memcpy(*dst_buf + *dst_sz, src_buf, src_sz);
  *dst_sz += src_sz;
  return 0;
}

int peer_queue_to_encrypt(peer_t *peer, const uint8_t *buf, ssize_t len)
{
  return __queue(&peer->encrypt_buf, &peer->encrypt_len, buf, len);
}

int peer_queue_to_decrypt(peer_t *peer, const uint8_t *buf, ssize_t len)
{
  return __queue(&peer->write_buf, &peer->write_len, buf, len);
}

int peer_queue_to_process(peer_t *peer, const uint8_t *buf, ssize_t len)
{
  return __queue(&peer->processing_buf, &peer->processing_len, buf, len);
}


bool peer_valid(const peer_t * const peer) { return peer->fd != -1; }
bool peer_want_write(peer_t *peer) { return (peer->write_len>0); }
