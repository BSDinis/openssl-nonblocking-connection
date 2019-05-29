/**
 * implementation
 */

#include "peer_type.h"
#include "macros.h"
#include <string.h>

int peer_create(peer_t *peer, SSL_CTX *ctx, int fd, buf_operator_t op, bool server)
{
  /* missing stuff */
  memset(peer, 0, sizeof(peer_t));
  peer->fd = -1; // marks invalid (only gets validated at the end)

  peer->ssl  = SSL_new(ctx);
  if (!peer->ssl) {
    LOG("failed to create SSL object");
    return -1;
  }


  peer->rbio = BIO_new(BIO_s_mem());
  if (!peer->rbio) {
    LOG("failed to create read BIO");
    SSL_free(peer->ssl);
    return -1;
  }

  peer->wbio = BIO_new(BIO_s_mem());
  if (!peer->wbio) {
    LOG("failed to create write BIO");
    SSL_free(peer->ssl);
    BIO_free(peer->rbio);
    return -1;
  }

  if (server)      SSL_set_accept_state(peer->ssl);
  else /*client*/ SSL_set_connect_state(peer->ssl);

  SSL_set_bio(peer->ssl, peer->rbio, peer->wbio);

  peer->fd = fd;
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

bool peer_valid(const peer_t * const peer) { return peer->fd != -1; }
