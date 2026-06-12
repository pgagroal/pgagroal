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

#ifndef MCTF_H
#define MCTF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Special error codes */
#define MCTF_CODE_SKIPPED (-1)

/* Filter types */
typedef enum {
   MCTF_FILTER_NONE = 0,
   MCTF_FILTER_TEST,
   MCTF_FILTER_MODULE
} mctf_filter_type_t;

/* Test function pointer type */
typedef int (*mctf_test_func_t)(void);

/**
 * Definition of a single test case registered in MCTF.
 */
typedef struct mctf_test
{
   char* name;             /**< Human readable test name. */
   char* module;           /**< Logical module/group this test belongs to. */
   char* file;             /**< Source file where the test is defined. */
   mctf_test_func_t func;  /**< Function implementing the test. */
   struct mctf_test* next; /**< Pointer to the next test in the registration list. */
} mctf_test_t;

/**
 * Execution result for a single test case.
 */
typedef struct mctf_result
{
   const char* test_name; /**< Name of the test that produced this result. */
   const char* file;      /**< Source file where the failing assertion occurred. */
   int line;              /**< Line number of the failing assertion, or 0 on success. */
   bool passed;           /**< True if the test completed successfully. */
   bool skipped;          /**< True if the test was explicitly skipped. */
   int error_code;        /**< Error code associated with a failure or skip. */
   char* error_message;   /**< Dynamically allocated, human readable error message. */
   long elapsed_ms;       /**< Execution time of the test in milliseconds. */
   long log_offset_start; /**< Start offset in pgagroal log for this test, or -1 if unavailable. */
   long log_offset_end;   /**< End offset in pgagroal log for this test, or -1 if unavailable. */
   bool has_error_log;    /**< True if an ERROR-level line was observed in this test log slice. */
} mctf_result_t;

/**
 * Aggregated state for running and tracking multiple tests.
 */
typedef struct mctf_runner
{
   mctf_test_t* tests;     /**< Head of the linked list of registered tests. */
   mctf_result_t* results; /**< Array of collected test results. */
   size_t test_count;      /**< Number of registered tests. */
   size_t result_count;    /**< Number of populated entries in @ref results. */
   size_t passed_count;    /**< Count of tests that passed. */
   size_t failed_count;    /**< Count of tests that failed. */
   size_t skipped_count;   /**< Count of tests that were skipped. */
} mctf_runner_t;

/* Global error state */
extern int mctf_errno;
extern char* mctf_errmsg;

/* Initialize MCTF framework */
void
mctf_init(void);

/* Cleanup MCTF framework */
void
mctf_cleanup(void);

/* Register a test (called automatically via constructor) */
void
mctf_register_test(const char* name, const char* module, const char* file, mctf_test_func_t func);

/* Extract module name from file path */
const char*
mctf_extract_module_name(const char* file_path);

/* Extract filename from file path */
const char*
mctf_extract_filename(const char* file_path);

/* Run all registered tests with optional filtering */
int
mctf_run_tests(mctf_filter_type_t filter_type, const char* filter);

/* Print test summary */
void
mctf_print_summary(void);

/* Open log file */
int
mctf_open_log(const char* log_path);

/* Close log file */
void
mctf_close_log(void);

/* Log environment variables */
void
mctf_log_environment(void);

/* Get test results */
const mctf_result_t*
mctf_get_results(size_t* count);

/* Return true if a result contains ERROR-level log output. */
bool
mctf_result_has_error_log(size_t result_index);

/* Internal helper: format error message */
char*
mctf_format_error(const char* format, ...);

/* Test registration macro with constructor attribute */
#define MCTF_TEST(test_name)                                            \
   static int test_name##_impl(void);                                   \
   static void test_name##_register(void) __attribute__((constructor)); \
   static void test_name##_register(void)                               \
   {                                                                    \
      mctf_register_test(#test_name,                                    \
                         mctf_extract_module_name(__FILE__),            \
                         mctf_extract_filename(__FILE__),               \
                         test_name##_impl);                             \
   }                                                                    \
   static int test_name##_impl(void)

/* Assertion macro with cleanup label and optional message */
#define MCTF_ASSERT(condition, error_label, ...)                                 \
   do                                                                            \
   {                                                                             \
      if (!(condition))                                                          \
      {                                                                          \
         mctf_errno = __LINE__;                                                  \
         if (sizeof(#__VA_ARGS__) > 1)                                           \
         {                                                                       \
            mctf_errmsg = mctf_format_error(__VA_ARGS__);                        \
         }                                                                       \
         else                                                                    \
         {                                                                       \
            mctf_errmsg = mctf_format_error("Assertion failed: %s", #condition); \
         }                                                                       \
         goto error_label;                                                       \
      }                                                                          \
   }                                                                             \
   while (0)

/* Pointer assertion macros */
#define MCTF_ASSERT_PTR_NONNULL(ptr, error_label, ...)          \
   do                                                           \
   {                                                            \
      if ((ptr) == NULL)                                        \
      {                                                         \
         mctf_errno = __LINE__;                                 \
         if (sizeof(#__VA_ARGS__) > 1)                          \
         {                                                      \
            mctf_errmsg = mctf_format_error(__VA_ARGS__);       \
         }                                                      \
         else                                                   \
         {                                                      \
            mctf_errmsg = mctf_format_error("Pointer is NULL"); \
         }                                                      \
         goto error_label;                                      \
      }                                                         \
   }                                                            \
   while (0)

#define MCTF_ASSERT_PTR_NULL(ptr, error_label, ...)                 \
   do                                                               \
   {                                                                \
      if ((ptr) != NULL)                                            \
      {                                                             \
         mctf_errno = __LINE__;                                     \
         if (sizeof(#__VA_ARGS__) > 1)                              \
         {                                                          \
            mctf_errmsg = mctf_format_error(__VA_ARGS__);           \
         }                                                          \
         else                                                       \
         {                                                          \
            mctf_errmsg = mctf_format_error("Pointer is not NULL"); \
         }                                                          \
         goto error_label;                                          \
      }                                                             \
   }                                                                \
   while (0)

/* Integer assertion macros */
#define MCTF_ASSERT_INT_EQ(actual, expected, error_label, ...)                          \
   do                                                                                   \
   {                                                                                    \
      int _actual = (actual);                                                           \
      int _expected = (expected);                                                       \
      if (_actual != _expected)                                                         \
      {                                                                                 \
         mctf_errno = __LINE__;                                                         \
         if (sizeof(#__VA_ARGS__) > 1)                                                  \
         {                                                                              \
            mctf_errmsg = mctf_format_error(__VA_ARGS__);                               \
         }                                                                              \
         else                                                                           \
         {                                                                              \
            mctf_errmsg = mctf_format_error("Expected %d, got %d", _expected, _actual); \
         }                                                                              \
         goto error_label;                                                              \
      }                                                                                 \
   }                                                                                    \
   while (0)

/* String assertion macros */
#define MCTF_ASSERT_STR_EQ(actual, expected, error_label, ...)                     \
   do                                                                              \
   {                                                                               \
      const char* _actual = (actual);                                              \
      const char* _expected = (expected);                                          \
      if (_actual == NULL || _expected == NULL || strcmp(_actual, _expected) != 0) \
      {                                                                            \
         mctf_errno = __LINE__;                                                    \
         if (sizeof(#__VA_ARGS__) > 1)                                             \
         {                                                                         \
            mctf_errmsg = mctf_format_error(__VA_ARGS__);                          \
         }                                                                         \
         else                                                                      \
         {                                                                         \
            mctf_errmsg = mctf_format_error("Expected '%s', got '%s'",             \
                                            _expected ? _expected : "NULL",        \
                                            _actual ? _actual : "NULL");           \
         }                                                                         \
         goto error_label;                                                         \
      }                                                                            \
   }                                                                               \
   while (0)

/* String inequality assertion macro */
#define MCTF_ASSERT_STR_NEQ(actual, expected, error_label, ...)                         \
   do                                                                                   \
   {                                                                                    \
      const char* _actual = (actual);                                                   \
      const char* _expected = (expected);                                               \
      bool _equal = false;                                                              \
      if (_actual == NULL && _expected == NULL)                                         \
      {                                                                                 \
         _equal = true;                                                                 \
      }                                                                                 \
      else if (_actual != NULL && _expected != NULL && strcmp(_actual, _expected) == 0) \
      {                                                                                 \
         _equal = true;                                                                 \
      }                                                                                 \
      if (_equal)                                                                       \
      {                                                                                 \
         mctf_errno = __LINE__;                                                         \
         if (sizeof(#__VA_ARGS__) > 1)                                                  \
         {                                                                              \
            mctf_errmsg = mctf_format_error(__VA_ARGS__);                               \
         }                                                                              \
         else                                                                           \
         {                                                                              \
            mctf_errmsg = mctf_format_error("Expected '%s' to be different from '%s'",  \
                                            _actual ? _actual : "NULL",                 \
                                            _expected ? _expected : "NULL");            \
         }                                                                              \
         goto error_label;                                                              \
      }                                                                                 \
   }                                                                                    \
   while (0)

/* Float assertion macros */
#define MCTF_ASSERT_FLOAT_EQ(actual, expected, error_label, ...)                           \
   do                                                                                      \
   {                                                                                       \
      float _actual = (actual);                                                            \
      float _expected = (expected);                                                        \
      float _diff = (_actual > _expected) ? (_actual - _expected) : (_expected - _actual); \
      if (_diff > 0.0001f)                                                                 \
      {                                                                                    \
         mctf_errno = __LINE__;                                                            \
         if (sizeof(#__VA_ARGS__) > 1)                                                     \
         {                                                                                 \
            mctf_errmsg = mctf_format_error(__VA_ARGS__);                                  \
         }                                                                                 \
         else                                                                              \
         {                                                                                 \
            mctf_errmsg = mctf_format_error("Expected %f, got %f", _expected, _actual);    \
         }                                                                                 \
         goto error_label;                                                                 \
      }                                                                                    \
   }                                                                                       \
   while (0)

/* Double assertion macros */
#define MCTF_ASSERT_DOUBLE_EQ(actual, expected, error_label, ...)                           \
   do                                                                                       \
   {                                                                                        \
      double _actual = (actual);                                                            \
      double _expected = (expected);                                                        \
      double _diff = (_actual > _expected) ? (_actual - _expected) : (_expected - _actual); \
      if (_diff > 0.000001)                                                                 \
      {                                                                                     \
         mctf_errno = __LINE__;                                                             \
         if (sizeof(#__VA_ARGS__) > 1)                                                      \
         {                                                                                  \
            mctf_errmsg = mctf_format_error(__VA_ARGS__);                                   \
         }                                                                                  \
         else                                                                               \
         {                                                                                  \
            mctf_errmsg = mctf_format_error("Expected %lf, got %lf", _expected, _actual);   \
         }                                                                                  \
         goto error_label;                                                                  \
      }                                                                                     \
   }                                                                                        \
   while (0)

/* Skip test macro */
#define MCTF_SKIP(...)                                 \
   do                                                  \
   {                                                   \
      mctf_errno = MCTF_CODE_SKIPPED;                  \
      if (__VA_ARGS__)                                 \
      {                                                \
         mctf_errmsg = mctf_format_error(__VA_ARGS__); \
      }                                                \
      else                                             \
      {                                                \
         mctf_errmsg = strdup("Test skipped");         \
      }                                                \
      return MCTF_CODE_SKIPPED;                        \
   }                                                   \
   while (0)

/* Finish test macro */
#define MCTF_FINISH() \
   return mctf_errno

#ifdef __cplusplus
}
#endif

#endif /* MCTF_H */
