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
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
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

   name = X509_NAME_new();
   if (name == NULL ||
       X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                  (const unsigned char*)"localhost", -1, -1, 0) != 1 ||
       X509_set_subject_name(crt, name) != 1 ||
       X509_set_issuer_name(crt, name) != 1)
   {
      X509_NAME_free(name);
      X509_free(crt);
      EVP_PKEY_free(pkey);
      return 1;
   }
   X509_NAME_free(name);

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

// The socket-driver shim interoperates with a stock SSL_set_fd peer over a real socket
MCTF_TEST(test_pgagroal_tls_socket_shim_roundtrip)
{
   const char* msg = "socket driver round trip";
   SSL_CTX* sctx = NULL;
   SSL_CTX* cctx = NULL;
   struct tls* client = NULL;
   pid_t pid = -1;
   int sv[2] = {-1, -1};
   unsigned char out[256];
   size_t nread = 0;

   sctx = tls_test_server_ctx();
   cctx = tls_test_client_ctx();
   MCTF_ASSERT_PTR_NONNULL(sctx, cleanup, "server SSL_CTX creation failed");
   MCTF_ASSERT_PTR_NONNULL(cctx, cleanup, "client SSL_CTX creation failed");

   MCTF_ASSERT_INT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, cleanup, "socketpair failed");

   pid = fork();
   MCTF_ASSERT(pid >= 0, cleanup, "fork failed");

   if (pid == 0)
   {
      /* child: stock socket-bound TLS server -- accept, echo one message, close */
      SSL* ssl = SSL_new(sctx);
      char buf[256];
      int n;

      close(sv[1]);
      SSL_set_fd(ssl, sv[0]);
      if (SSL_accept(ssl) == 1)
      {
         n = SSL_read(ssl, buf, sizeof(buf));
         if (n > 0)
         {
            SSL_write(ssl, buf, n);
         }
      }
      SSL_shutdown(ssl);
      SSL_free(ssl);
      close(sv[0]);
      _exit(0);
   }

   close(sv[0]);
   sv[0] = -1;

   /* parent: drive the client end entirely through the wrapper + socket shim */
   MCTF_ASSERT_INT_EQ(pgagroal_tls_create(cctx, false, &client), PGAGROAL_TLS_OK,
                      cleanup, "client tls context creation failed");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_socket_handshake(client, sv[1]), PGAGROAL_TLS_OK,
                      cleanup, "socket handshake against stock peer failed");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_socket_write(client, sv[1], msg, strlen(msg)), PGAGROAL_TLS_OK,
                      cleanup, "socket write failed");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_socket_read(client, sv[1], out, sizeof(out), &nread),
                      PGAGROAL_TLS_OK, cleanup, "socket read failed");
   MCTF_ASSERT_INT_EQ((int)nread, (int)strlen(msg), cleanup, "echo length mismatch");
   MCTF_ASSERT(memcmp(out, msg, strlen(msg)) == 0, cleanup, "echo payload mismatch");

cleanup:
   if (pid > 0)
   {
      waitpid(pid, NULL, 0);
   }
   pgagroal_tls_free(client);
   if (sv[0] >= 0)
   {
      close(sv[0]);
   }
   if (sv[1] >= 0)
   {
      close(sv[1]);
   }
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

// The wrapper is recoverable from its SSL object, enabling transparent I/O routing
MCTF_TEST(test_pgagroal_tls_from_ssl_backpointer)
{
   SSL_CTX* cctx = NULL;
   struct tls* client = NULL;

   cctx = tls_test_client_ctx();
   MCTF_ASSERT_PTR_NONNULL(cctx, cleanup, "client SSL_CTX creation failed");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_create(cctx, false, &client), PGAGROAL_TLS_OK,
                      cleanup, "client tls context creation failed");

   MCTF_ASSERT(pgagroal_tls_from_ssl(client->ssl) == client, cleanup,
               "from_ssl should recover the owning wrapper");
   MCTF_ASSERT_PTR_NULL(pgagroal_tls_from_ssl(NULL), cleanup,
                        "from_ssl(NULL) should be NULL");

   pgagroal_tls_set_fd(client, 42);
   MCTF_ASSERT_INT_EQ(client->fd, 42, cleanup, "set_fd should store the descriptor");

cleanup:
   pgagroal_tls_free(client);
   if (cctx != NULL)
   {
      SSL_CTX_free(cctx);
   }
   MCTF_FINISH();
}

/* A sender (write dir) and receiver (read dir) sharing one secret: loopback. */
static void
record_mk_pair(struct tls_record* snd, struct tls_record* rcv, int aead, const unsigned char* secret, size_t sl)
{
   memset(snd, 0, sizeof(*snd));
   memset(rcv, 0, sizeof(*rcv));
   snd->aead = aead;
   rcv->aead = aead;
   pgagroal_tls_record_dir_init(&snd->write, aead, secret, sl);
   pgagroal_tls_record_dir_init(&rcv->read, aead, secret, sl);
}

// A sealed record opens back to the original plaintext, advancing the sequence
MCTF_TEST(test_pgagroal_tls_record_roundtrip)
{
   unsigned char secret[32];
   struct tls_record snd, rcv;
   unsigned char rec[512];
   unsigned char pt[512];
   size_t rlen = 0;
   size_t plen = 0;

   RAND_bytes(secret, sizeof(secret));
   record_mk_pair(&snd, &rcv, PGAGROAL_TLS_AEAD_AES_128_GCM, secret, 32);

   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_seal(&snd, (const unsigned char*)"hello", 5, rec, sizeof(rec), &rlen),
                      PGAGROAL_TLS_OK, cleanup, "seal hello");
   MCTF_ASSERT(rec[0] == 0x17, cleanup, "record is application_data");
   MCTF_ASSERT(snd.write.seq == 1, cleanup, "write seq advanced");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_open(&rcv, rec, rlen, pt, sizeof(pt), &plen),
                      PGAGROAL_TLS_OK, cleanup, "open hello");
   MCTF_ASSERT(plen == 5 && memcmp(pt, "hello", 5) == 0, cleanup, "hello round trip");
   MCTF_ASSERT(rcv.read.seq == 1, cleanup, "read seq advanced");

cleanup:
   MCTF_FINISH();
}

// Export/import preserves the sequence state so the stream continues across a move
MCTF_TEST(test_pgagroal_tls_record_serialize)
{
   unsigned char secret[32];
   struct tls_record snd, rcv, snd2;
   unsigned char rec[512];
   unsigned char pt[512];
   unsigned char blob[256];
   size_t rlen = 0;
   size_t plen = 0;
   size_t blen = 0;

   RAND_bytes(secret, sizeof(secret));
   record_mk_pair(&snd, &rcv, PGAGROAL_TLS_AEAD_AES_128_GCM, secret, 32);

   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_seal(&snd, (const unsigned char*)"one", 3, rec, sizeof(rec), &rlen),
                      PGAGROAL_TLS_OK, cleanup, "seal one");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_open(&rcv, rec, rlen, pt, sizeof(pt), &plen),
                      PGAGROAL_TLS_OK, cleanup, "open one");

   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_export(&snd, blob, sizeof(blob), &blen),
                      PGAGROAL_TLS_OK, cleanup, "export");
   memset(&snd2, 0, sizeof(snd2));
   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_import(blob, blen, &snd2),
                      PGAGROAL_TLS_OK, cleanup, "import");
   MCTF_ASSERT(snd2.write.seq == snd.write.seq, cleanup, "sequence preserved across import");

   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_seal(&snd2, (const unsigned char*)"two", 3, rec, sizeof(rec), &rlen),
                      PGAGROAL_TLS_OK, cleanup, "seal two (imported)");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_open(&rcv, rec, rlen, pt, sizeof(pt), &plen),
                      PGAGROAL_TLS_OK, cleanup, "open two");
   MCTF_ASSERT(plen == 3 && memcmp(pt, "two", 3) == 0, cleanup, "two round trip after import");

cleanup:
   MCTF_FINISH();
}

// A tampered record fails AEAD integrity
MCTF_TEST(test_pgagroal_tls_record_tamper)
{
   unsigned char secret[32];
   struct tls_record snd, rcv;
   unsigned char rec[512];
   unsigned char pt[512];
   size_t rlen = 0;
   size_t plen = 0;

   RAND_bytes(secret, sizeof(secret));
   record_mk_pair(&snd, &rcv, PGAGROAL_TLS_AEAD_AES_128_GCM, secret, 32);

   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_seal(&snd, (const unsigned char*)"secret-data", 11, rec, sizeof(rec), &rlen),
                      PGAGROAL_TLS_OK, cleanup, "seal");
   rec[7] ^= 0x01;
   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_open(&rcv, rec, rlen, pt, sizeof(pt), &plen),
                      PGAGROAL_TLS_ERROR, cleanup, "tampered record rejected");

cleanup:
   MCTF_FINISH();
}

// The AES-256-GCM suite (SHA-384, 48-byte secret) seals, opens, and survives export/import
MCTF_TEST(test_pgagroal_tls_record_aes256)
{
   unsigned char secret[48];
   struct tls_record snd, rcv, snd2;
   unsigned char rec[512];
   unsigned char pt[512];
   unsigned char blob[256];
   size_t rlen = 0;
   size_t plen = 0;
   size_t blen = 0;

   RAND_bytes(secret, sizeof(secret));
   record_mk_pair(&snd, &rcv, PGAGROAL_TLS_AEAD_AES_256_GCM, secret, 48);

   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_seal(&snd, (const unsigned char*)"aes256", 6, rec, sizeof(rec), &rlen),
                      PGAGROAL_TLS_OK, cleanup, "seal aes256");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_open(&rcv, rec, rlen, pt, sizeof(pt), &plen),
                      PGAGROAL_TLS_OK, cleanup, "open aes256");
   MCTF_ASSERT(plen == 6 && memcmp(pt, "aes256", 6) == 0, cleanup, "aes256 round trip");

   /* Export/import must preserve the SHA-384 secret length and the sequence */
   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_export(&snd, blob, sizeof(blob), &blen),
                      PGAGROAL_TLS_OK, cleanup, "export aes256");
   memset(&snd2, 0, sizeof(snd2));
   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_import(blob, blen, &snd2),
                      PGAGROAL_TLS_OK, cleanup, "import aes256");
   MCTF_ASSERT(snd2.aead == PGAGROAL_TLS_AEAD_AES_256_GCM, cleanup, "aead preserved across import");
   MCTF_ASSERT(snd2.write.secret_len == 48, cleanup, "sha-384 secret length preserved");

   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_seal(&snd2, (const unsigned char*)"more", 4, rec, sizeof(rec), &rlen),
                      PGAGROAL_TLS_OK, cleanup, "seal after import");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_record_open(&rcv, rec, rlen, pt, sizeof(pt), &plen),
                      PGAGROAL_TLS_OK, cleanup, "open after import");
   MCTF_ASSERT(plen == 4 && memcmp(pt, "more", 4) == 0, cleanup, "round trip after import");

cleanup:
   MCTF_FINISH();
}

static SSL_CTX*
tls_test_server_ctx_tls13(void)
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
   SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
   SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
   SSL_CTX_set_ciphersuites(ctx, "TLS_AES_128_GCM_SHA256");
   SSL_CTX_set_num_tickets(ctx, 0);
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
tls_test_client_ctx_tls13(void)
{
   SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());

   if (ctx == NULL)
   {
      return NULL;
   }
   SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
   SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
   SSL_CTX_set_num_tickets(ctx, 0);
   SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
   return ctx;
}

/* OpenSSL writes one app record; our harvested record context must decrypt it. */
static int
harvest_check_dir(struct tls* writer, struct tls_record* reader, const char* msg)
{
   unsigned char rec[4096];
   unsigned char pt[4096];
   size_t rlen = 0;
   size_t plen = 0;
   size_t written = 0;

   if (pgagroal_tls_write(writer, msg, strlen(msg), &written) != PGAGROAL_TLS_OK)
   {
      return 1;
   }
   if (pgagroal_tls_drain(writer, rec, sizeof(rec), &rlen) != PGAGROAL_TLS_OK || rlen == 0)
   {
      return 1;
   }
   if (pgagroal_tls_record_open(reader, rec, rlen, pt, sizeof(pt), &plen) != PGAGROAL_TLS_OK)
   {
      return 1;
   }
   if (plen != strlen(msg) || memcmp(pt, msg, plen) != 0)
   {
      return 1;
   }
   return 0;
}

// After a TLS 1.3 handshake, the harvested record context decrypts OpenSSL's own records
MCTF_TEST(test_pgagroal_tls_harvest_handoff)
{
   SSL_CTX* sctx = NULL;
   SSL_CTX* cctx = NULL;
   struct tls* s = NULL;
   struct tls* c = NULL;
   struct tls_record srec;
   struct tls_record crec;
   int i;

   sctx = tls_test_server_ctx_tls13();
   cctx = tls_test_client_ctx_tls13();
   MCTF_ASSERT_PTR_NONNULL(sctx, cleanup, "server ctx");
   MCTF_ASSERT_PTR_NONNULL(cctx, cleanup, "client ctx");

   MCTF_ASSERT_INT_EQ(pgagroal_tls_create(sctx, true, &s), PGAGROAL_TLS_OK, cleanup, "server create");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_create(cctx, false, &c), PGAGROAL_TLS_OK, cleanup, "client create");

   for (i = 0; i < 32; i++)
   {
      pgagroal_tls_handshake(c);
      pgagroal_tls_handshake(s);
      tls_test_transfer(c, s);
      tls_test_transfer(s, c);
      if (s->handshake_complete && c->handshake_complete)
      {
         break;
      }
   }
   MCTF_ASSERT(s->handshake_complete && c->handshake_complete, cleanup, "handshake complete");

   MCTF_ASSERT_INT_EQ(pgagroal_tls_harvest(s, &srec), PGAGROAL_TLS_OK, cleanup, "harvest server");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_harvest(c, &crec), PGAGROAL_TLS_OK, cleanup, "harvest client");

   MCTF_ASSERT_INT_EQ(harvest_check_dir(s, &crec, "from the server"), 0, cleanup, "server->client decrypt");
   MCTF_ASSERT_INT_EQ(harvest_check_dir(c, &srec, "from the client"), 0, cleanup, "client->server decrypt");

cleanup:
   pgagroal_tls_free(s);
   pgagroal_tls_free(c);
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

// After pgagroal_tls_own, I/O flows over a real socket via our own record layer
MCTF_TEST(test_pgagroal_tls_owned_io)
{
   SSL_CTX* sctx = NULL;
   SSL_CTX* cctx = NULL;
   struct tls* s = NULL;
   struct tls* c = NULL;
   int sv[2] = {-1, -1};
   unsigned char out[256];
   size_t n = 0;
   int i;

   sctx = tls_test_server_ctx_tls13();
   cctx = tls_test_client_ctx_tls13();
   MCTF_ASSERT_PTR_NONNULL(sctx, cleanup, "server ctx");
   MCTF_ASSERT_PTR_NONNULL(cctx, cleanup, "client ctx");

   MCTF_ASSERT_INT_EQ(pgagroal_tls_create(sctx, true, &s), PGAGROAL_TLS_OK, cleanup, "server create");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_create(cctx, false, &c), PGAGROAL_TLS_OK, cleanup, "client create");

   for (i = 0; i < 32; i++)
   {
      pgagroal_tls_handshake(c);
      pgagroal_tls_handshake(s);
      tls_test_transfer(c, s);
      tls_test_transfer(s, c);
      if (s->handshake_complete && c->handshake_complete)
      {
         break;
      }
   }
   MCTF_ASSERT(s->handshake_complete && c->handshake_complete, cleanup, "handshake complete");

   MCTF_ASSERT_INT_EQ(pgagroal_tls_own(s), PGAGROAL_TLS_OK, cleanup, "own server");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_own(c), PGAGROAL_TLS_OK, cleanup, "own client");
   MCTF_ASSERT(s->owned && c->owned, cleanup, "owned flag set");

   MCTF_ASSERT_INT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, cleanup, "socketpair");
   pgagroal_tls_set_fd(s, sv[0]);
   pgagroal_tls_set_fd(c, sv[1]);

   /* server -> client over our own record layer */
   MCTF_ASSERT_INT_EQ(pgagroal_tls_socket_write(s, sv[0], "ping", 4), PGAGROAL_TLS_OK, cleanup, "owned write s->c");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_socket_read(c, sv[1], out, sizeof(out), &n), PGAGROAL_TLS_OK, cleanup, "owned read c");
   MCTF_ASSERT(n == 4 && memcmp(out, "ping", 4) == 0, cleanup, "s->c via owned layer");

   /* client -> server over our own record layer */
   MCTF_ASSERT_INT_EQ(pgagroal_tls_socket_write(c, sv[1], "pong", 4), PGAGROAL_TLS_OK, cleanup, "owned write c->s");
   MCTF_ASSERT_INT_EQ(pgagroal_tls_socket_read(s, sv[0], out, sizeof(out), &n), PGAGROAL_TLS_OK, cleanup, "owned read s");
   MCTF_ASSERT(n == 4 && memcmp(out, "pong", 4) == 0, cleanup, "c->s via owned layer");

cleanup:
   pgagroal_tls_free(s);
   pgagroal_tls_free(c);
   if (sv[0] >= 0)
   {
      close(sv[0]);
   }
   if (sv[1] >= 0)
   {
      close(sv[1]);
   }
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
