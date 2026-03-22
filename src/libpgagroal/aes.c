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
#include <aes.h>
#include <logging.h>
#include <security.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <pthread.h>
#include <string.h>

static int is_gcm(int mode);
static int get_tag_length(int mode);
static int derive_key_iv(char* password, unsigned char* salt, unsigned char* key, unsigned char* iv, int mode);
static int aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length, int mode);
static int aes_decrypt(char* ciphertext, int ciphertext_length, unsigned char* key, unsigned char* iv, char** plaintext, int mode);
static const EVP_CIPHER* (*get_cipher(int mode))(void);
static int get_key_length(int mode);
static int encrypt_decrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** res_buffer, size_t* res_size, int enc, int mode);

static _Thread_local unsigned char master_key_cache[EVP_MAX_KEY_LENGTH];
static _Thread_local unsigned char cached_password_hash[EVP_MAX_MD_SIZE];
static _Thread_local unsigned int cached_password_hash_len = 0;
static _Thread_local bool master_key_cached = false;

static unsigned char master_salt_cache[PBKDF2_SALT_LENGTH];
static bool master_salt_set = false;

void
pgagroal_set_master_salt(unsigned char* salt)
{
   if (salt == NULL && !master_salt_set)
   {
      return;
   }

   if (salt != NULL && master_salt_set && memcmp(master_salt_cache, salt, PBKDF2_SALT_LENGTH) == 0)
   {
      return;
   }

   /* If setting a new salt or clearing, invalidate caches */
   pgagroal_clear_aes_cache();

   if (salt != NULL)
   {
      memcpy(master_salt_cache, salt, PBKDF2_SALT_LENGTH);
      master_salt_set = true;
   }
   else
   {
      pgagroal_cleanse(master_salt_cache, sizeof(master_salt_cache));
      master_salt_set = false;
   }
}

int
pgagroal_encrypt(char* plaintext, char* password, char** ciphertext, int* ciphertext_length, int mode)
{
   if (ciphertext == NULL || ciphertext_length == NULL)
   {
      return 1;
   }

   *ciphertext = NULL;
   *ciphertext_length = 0;
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[PBKDF2_SALT_LENGTH];
   char* encrypted = NULL;
   int encrypted_length = 0;
   char* output = NULL;
   int ret = 1;

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   /* Generate a cryptographically random salt */
   if (RAND_bytes(salt, PBKDF2_SALT_LENGTH) != 1)
   {
      goto cleanup;
   }

   if (derive_key_iv(password, salt, key, iv, mode) != 0)
   {
      goto cleanup;
   }

   if (aes_encrypt(plaintext, key, iv, &encrypted, &encrypted_length, mode) != 0)
   {
      goto cleanup;
   }

   /* Prepend salt and IV to ciphertext: [salt][iv][encrypted] */
   output = malloc(PBKDF2_SALT_LENGTH + PBKDF2_IV_LENGTH + encrypted_length);
   if (output == NULL)
   {
      goto cleanup;
   }

   memcpy(output, salt, PBKDF2_SALT_LENGTH);
   memcpy(output + PBKDF2_SALT_LENGTH, iv, PBKDF2_IV_LENGTH);
   memcpy(output + PBKDF2_SALT_LENGTH + PBKDF2_IV_LENGTH, encrypted, encrypted_length);

   *ciphertext = output;
   *ciphertext_length = PBKDF2_SALT_LENGTH + PBKDF2_IV_LENGTH + encrypted_length;
   ret = 0;

cleanup:
   free(encrypted);

   /* Wipe key material from stack */
   pgagroal_cleanse(key, sizeof(key));
   pgagroal_cleanse(iv, sizeof(iv));

   return ret;
}

int
pgagroal_decrypt(char* ciphertext, int ciphertext_length, char* password, char** plaintext, int mode)
{
   if (plaintext == NULL)
   {
      return 1;
   }

   *plaintext = NULL;
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[PBKDF2_SALT_LENGTH];

   /* The ciphertext must be at least salt_length + iv_length + tag_length */
   if (ciphertext_length < PBKDF2_SALT_LENGTH + PBKDF2_IV_LENGTH + get_tag_length(mode))
   {
      return 1;
   }

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   /* Extract salt from the first PBKDF2_SALT_LENGTH bytes */
   memcpy(salt, ciphertext, PBKDF2_SALT_LENGTH);

   /* Extract IV from the next PBKDF2_IV_LENGTH bytes */
   memcpy(iv, ciphertext + PBKDF2_SALT_LENGTH, PBKDF2_IV_LENGTH);

   int ret = 1;

   if (derive_key_iv(password, salt, key, NULL, mode) != 0)
   {
      goto cleanup;
   }

   ret = aes_decrypt(ciphertext + PBKDF2_SALT_LENGTH + PBKDF2_IV_LENGTH,
                     ciphertext_length - PBKDF2_SALT_LENGTH - PBKDF2_IV_LENGTH,
                     key, iv, plaintext, mode);

cleanup:
   /* Wipe key material from stack */
   pgagroal_cleanse(key, sizeof(key));
   pgagroal_cleanse(iv, sizeof(iv));

   return ret;
}

// [private]
static int
derive_key_iv(char* password, unsigned char* salt, unsigned char* key, unsigned char* iv, int mode)
{
   int key_length;
   int iv_length;
   unsigned char derived[EVP_MAX_KEY_LENGTH + EVP_MAX_IV_LENGTH];
   size_t password_length = strlen(password);
   unsigned char ms[PBKDF2_SALT_LENGTH];

   if (password_length >= MAX_PASSWORD_LENGTH || password_length > INT_MAX)
   {
      pgagroal_cleanse(ms, sizeof(ms));
      return 1;
   }

   if (master_salt_set)
   {
      memcpy(ms, master_salt_cache, PBKDF2_SALT_LENGTH);
   }
   else
   {
      pgagroal_cleanse(ms, sizeof(ms));
      pgagroal_log_error("derive_key_iv: Master salt is not set, cannot derive key securely");
      return 1;
   }

   key_length = get_key_length(mode);
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);
   if (cipher_fp == NULL)
   {
      pgagroal_cleanse(ms, sizeof(ms));
      return 1;
   }
   iv_length = EVP_CIPHER_iv_length(cipher_fp());

   /* Step 1: Derive Master Key (Heavily cached) */
   unsigned char current_hash[EVP_MAX_MD_SIZE];
   unsigned int current_hash_len = 0;
   if (!EVP_Digest(password, password_length, current_hash, &current_hash_len, EVP_sha256(), NULL))
   {
      pgagroal_cleanse(ms, sizeof(ms));
      return 1;
   }

   if (!master_key_cached || current_hash_len != cached_password_hash_len || memcmp(cached_password_hash, current_hash, current_hash_len) != 0)
   {
      if (!PKCS5_PBKDF2_HMAC(password, password_length,
                             ms, PBKDF2_SALT_LENGTH,
                             PBKDF2_ITERATIONS,
                             EVP_sha256(),
                             EVP_MAX_KEY_LENGTH,
                             master_key_cache))
      {
         pgagroal_cleanse(current_hash, sizeof(current_hash));
         pgagroal_cleanse(ms, sizeof(ms));
         return 1;
      }
      memcpy(cached_password_hash, current_hash, current_hash_len);
      cached_password_hash_len = current_hash_len;
      master_key_cached = true;
   }
   pgagroal_cleanse(current_hash, sizeof(current_hash));

   /* Step 2: Derive File/Session Key (Fast) */
   if (!PKCS5_PBKDF2_HMAC((char*)master_key_cache, EVP_MAX_KEY_LENGTH,
                          salt, PBKDF2_SALT_LENGTH,
                          1,
                          EVP_sha256(),
                          key_length + iv_length,
                          derived))
   {
      pgagroal_cleanse(ms, sizeof(ms));
      return 1;
   }

   memcpy(key, derived, key_length);
   if (iv != NULL)
   {
      memcpy(iv, derived + key_length, iv_length);
   }

   /* Wipe sensitive derived material */
   pgagroal_cleanse(derived, sizeof(derived));
   /* Wipe stack copy of salt */
   pgagroal_cleanse(ms, sizeof(ms));

   return 0;
}

void
pgagroal_clear_aes_cache(void)
{
   pgagroal_cleanse(master_key_cache, sizeof(master_key_cache));
   pgagroal_cleanse(cached_password_hash, sizeof(cached_password_hash));
   cached_password_hash_len = 0;
   master_key_cached = false;
}

/**
 * Ensure AES caches are wiped when the library or process is unloaded.
 */
static void __attribute__((destructor))
pgagroal_aes_cache_destructor(void)
{
   pgagroal_clear_aes_cache();
}

// [private]
static int
aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length, int mode)
{
   EVP_CIPHER_CTX* ctx = NULL;
   int length;
   size_t size;
   unsigned char* ct = NULL;
   int ct_length = 0;
   int gcm = is_gcm(mode);
   int tag_len = get_tag_length(mode);
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);

   if (ciphertext == NULL || ciphertext_length == NULL)
   {
      return 1;
   }

   *ciphertext = NULL;
   *ciphertext_length = 0;

   if (cipher_fp == NULL)
   {
      goto error;
   }

   if (!(ctx = EVP_CIPHER_CTX_new()))
   {
      goto error;
   }

   if (EVP_EncryptInit_ex(ctx, cipher_fp(), NULL, key, iv) != 1)
   {
      goto error;
   }

   size = strlen(plaintext) + tag_len + EVP_CIPHER_block_size(cipher_fp());
   ct = malloc(size);

   if (ct == NULL)
   {
      goto error;
   }

   memset(ct, 0, size);

   if (EVP_EncryptUpdate(ctx,
                         ct, &length,
                         (unsigned char*)plaintext, strlen((char*)plaintext)) != 1)
   {
      goto error;
   }

   ct_length = length;

   if (EVP_EncryptFinal_ex(ctx, ct + length, &length) != 1)
   {
      goto error;
   }

   ct_length += length;

   if (gcm)
   {
      if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag_len, ct + ct_length) != 1)
      {
         goto error;
      }
      ct_length += tag_len;
   }

   EVP_CIPHER_CTX_free(ctx);

   *ciphertext = (char*)ct;
   *ciphertext_length = ct_length;

   return 0;

error:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   free(ct);

   return 1;
}

// [private]
static int
aes_decrypt(char* ciphertext, int ciphertext_length, unsigned char* key, unsigned char* iv, char** plaintext, int mode)
{
   EVP_CIPHER_CTX* ctx = NULL;
   int plaintext_length;
   int length;
   size_t size;
   char* pt = NULL;
   int gcm = is_gcm(mode);
   int tag_len = get_tag_length(mode);
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);

   if (plaintext == NULL)
   {
      return 1;
   }

   *plaintext = NULL;

   if (cipher_fp == NULL)
   {
      return 1;
   }

   if (gcm && ciphertext_length < tag_len)
   {
      return 1;
   }

   if (!(ctx = EVP_CIPHER_CTX_new()))
   {
      goto error;
   }

   if (EVP_DecryptInit_ex(ctx, cipher_fp(), NULL, key, iv) != 1)
   {
      goto error;
   }

   size = ciphertext_length + EVP_CIPHER_block_size(cipher_fp());
   pt = malloc(size);

   if (pt == NULL)
   {
      goto error;
   }

   memset(pt, 0, size);

   if (EVP_DecryptUpdate(ctx,
                         (unsigned char*)pt, &length,
                         (unsigned char*)ciphertext, ciphertext_length - tag_len) != 1)
   {
      goto error;
   }

   plaintext_length = length;

   if (gcm)
   {
      if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag_len, ciphertext + ciphertext_length - tag_len) != 1)
      {
         goto error;
      }
   }

   if (EVP_DecryptFinal_ex(ctx, (unsigned char*)pt + length, &length) != 1)
   {
      goto error;
   }

   plaintext_length += length;

   EVP_CIPHER_CTX_free(ctx);

   pt[plaintext_length] = 0;
   *plaintext = pt;

   return 0;

error:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   free(pt);

   return 1;
}

static const EVP_CIPHER* (*get_cipher(int mode))(void)
{
   if (mode == ENCRYPTION_AES_256_GCM)
   {
      return &EVP_aes_256_gcm;
   }
   if (mode == ENCRYPTION_AES_192_GCM)
   {
      return &EVP_aes_192_gcm;
   }
   if (mode == ENCRYPTION_AES_128_GCM)
   {
      return &EVP_aes_128_gcm;
   }
   return NULL;
}

// [private]
static int
get_key_length(int mode)
{
   switch (mode)
   {
      case ENCRYPTION_AES_256_GCM:
         return 32;
      case ENCRYPTION_AES_192_GCM:
         return 24;
      case ENCRYPTION_AES_128_GCM:
         return 16;
      default:
         return 32;
   }
}

static int
is_gcm(int mode)
{
   switch (mode)
   {
      case ENCRYPTION_AES_256_GCM:
      case ENCRYPTION_AES_192_GCM:
      case ENCRYPTION_AES_128_GCM:
         return 1;
      default:
         return 0;
   }
}

static int
get_tag_length(int mode)
{
   if (is_gcm(mode))
   {
      return GCM_TAG_LENGTH;
   }

   return 0;
}

int
pgagroal_encrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** enc_buffer, size_t* enc_size, int mode)
{
   return encrypt_decrypt_buffer(origin_buffer, origin_size, enc_buffer, enc_size, 1, mode);
}

int
pgagroal_decrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** dec_buffer, size_t* dec_size, int mode)
{
   return encrypt_decrypt_buffer(origin_buffer, origin_size, dec_buffer, dec_size, 0, mode);
}

static int
encrypt_decrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** res_buffer, size_t* res_size, int enc, int mode)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[PBKDF2_SALT_LENGTH];
   char* master_key = NULL;
   EVP_CIPHER_CTX* ctx = NULL;
   const EVP_CIPHER* (*cipher_fp)(void) = NULL;
   size_t cipher_block_size = 0;
   size_t outbuf_size = 0;
   size_t outl = 0;
   size_t f_len = 0;
   unsigned char* actual_input = NULL;
   size_t actual_input_size = 0;
   unsigned char* out_buf = NULL;
   int tag_len = 0;
   int gcm = is_gcm(mode);

   if (res_buffer == NULL || res_size == NULL)
   {
      return 1;
   }

   *res_buffer = NULL;
   *res_size = 0;

   cipher_fp = get_cipher(mode);
   if (cipher_fp == NULL)
   {
      pgagroal_log_error("Invalid encryption method specified");
      goto error;
   }

   cipher_block_size = EVP_CIPHER_block_size(cipher_fp());
   tag_len = get_tag_length(mode);

   if (pgagroal_get_master_key(NULL, &master_key))
   {
      pgagroal_log_error("pgagroal_get_master_key: Invalid master key");
      goto error;
   }

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   if (enc == 1)
   {
      /* Encryption: generate a random salt */
      if (RAND_bytes(salt, PBKDF2_SALT_LENGTH) != 1)
      {
         pgagroal_log_error("RAND_bytes: Failed to generate salt");
         goto error;
      }

      if (derive_key_iv(master_key, salt, key, iv, mode) != 0)
      {
         pgagroal_log_error("derive_key_iv: Failed to derive key and iv");
         goto error;
      }

      /* Output buffer: salt + IV + encrypted data + padding + tag */
      outbuf_size = PBKDF2_SALT_LENGTH + PBKDF2_IV_LENGTH + origin_size + cipher_block_size + tag_len;
      out_buf = (unsigned char*)malloc(outbuf_size + 1);
      if (out_buf == NULL)
      {
         pgagroal_log_error("pgagroal_encrypt_decrypt_buffer: Allocation failure");
         goto error;
      }

      /* Prepend salt and IV */
      memcpy(out_buf, salt, PBKDF2_SALT_LENGTH);
      memcpy(out_buf + PBKDF2_SALT_LENGTH, iv, PBKDF2_IV_LENGTH);

      if (!(ctx = EVP_CIPHER_CTX_new()))
      {
         pgagroal_log_error("EVP_CIPHER_CTX_new: Failed to create context");
         goto error;
      }

      if (EVP_CipherInit_ex(ctx, cipher_fp(), NULL, key, iv, enc) == 0)
      {
         pgagroal_log_error("EVP_CipherInit_ex: Failed to initialize cipher context");
         goto error;
      }

      if (origin_size > INT_MAX)
      {
         pgagroal_log_error("encrypt_decrypt_buffer: Input size exceeds INT_MAX");
         goto error;
      }

      int outl_tmp = 0;
      if (EVP_CipherUpdate(ctx, out_buf + PBKDF2_SALT_LENGTH + PBKDF2_IV_LENGTH, &outl_tmp, origin_buffer, (int)origin_size) == 0)
      {
         pgagroal_log_error("EVP_CipherUpdate: Failed to process data");
         goto error;
      }
      outl = (size_t)outl_tmp;

      *res_size = PBKDF2_SALT_LENGTH + PBKDF2_IV_LENGTH + outl;

      int f_len_tmp = 0;
      if (EVP_CipherFinal_ex(ctx, out_buf + PBKDF2_SALT_LENGTH + PBKDF2_IV_LENGTH + outl, &f_len_tmp) == 0)
      {
         pgagroal_log_error("EVP_CipherFinal_ex: Failed to finalize operation");
         goto error;
      }
      f_len = (size_t)f_len_tmp;

      *res_size += f_len;

      if (gcm)
      {
         if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag_len, out_buf + *res_size) != 1)
         {
            pgagroal_log_error("EVP_CIPHER_CTX_ctrl: Failed to get GCM tag");
            goto error;
         }
         *res_size += tag_len;
      }
   }
   else
   {
      /* Decryption: extract salt + iv + data + tag */
      if (origin_size < PBKDF2_SALT_LENGTH + PBKDF2_IV_LENGTH + tag_len)
      {
         pgagroal_log_error("encrypt_decrypt_buffer: Input too short for decryption");
         goto error;
      }
      memcpy(salt, origin_buffer, PBKDF2_SALT_LENGTH);
      memcpy(iv, origin_buffer + PBKDF2_SALT_LENGTH, PBKDF2_IV_LENGTH);

      actual_input = origin_buffer + PBKDF2_SALT_LENGTH + PBKDF2_IV_LENGTH;
      actual_input_size = origin_size - PBKDF2_SALT_LENGTH - PBKDF2_IV_LENGTH - tag_len;

      if (derive_key_iv(master_key, salt, key, NULL, mode) != 0)
      {
         pgagroal_log_error("derive_key_iv: Failed to derive key and iv");
         goto error;
      }

      outbuf_size = actual_input_size;
      out_buf = (unsigned char*)malloc(outbuf_size + 1);
      if (out_buf == NULL)
      {
         pgagroal_log_error("pgagroal_encrypt_decrypt_buffer: Allocation failure");
         goto error;
      }

      if (!(ctx = EVP_CIPHER_CTX_new()))
      {
         pgagroal_log_error("EVP_CIPHER_CTX_new: Failed to create context");
         goto error;
      }

      if (EVP_CipherInit_ex(ctx, cipher_fp(), NULL, key, iv, enc) == 0)
      {
         pgagroal_log_error("EVP_CipherInit_ex: Failed to initialize cipher context");
         goto error;
      }

      if (actual_input_size > INT_MAX)
      {
         pgagroal_log_error("encrypt_decrypt_buffer: Actual input size exceeds INT_MAX");
         goto error;
      }

      int outl_tmp = 0;
      if (EVP_CipherUpdate(ctx, out_buf, &outl_tmp, actual_input, (int)actual_input_size) == 0)
      {
         pgagroal_log_error("EVP_CipherUpdate: Failed to process data");
         goto error;
      }
      outl = (size_t)outl_tmp;

      *res_size = outl;

      if (gcm)
      {
         if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag_len, origin_buffer + origin_size - tag_len) != 1)
         {
            pgagroal_log_error("EVP_CIPHER_CTX_ctrl: Failed to set GCM tag");
            goto error;
         }
      }

      int f_len_tmp = 0;
      if (EVP_CipherFinal_ex(ctx, out_buf + outl, &f_len_tmp) == 0)
      {
         pgagroal_log_error("EVP_CipherFinal_ex: Failed to finalize operation");
         goto error;
      }
      f_len = (size_t)f_len_tmp;

      *res_size += f_len;
      out_buf[*res_size] = '\0';
   }

   /* Wipe key material from stack */
   pgagroal_cleanse(key, sizeof(key));
   pgagroal_cleanse(iv, sizeof(iv));

   EVP_CIPHER_CTX_free(ctx);

   if (master_key != NULL)
   {
      pgagroal_cleanse(master_key, strlen(master_key));
      free(master_key);
   }

   *res_buffer = out_buf;

   return 0;

error:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   /* Wipe key material from stack */
   pgagroal_cleanse(key, sizeof(key));
   pgagroal_cleanse(iv, sizeof(iv));

   if (master_key != NULL)
   {
      pgagroal_cleanse(master_key, strlen(master_key));
      free(master_key);
   }

   free(out_buf);

   return 1;
}
