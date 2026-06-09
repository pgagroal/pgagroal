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

#include <tls.h>

#include <stddef.h>
#include <stdlib.h>

#include <openssl/bio.h>

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
