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

/* pgagroal */
#include <pgagroal.h>
#include <shmem.h>

/* system */
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>

void* shmem = NULL;
void* pipeline_shmem = NULL;
void* prometheus_shmem = NULL;
void* prometheus_cache_shmem = NULL;

int
pgagroal_create_shared_memory(size_t size, unsigned char hp, void** shmem)
{
   void* s = NULL;
   int protection = PROT_READ | PROT_WRITE;
   int visibility = MAP_ANONYMOUS | MAP_SHARED;

   *shmem = NULL;

#ifdef HAVE_LINUX
   if (hp == HUGEPAGE_TRY || hp == HUGEPAGE_ON)
   {
      visibility = visibility | MAP_HUGETLB;
   }

#endif

   s = mmap(NULL, size, protection, visibility, -1, 0);
   if (s == (void*)-1)
   {
      errno = 0;
      s = NULL;

      if (hp == HUGEPAGE_OFF || hp == HUGEPAGE_ON)
      {
         return 1;
      }
   }

   if (s == NULL)
   {
      visibility = MAP_ANONYMOUS | MAP_SHARED;
      s = mmap(NULL, size, protection, visibility, -1, 0);

      if (s == (void*)-1)
      {
         errno = 0;
         return 1;
      }
   }

   memset(s, 0, size);

   *shmem = s;

   return 0;
}

int
pgagroal_resize_shared_memory(size_t size, void* shmem, size_t* new_size, void** new_shmem)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   *new_size = size + (config->max_connections * sizeof(struct connection));
   if (pgagroal_create_shared_memory(*new_size, config->common.hugepage, new_shmem))
   {
      return 1;
   }

   memset(*new_shmem, 0, *new_size);
   memcpy(*new_shmem, shmem, size);

   return 0;
}

int
pgagroal_destroy_shared_memory(void* shmem, size_t size)
{
   return munmap(shmem, size);
}
