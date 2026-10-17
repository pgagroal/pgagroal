/*
 * Copyright (C) 2026 The pgagroal community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <pgagroal.h>
#include <tls.h>
#include <logging.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/x509.h>

static int
classify(SSL* ssl, int rc)
{
   int err = SSL_get_error(ssl, rc);

   switch (err)
   {
      case SSL_ERROR_WANT_READ:
      case SSL_ERROR_WANT_WRITE:
         return PGAGROAL_TLS_WANT_IO;
      case SSL_ERROR_ZERO_RETURN:
         return PGAGROAL_TLS_CLOSED;
      default:
         return PGAGROAL_TLS_ERROR;
   }
}

/* Capture the TLS 1.3 application traffic secrets as OpenSSL derives them,
 * recovering the owning context via the SSL app-data back-pointer. */
static void
keylog_cb(const SSL* ssl, const char* line)
{
   static const char* const labels[2] = {"CLIENT_TRAFFIC_SECRET_0 ", "SERVER_TRAFFIC_SECRET_0 "};
   struct tls* t = (struct tls*)SSL_get_app_data(ssl);
   int which;

   if (t == NULL)
   {
      return;
   }

   for (which = 0; which < 2; which++)
   {
      size_t llen = strlen(labels[which]);
      const char* hex;
      unsigned char* dst;
      size_t n = 0;

      if (strncmp(line, labels[which], llen) != 0)
      {
         continue;
      }

      /* line = "<LABEL> <client_random_hex> <secret_hex>" */
      hex = strchr(line + llen, ' ');
      if (hex == NULL)
      {
         return;
      }
      hex++;

      dst = (which == 0) ? t->client_secret : t->server_secret;
      while (hex[0] != '\0' && hex[1] != '\0' && hex[0] != '\n' && n < 48)
      {
         unsigned int b;
         if (sscanf(hex, "%2x", &b) != 1)
         {
            break;
         }
         dst[n++] = (unsigned char)b;
         hex += 2;
      }
      t->secret_len = n;
      t->secret_mask |= (which == 0) ? 0x1 : 0x2;
      return;
   }
}

int
pgagroal_tls_create(SSL_CTX* ctx, bool server, struct tls** tls)
{
   struct tls* t = NULL;

   if (ctx == NULL || tls == NULL)
   {
      goto error;
   }

   *tls = NULL;

   t = (struct tls*)calloc(1, sizeof(struct tls));
   if (t == NULL)
   {
      goto error;
   }

   t->server = server;
   t->handshake_complete = false;
   t->fd = -1;

   t->ssl = SSL_new(ctx);
   if (t->ssl == NULL)
   {
      goto error;
   }

   t->rbio = BIO_new(BIO_s_mem());
   t->wbio = BIO_new(BIO_s_mem());
   if (t->rbio == NULL || t->wbio == NULL)
   {
      goto error;
   }

   /* An empty memory BIO should ask for more bytes, not signal EOF */
   BIO_set_mem_eof_return(t->rbio, -1);
   BIO_set_mem_eof_return(t->wbio, -1);

   /* The SSL object takes ownership of both BIOs */
   SSL_set_bio(t->ssl, t->rbio, t->wbio);

   /* Back-pointer so the I/O layer can recover the wrapper from the SSL object */
   SSL_set_app_data(t->ssl, t);

   /* Capture the TLS 1.3 traffic secrets during the handshake for later harvest */
   SSL_CTX_set_keylog_callback(ctx, keylog_cb);

   if (server)
   {
      SSL_set_accept_state(t->ssl);
   }
   else
   {
      SSL_set_connect_state(t->ssl);
   }

   *tls = t;
   return PGAGROAL_TLS_OK;

error:
   if (t != NULL)
   {
      if (t->ssl != NULL)
      {
         SSL_free(t->ssl);
      }
      else
      {
         if (t->rbio != NULL)
         {
            BIO_free(t->rbio);
         }
         if (t->wbio != NULL)
         {
            BIO_free(t->wbio);
         }
      }
      free(t);
   }

   return PGAGROAL_TLS_ERROR;
}

void
pgagroal_tls_free(struct tls* tls)
{
   if (tls == NULL)
   {
      return;
   }

   if (tls->ssl != NULL)
   {
      /* Frees the SSL object and the two BIOs it owns */
      SSL_free(tls->ssl);
   }

   free(tls);
}

void
pgagroal_tls_set_fd(struct tls* tls, int fd)
{
   tls->fd = fd;
}

struct tls*
pgagroal_tls_from_ssl(SSL* ssl)
{
   if (ssl == NULL)
   {
      return NULL;
   }

   return (struct tls*)SSL_get_app_data(ssl);
}

int
pgagroal_tls_feed(struct tls* tls, const void* buf, size_t len, size_t* consumed)
{
   int written;

   if (consumed != NULL)
   {
      *consumed = 0;
   }

   if (tls == NULL || tls->rbio == NULL || (buf == NULL && len > 0))
   {
      return PGAGROAL_TLS_ERROR;
   }

   if (len == 0)
   {
      return PGAGROAL_TLS_OK;
   }

   written = BIO_write(tls->rbio, buf, (int)len);
   if (written <= 0)
   {
      return PGAGROAL_TLS_ERROR;
   }

   if (consumed != NULL)
   {
      *consumed = (size_t)written;
   }

   return PGAGROAL_TLS_OK;
}

int
pgagroal_tls_drain(struct tls* tls, void* buf, size_t cap, size_t* produced)
{
   int nread;

   if (produced != NULL)
   {
      *produced = 0;
   }

   if (tls == NULL || tls->wbio == NULL || buf == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }

   if (cap == 0)
   {
      return PGAGROAL_TLS_OK;
   }

   nread = BIO_read(tls->wbio, buf, (int)cap);
   if (nread <= 0)
   {
      /* Nothing queued is not an error for a memory BIO */
      if (BIO_should_retry(tls->wbio))
      {
         return PGAGROAL_TLS_OK;
      }
      return PGAGROAL_TLS_ERROR;
   }

   if (produced != NULL)
   {
      *produced = (size_t)nread;
   }

   return PGAGROAL_TLS_OK;
}

bool
pgagroal_tls_pending(struct tls* tls)
{
   if (tls == NULL || tls->wbio == NULL)
   {
      return false;
   }

   return BIO_pending(tls->wbio) > 0;
}

int
pgagroal_tls_handshake(struct tls* tls)
{
   int rc;

   if (tls == NULL || tls->ssl == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }

   if (tls->handshake_complete)
   {
      return PGAGROAL_TLS_OK;
   }

   rc = SSL_do_handshake(tls->ssl);
   if (rc == 1)
   {
      tls->handshake_complete = true;
      return PGAGROAL_TLS_OK;
   }

   return classify(tls->ssl, rc);
}

int
pgagroal_tls_write(struct tls* tls, const void* buf, size_t len, size_t* written)
{
   int rc;

   if (written != NULL)
   {
      *written = 0;
   }

   if (tls == NULL || tls->ssl == NULL || (buf == NULL && len > 0))
   {
      return PGAGROAL_TLS_ERROR;
   }

   if (len == 0)
   {
      return PGAGROAL_TLS_OK;
   }

   rc = SSL_write(tls->ssl, buf, (int)len);
   if (rc > 0)
   {
      if (written != NULL)
      {
         *written = (size_t)rc;
      }
      return PGAGROAL_TLS_OK;
   }

   return classify(tls->ssl, rc);
}

int
pgagroal_tls_read(struct tls* tls, void* buf, size_t cap, size_t* nread)
{
   int rc;

   if (nread != NULL)
   {
      *nread = 0;
   }

   if (tls == NULL || tls->ssl == NULL || buf == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }

   if (cap == 0)
   {
      return PGAGROAL_TLS_OK;
   }

   rc = SSL_read(tls->ssl, buf, (int)cap);
   if (rc > 0)
   {
      if (nread != NULL)
      {
         *nread = (size_t)rc;
      }
      return PGAGROAL_TLS_OK;
   }

   return classify(tls->ssl, rc);
}

static int
socket_write_all(int fd, const unsigned char* buf, size_t len)
{
   size_t off = 0;

   while (off < len)
   {
      ssize_t n = write(fd, buf + off, len - off);
      if (n > 0)
      {
         off += (size_t)n;
      }
      else if (n < 0 && errno == EINTR)
      {
         continue;
      }
      else
      {
         return PGAGROAL_TLS_ERROR;
      }
   }

   return PGAGROAL_TLS_OK;
}

/* Drain all pending outbound ciphertext to the socket. */
static int
socket_flush(struct tls* tls, int fd)
{
   unsigned char buf[16384];
   size_t produced = 0;

   while (pgagroal_tls_drain(tls, buf, sizeof(buf), &produced) == PGAGROAL_TLS_OK && produced > 0)
   {
      if (socket_write_all(fd, buf, produced) != PGAGROAL_TLS_OK)
      {
         return PGAGROAL_TLS_ERROR;
      }
   }

   return PGAGROAL_TLS_OK;
}

/* Read one chunk of ciphertext from the socket and feed it to the engine. */
static int
socket_fill(struct tls* tls, int fd)
{
   unsigned char buf[16384];
   ssize_t n;

   do
   {
      n = read(fd, buf, sizeof(buf));
   }
   while (n < 0 && errno == EINTR);

   if (n <= 0)
   {
      return PGAGROAL_TLS_ERROR;
   }

   return pgagroal_tls_feed(tls, buf, (size_t)n, NULL);
}

/* Owned-mode read: frame one TLS record off the socket and decrypt it ourselves. */
static int
owned_read(struct tls* tls, int fd, void* buf, size_t cap, size_t* nread)
{
   for (;;)
   {
      if (tls->rbuf_len >= 5)
      {
         size_t body = ((size_t)tls->rbuf[3] << 8) | (size_t)tls->rbuf[4];
         size_t total = 5 + body;

         if (total > sizeof(tls->rbuf))
         {
            return PGAGROAL_TLS_ERROR;
         }
         if (tls->rbuf_len >= total)
         {
            int rc = pgagroal_tls_record_open(&tls->record, tls->rbuf, total, buf, cap, nread);
            memmove(tls->rbuf, tls->rbuf + total, tls->rbuf_len - total);
            tls->rbuf_len -= total;
            return rc;
         }
      }

      {
         size_t room = sizeof(tls->rbuf) - tls->rbuf_len;
         ssize_t n;

         if (room == 0)
         {
            return PGAGROAL_TLS_ERROR;
         }
         do
         {
            n = read(fd, tls->rbuf + tls->rbuf_len, room);
         }
         while (n < 0 && errno == EINTR);
         if (n == 0)
         {
            return PGAGROAL_TLS_CLOSED;
         }
         if (n < 0)
         {
            return PGAGROAL_TLS_ERROR;
         }
         tls->rbuf_len += (size_t)n;
      }
   }
}

/* Owned-mode write: seal application data into TLS records and write them out. */
static int
owned_write(struct tls* tls, int fd, const unsigned char* buf, size_t len)
{
   unsigned char rec[PGAGROAL_TLS_RECORD_MAX];
   size_t off = 0;

   while (off < len)
   {
      size_t chunk = len - off;
      size_t rlen = 0;

      if (chunk > 16384)
      {
         chunk = 16384; /* one TLS record carries at most 2^14 plaintext bytes */
      }
      if (pgagroal_tls_record_seal(&tls->record, buf + off, chunk, rec, sizeof(rec), &rlen) != PGAGROAL_TLS_OK)
      {
         return PGAGROAL_TLS_ERROR;
      }
      if (socket_write_all(fd, rec, rlen) != PGAGROAL_TLS_OK)
      {
         return PGAGROAL_TLS_ERROR;
      }
      off += chunk;
   }

   return PGAGROAL_TLS_OK;
}

int
pgagroal_tls_socket_handshake(struct tls* tls, int fd)
{
   if (tls == NULL || fd < 0)
   {
      return PGAGROAL_TLS_ERROR;
   }

   for (;;)
   {
      int rc = pgagroal_tls_handshake(tls);

      if (socket_flush(tls, fd) != PGAGROAL_TLS_OK)
      {
         return PGAGROAL_TLS_ERROR;
      }

      if (rc == PGAGROAL_TLS_OK)
      {
         return PGAGROAL_TLS_OK;
      }
      if (rc != PGAGROAL_TLS_WANT_IO)
      {
         return PGAGROAL_TLS_ERROR;
      }

      if (socket_fill(tls, fd) != PGAGROAL_TLS_OK)
      {
         return PGAGROAL_TLS_ERROR;
      }
   }
}

int
pgagroal_tls_socket_read(struct tls* tls, int fd, void* buf, size_t cap, size_t* nread)
{
   if (nread != NULL)
   {
      *nread = 0;
   }

   if (tls == NULL || fd < 0 || buf == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }

   if (tls->owned)
   {
      return owned_read(tls, fd, buf, cap, nread);
   }

   for (;;)
   {
      int rc = pgagroal_tls_read(tls, buf, cap, nread);

      if (rc == PGAGROAL_TLS_OK || rc == PGAGROAL_TLS_CLOSED)
      {
         return rc;
      }
      if (rc != PGAGROAL_TLS_WANT_IO)
      {
         return PGAGROAL_TLS_ERROR;
      }

      /* The engine may owe the peer a flight before it can receive more. */
      if (socket_flush(tls, fd) != PGAGROAL_TLS_OK)
      {
         return PGAGROAL_TLS_ERROR;
      }
      if (socket_fill(tls, fd) != PGAGROAL_TLS_OK)
      {
         return PGAGROAL_TLS_ERROR;
      }
   }
}

int
pgagroal_tls_socket_write(struct tls* tls, int fd, const void* buf, size_t len)
{
   size_t off = 0;

   if (tls == NULL || fd < 0 || (buf == NULL && len > 0))
   {
      return PGAGROAL_TLS_ERROR;
   }

   if (tls->owned)
   {
      return owned_write(tls, fd, (const unsigned char*)buf, len);
   }

   while (off < len)
   {
      size_t written = 0;
      int rc = pgagroal_tls_write(tls, (const unsigned char*)buf + off, len - off, &written);

      if (rc == PGAGROAL_TLS_OK)
      {
         off += written;
         if (socket_flush(tls, fd) != PGAGROAL_TLS_OK)
         {
            return PGAGROAL_TLS_ERROR;
         }
      }
      else if (rc == PGAGROAL_TLS_WANT_IO)
      {
         if (socket_flush(tls, fd) != PGAGROAL_TLS_OK)
         {
            return PGAGROAL_TLS_ERROR;
         }
         if (socket_fill(tls, fd) != PGAGROAL_TLS_OK)
         {
            return PGAGROAL_TLS_ERROR;
         }
      }
      else
      {
         return PGAGROAL_TLS_ERROR;
      }
   }

   return PGAGROAL_TLS_OK;
}

int
pgagroal_tls_configure_server_ctx(SSL_CTX* ctx, char* key_file, char* cert_file, char* ca_file)
{
   STACK_OF(X509_NAME)* root_cert_list = NULL;

   if (strlen(cert_file) == 0)
   {
      pgagroal_log_error("No TLS certificate defined");
      goto error;
   }

   if (strlen(key_file) == 0)
   {
      pgagroal_log_error("No TLS private key defined");
      goto error;
   }

   if (SSL_CTX_use_certificate_chain_file(ctx, cert_file) != 1)
   {
      unsigned long err;

      err = ERR_get_error();
      pgagroal_log_error("Couldn't load TLS certificate: %s", cert_file);
      pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
      goto error;
   }

   if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1)
   {
      unsigned long err;

      err = ERR_get_error();
      pgagroal_log_error("Couldn't load TLS private key: %s", key_file);
      pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
      goto error;
   }

   if (SSL_CTX_check_private_key(ctx) != 1)
   {
      unsigned long err;

      err = ERR_get_error();
      pgagroal_log_error("TLS private key check failed: %s", key_file);
      pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
      goto error;
   }

   if (strlen(ca_file) > 0)
   {
      if (SSL_CTX_load_verify_locations(ctx, ca_file, NULL) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("Couldn't load TLS CA: %s", ca_file);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      root_cert_list = SSL_load_client_CA_file(ca_file);
      if (root_cert_list == NULL)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("Couldn't load TLS CA: %s", ca_file);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      SSL_CTX_set_verify(ctx, (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE), NULL);
      SSL_CTX_set_client_CA_list(ctx, root_cert_list);
   }

   return 0;

error:

   return 1;
}

int
pgagroal_tls_create_server(SSL_CTX* ctx, char* key_file, char* cert_file, char* ca_file, struct tls** tls)
{
   if (pgagroal_tls_configure_server_ctx(ctx, key_file, cert_file, ca_file))
   {
      goto error;
   }

   if (pgagroal_tls_create(ctx, true, tls) != PGAGROAL_TLS_OK)
   {
      goto error;
   }

   return 0;

error:

   SSL_CTX_free(ctx);

   return 1;
}

int
pgagroal_tls_create_client(SSL_CTX* ctx, char* key, char* cert, char* root, struct tls** tls)
{
   struct tls* t = NULL;
   bool have_cert = false;
   bool have_rootcert = false;

   if (root != NULL && strlen(root) > 0)
   {
      if (SSL_CTX_load_verify_locations(ctx, root, NULL) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("Couldn't load TLS CA: %s", root);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      have_rootcert = true;
   }

   if (cert != NULL && strlen(cert) > 0)
   {
      if (SSL_CTX_use_certificate_chain_file(ctx, cert) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("Couldn't load TLS certificate: %s", cert);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      have_cert = true;
   }

   if (pgagroal_tls_create(ctx, false, &t) != PGAGROAL_TLS_OK)
   {
      goto error;
   }

   if (have_cert && key != NULL && strlen(key) > 0)
   {
      if (SSL_use_PrivateKey_file(t->ssl, key, SSL_FILETYPE_PEM) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("Couldn't load TLS private key: %s", key);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      if (SSL_check_private_key(t->ssl) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("TLS private key check failed: %s", key);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }
   }

   if (have_rootcert)
   {
      SSL_set_verify(t->ssl, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, NULL);
   }

   *tls = t;

   return 0;

error:

   if (t != NULL)
   {
      pgagroal_tls_free(t);
   }
   SSL_CTX_free(ctx);

   return 1;
}

int
pgagroal_create_ssl_ctx(bool client, SSL_CTX** ctx)
{
   SSL_CTX* c = NULL;

   if (client)
   {
      c = SSL_CTX_new(TLS_client_method());
   }
   else
   {
      c = SSL_CTX_new(TLS_server_method());
   }

   if (c == NULL)
   {
      goto error;
   }

   if (SSL_CTX_set_min_proto_version(c, TLS1_2_VERSION) == 0)
   {
      goto error;
   }

   SSL_CTX_set_mode(c, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
   SSL_CTX_set_options(c, SSL_OP_NO_TICKET);
   SSL_CTX_set_session_cache_mode(c, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);

   *ctx = c;

   return 0;

error:

   if (c != NULL)
   {
      SSL_CTX_free(c);
   }

   return 1;
}

int
pgagroal_create_ssl_server(SSL_CTX* ctx, char* key_file, char* cert_file, char* ca_file, int socket, SSL** ssl)
{
   SSL* s = NULL;

   if (pgagroal_tls_configure_server_ctx(ctx, key_file, cert_file, ca_file))
   {
      goto error;
   }

   s = SSL_new(ctx);

   if (s == NULL)
   {
      goto error;
   }

   if (SSL_set_fd(s, socket) == 0)
   {
      goto error;
   }

   *ssl = s;

   return 0;

error:

   if (s != NULL)
   {
      SSL_shutdown(s);
      SSL_free(s);
   }
   SSL_CTX_free(ctx);

   return 1;
}

int
pgagroal_create_ssl_client(SSL_CTX* ctx, char* key, char* cert, char* root, int socket, SSL** ssl)
{
   SSL* s = NULL;
   bool have_cert = false;
   bool have_rootcert = false;

   if (root != NULL && strlen(root) > 0)
   {
      if (SSL_CTX_load_verify_locations(ctx, root, NULL) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("Couldn't load TLS CA: %s", root);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      have_rootcert = true;
   }

   if (cert != NULL && strlen(cert) > 0)
   {
      if (SSL_CTX_use_certificate_chain_file(ctx, cert) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("Couldn't load TLS certificate: %s", cert);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      have_cert = true;
   }

   s = SSL_new(ctx);

   if (s == NULL)
   {
      goto error;
   }

   if (SSL_set_fd(s, socket) == 0)
   {
      goto error;
   }

   if (have_cert && key != NULL && strlen(key) > 0)
   {
      if (SSL_use_PrivateKey_file(s, key, SSL_FILETYPE_PEM) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("Couldn't load TLS private key: %s", key);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      if (SSL_check_private_key(s) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgagroal_log_error("TLS private key check failed: %s", key);
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }
   }

   if (have_rootcert)
   {
      SSL_set_verify(s, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, NULL);
   }

   *ssl = s;

   return 0;

error:

   if (s != NULL)
   {
      SSL_shutdown(s);
      SSL_free(s);
   }
   SSL_CTX_free(ctx);

   return 1;
}

/* The fixed AEAD parameters for each supported TLS 1.3 suite. */
static int
aead_lookup(int aead, const EVP_CIPHER** cipher, size_t* key_len, const char** hash)
{
   switch (aead)
   {
      case PGAGROAL_TLS_AEAD_AES_128_GCM:
         *cipher = EVP_aes_128_gcm();
         *key_len = 16;
         *hash = "SHA256";
         return PGAGROAL_TLS_OK;
      case PGAGROAL_TLS_AEAD_AES_256_GCM:
         *cipher = EVP_aes_256_gcm();
         *key_len = 32;
         *hash = "SHA384";
         return PGAGROAL_TLS_OK;
      default:
         return PGAGROAL_TLS_ERROR;
   }
}

/* RFC 8446 7.1 HKDF-Expand-Label with an empty context. */
static int
hkdf_expand_label(const char* hash, const unsigned char* secret, size_t secret_len,
                  const char* label, unsigned char* out, size_t out_len)
{
   unsigned char info[256];
   size_t ilen = 0;
   char full[64];
   int flen;
   EVP_KDF* kdf;
   EVP_KDF_CTX* kctx;
   OSSL_PARAM params[5];
   int ok;

   flen = snprintf(full, sizeof(full), "tls13 %s", label);
   if (flen <= 0)
   {
      return PGAGROAL_TLS_ERROR;
   }

   info[ilen++] = (unsigned char)(out_len >> 8);
   info[ilen++] = (unsigned char)(out_len & 0xff);
   info[ilen++] = (unsigned char)flen;
   memcpy(info + ilen, full, (size_t)flen);
   ilen += (size_t)flen;
   info[ilen++] = 0x00;

   kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
   if (kdf == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }
   kctx = EVP_KDF_CTX_new(kdf);
   EVP_KDF_free(kdf);
   if (kctx == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }

   params[0] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_MODE, "EXPAND_ONLY", 0);
   params[1] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, (char*)hash, 0);
   params[2] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (void*)secret, secret_len);
   params[3] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, info, ilen);
   params[4] = OSSL_PARAM_construct_end();

   ok = EVP_KDF_derive(kctx, out, out_len, params) > 0;
   EVP_KDF_CTX_free(kctx);

   return ok ? PGAGROAL_TLS_OK : PGAGROAL_TLS_ERROR;
}

/* Per-record nonce: static IV XOR the 64-bit sequence number (RFC 8446 5.3). */
static void
build_nonce(const unsigned char* iv, uint64_t seq, unsigned char* nonce)
{
   int i;

   memcpy(nonce, iv, 12);
   for (i = 0; i < 8; i++)
   {
      nonce[11 - i] ^= (unsigned char)(seq >> (8 * i));
   }
}

static int
aead_seal(const EVP_CIPHER* cipher, const unsigned char* key, const unsigned char* nonce,
          const unsigned char* aad, size_t aad_len,
          const unsigned char* pt, size_t pt_len,
          unsigned char* ct, unsigned char* tag)
{
   EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
   int len;
   int rc = 0;

   if (c == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }
   if (EVP_EncryptInit_ex(c, cipher, NULL, NULL, NULL) != 1 ||
       EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_SET_IVLEN, 12, NULL) != 1 ||
       EVP_EncryptInit_ex(c, NULL, NULL, key, nonce) != 1 ||
       EVP_EncryptUpdate(c, NULL, &len, aad, (int)aad_len) != 1 ||
       EVP_EncryptUpdate(c, ct, &len, pt, (int)pt_len) != 1 ||
       EVP_EncryptFinal_ex(c, ct + len, &len) != 1 ||
       EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_GET_TAG, 16, tag) != 1)
   {
      goto done;
   }
   rc = 1;
done:
   EVP_CIPHER_CTX_free(c);
   return rc ? PGAGROAL_TLS_OK : PGAGROAL_TLS_ERROR;
}

static int
aead_open(const EVP_CIPHER* cipher, const unsigned char* key, const unsigned char* nonce,
          const unsigned char* aad, size_t aad_len,
          const unsigned char* ct, size_t ct_len,
          const unsigned char* tag, unsigned char* out, int* out_len)
{
   EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
   int len = 0;
   int rc = 0;

   if (c == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }
   if (EVP_DecryptInit_ex(c, cipher, NULL, NULL, NULL) != 1 ||
       EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_SET_IVLEN, 12, NULL) != 1 ||
       EVP_DecryptInit_ex(c, NULL, NULL, key, nonce) != 1 ||
       EVP_DecryptUpdate(c, NULL, &len, aad, (int)aad_len) != 1 ||
       EVP_DecryptUpdate(c, out, &len, ct, (int)ct_len) != 1)
   {
      goto done;
   }
   *out_len = len;
   if (EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_SET_TAG, 16, (void*)tag) != 1 ||
       EVP_DecryptFinal_ex(c, out + len, &len) != 1)
   {
      goto done; /* tag mismatch */
   }
   *out_len += len;
   rc = 1;
done:
   EVP_CIPHER_CTX_free(c);
   return rc ? PGAGROAL_TLS_OK : PGAGROAL_TLS_ERROR;
}

int
pgagroal_tls_record_dir_init(struct tls_record_dir* dir, int aead, const unsigned char* secret, size_t secret_len)
{
   const EVP_CIPHER* cipher;
   size_t key_len;
   const char* hash;

   if (dir == NULL || secret == NULL || secret_len > sizeof(dir->secret))
   {
      return PGAGROAL_TLS_ERROR;
   }
   if (aead_lookup(aead, &cipher, &key_len, &hash) != PGAGROAL_TLS_OK)
   {
      return PGAGROAL_TLS_ERROR;
   }

   memcpy(dir->secret, secret, secret_len);
   dir->secret_len = secret_len;

   if (hkdf_expand_label(hash, secret, secret_len, "key", dir->key, key_len) != PGAGROAL_TLS_OK ||
       hkdf_expand_label(hash, secret, secret_len, "iv", dir->iv, sizeof(dir->iv)) != PGAGROAL_TLS_OK)
   {
      return PGAGROAL_TLS_ERROR;
   }
   dir->seq = 0;

   return PGAGROAL_TLS_OK;
}

int
pgagroal_tls_record_seal(struct tls_record* record, const unsigned char* plaintext, size_t plaintext_len, unsigned char* out, size_t cap, size_t* out_len)
{
   const EVP_CIPHER* cipher;
   size_t key_len;
   const char* hash;
   unsigned char nonce[12];
   unsigned char inner[16384];
   size_t inner_len;
   size_t record_len;

   if (out_len != NULL)
   {
      *out_len = 0;
   }
   if (record == NULL || (plaintext == NULL && plaintext_len > 0) || out == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }
   if (aead_lookup(record->aead, &cipher, &key_len, &hash) != PGAGROAL_TLS_OK)
   {
      return PGAGROAL_TLS_ERROR;
   }

   inner_len = plaintext_len + 1; /* + inner content type */
   if (inner_len > sizeof(inner))
   {
      return PGAGROAL_TLS_ERROR;
   }
   record_len = 5 + inner_len + 16; /* header + ciphertext + tag */
   if (cap < record_len)
   {
      return PGAGROAL_TLS_ERROR;
   }

   memcpy(inner, plaintext, plaintext_len);
   inner[plaintext_len] = 0x17; /* TLSInnerPlaintext content type = application_data */

   out[0] = 0x17;
   out[1] = 0x03;
   out[2] = 0x03;
   out[3] = (unsigned char)((inner_len + 16) >> 8);
   out[4] = (unsigned char)((inner_len + 16) & 0xff);

   build_nonce(record->write.iv, record->write.seq, nonce);

   if (aead_seal(cipher, record->write.key, nonce, out, 5, inner, inner_len,
                 out + 5, out + 5 + inner_len) != PGAGROAL_TLS_OK)
   {
      return PGAGROAL_TLS_ERROR;
   }

   record->write.seq++;
   if (out_len != NULL)
   {
      *out_len = record_len;
   }

   return PGAGROAL_TLS_OK;
}

int
pgagroal_tls_record_open(struct tls_record* record, const unsigned char* rec, size_t rec_len, unsigned char* out, size_t cap, size_t* out_len)
{
   const EVP_CIPHER* cipher;
   size_t key_len;
   const char* hash;
   unsigned char nonce[12];
   unsigned char inner[16384];
   int inner_len = 0;
   size_t body_len;
   size_t ct_len;

   if (out_len != NULL)
   {
      *out_len = 0;
   }
   if (record == NULL || rec == NULL || out == NULL || rec_len < 5 + 16 + 1)
   {
      return PGAGROAL_TLS_ERROR;
   }
   if (aead_lookup(record->aead, &cipher, &key_len, &hash) != PGAGROAL_TLS_OK)
   {
      return PGAGROAL_TLS_ERROR;
   }

   body_len = ((size_t)rec[3] << 8) | (size_t)rec[4];
   if (body_len < 16 || 5 + body_len > rec_len || body_len - 16 > sizeof(inner))
   {
      return PGAGROAL_TLS_ERROR;
   }
   ct_len = body_len - 16;

   build_nonce(record->read.iv, record->read.seq, nonce);

   /* AAD is the 5-byte record header */
   if (aead_open(cipher, record->read.key, nonce, rec, 5,
                 rec + 5, ct_len, rec + 5 + ct_len, inner, &inner_len) != PGAGROAL_TLS_OK)
   {
      return PGAGROAL_TLS_ERROR;
   }

   /* TLSInnerPlaintext = content || content_type || zero padding */
   while (inner_len > 0 && inner[inner_len - 1] == 0x00)
   {
      inner_len--;
   }
   if (inner_len <= 0)
   {
      return PGAGROAL_TLS_ERROR;
   }
   /* Resume only application_data (0x17); fail closed on a post-handshake record
    * or alert rather than hand non-application bytes up. PG sends none mid-session. */
   if (inner[inner_len - 1] != 0x17)
   {
      return PGAGROAL_TLS_ERROR;
   }
   inner_len--; /* drop the content type byte */

   if ((size_t)inner_len > cap)
   {
      return PGAGROAL_TLS_ERROR;
   }
   memcpy(out, inner, (size_t)inner_len);
   record->read.seq++;
   if (out_len != NULL)
   {
      *out_len = (size_t)inner_len;
   }

   return PGAGROAL_TLS_OK;
}

static void
export_dir(unsigned char** p, const struct tls_record_dir* dir)
{
   int i;

   *(*p)++ = (unsigned char)dir->secret_len;
   memcpy(*p, dir->secret, dir->secret_len);
   *p += dir->secret_len;
   for (i = 0; i < 8; i++)
   {
      *(*p)++ = (unsigned char)(dir->seq >> (8 * (7 - i)));
   }
}

static int
import_dir(const unsigned char** p, const unsigned char* end, int aead, struct tls_record_dir* dir)
{
   size_t sl;
   uint64_t seq = 0;
   int i;

   if (*p >= end)
   {
      return PGAGROAL_TLS_ERROR;
   }
   sl = *(*p)++;
   if (sl > sizeof(dir->secret) || *p + sl + 8 > end)
   {
      return PGAGROAL_TLS_ERROR;
   }
   if (pgagroal_tls_record_dir_init(dir, aead, *p, sl) != PGAGROAL_TLS_OK)
   {
      return PGAGROAL_TLS_ERROR;
   }
   *p += sl;
   for (i = 0; i < 8; i++)
   {
      seq = (seq << 8) | (uint64_t)(*(*p)++);
   }
   dir->seq = seq;

   return PGAGROAL_TLS_OK;
}

int
pgagroal_tls_record_export(const struct tls_record* record, unsigned char* buf, size_t cap, size_t* len)
{
   unsigned char* p = buf;
   size_t need;

   if (len != NULL)
   {
      *len = 0;
   }
   if (record == NULL || buf == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }

   need = 2 + (1 + record->read.secret_len + 8) + (1 + record->write.secret_len + 8);
   if (cap < need)
   {
      return PGAGROAL_TLS_ERROR;
   }

   *p++ = 1; /* format version */
   *p++ = (unsigned char)record->aead;
   export_dir(&p, &record->read);
   export_dir(&p, &record->write);

   if (len != NULL)
   {
      *len = (size_t)(p - buf);
   }

   return PGAGROAL_TLS_OK;
}

int
pgagroal_tls_record_import(const unsigned char* buf, size_t len, struct tls_record* record)
{
   const unsigned char* p = buf;
   const unsigned char* end = buf + len;

   if (buf == NULL || record == NULL || len < 2)
   {
      return PGAGROAL_TLS_ERROR;
   }

   memset(record, 0, sizeof(*record));

   if (*p++ != 1) /* format version */
   {
      return PGAGROAL_TLS_ERROR;
   }
   record->aead = *p++;

   if (import_dir(&p, end, record->aead, &record->read) != PGAGROAL_TLS_OK ||
       import_dir(&p, end, record->aead, &record->write) != PGAGROAL_TLS_OK)
   {
      return PGAGROAL_TLS_ERROR;
   }

   return PGAGROAL_TLS_OK;
}

int
pgagroal_tls_harvest(struct tls* tls, struct tls_record* record)
{
   const unsigned char* wsec;
   const unsigned char* rsec;
   int aead;
   uint16_t id;

   if (tls == NULL || tls->ssl == NULL || record == NULL ||
       !tls->handshake_complete || tls->secret_mask != 0x3)
   {
      return PGAGROAL_TLS_ERROR;
   }

   /* TLS 1.3 AEAD suites only */
   id = SSL_CIPHER_get_protocol_id(SSL_get_current_cipher(tls->ssl));
   switch (id)
   {
      case 0x1301:
         aead = PGAGROAL_TLS_AEAD_AES_128_GCM;
         break;
      case 0x1302:
         aead = PGAGROAL_TLS_AEAD_AES_256_GCM;
         break;
      default:
         return PGAGROAL_TLS_ERROR;
   }

   memset(record, 0, sizeof(*record));
   record->aead = aead;

   /* Send with our own role's secret; receive with the peer's. */
   wsec = tls->server ? tls->server_secret : tls->client_secret;
   rsec = tls->server ? tls->client_secret : tls->server_secret;

   if (pgagroal_tls_record_dir_init(&record->write, aead, wsec, tls->secret_len) != PGAGROAL_TLS_OK ||
       pgagroal_tls_record_dir_init(&record->read, aead, rsec, tls->secret_len) != PGAGROAL_TLS_OK)
   {
      return PGAGROAL_TLS_ERROR;
   }

   return PGAGROAL_TLS_OK;
}

int
pgagroal_tls_own(struct tls* tls)
{
   int n;

   if (tls == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }
   if (pgagroal_tls_harvest(tls, &tls->record) != PGAGROAL_TLS_OK)
   {
      pgagroal_log_debug("TLS: record-layer harvest failed; staying OpenSSL-backed (not parkable)");
      return PGAGROAL_TLS_ERROR;
   }

   /* Capture any ciphertext OpenSSL buffered but did not consume during the
    * handshake, so owned_read does not miss records already off the socket. */
   tls->rbuf_len = 0;
   if (tls->rbio != NULL)
   {
      n = BIO_read(tls->rbio, tls->rbuf, (int)sizeof(tls->rbuf));
      if (n > 0)
      {
         tls->rbuf_len = (size_t)n;
      }
   }

   tls->owned = true;

   pgagroal_log_debug("TLS: owning record layer (aead=%d, buffered=%zu bytes)", tls->record.aead, tls->rbuf_len);

   return PGAGROAL_TLS_OK;
}

int
pgagroal_tls_cert_endpoint_hash(SSL* ssl, bool peer, unsigned char* out, size_t cap, size_t* out_len)
{
   X509* cert = NULL;
   const EVP_MD* md = NULL;
   unsigned char hash[EVP_MAX_MD_SIZE];
   unsigned int len = 0;
   int algo_nid = 0;
   int result = PGAGROAL_TLS_ERROR;

   if (out_len != NULL)
   {
      *out_len = 0;
   }
   if (ssl == NULL || out == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }

   /* tls-server-end-point binds to the server's certificate: our own when we
    * are the TLS server, the peer's when we are the client. */
   cert = peer ? SSL_get_peer_certificate(ssl) : SSL_get_certificate(ssl);
   if (cert == NULL)
   {
      goto error;
   }

   /* RFC 5929 4.1: hash with the certificate signature's hash, except MD5 and
    * SHA-1 which are upgraded to SHA-256. */
   if (!X509_get_signature_info(cert, &algo_nid, NULL, NULL, NULL))
   {
      goto error;
   }
   if (algo_nid == NID_md5 || algo_nid == NID_sha1)
   {
      md = EVP_sha256();
   }
   else
   {
      md = EVP_get_digestbynid(algo_nid);
   }
   if (md == NULL)
   {
      goto error;
   }

   if (!X509_digest(cert, md, hash, &len))
   {
      goto error;
   }
   if ((size_t)len > cap)
   {
      goto error;
   }

   memcpy(out, hash, len);
   if (out_len != NULL)
   {
      *out_len = (size_t)len;
   }
   result = PGAGROAL_TLS_OK;

error:

   /* SSL_get_peer_certificate increments the refcount; SSL_get_certificate does not. */
   if (peer && cert != NULL)
   {
      X509_free(cert);
   }

   return result;
}

int
pgagroal_tls_cert_common_name(X509* cert, char** cn)
{
   const X509_NAME* subject = NULL;
   int length = 0;
   char* name = NULL;

   if (cn == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }

   *cn = NULL;

   if (cert == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }

   subject = X509_get_subject_name(cert);
   if (subject == NULL)
   {
      goto error;
   }

   length = X509_NAME_get_text_by_NID(subject, NID_commonName, NULL, 0);
   if (length <= 0)
   {
      goto error;
   }

   name = (char*)calloc(1, length + 1);
   if (name == NULL)
   {
      goto error;
   }

   if (X509_NAME_get_text_by_NID(subject, NID_commonName, name, length + 1) != length)
   {
      goto error;
   }

   /* An embedded NUL would let "alice\0.example.com" pass as "alice" */
   if (strlen(name) != (size_t)length)
   {
      pgagroal_log_warn("TLS certificate Common Name contains a NUL byte");
      goto error;
   }

   *cn = name;

   return PGAGROAL_TLS_OK;

error:

   free(name);

   return PGAGROAL_TLS_ERROR;
}

int
pgagroal_tls_load_crl(SSL_CTX* ctx, char* crl_file)
{
   X509_STORE* store = NULL;

   if (ctx == NULL)
   {
      return PGAGROAL_TLS_ERROR;
   }

   if (crl_file == NULL || strlen(crl_file) == 0)
   {
      return PGAGROAL_TLS_OK;
   }

   store = SSL_CTX_get_cert_store(ctx);
   if (store == NULL || X509_STORE_load_file(store, crl_file) != 1)
   {
      unsigned long err;

      err = ERR_get_error();
      pgagroal_log_error("Couldn't load TLS CRL: %s", crl_file);
      if (ERR_reason_error_string(err) != NULL)
      {
         pgagroal_log_error("Reason: %s", ERR_reason_error_string(err));
      }
      return PGAGROAL_TLS_ERROR;
   }

   /* Check the whole chain, so a revoked intermediate CA is rejected as well */
   if (X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL) != 1)
   {
      pgagroal_log_error("Couldn't enable TLS CRL checking: %s", crl_file);
      return PGAGROAL_TLS_ERROR;
   }

   return PGAGROAL_TLS_OK;
}
