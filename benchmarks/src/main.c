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
#include <bench.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
   printf("pgagroal-bench: the C part of benchmarks/bench.sh\n");
   printf("\n");
   printf("Usage:\n");
   printf("  pgagroal-bench compare BASELINE_RUN CANDIDATE_RUN [--band PCT]\n");
   printf("                         [--baseline-previous RUN] [--candidate-previous RUN]\n");
   printf("  pgagroal-bench check-build COMPILE_COMMANDS SRC_DIR\n");
   printf("  pgagroal-bench port-free PORT\n");
   printf("  pgagroal-bench meta FILE KEY=VALUE ...\n");
   printf("\n");
   printf("Use benchmarks/bench.sh, see doc/BENCHMARK.md\n");
}

static int
to_port(char* s)
{
   char* end = NULL;
   long port = strtol(s, &end, 10);

   if (*s == '\0' || *end != '\0' || port < 1 || port > 65535)
   {
      return -1;
   }

   return (int)port;
}

int
main(int argc, char** argv)
{
   int port;

   if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
   {
      usage();
      return argc < 2 ? 1 : 0;
   }

   if (!strcmp(argv[1], "compare"))
   {
      return bench_compare(argc - 2, argv + 2);
   }
   else if (!strcmp(argv[1], "check-build") && argc == 4)
   {
      return bench_check_build(argv[2], argv[3]);
   }
   else if (!strcmp(argv[1], "port-free") && argc == 3)
   {
      port = to_port(argv[2]);
      if (port == -1)
      {
         fprintf(stderr, "error: '%s' is not a port\n", argv[2]);
         return 1;
      }
      return bench_port_free(port);
   }
   else if (!strcmp(argv[1], "meta") && argc >= 3)
   {
      return bench_meta(argv[2], argc - 3, argv + 3);
   }

   usage();
   return 1;
}
