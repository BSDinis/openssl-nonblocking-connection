/*
Copyright (c) 2017 Darren Smith

ssl_examples is free software; you can redistribute it and/or modify
it under the terms of the MIT license. See LICENSE for details.
*/

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include "macros.h"
#include "peer_type.h"

/* Global SSL context */
SSL_CTX *ctx;
peer_t client;

#define DEFAULT_BUF_SIZE 64

void print_unencrypted_data(uint8_t *buf, ssize_t len) {
  printf("%.*s", (int)len, (char *) buf);
}


void ssl_client_init(peer_t *p,
                     int fd,
                     bool mode)
{
  peer_create(p, ctx, fd, print_unencrypted_data, mode);
}


/* Obtain the return value of an SSL operation and convert into a simplified
 * error code, which is easier to examine for failure. */
typedef enum { SSLSTATUS_OK, SSLSTATUS_WANT_IO, SSLSTATUS_FAIL} ssl_status_t ;

static ssl_status_t get_sslstatus(SSL* ssl, int n)
{
  switch (SSL_get_error(ssl, n))
  {
    case SSL_ERROR_NONE:
      return SSLSTATUS_OK;
    case SSL_ERROR_WANT_WRITE:
    case SSL_ERROR_WANT_READ:
      return SSLSTATUS_WANT_IO;
    case SSL_ERROR_ZERO_RETURN:
    case SSL_ERROR_SYSCALL:
    default:
      return SSLSTATUS_FAIL;
  }
}

/* Handle request to send unencrypted data to the SSL.  All we do here is just
 * queue the data into the encrypt_buf for later processing by the SSL
 * object. */
void send_unencrypted_bytes(const uint8_t *buf, ssize_t len)
{
  client.encrypt_buf = realloc(client.encrypt_buf, client.encrypt_len + len);
  memcpy(client.encrypt_buf+client.encrypt_len, buf, len);
  client.encrypt_len += len;
}

/* Queue encrypted bytes. Should only be used when the SSL object has requested a
 * write operation. */
void queue_encrypted_bytes(const uint8_t *buf, ssize_t len)
{
  client.write_buf = realloc(client.write_buf, client.write_len + len);
  memcpy(client.write_buf+client.write_len, buf, len);
  client.write_len += len;
}

ssl_status_t do_ssl_handshake()
{
  uint8_t buf[DEFAULT_BUF_SIZE];
  ssl_status_t status;

  int n = SSL_do_handshake(client.ssl);
  status = get_sslstatus(client.ssl, n);

  /* Did SSL request to write bytes? */
  if (status == SSLSTATUS_WANT_IO)
    do {
      n = BIO_read(client.wbio, buf, sizeof(buf));
      if (n > 0)
        queue_encrypted_bytes(buf, n);
      else if (!BIO_should_retry(client.wbio))
        return SSLSTATUS_FAIL;
    } while (n>0);

  return status;
}

/* Process SSL bytes received from the peer. The data needs to be fed into the
   SSL object to be unencrypted.  On success, returns 0, on SSL error -1. */
int on_read_cb(uint8_t * src, ssize_t len)
{
  uint8_t buf[DEFAULT_BUF_SIZE];
  ssl_status_t status;
  int n;

  while (len > 0) {
    n = BIO_write(client.rbio, src, len);

    if (n<=0)
      return -1; /* assume bio write failure is unrecoverable */

    src += n;
    len -= n;

    if (!SSL_is_init_finished(client.ssl)) {
      if (do_ssl_handshake() == SSLSTATUS_FAIL)
        return -1;
      if (!SSL_is_init_finished(client.ssl))
        return 0;
    }

    /* The encrypted data is now in the input bio so now we can perform actual
     * read of unencrypted data. */

    do {
      n = SSL_read(client.ssl, buf, sizeof(buf));
      if (n > 0)
        client.io_on_read(buf, n);
    } while (n > 0);

    status = get_sslstatus(client.ssl, n);

    /* Did SSL request to write bytes? This can happen if peer has requested SSL
     * renegotiation. */
    if (status == SSLSTATUS_WANT_IO)
      do {
        n = BIO_read(client.wbio, buf, sizeof(buf));
        if (n > 0)
          queue_encrypted_bytes(buf, n);
        else if (!BIO_should_retry(client.wbio))
          return -1;
      } while (n>0);

    if (status == SSLSTATUS_FAIL)
      return -1;
  }

  return 0;
}

/* Process outbound unencrypted data that is waiting to be encrypted.  The
 * waiting data resides in encrypt_buf.  It needs to be passed into the SSL
 * object for encryption, which in turn generates the encrypted bytes that then
 * will be queued for later socket write. */
int do_encrypt()
{
  uint8_t buf[DEFAULT_BUF_SIZE];
  ssl_status_t status;

  if (!SSL_is_init_finished(client.ssl))
    return 0;

  while (client.encrypt_len>0) {
    int n = SSL_write(client.ssl, client.encrypt_buf, client.encrypt_len);
    status = get_sslstatus(client.ssl, n);

    if (n>0) {
      /* consume the waiting bytes that have been used by SSL */
      if (n < client.encrypt_len)
        memmove(client.encrypt_buf, client.encrypt_buf+n, client.encrypt_len-n);
      client.encrypt_len -= n;
      client.encrypt_buf = realloc(client.encrypt_buf, client.encrypt_len);

      /* take the output of the SSL object and queue it for socket write */
      do {
        n = BIO_read(client.wbio, buf, sizeof(buf));
        if (n > 0)
          queue_encrypted_bytes(buf, n);
        else if (!BIO_should_retry(client.wbio))
          return -1;
      } while (n>0);
    }

    if (status == SSLSTATUS_FAIL)
      return -1;

    if (n==0)
      break;
  }
  return 0;
}

/* Read bytes from stdin and queue for later encryption. */
void do_stdin_read()
{
  uint8_t buf[DEFAULT_BUF_SIZE];
  ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
  if (n>0)
    send_unencrypted_bytes(buf, (size_t)n);
}

/* Read encrypted bytes from socket. */
int do_sock_read()
{
  uint8_t buf[DEFAULT_BUF_SIZE];
  ssize_t n = read(client.fd, buf, sizeof(buf));

  if (n>0)
    return on_read_cb(buf, (size_t)n);
  else
    return -1;
}

/* Write encrypted bytes to the socket. */
int do_sock_write()
{
  ssize_t n = write(client.fd, client.write_buf, client.write_len);
  if (n>0) {
    if (n < client.write_len)
      memmove(client.write_buf, client.write_buf+n, client.write_len-n);
    client.write_len -= n;
    client.write_buf = realloc(client.write_buf, client.write_len);
    return 0;
  }
  else
    return -1;
}

void ssl_init(const char * certfile, const char * keyfile)
{
  printf("initialising SSL\n");

  /* SSL library initialisation */
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();
  ERR_load_BIO_strings();
  ERR_load_crypto_strings();

  /* create the SSL server context */
  ctx = SSL_CTX_new(SSLv23_method());
  if (!ctx)
    die("SSL_CTX_new()");

  /* Load certificate and private key files, and check consistency */
  if (certfile && keyfile) {
    if (SSL_CTX_use_certificate_file(ctx, certfile,  SSL_FILETYPE_PEM) != 1)
      LOG_KILL("SSL_CTX_use_certificate_file failed");

    if (SSL_CTX_use_PrivateKey_file(ctx, keyfile, SSL_FILETYPE_PEM) != 1)
      LOG_KILL("SSL_CTX_use_PrivateKey_file failed");

    /* Make sure the key and certificate file match. */
    if (SSL_CTX_check_private_key(ctx) != 1)
      LOG_KILL("SSL_CTX_check_private_key failed");
    else
      printf("certificate and private key loaded and verified\n");
  }

  /* Recommended to avoid SSLv2 & SSLv3 */
  SSL_CTX_set_options(ctx, SSL_OP_ALL|SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);
}

