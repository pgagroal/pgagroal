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

#include <tsclient.h>
#include <mctf.h>
#include <utils.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

char* user = NULL;
char* database = NULL;

static void
usage(const char* progname)
{
   printf("Usage: %s [OPTIONS] <project_directory> <user> <database>\n", progname);
   printf("\nOptions:\n");
   printf("  -t, --test NAME     Run only tests matching NAME (test name pattern)\n");
   printf("  -m, --module NAME   Run all tests in module NAME\n");
   printf("  -h, --help          Show this help message\n");
   printf("\n");
   printf("Examples:\n");
   printf("  %s <dir> <user> <db>              Run full test suite\n", progname);
   printf("  %s -m connection <dir> <user> <db> Run all tests in 'connection' module\n", progname);
   printf("  %s -t test_pgagroal_connection <dir> <user> <db> Run test matching 'test_pgagroal_connection'\n", progname);
   printf("\n");
   printf("Legacy format (for backward compatibility):\n");
   printf("  %s <project_directory> <user> <database>\n", progname);
}

/**
 * Get backtrace as a string for immediate display
 */
static char*
pgagroal_backtrace_string(void)
{
#ifdef HAVE_EXECINFO_H
   void* bt[1024];
   int bt_size = backtrace(bt, 1024);
   char* result = NULL;
   size_t total_len = 0;

   if (bt_size <= 0)
   {
      return NULL;
   }

   char** symbols = backtrace_symbols(bt, bt_size);
   if (symbols == NULL)
   {
      return NULL;
   }

   /* Calculate total length needed */
   for (int i = 0; i < bt_size; i++)
   {
      total_len += strlen(symbols[i]) + 10; /* "#%d  %s\n" */
   }

   result = (char*)calloc(total_len + 1, sizeof(char));
   if (result == NULL)
   {
      free(symbols);
      return NULL;
   }

   /* Build backtrace string */
   {
      size_t pos = 0;
      for (int i = 0; i < bt_size; i++)
      {
         int written = snprintf(result + pos, total_len + 1 - pos, "  #%d  %s\n", i, symbols[i]);
         if (written > 0 && (size_t)written < (total_len + 1 - pos))
         {
            pos += (size_t)written;
         }
         else
         {
            break;
         }
      }
   }

   free(symbols);
   return result;
#else
   return NULL;
#endif
}

/**
 * Signal handler for SIGABRT (assertion failures)
 * - pgagroal_backtrace_string(): Gets backtrace as string for immediate display
 * - pgagroal_backtrace(): Logs backtrace to debug log (if logging is initialized)
 * - pgagroal_os_kernel_version(): Provides system information (OS, kernel version)
 */
static void
sigabrt_handler(int sig)
{
   char* bt = NULL;
   char* os = NULL;
   int kernel_major = 0, kernel_minor = 0, kernel_patch = 0;
   (void)sig;

   fprintf(stderr, "\n========================================\n");
   fprintf(stderr, "FATAL: Received SIGABRT (assertion failure)\n");
   fprintf(stderr, "========================================\n\n");

   /* Get system information (OS and kernel version) */
   if (pgagroal_os_kernel_version(&os, &kernel_major, &kernel_minor, &kernel_patch) == 0)
   {
      fprintf(stderr, "System: %s %d.%d.%d\n\n", os ? os : "Unknown",
              kernel_major, kernel_minor, kernel_patch);
      if (os)
      {
         free(os);
      }
   }

   /* Get backtrace string for immediate display */
   bt = pgagroal_backtrace_string();
   if (bt != NULL)
   {
      fprintf(stderr, "%s\n", bt);
      free(bt);
   }
   else
   {
      fprintf(stderr, "Failed to generate backtrace\n");
   }

   fprintf(stderr, "\n========================================\n");
   fflush(stderr);

   signal(SIGABRT, SIG_DFL);
   abort();
}

/**
 * Signal handler for SIGSEGV (segmentation faults)
 * - pgagroal_backtrace_string(): Gets backtrace as string for immediate display
 * - pgagroal_backtrace(): Logs backtrace to debug log (if logging is initialized)
 * - pgagroal_os_kernel_version(): Provides system information (OS, kernel version)
 */
static void
sigsegv_handler(int sig)
{
   char* bt = NULL;
   char* os = NULL;
   int kernel_major = 0, kernel_minor = 0, kernel_patch = 0;
   (void)sig;

   fprintf(stderr, "\n========================================\n");
   fprintf(stderr, "FATAL: Received SIGSEGV (segmentation fault)\n");
   fprintf(stderr, "========================================\n\n");

   /* Get system information (OS and kernel version) */
   if (pgagroal_os_kernel_version(&os, &kernel_major, &kernel_minor, &kernel_patch) == 0)
   {
      fprintf(stderr, "System: %s %d.%d.%d\n\n", os ? os : "Unknown",
              kernel_major, kernel_minor, kernel_patch);
      if (os)
      {
         free(os);
      }
   }

   /* Get backtrace string for immediate display */
   bt = pgagroal_backtrace_string();
   if (bt != NULL)
   {
      fprintf(stderr, "%s\n", bt);
      free(bt);
   }
   else
   {
      fprintf(stderr, "Failed to generate backtrace\n");
   }

   fprintf(stderr, "\n========================================\n");
   fflush(stderr);

   signal(SIGSEGV, SIG_DFL);
   raise(SIGSEGV);
}

/**
 * Setup signal handlers for better error reporting
 */
static void
setup_signal_handlers(void)
{
   struct sigaction sa_abrt, sa_segv;

   memset(&sa_abrt, 0, sizeof(sa_abrt));
   sa_abrt.sa_handler = sigabrt_handler;
   sigemptyset(&sa_abrt.sa_mask);
   sa_abrt.sa_flags = 0;
   if (sigaction(SIGABRT, &sa_abrt, NULL) != 0)
   {
      fprintf(stderr, "Warning: Failed to setup SIGABRT handler: %s\n", strerror(errno));
   }

   memset(&sa_segv, 0, sizeof(sa_segv));
   sa_segv.sa_handler = sigsegv_handler;
   sigemptyset(&sa_segv.sa_mask);
   sa_segv.sa_flags = 0;
   if (sigaction(SIGSEGV, &sa_segv, NULL) != 0)
   {
      fprintf(stderr, "Warning: Failed to setup SIGSEGV handler: %s\n", strerror(errno));
   }
}

/**
 * Build MCTF log path
 * Format: /tmp/pgagroal-test/log/pgagroal-test.log
 */
static int
build_mctf_log_path(char* path, size_t size)
{
   int n;

   if (path == NULL || size == 0)
   {
      return 1;
   }

   /* Use /tmp/pgagroal-test/log/pgagroal-test.log (matches pgmoneta's /tmp/pgmoneta-test/log/pgmoneta-test.log) */
   n = snprintf(path, size, "/tmp/pgagroal-test/log/pgagroal-test.log");

   if (n <= 0 || (size_t)n >= size)
   {
      return 1;
   }

   return 0;
}

int
main(int argc, char* argv[])
{
   mctf_filter_type_t filter_type = MCTF_FILTER_NONE;
   const char* filter = NULL;
   const char* project_dir = NULL;
   int number_failed;
   int opt;
   int option_index = 0;
   char mctf_log_path[512];

   static struct option long_options[] = {
      {"test", required_argument, 0, 't'},
      {"module", required_argument, 0, 'm'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

   /* Parse command-line options */
   while ((opt = getopt_long(argc, argv, "t:m:h", long_options, &option_index)) != -1)
   {
      switch (opt)
      {
         case 't':
         case 'm':
            if (filter_type != MCTF_FILTER_NONE)
            {
               fprintf(stderr, "Error: Cannot specify both -t and -m options\n");
               usage(argv[0]);
               return EXIT_FAILURE;
            }
            filter = optarg;
            filter_type = (opt == 't') ? MCTF_FILTER_TEST : MCTF_FILTER_MODULE;
            break;
         case 'h':
            usage(argv[0]);
            return EXIT_SUCCESS;
         default:
            usage(argv[0]);
            return EXIT_FAILURE;
      }
   }

   /* Setup signal handlers for better error reporting */
   setup_signal_handlers();

   /* Handle legacy format: <project_dir> <user> <database> */
   /* We need exactly 3 positional arguments: project_dir, user, database */
   /* optind points to first non-option argument after getopt */
   if (optind + 2 >= argc)
   {
      /* Not enough arguments - we need argv[optind], argv[optind+1], argv[optind+2] */
      fprintf(stderr, "Error: Missing required arguments (project_directory, user, database)\n");
      usage(argv[0]);
      return EXIT_FAILURE;
   }
   else if (optind + 3 < argc)
   {
      /* Too many arguments */
      fprintf(stderr, "Error: Too many arguments\n");
      usage(argv[0]);
      return EXIT_FAILURE;
   }

   /* Extract the 3 required arguments */
   project_dir = argv[optind];
   user = strdup(argv[optind + 1]);
   database = strdup(argv[optind + 2]);

   if (user == NULL || database == NULL)
   {
      fprintf(stderr, "Error: Failed to allocate memory\n");
      return EXIT_FAILURE;
   }

   /* Initialize test client */
   if (pgagroal_tsclient_init((char*)project_dir))
   {
      goto error;
   }

   /* Initialize MCTF */
   mctf_init();

   /* Build and open log file */
   if (build_mctf_log_path(mctf_log_path, sizeof(mctf_log_path)) == 0)
   {
      if (mctf_open_log(mctf_log_path) != 0)
      {
         fprintf(stderr, "Warning: Failed to open MCTF log file at '%s'\n", mctf_log_path);
      }
   }

   /* Log all environment variables before starting the test suite so that
    * the execution context is captured alongside the test output.
    */
   mctf_log_environment();

   /* Run tests */
   number_failed = mctf_run_tests(filter_type, filter);

   /* Print summary */
   mctf_print_summary();

   /* Cleanup */
   mctf_close_log();
   mctf_cleanup();
   pgagroal_tsclient_destroy();
   free(user);
   free(database);

   return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;

error:
   if (user)
   {
      free(user);
   }
   if (database)
   {
      free(database);
   }
   pgagroal_tsclient_destroy();
   return EXIT_FAILURE;
}
