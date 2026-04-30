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

#ifndef PGAGROAL_SHMEM_H
#define PGAGROAL_SHMEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

/**
 * Create a shared memory segment
 * @param size The size of the segment
 * @param hp Huge page value
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_shared_memory(size_t size, unsigned char hp, void** shmem);

/**
 * Resize a shared memory segment
 * @param size The size of the segment
 * @param shmem The pointer to the segment
 * @param new_size The size of the new segment
 * @param new_shmem The pointer to the new segment
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_resize_shared_memory(size_t size, void* shmem, size_t* new_size, void** new_shmem);

/**
 * Destroy a shared memory segment
 * @param shmem The shared memory segment
 * @param size The size
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_destroy_shared_memory(void* shmem, size_t size);

#ifdef __cplusplus
}
#endif

#endif
