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

#include <mctf.h>
#include <tls.h>

#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/x509.h>

// Generate a throwaway self-signed certificate and key for the server side
static int
tls_test_self_signed(EVP_PKEY** out_key, X509** out_crt)
{
   EVP_PKEY* pkey = EVP_RSA_gen(2048);
   X509* crt = NULL;
   X509_NAME* name = NULL;

   if (pkey == NULL)
   {
      return 1;
   }

   crt = X509_new();
   if (crt == NULL)
   {
      EVP_PKEY_free(pkey);
      return 1;
   }

   ASN1_INTEGER_set(X509_get_serialNumber(crt), 1);
   X509_gmtime_adj(X509_getm_notBefore(crt), 0);
   X509_gmtime_adj(X509_getm_notAfter(crt), 60L * 60L * 24L * 365L);
   X509_set_pubkey(crt, pkey);

   name = X509_get_subject_name(crt);
   X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                              (const unsigned char*)"localhost", -1, -1, 0);
   X509_set_issuer_name(crt, name);

   if (X509_sign(crt, pkey, EVP_sha256()) == 0)
   {
      X509_free(crt);
      EVP_PKEY_free(pkey);
      return 1;
   }

   *out_key = pkey;
   *out_crt = crt;
   return 0;
}

static SSL_CTX*
tls_test_server_ctx(void)
{
   EVP_PKEY* key = NULL;
   X509* crt = NULL;
   SSL_CTX* ctx = NULL;

   if (tls_test_self_signed(&key, &crt))
   {
      return NULL;
   }

   ctx = SSL_CTX_new(TLS_server_method());
   if (ctx == NULL)
   {
      goto error;
   }

   SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

   if (SSL_CTX_use_certificate(ctx, crt) != 1 || SSL_CTX_use_PrivateKey(ctx, key) != 1)
   {
      goto error;
   }

   X509_free(crt);
   EVP_PKEY_free(key);
   return ctx;

error:
   if (ctx != NULL)
   {
      SSL_CTX_free(ctx);
   }
   X509_free(crt);
   EVP_PKEY_free(key);
   return NULL;
}

static SSL_CTX*
tls_test_client_ctx(void)
{
   SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());

   if (ctx == NULL)
   {
      return NULL;
   }

   SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
   SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
   return ctx;
}

// Shuttle all pending ciphertext from one endpoint to the other
static int
tls_test_transfer(struct tls* from, struct tls* to)
{
   unsigned char buf[16384];
   size_t produced = 0;
   size_t consumed = 0;

   while (pgagroal_tls_drain(from, buf, sizeof(buf), &produced) == PGAGROAL_TLS_OK && produced > 0)
   {
      if (pgagroal_tls_feed(to, buf, produced, &consumed) != PGAGROAL_TLS_OK)
      {
         return 1;
      }
   }
   return 0;
}

// Drive both handshakes to completion through the memory BIOs
static int
tls_test_handshake(struct tls* client, struct tls* server)
{
   for (int i = 0; i < 32; i++)
   {
      int cr = pgagroal_tls_handshake(client);
      int sr = pgagroal_tls_handshake(server);

      if (cr == PGAGROAL_TLS_ERROR || sr == PGAGROAL_TLS_ERROR)
      {
         return 1;
      }

      if (tls_test_transfer(client, server) || tls_test_transfer(server, client))
      {
         return 1;
      }

      if (cr == PGAGROAL_TLS_OK && sr == PGAGROAL_TLS_OK)
      {
         return 0;
      }
   }
   return 1;
}

// A handshake completes between two endpoints backed only by memory BIOs
MCTF_TEST(test_pgagroal_tls_handshake_over_mem_bios)
{
   SSL_CTX* sctx = NULL;
   SSL_CTX* cctx = NULL;
   struct tls* server = NULL;
   struct tls* client = NULL;

   sctx = tls_test_server_ctx();
   cctx = tls_test_client_ctx();
   MCTF_ASSERT_PTR_NONNULL(sctx, cleanup, "server SSL_CTX creation failed");
   MCTF_ASSERT_PTR_NONNULL(cctx, cleanup, "client SSL_CTX creation failed");

   MCTF_ASSERT_INT_EQ(pgagroal_tls_create(cctx, false, &client), PGAGROAL_TLS_OK,
                      cleanup, "client tls context creation failed");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_create(sctx, true, &server), PGAGROAL_TLS_OK,
                      cleanup, "server tls context creation failed");

   MCTF_ASSERT(!client->handshake_complete, cleanup, "client handshake should not start complete");
   MCTF_ASSERT(!server->handshake_complete, cleanup, "server handshake should not start complete");

   MCTF_ASSERT_INT_EQ(tls_test_handshake(client, server), 0, cleanup,
                      "handshake did not complete over memory BIOs");

   MCTF_ASSERT(client->handshake_complete, cleanup, "client handshake_complete not set");
   MCTF_ASSERT(server->handshake_complete, cleanup, "server handshake_complete not set");

cleanup:
   pgagroal_tls_free(client);
   pgagroal_tls_free(server);
   if (sctx != NULL)
   {
      SSL_CTX_free(sctx);
   }
   if (cctx != NULL)
   {
      SSL_CTX_free(cctx);
   }
   MCTF_FINISH();
}

// Application data round-trips in both directions
MCTF_TEST(test_pgagroal_tls_app_data_roundtrip)
{
   const char* c2s = "client to server payload";
   const char* s2c = "server to client payload";
   SSL_CTX* sctx = NULL;
   SSL_CTX* cctx = NULL;
   struct tls* server = NULL;
   struct tls* client = NULL;
   unsigned char out[256];
   size_t written = 0;
   size_t nread = 0;

   sctx = tls_test_server_ctx();
   cctx = tls_test_client_ctx();
   MCTF_ASSERT_PTR_NONNULL(sctx, cleanup, "server SSL_CTX creation failed");
   MCTF_ASSERT_PTR_NONNULL(cctx, cleanup, "client SSL_CTX creation failed");

   MCTF_ASSERT_INT_EQ(pgagroal_tls_create(cctx, false, &client), PGAGROAL_TLS_OK,
                      cleanup, "client tls context creation failed");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_create(sctx, true, &server), PGAGROAL_TLS_OK,
                      cleanup, "server tls context creation failed");
   MCTF_ASSERT_INT_EQ(tls_test_handshake(client, server), 0, cleanup,
                      "handshake did not complete");

   MCTF_ASSERT_INT_EQ(pgagroal_tls_write(client, c2s, strlen(c2s), &written),
                      PGAGROAL_TLS_OK, cleanup, "client write failed");
   MCTF_ASSERT_INT_EQ((int)written, (int)strlen(c2s), cleanup, "client short write");
   MCTF_ASSERT_INT_EQ(tls_test_transfer(client, server), 0, cleanup, "c2s transfer failed");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_read(server, out, sizeof(out), &nread),
                      PGAGROAL_TLS_OK, cleanup, "server read failed");
   MCTF_ASSERT_INT_EQ((int)nread, (int)strlen(c2s), cleanup, "server short read");
   MCTF_ASSERT(memcmp(out, c2s, strlen(c2s)) == 0, cleanup, "c2s payload mismatch");

   written = 0;
   nread = 0;
   memset(out, 0, sizeof(out));
   MCTF_ASSERT_INT_EQ(pgagroal_tls_write(server, s2c, strlen(s2c), &written),
                      PGAGROAL_TLS_OK, cleanup, "server write failed");
   MCTF_ASSERT_INT_EQ((int)written, (int)strlen(s2c), cleanup, "server short write");
   MCTF_ASSERT_INT_EQ(tls_test_transfer(server, client), 0, cleanup, "s2c transfer failed");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_read(client, out, sizeof(out), &nread),
                      PGAGROAL_TLS_OK, cleanup, "client read failed");
   MCTF_ASSERT_INT_EQ((int)nread, (int)strlen(s2c), cleanup, "client short read");
   MCTF_ASSERT(memcmp(out, s2c, strlen(s2c)) == 0, cleanup, "s2c payload mismatch");

cleanup:
   pgagroal_tls_free(client);
   pgagroal_tls_free(server);
   if (sctx != NULL)
   {
      SSL_CTX_free(sctx);
   }
   if (cctx != NULL)
   {
      SSL_CTX_free(cctx);
   }
   MCTF_FINISH();
}

// A payload larger than a single TLS record survives the pump intact
MCTF_TEST(test_pgagroal_tls_large_payload)
{
   const size_t total = 64 * 1024;
   SSL_CTX* sctx = NULL;
   SSL_CTX* cctx = NULL;
   struct tls* server = NULL;
   struct tls* client = NULL;
   unsigned char* msg = NULL;
   unsigned char* got = NULL;
   size_t sent = 0;
   size_t received = 0;

   msg = (unsigned char*)malloc(total);
   got = (unsigned char*)malloc(total);
   MCTF_ASSERT_PTR_NONNULL(msg, cleanup, "alloc msg failed");
   MCTF_ASSERT_PTR_NONNULL(got, cleanup, "alloc got failed");
   for (size_t i = 0; i < total; i++)
   {
      msg[i] = (unsigned char)(i * 31 + 7);
   }

   sctx = tls_test_server_ctx();
   cctx = tls_test_client_ctx();
   MCTF_ASSERT_PTR_NONNULL(sctx, cleanup, "server SSL_CTX creation failed");
   MCTF_ASSERT_PTR_NONNULL(cctx, cleanup, "client SSL_CTX creation failed");

   MCTF_ASSERT_INT_EQ(pgagroal_tls_create(cctx, false, &client), PGAGROAL_TLS_OK,
                      cleanup, "client tls context creation failed");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_create(sctx, true, &server), PGAGROAL_TLS_OK,
                      cleanup, "server tls context creation failed");
   MCTF_ASSERT_INT_EQ(tls_test_handshake(client, server), 0, cleanup,
                      "handshake did not complete");

   while (sent < total)
   {
      size_t written = 0;
      int rc = pgagroal_tls_write(client, msg + sent, total - sent, &written);
      MCTF_ASSERT(rc == PGAGROAL_TLS_OK, cleanup, "large write failed");
      sent += written;
   }
   MCTF_ASSERT_INT_EQ(tls_test_transfer(client, server), 0, cleanup, "large transfer failed");

   while (received < total)
   {
      size_t nread = 0;
      int rc = pgagroal_tls_read(server, got + received, total - received, &nread);
      if (rc == PGAGROAL_TLS_WANT_IO)
      {
         break;
      }
      MCTF_ASSERT(rc == PGAGROAL_TLS_OK, cleanup, "large read failed");
      received += nread;
   }

   MCTF_ASSERT_INT_EQ((int)received, (int)total, cleanup, "did not receive full payload");
   MCTF_ASSERT(memcmp(msg, got, total) == 0, cleanup, "large payload mismatch");

cleanup:
   free(msg);
   free(got);
   pgagroal_tls_free(client);
   pgagroal_tls_free(server);
   if (sctx != NULL)
   {
      SSL_CTX_free(sctx);
   }
   if (cctx != NULL)
   {
      SSL_CTX_free(cctx);
   }
   MCTF_FINISH();
}

// Malformed ciphertext is reported as an error rather than crashing
MCTF_TEST(test_pgagroal_tls_rejects_garbage)
{
   const unsigned char garbage[64] = {0xde, 0xad, 0xbe, 0xef};
   SSL_CTX* sctx = NULL;
   struct tls* server = NULL;
   size_t consumed = 0;
   int rc;

   sctx = tls_test_server_ctx();
   MCTF_ASSERT_PTR_NONNULL(sctx, cleanup, "server SSL_CTX creation failed");

   MCTF_ASSERT_INT_EQ(pgagroal_tls_create(sctx, true, &server), PGAGROAL_TLS_OK,
                      cleanup, "server tls context creation failed");

   MCTF_ASSERT_INT_EQ(pgagroal_tls_feed(server, garbage, sizeof(garbage), &consumed),
                      PGAGROAL_TLS_OK, cleanup, "feeding bytes should succeed at the BIO layer");

   rc = pgagroal_tls_handshake(server);
   MCTF_ASSERT(rc == PGAGROAL_TLS_ERROR, cleanup,
               "handshake on malformed ClientHello must report an error");
   MCTF_ASSERT(!server->handshake_complete, cleanup,
               "handshake must not be marked complete on garbage input");

cleanup:
   pgagroal_tls_free(server);
   if (sctx != NULL)
   {
      SSL_CTX_free(sctx);
   }
   MCTF_FINISH();
}
