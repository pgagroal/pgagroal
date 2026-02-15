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

#include <tscommon.h>
#include <pgagroal.h>
#include <configuration.h>
#include <mctf.h>
#include <stdlib.h>
#include <string.h>

void
pgagroal_test_assert_conf_set_fail(char* key, char* value)
{
   struct main_configuration config;
   memset(&config, 0, sizeof(struct main_configuration));
   int ret = pgagroal_apply_main_configuration(&config, NULL, PGAGROAL_MAIN_INI_SECTION, key, value);
   if (ret == 0)
   {
      mctf_errno = 1;
      mctf_errmsg = mctf_format_error("Expected conf set to fail for key='%s' value='%s', but it succeeded", key, value);
   }
}

void
pgagroal_test_assert_conf_set_ok(char* key, char* value)
{
   struct main_configuration config;
   memset(&config, 0, sizeof(struct main_configuration));
   int ret = pgagroal_apply_main_configuration(&config, NULL, PGAGROAL_MAIN_INI_SECTION, key, value);
   if (ret != 0)
   {
      mctf_errno = 1;
      mctf_errmsg = mctf_format_error("Expected conf set to succeed for key='%s' value='%s', but it failed with %d", key, value, ret);
   }
}
