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

static int derive_key_iv(char* password, unsigned char* key, unsigned char* iv, int mode);
static int aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length, int mode);
static int aes_decrypt(char* ciphertext, int ciphertext_length, unsigned char* key, unsigned char* iv, char** plaintext, int mode);
static const EVP_CIPHER* (*get_cipher(int mode))(void);

static int encrypt_decrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** res_buffer, size_t* res_size, int enc, int mode);

int
pgagroal_encrypt(char* plaintext, char* password, char** ciphertext, int* ciphertext_length, int mode)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   if (derive_key_iv(password, key, iv, mode) != 0)
   {
      return 1;
   }

   return aes_encrypt(plaintext, key, iv, ciphertext, ciphertext_length, mode);
}

int
pgagroal_decrypt(char* ciphertext, int ciphertext_length, char* password, char** plaintext, int mode)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   if (derive_key_iv(password, key, iv, mode) != 0)
   {
      return 1;
   }

   return aes_decrypt(ciphertext, ciphertext_length, key, iv, plaintext, mode);
}

// [private]
static int
derive_key_iv(char* password, unsigned char* key, unsigned char* iv, int mode)
{
   if (!EVP_BytesToKey(get_cipher(mode)(), EVP_sha1(), NULL,
                       (unsigned char*)password, strlen(password), 1,
                       key, iv))
   {
      return 1;
   }

   return 0;
}

// [private]
static int
aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length, int mode)
{
   EVP_CIPHER_CTX* ctx = NULL;
   int length;
   size_t size;
   unsigned char* ct = NULL;
   int ct_length;
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);
   if (!(ctx = EVP_CIPHER_CTX_new()))
   {
      goto error;
   }

   if (EVP_EncryptInit_ex(ctx, cipher_fp(), NULL, key, iv) != 1)
   {
      goto error;
   }

   size = strlen(plaintext) + EVP_CIPHER_block_size(cipher_fp());
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
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);

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
                         (unsigned char*)ciphertext, ciphertext_length) != 1)
   {
      goto error;
   }

   plaintext_length = length;

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
   if (mode == ENCRYPTION_AES_256_CBC)
   {
      return &EVP_aes_256_cbc;
   }
   if (mode == ENCRYPTION_AES_192_CBC)
   {
      return &EVP_aes_192_cbc;
   }
   if (mode == ENCRYPTION_AES_128_CBC)
   {
      return &EVP_aes_128_cbc;
   }
   if (mode == ENCRYPTION_AES_256_CTR)
   {
      return &EVP_aes_256_ctr;
   }
   if (mode == ENCRYPTION_AES_192_CTR)
   {
      return &EVP_aes_192_ctr;
   }
   if (mode == ENCRYPTION_AES_128_CTR)
   {
      return &EVP_aes_128_ctr;
   }
   return &EVP_aes_256_cbc;
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
   char* master_key = NULL;
   EVP_CIPHER_CTX* ctx = NULL;
   const EVP_CIPHER* (*cipher_fp)(void) = NULL;
   size_t cipher_block_size = 0;
   size_t outbuf_size = 0;
   size_t outl = 0;
   size_t f_len = 0;

   cipher_fp = get_cipher(mode);
   if (cipher_fp == NULL)
   {
      pgagroal_log_error("Invalid encryption method specified");
      goto error;
   }

   cipher_block_size = EVP_CIPHER_block_size(cipher_fp());

   if (enc == 1)
   {
      outbuf_size = origin_size + cipher_block_size;
   }
   else
   {
      outbuf_size = origin_size;
   }

   *res_buffer = (unsigned char*)malloc(outbuf_size + 1);
   if (*res_buffer == NULL)
   {
      pgagroal_log_error("pgagroal_encrypt_decrypt_buffer: Allocation failure");
      goto error;
   }

   if (pgagroal_get_master_key(&master_key))
   {
      pgagroal_log_error("pgagroal_get_master_key: Invalid master key");
      goto error;
   }

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   if (derive_key_iv(master_key, key, iv, mode) != 0)
   {
      pgagroal_log_error("derive_key_iv: Failed to derive key and iv");
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

   if (EVP_CipherUpdate(ctx, *res_buffer, (int*)&outl, origin_buffer, origin_size) == 0)
   {
      pgagroal_log_error("EVP_CipherUpdate: Failed to process data");
      goto error;
   }

   *res_size = outl;

   if (EVP_CipherFinal_ex(ctx, *res_buffer + outl, (int*)&f_len) == 0)
   {
      pgagroal_log_error("EVP_CipherFinal_ex: Failed to finalize operation");
      goto error;
   }

   *res_size += f_len;

   if (enc == 0)
   {
      (*res_buffer)[*res_size] = '\0';
   }

   EVP_CIPHER_CTX_free(ctx);
   free(master_key);

   return 0;

error:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   free(master_key);

   free(*res_buffer);
   *res_buffer = NULL;

   return 1;
}
