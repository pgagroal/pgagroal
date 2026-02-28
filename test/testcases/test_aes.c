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
#include <pgagroal.h>
#include <aes.h>
#include <mctf.h>

#include <stdlib.h>
#include <string.h>

/**
 * Test: AES-256-CBC encrypt/decrypt round-trip.
 *
 * Encrypts a known plaintext string with a password, then decrypts
 * the ciphertext and verifies the output matches the original input.
 */
MCTF_TEST(test_aes_encrypt_decrypt_roundtrip)
{
   char* plaintext = "pgagroal-test-password-round-trip";
   char* password = "master-key-for-testing";
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;
   int ret_enc = 0;
   int ret_dec = 0;

   ret_enc = pgagroal_encrypt(plaintext, password, &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_CBC);

   MCTF_ASSERT(ret_enc == 0, cleanup, "pgagroal_encrypt should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext, cleanup, "ciphertext should not be NULL");
   MCTF_ASSERT(ciphertext_length > 0, cleanup, "ciphertext_length should be greater than 0");

   ret_dec = pgagroal_decrypt(ciphertext, ciphertext_length, password, &decrypted, ENCRYPTION_AES_256_CBC);

   MCTF_ASSERT(ret_dec == 0, cleanup, "pgagroal_decrypt should succeed");
   MCTF_ASSERT_PTR_NONNULL(decrypted, cleanup, "decrypted should not be NULL");
   MCTF_ASSERT_STR_EQ(decrypted, plaintext, cleanup, "decrypted text should match original plaintext");

cleanup:
   free(ciphertext);
   free(decrypted);
   MCTF_FINISH();
}

/**
 * Test: Salt verification — same password produces different ciphertext.
 *
 * Encrypts the exact same plaintext with the same password twice and
 * verifies that the two ciphertext outputs are different. This proves
 * the 16-byte random salt is working correctly.
 */
MCTF_TEST(test_aes_salt_produces_unique_ciphertext)
{
   char* plaintext = "identical-password-for-salt-test";
   char* password = "master-key-for-testing";
   char* ciphertext_a = NULL;
   int ciphertext_a_length = 0;
   char* ciphertext_b = NULL;
   int ciphertext_b_length = 0;
   int ret_a = 0;
   int ret_b = 0;
   int blobs_are_identical = 0;

   ret_a = pgagroal_encrypt(plaintext, password, &ciphertext_a, &ciphertext_a_length, ENCRYPTION_AES_256_CBC);

   MCTF_ASSERT(ret_a == 0, cleanup, "first pgagroal_encrypt should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext_a, cleanup, "ciphertext_a should not be NULL");
   MCTF_ASSERT(ciphertext_a_length > 0, cleanup, "ciphertext_a_length should be greater than 0");

   ret_b = pgagroal_encrypt(plaintext, password, &ciphertext_b, &ciphertext_b_length, ENCRYPTION_AES_256_CBC);

   MCTF_ASSERT(ret_b == 0, cleanup, "second pgagroal_encrypt should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext_b, cleanup, "ciphertext_b should not be NULL");
   MCTF_ASSERT(ciphertext_b_length > 0, cleanup, "ciphertext_b_length should be greater than 0");

   /* Same plaintext + same password must produce different ciphertext due to random salt */
   if (ciphertext_a_length == ciphertext_b_length)
   {
      blobs_are_identical = (memcmp(ciphertext_a, ciphertext_b, ciphertext_a_length) == 0);
   }

   MCTF_ASSERT(!blobs_are_identical, cleanup,
               "encrypting the same plaintext twice must produce different ciphertext (salt verification)");

cleanup:
   free(ciphertext_a);
   free(ciphertext_b);
   MCTF_FINISH();
}

/**
 * Test: AES-192-CBC encrypt/decrypt round-trip.
 *
 * Verifies the round-trip works for a different AES mode to ensure
 * get_key_length returns the correct key size for AES-192.
 */
MCTF_TEST(test_aes_192_cbc_roundtrip)
{
   char* plaintext = "test-192-cbc-mode-round-trip";
   char* password = "master-key-192-test";
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;
   int ret_enc = 0;
   int ret_dec = 0;

   ret_enc = pgagroal_encrypt(plaintext, password, &ciphertext, &ciphertext_length, ENCRYPTION_AES_192_CBC);

   MCTF_ASSERT(ret_enc == 0, cleanup, "pgagroal_encrypt with AES-192-CBC should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext, cleanup, "ciphertext should not be NULL");

   ret_dec = pgagroal_decrypt(ciphertext, ciphertext_length, password, &decrypted, ENCRYPTION_AES_192_CBC);

   MCTF_ASSERT(ret_dec == 0, cleanup, "pgagroal_decrypt with AES-192-CBC should succeed");
   MCTF_ASSERT_STR_EQ(decrypted, plaintext, cleanup, "decrypted text should match original for AES-192-CBC");

cleanup:
   free(ciphertext);
   free(decrypted);
   MCTF_FINISH();
}

/**
 * Test: AES-256-CTR encrypt/decrypt round-trip.
 *
 * Verifies the round-trip works for CTR mode.
 */
MCTF_TEST(test_aes_256_ctr_roundtrip)
{
   char* plaintext = "test-256-ctr-mode-round-trip";
   char* password = "master-key-ctr-test";
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;
   int ret_enc = 0;
   int ret_dec = 0;

   ret_enc = pgagroal_encrypt(plaintext, password, &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_CTR);

   MCTF_ASSERT(ret_enc == 0, cleanup, "pgagroal_encrypt with AES-256-CTR should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext, cleanup, "ciphertext should not be NULL");

   ret_dec = pgagroal_decrypt(ciphertext, ciphertext_length, password, &decrypted, ENCRYPTION_AES_256_CTR);

   MCTF_ASSERT(ret_dec == 0, cleanup, "pgagroal_decrypt with AES-256-CTR should succeed");
   MCTF_ASSERT_STR_EQ(decrypted, plaintext, cleanup, "decrypted text should match original for AES-256-CTR");

cleanup:
   free(ciphertext);
   free(decrypted);
   MCTF_FINISH();
}

/**
 * Test: Decryption with wrong password fails.
 *
 * Encrypts data with one password and attempts to decrypt with a
 * different password. Verifies that decryption fails.
 */
MCTF_TEST(test_aes_decrypt_wrong_password_fails)
{
   char* plaintext = "secret-data-wrong-password-test";
   char* correct_password = "correct-master-key";
   char* wrong_password = "wrong-master-key";
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;
   int ret_enc = 0;
   int ret_dec = 0;

   ret_enc = pgagroal_encrypt(plaintext, correct_password, &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_CBC);

   MCTF_ASSERT(ret_enc == 0, cleanup, "pgagroal_encrypt should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext, cleanup, "ciphertext should not be NULL");

   ret_dec = pgagroal_decrypt(ciphertext, ciphertext_length, wrong_password, &decrypted, ENCRYPTION_AES_256_CBC);

   MCTF_ASSERT(ret_dec != 0, cleanup, "pgagroal_decrypt with wrong password should fail");

cleanup:
   free(ciphertext);
   free(decrypted);
   MCTF_FINISH();
}

/**
 * Test: Truncated ciphertext is rejected gracefully.
 *
 * Feeds a buffer shorter than the 16-byte salt into pgagroal_decrypt
 * and verifies it returns an error instead of reading out of bounds.
 */
MCTF_TEST(test_aes_decrypt_truncated_ciphertext_fails)
{
   char truncated_buf[10];
   char* password = "master-key-for-testing";
   char* decrypted = NULL;
   int ret = 0;

   memset(truncated_buf, 0xAB, sizeof(truncated_buf));

   ret = pgagroal_decrypt(truncated_buf, sizeof(truncated_buf), password, &decrypted, ENCRYPTION_AES_256_CBC);

   MCTF_ASSERT(ret != 0, cleanup, "pgagroal_decrypt should reject ciphertext shorter than salt length");
   MCTF_ASSERT_PTR_NULL(decrypted, cleanup, "decrypted should be NULL on failure");

cleanup:
   free(decrypted);
   MCTF_FINISH();
}
