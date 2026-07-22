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
 *
 */

/*
 * Behavioural tests for long backend passwords (cloud IAM DB-auth tokens).
 *
 * Cloud managed Postgres (AWS RDS/Aurora IAM auth, Azure AD, GCP IAM) issues
 * short-lived credentials that are far longer than a human password: an AWS RDS
 * IAM token is a presigned SigV4 URL of roughly 1.8 KB. pgagroal must be able to
 * store such a credential for a backend user, load it from an encrypted users
 * file, and hold the auth message that carries it to the backend.
 *
 * These cases pin the observable behaviour, not the constant values:
 *   - accept: a ~1.8 KB IAM-token-sized password survives an encrypt ->
 *     base64 -> users-file -> load round-trip intact;
 *   - reject: a password beyond the supported byte-length limit is still refused;
 *   - invariant: the per-connection auth-message buffer can hold the largest
 *     storable password once wrapped in a PostgreSQL PasswordMessage.
 */

#if defined(__linux__)
#define _XOPEN_SOURCE 700
#define _GNU_SOURCE
#endif

#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include <errno.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <pgagroal.h>
#include <aes.h>
#include <configuration.h>
#include <mctf.h>
#include <security.h>
#include <utils.h>

/* PasswordMessage framing overhead: 'p' (1) + int32 length (4) + trailing NUL (1). */
#define PASSWORD_MESSAGE_OVERHEAD 6

/* An AWS RDS IAM authentication token is a presigned URL of ~1.8 KB. */
#define IAM_TOKEN_LEN 1874

static unsigned char mock_salt[PBKDF2_SALT_LENGTH];
static char original_home[MAX_PATH];
static bool original_home_set = false;
static char temp_home[MAX_PATH];

static int
unlink_cb(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf)
{
   int rv = remove(fpath);

   (void)sb;
   (void)typeflag;
   (void)ftwbuf;

   if (rv)
   {
      perror(fpath);
   }

   return rv;
}

static int
rm_rf(const char* path)
{
   return nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}

static int
setup_mock_master_key(void)
{
   char path[MAX_PATH];
   char dir[MAX_PATH];
   char template[] = "/tmp/pgagroal_pwlen_test.XXXXXX";
   char* enc_key = NULL;
   size_t len1 = 0;
   char* enc_salt = NULL;
   size_t len2 = 0;
   FILE* f = NULL;

   memset(mock_salt, 0xAA, sizeof(mock_salt));
   pgagroal_set_master_salt(mock_salt);

   original_home_set = false;
   if (getenv("HOME") != NULL)
   {
      pgagroal_snprintf(original_home, sizeof(original_home), "%s", getenv("HOME"));
      original_home_set = true;
   }

   if (mkdtemp(template) == NULL)
   {
      return 1;
   }

   pgagroal_snprintf(temp_home, sizeof(temp_home), "%s", template);
   if (setenv("HOME", temp_home, 1) != 0)
   {
      return 1;
   }

   pgagroal_snprintf(dir, sizeof(dir), "%s/.pgagroal", temp_home);
   if (mkdir(dir, S_IRWXU) != 0)
   {
      return 1;
   }
   chmod(dir, 0700);

   pgagroal_snprintf(path, sizeof(path), "%s/.pgagroal/master.key", temp_home);
   f = fopen(path, "w");
   if (f == NULL)
   {
      return 1;
   }

   if (pgagroal_base64_encode("mock-master-key-str", strlen("mock-master-key-str"), &enc_key, &len1) != 0)
   {
      fclose(f);
      return 1;
   }
   if (pgagroal_base64_encode((char*)mock_salt, PBKDF2_SALT_LENGTH, &enc_salt, &len2) != 0)
   {
      free(enc_key);
      fclose(f);
      return 1;
   }
   if (fprintf(f, "%s\n%s\n", enc_key, enc_salt) < 0)
   {
      free(enc_key);
      free(enc_salt);
      fclose(f);
      return 1;
   }
   free(enc_key);
   free(enc_salt);
   fclose(f);
   chmod(path, 0600);
   return 0;
}

static void
teardown_mock_master_key(void)
{
   pgagroal_set_master_salt(NULL);
   if (temp_home[0] != '\0')
   {
      rm_rf(temp_home);
      if (original_home_set)
      {
         setenv("HOME", original_home, 1);
      }
      else
      {
         unsetenv("HOME");
      }
      memset(temp_home, 0, sizeof(temp_home));
   }
}

/* Build a printable, colon-free, NUL-free password of the requested length. */
static char*
make_password(size_t len)
{
   static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~%=&";
   size_t n = sizeof(alphabet) - 1;
   char* p = malloc(len + 1);

   if (p == NULL)
   {
      return NULL;
   }
   for (size_t i = 0; i < len; i++)
   {
      p[i] = alphabet[i % n];
   }
   p[len] = '\0';
   return p;
}

/*
 * Write a users file entry "username:base64(AES-256-GCM(password))" exactly the
 * way pgagroal-admin does, using the mock master key, so the real loader path is
 * exercised end to end. Returns 0 on success and fills out_path.
 */
static int
write_users_file(const char* username, const char* password, char* out_path, size_t out_sz)
{
   char* master_key = NULL;
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* encoded = NULL;
   size_t encoded_length = 0;
   FILE* f = NULL;
   int rc = 1;

   if (pgagroal_get_master_key(&master_key))
   {
      goto done;
   }
   if (pgagroal_encrypt((char*)password, master_key, &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_GCM))
   {
      goto done;
   }
   if (pgagroal_base64_encode(ciphertext, ciphertext_length, &encoded, &encoded_length))
   {
      goto done;
   }

   pgagroal_snprintf(out_path, out_sz, "%s/.pgagroal/users.conf", temp_home);
   f = fopen(out_path, "w");
   if (f == NULL)
   {
      goto done;
   }
   if (fprintf(f, "%s:%s\n", username, encoded) < 0)
   {
      fclose(f);
      goto done;
   }
   fclose(f);
   rc = 0;

done:
   free(master_key);
   free(ciphertext);
   free(encoded);
   return rc;
}

/*
 * Accept: an IAM-token-sized (~1.8 KB) password round-trips through the encrypted
 * users file and is stored intact for the backend user.
 */
MCTF_TEST(test_password_length_accept_iam_token)
{
   char users_path[MAX_PATH];
   char* token = NULL;
   struct main_configuration* config = NULL;
   int ret;

   MCTF_ASSERT(setup_mock_master_key() == 0, cleanup, "mock master key setup failed");

   token = make_password(IAM_TOKEN_LEN);
   MCTF_ASSERT_PTR_NONNULL(token, cleanup, "token allocation failed");

   MCTF_ASSERT(write_users_file("iamuser", token, users_path, sizeof(users_path)) == 0,
               cleanup, "failed to write encrypted users file");

   config = calloc(1, sizeof(struct main_configuration));
   MCTF_ASSERT_PTR_NONNULL(config, cleanup, "config allocation failed");

   ret = pgagroal_read_users_configuration(config, users_path);
   MCTF_ASSERT_INT_EQ(ret, PGAGROAL_CONFIGURATION_STATUS_OK, cleanup,
                      "loading a users file with an IAM-token-sized password should succeed");
   MCTF_ASSERT_INT_EQ(config->number_of_users, 1, cleanup, "exactly one user should be loaded");
   MCTF_ASSERT_STR_EQ(config->users[0].username, "iamuser", cleanup, "username should be stored");
   MCTF_ASSERT_STR_EQ(config->users[0].password, token, cleanup,
                      "the full IAM token should be stored intact");

cleanup:
   free(token);
   free(config);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/*
 * Reject: a password beyond the supported byte-length limit is still refused - the
 * entry is not stored. Guards against removing the upper bound entirely.
 */
MCTF_TEST(test_password_length_reject_over_max)
{
   char users_path[MAX_PATH];
   char* huge = NULL;
   struct main_configuration* config = NULL;

   MCTF_ASSERT(setup_mock_master_key() == 0, cleanup, "mock master key setup failed");

   /* Comfortably beyond MAX_PASSWORD_LENGTH so the byte-length check rejects it. */
   huge = make_password((size_t)MAX_PASSWORD_LENGTH + 1000);
   MCTF_ASSERT_PTR_NONNULL(huge, cleanup, "password allocation failed");

   MCTF_ASSERT(write_users_file("bighead", huge, users_path, sizeof(users_path)) == 0,
               cleanup, "failed to write encrypted users file");

   config = calloc(1, sizeof(struct main_configuration));
   MCTF_ASSERT_PTR_NONNULL(config, cleanup, "config allocation failed");

   pgagroal_read_users_configuration(config, users_path);

   /* Rejected entries are not copied into the users table. */
   MCTF_ASSERT_INT_EQ((int)strlen(config->users[0].password), 0, cleanup,
                      "an over-length password must not be stored");
   MCTF_ASSERT_INT_EQ((int)strlen(config->users[0].username), 0, cleanup,
                      "an over-length entry must not populate the username either");

cleanup:
   free(huge);
   free(config);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/*
 * Invariant: the per-connection auth-message buffer used to relay a password to
 * the backend must be large enough to hold the largest storable password once
 * wrapped in a PostgreSQL PasswordMessage. Without this, fronting a backend that
 * expects an IAM token would overflow connection.security_messages.
 */
MCTF_TEST(test_password_length_security_buffer_fits_password)
{
   size_t max_password_bytes = (size_t)MAX_PASSWORD_LENGTH - 1;
   size_t max_message = max_password_bytes + PASSWORD_MESSAGE_OVERHEAD;

   MCTF_ASSERT((size_t)SECURITY_BUFFER_SIZE >= max_message, cleanup,
               "SECURITY_BUFFER_SIZE must hold a PasswordMessage carrying a max-length password");

cleanup:
   MCTF_FINISH();
}
