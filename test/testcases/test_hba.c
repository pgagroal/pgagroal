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

#include <security.h>
#include <mctf.h>

MCTF_TEST(test_hba_invalid_masks)
{
   MCTF_ASSERT(!pgagroal_is_allowed_address("127.0.0.1", "127.0.0.0/abc"), cleanup, "abc should deny");
   MCTF_ASSERT(!pgagroal_is_allowed_address("127.0.0.1", "192.168.1.0/1000"), cleanup, "1000 should deny");
   MCTF_ASSERT(!pgagroal_is_allowed_address("127.0.0.1", "192.168.1.0/24abc"), cleanup, "24abc should deny");
   MCTF_ASSERT(!pgagroal_is_allowed_address("127.0.0.1", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/24"), cleanup, "long addr should deny");
cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_hba_ipv6)
{
   MCTF_ASSERT(pgagroal_is_allowed_address("2001:db8::1", "2001:db8::/64"), cleanup, "2001:db8::1 in /64 should allow");
   MCTF_ASSERT(pgagroal_is_allowed_address("2001:db8::2", "2001:db8::/64"), cleanup, "2001:db8::2 in /64 should allow");
   MCTF_ASSERT(!pgagroal_is_allowed_address("::1", "2001:db8::/64"), cleanup, "::1 not in 2001:db8::/64 should deny");
   MCTF_ASSERT(!pgagroal_is_allowed_address("2001:abcd::1", "2001:db8::/64"), cleanup, "2001:abcd not in 2001:db8 should deny");
cleanup:
   MCTF_FINISH();
}