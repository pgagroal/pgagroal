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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <pgagroal.h>

#include <tsclient.h>
#include <mctf.h>

#include <stdlib.h>
#include <string.h>

MCTF_TEST(test_prometheus_help_type_format)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   char* metrics = NULL;

   if (config == NULL)
   {
      MCTF_SKIP("configuration not available");
   }

   if (config->common.metrics <= 0)
   {
      MCTF_SKIP("metrics endpoint not enabled");
   }

   metrics = pgagroal_tsclient_scrape_metrics();

   MCTF_ASSERT(metrics != NULL, cleanup,
               "failed to scrape the Prometheus metrics endpoint");

   MCTF_ASSERT(strstr(metrics, "# HELP pgagroal_state ") != NULL, cleanup,
               "Prometheus HELP line is missing or incorrectly formatted");

   MCTF_ASSERT(strstr(metrics, "# TYPE pgagroal_state gauge") != NULL, cleanup,
               "Prometheus TYPE line is missing or incorrectly formatted");

   MCTF_ASSERT(strstr(metrics, "#HELP pgagroal_state ") == NULL, cleanup,
               "Prometheus output still contains the old #HELP format");

   MCTF_ASSERT(strstr(metrics, "#TYPE pgagroal_state ") == NULL, cleanup,
               "Prometheus output still contains the old #TYPE format");

cleanup:
   free(metrics);
   MCTF_FINISH();
}
