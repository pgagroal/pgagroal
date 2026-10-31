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

/*
 * Compare two runs of bench.sh: a baseline and a candidate.
 *
 * A run directory has meta.json (branch, commit, machine, ...) and one
 * iter-N.json per iteration: {workload: {"tps": ..., "p99_ms": ...}}.
 *
 * Each side is the median of its iterations. TPS: higher is better; p99:
 * lower is better. A difference is reported only if it is at least the band
 * (10%) AND the min-max ranges of the two sides don't overlap. Otherwise it
 * is "no change": overlapping ranges mean the difference is noise.
 *
 * Drift: a whole run can be shifted (busy machine), so even the same code can
 * look faster. With --baseline-previous / --candidate-previous, each branch is
 * also compared with its own previous run. If they differ, a DRIFT warning
 * says differences of that size are not a result.
 *
 * Exit: 0 = compared (also when slower), 1 = wrong arguments,
 * 2 = bad or missing data.
 */

/* pgagroal */
#include <bench.h>
#include <json.h>
#include <utils.h>
#include <value.h>

/* system */
#include <glob.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define INTEGRITY_EXIT 2
#define MAX_MESSAGES   64
#define MESSAGE_LENGTH 1024

/** @struct metric
 * Defines a measured metric
 */
struct metric
{
   char* name;            /**< The name */
   bool higher_is_better; /**< Higher is better (ratio direction) */
   char* format;          /**< The display format */
};

/** @struct run
 * Defines one run directory
 */
struct run
{
   struct json* meta;       /**< meta.json */
   struct json** iters;     /**< One iter-N.json per iteration */
   int number_of_iters;     /**< The number of iterations */
   char** workloads;        /**< The workloads, in the order of the first iteration */
   int number_of_workloads; /**< The number of workloads */
};

/** @struct messages
 * Defines a list of unique messages
 */
struct messages
{
   char* list[MAX_MESSAGES]; /**< The messages */
   int size;                 /**< The number of messages */
};

static struct metric metrics[] = {
   {"tps", true, "%.1f"},
   {"p99_ms", false, "%.2f"},
   {NULL, false, NULL}};

static char integrity_error[MESSAGE_LENGTH];

static int load_run(char* label, char* run_dir, struct run* run);
static void free_run(struct run* run);
static struct json* read_json(char* path);
static bool number_of(struct json* item, char* key, double* value);
static char* string_of(struct json* item, char* key, char* buf, size_t size);
static bool has_workload(struct run* run, char* workload);
static double median_of(struct run* run, char* workload, char* metric);
static void range_of(struct run* run, char* workload, char* metric, double* min, double* max);
static void classify(double base, double cand, bool higher_is_better, double band,
                     double base_min, double base_max, double cand_min, double cand_max,
                     char* out, size_t size);
static void drift_check(char* label, struct run* newest, char* previous_dir, double band,
                        struct messages* warnings, struct messages* notes);
static void warnings_for(struct run* base, struct run* cand, struct messages* warnings);
static void notes_for(struct run* base, struct run* cand, struct messages* notes);
static void describe(struct run* run, char* out, size_t size);
static void add_message(struct messages* messages, char* fmt, ...);
static void free_messages(struct messages* messages);
static int compare_doubles(const void* a, const void* b);
static int compare_paths(const void* a, const void* b);

int
bench_compare(int argc, char** argv)
{
   char* positional[2] = {NULL, NULL};
   int number_of_positional = 0;
   char* baseline_previous = NULL;
   char* candidate_previous = NULL;
   double band_percent = 10.0;
   double band;
   struct run base;
   struct run cand;
   struct messages warnings;
   struct messages notes;
   char buf[MESSAGE_LENGTH];
   char case_base[MESSAGE_LENGTH];
   char case_cand[MESSAGE_LENGTH];
   int rc = INTEGRITY_EXIT;

   memset(&base, 0, sizeof(base));
   memset(&cand, 0, sizeof(cand));
   memset(&warnings, 0, sizeof(warnings));
   memset(&notes, 0, sizeof(notes));

   for (int i = 0; i < argc; i++)
   {
      char* option = argv[i];

      if (!strcmp(option, "--band") || !strcmp(option, "--baseline-previous") ||
          !strcmp(option, "--candidate-previous"))
      {
         char* value = NULL;
         char* end = NULL;

         if (i + 1 >= argc)
         {
            fprintf(stderr, "error: %s needs a value\n", option);
            return 1;
         }
         value = argv[++i];

         if (!strcmp(option, "--baseline-previous"))
         {
            baseline_previous = value;
            continue;
         }
         if (!strcmp(option, "--candidate-previous"))
         {
            candidate_previous = value;
            continue;
         }

         band_percent = strtod(value, &end);
         if (*value == '\0' || *end != '\0' || !(band_percent > 0 && band_percent < 100))
         {
            fprintf(stderr, "error: --band must be between 0 and 100\n");
            return 1;
         }
      }
      else if (option[0] == '-')
      {
         fprintf(stderr, "error: unknown option '%s'\n", option);
         return 1;
      }
      else if (number_of_positional < 2)
      {
         positional[number_of_positional++] = option;
      }
      else
      {
         fprintf(stderr, "error: compare needs BASELINE_RUN and CANDIDATE_RUN\n");
         return 1;
      }
   }

   if (number_of_positional != 2)
   {
      fprintf(stderr, "error: compare needs BASELINE_RUN and CANDIDATE_RUN\n");
      return 1;
   }

   band = band_percent / 100.0;

   if (load_run("baseline", positional[0], &base) || load_run("candidate", positional[1], &cand))
   {
      goto integrity;
   }

   string_of(base.meta, "case", case_base, sizeof(case_base));
   string_of(cand.meta, "case", case_cand, sizeof(case_cand));
   if (strcmp(case_base, case_cand))
   {
      pgagroal_snprintf(integrity_error, sizeof(integrity_error), "different cases: '%s' vs '%s'", case_base, case_cand);
      goto integrity;
   }

   if (base.number_of_workloads != cand.number_of_workloads)
   {
      pgagroal_snprintf(integrity_error, sizeof(integrity_error), "baseline and candidate measure different workloads");
      goto integrity;
   }
   for (int i = 0; i < base.number_of_workloads; i++)
   {
      if (!has_workload(&cand, base.workloads[i]))
      {
         pgagroal_snprintf(integrity_error, sizeof(integrity_error), "baseline and candidate measure different workloads");
         goto integrity;
      }
   }

   printf("case         : %s\n", case_base);
   printf("runs         : %d baseline / %d candidate (median)\n", base.number_of_iters, cand.number_of_iters);
   if (string_of(base.meta, "build_type", buf, sizeof(buf)) != NULL)
   {
      printf("build        : %s\n", buf);
   }
   if (pgagroal_json_contains_key(base.meta, "cpus") || pgagroal_json_contains_key(base.meta, "os"))
   {
      char cpus[MESSAGE_LENGTH];
      char os[MESSAGE_LENGTH];

      if (string_of(base.meta, "cpus", cpus, sizeof(cpus)) == NULL)
      {
         pgagroal_snprintf(cpus, sizeof(cpus), "?");
      }
      if (string_of(base.meta, "os", os, sizeof(os)) == NULL)
      {
         pgagroal_snprintf(os, sizeof(os), "?");
      }
      printf("machine      : %s cores, %s", cpus, os);
      if (string_of(base.meta, "arch", buf, sizeof(buf)) != NULL)
      {
         printf(" %s", buf);
      }
      printf("\n");
   }
   if (string_of(base.meta, "ev_backend", buf, sizeof(buf)) != NULL)
   {
      printf("ev_backend   : %s\n", buf);
   }
   if (string_of(base.meta, "pg_version", buf, sizeof(buf)) != NULL)
   {
      printf("postgresql   : %s\n", buf);
   }
   describe(&base, buf, sizeof(buf));
   printf("baseline     : %s\n", buf);
   describe(&cand, buf, sizeof(buf));
   printf("candidate    : %s\n", buf);

   warnings_for(&base, &cand, &warnings);
   notes_for(&base, &cand, &notes);
   drift_check("baseline", &base, baseline_previous, band, &warnings, &notes);
   drift_check("candidate", &cand, candidate_previous, band, &warnings, &notes);

   if (warnings.size > 0)
   {
      printf("\n");
      for (int i = 0; i < warnings.size; i++)
      {
         printf("%s\n", warnings.list[i]);
      }
   }

   printf("\n");
   printf("%-20s  %-8s  %10s  %19s  %10s  %19s  change\n",
          "workload", "metric", "baseline", "min-max", "candidate", "min-max");
   printf("--------------------  --------  ----------  -------------------  ----------  -------------------  ------------\n");
   for (int w = 0; w < base.number_of_workloads; w++)
   {
      char* workload = base.workloads[w];

      for (int m = 0; metrics[m].name != NULL; m++)
      {
         double b = median_of(&base, workload, metrics[m].name);
         double c = median_of(&cand, workload, metrics[m].name);
         double bmin, bmax, cmin, cmax;
         char change[MESSAGE_LENGTH];
         char bs[64], cs[64], bmins[64], bmaxs[64], cmins[64], cmaxs[64];
         char brs[160], crs[160];

         range_of(&base, workload, metrics[m].name, &bmin, &bmax);
         range_of(&cand, workload, metrics[m].name, &cmin, &cmax);
         classify(b, c, metrics[m].higher_is_better, band, bmin, bmax, cmin, cmax, change, sizeof(change));

         pgagroal_snprintf(bs, sizeof(bs), metrics[m].format, b);
         pgagroal_snprintf(cs, sizeof(cs), metrics[m].format, c);
         pgagroal_snprintf(bmins, sizeof(bmins), metrics[m].format, bmin);
         pgagroal_snprintf(bmaxs, sizeof(bmaxs), metrics[m].format, bmax);
         pgagroal_snprintf(cmins, sizeof(cmins), metrics[m].format, cmin);
         pgagroal_snprintf(cmaxs, sizeof(cmaxs), metrics[m].format, cmax);
         pgagroal_snprintf(brs, sizeof(brs), "%s-%s", bmins, bmaxs);
         pgagroal_snprintf(crs, sizeof(crs), "%s-%s", cmins, cmaxs);

         printf("%-20s  %-8s  %10s  %19s  %10s  %19s  %s\n",
                workload, metrics[m].name, bs, brs, cs, crs, change);
      }
   }

   if (notes.size > 0)
   {
      printf("\n");
      for (int i = 0; i < notes.size; i++)
      {
         printf("Note: %s\n", notes.list[i]);
      }
   }

   printf("\n");
   printf("tps: higher is better. p99_ms: lower is better.\n");
   printf("No change: the difference is below %g%%, or the min-max ranges overlap.\n", band_percent);

   rc = 0;
   goto done;

integrity:

   fprintf(stderr, "BENCH INTEGRITY FAILURE: %s\n", integrity_error);
   rc = INTEGRITY_EXIT;

done:

   free_run(&base);
   free_run(&cand);
   free_messages(&warnings);
   free_messages(&notes);

   return rc;
}

static int
load_run(char* label, char* run_dir, struct run* run)
{
   struct stat st;
   char path[MAX_PATH];
   char pattern[MAX_PATH];
   char* keys[] = {"case", "branch", "commit", NULL};
   char* separator = "/";
   glob_t paths;
   bool globbed = false;

   memset(run, 0, sizeof(struct run));
   memset(&paths, 0, sizeof(paths));

   if (stat(run_dir, &st) || !S_ISDIR(st.st_mode))
   {
      pgagroal_snprintf(integrity_error, sizeof(integrity_error), "%s: %s is not a directory", label, run_dir);
      goto error;
   }

   if (run_dir[0] != '\0' && run_dir[strlen(run_dir) - 1] == '/')
   {
      separator = "";
   }

   pgagroal_snprintf(path, sizeof(path), "%s%smeta.json", run_dir, separator);
   run->meta = read_json(path);
   if (run->meta == NULL)
   {
      goto error;
   }
   if (run->meta->type != JSONItem)
   {
      pgagroal_snprintf(integrity_error, sizeof(integrity_error), "%s: meta.json is not an object", label);
      goto error;
   }
   for (int i = 0; keys[i] != NULL; i++)
   {
      enum value_type type = ValueNone;
      char* value = (char*)pgagroal_json_get_typed(run->meta, keys[i], &type);

      if (type != ValueString || value == NULL || value[0] == '\0')
      {
         pgagroal_snprintf(integrity_error, sizeof(integrity_error), "%s: meta.json has no '%s'", label, keys[i]);
         goto error;
      }
   }

   pgagroal_snprintf(pattern, sizeof(pattern), "%s%siter-*.json", run_dir, separator);
   if (glob(pattern, 0, NULL, &paths) != 0 || paths.gl_pathc == 0)
   {
      pgagroal_snprintf(integrity_error, sizeof(integrity_error), "%s: no iter-*.json files in %s", label, run_dir);
      goto error;
   }
   globbed = true;
   qsort(paths.gl_pathv, paths.gl_pathc, sizeof(char*), compare_paths);

   run->iters = calloc(paths.gl_pathc, sizeof(struct json*));
   if (run->iters == NULL)
   {
      goto error;
   }

   for (size_t p = 0; p < paths.gl_pathc; p++)
   {
      struct json* data = read_json(paths.gl_pathv[p]);
      struct json_iterator* iter = NULL;
      int number_of_keys = 0;

      if (data == NULL)
      {
         goto error;
      }
      run->iters[run->number_of_iters++] = data;

      if (data->type != JSONItem)
      {
         pgagroal_snprintf(integrity_error, sizeof(integrity_error), "%s: %s is not a non-empty object", label, paths.gl_pathv[p]);
         goto error;
      }

      pgagroal_json_iterator_create(data, &iter);
      while (pgagroal_json_iterator_next(iter))
      {
         struct json* workload = NULL;

         number_of_keys++;

         /* Every iteration must have the same workloads */
         if (p == 0)
         {
            char** workloads = realloc(run->workloads, (run->number_of_workloads + 1) * sizeof(char*));

            if (workloads == NULL)
            {
               pgagroal_json_iterator_destroy(iter);
               goto error;
            }
            run->workloads = workloads;
            run->workloads[run->number_of_workloads++] = strdup(iter->key);
         }
         else if (!has_workload(run, iter->key))
         {
            pgagroal_snprintf(integrity_error, sizeof(integrity_error), "%s: %s measures '%s', the first iteration does not",
                              label, paths.gl_pathv[p], iter->key);
            pgagroal_json_iterator_destroy(iter);
            goto error;
         }

         if (iter->value->type != ValueJSON)
         {
            pgagroal_snprintf(integrity_error, sizeof(integrity_error), "%s: %s %s is not an object", label, paths.gl_pathv[p], iter->key);
            pgagroal_json_iterator_destroy(iter);
            goto error;
         }

         workload = (struct json*)iter->value->data;
         for (int m = 0; metrics[m].name != NULL; m++)
         {
            double value = 0.0;

            if (!number_of(workload, metrics[m].name, &value) || !(value > 0))
            {
               pgagroal_snprintf(integrity_error, sizeof(integrity_error), "%s: %s %s.%s is not a positive number",
                                 label, paths.gl_pathv[p], iter->key, metrics[m].name);
               pgagroal_json_iterator_destroy(iter);
               goto error;
            }
         }
      }
      pgagroal_json_iterator_destroy(iter);

      if (number_of_keys == 0 || number_of_keys != run->number_of_workloads)
      {
         pgagroal_snprintf(integrity_error, sizeof(integrity_error), "%s: %s does not measure the same workloads as the first iteration",
                           label, paths.gl_pathv[p]);
         if (number_of_keys == 0)
         {
            pgagroal_snprintf(integrity_error, sizeof(integrity_error), "%s: %s is not a non-empty object", label, paths.gl_pathv[p]);
         }
         goto error;
      }
   }

   globfree(&paths);

   return 0;

error:

   if (globbed)
   {
      globfree(&paths);
   }

   return 1;
}

static void
free_run(struct run* run)
{
   pgagroal_json_destroy(run->meta);
   for (int i = 0; i < run->number_of_iters; i++)
   {
      pgagroal_json_destroy(run->iters[i]);
   }
   free(run->iters);
   for (int i = 0; i < run->number_of_workloads; i++)
   {
      free(run->workloads[i]);
   }
   free(run->workloads);
   memset(run, 0, sizeof(struct run));
}

static struct json*
read_json(char* path)
{
   struct json* obj = NULL;
   FILE* file = NULL;

   /* pgagroal_json_read_file can't tell a missing file from a broken one */
   file = fopen(path, "r");
   if (file == NULL)
   {
      pgagroal_snprintf(integrity_error, sizeof(integrity_error), "cannot read %s", path);
      return NULL;
   }
   fclose(file);

   if (pgagroal_json_read_file(path, &obj) || obj == NULL)
   {
      pgagroal_snprintf(integrity_error, sizeof(integrity_error), "cannot parse %s", path);
      pgagroal_json_destroy(obj);
      return NULL;
   }

   return obj;
}

static bool
number_of(struct json* item, char* key, double* value)
{
   enum value_type type = ValueNone;
   uintptr_t data;

   if (item == NULL || item->type != JSONItem)
   {
      return false;
   }

   data = pgagroal_json_get_typed(item, key, &type);
   if (type == ValueInt64)
   {
      *value = (double)(int64_t)data;
      return true;
   }
   if (type == ValueDouble)
   {
      *value = pgagroal_value_to_double(data);
      return true;
   }

   return false;
}

/* The value of a key as text, or NULL if the key is missing */
static char*
string_of(struct json* item, char* key, char* buf, size_t size)
{
   enum value_type type = ValueNone;
   uintptr_t data = pgagroal_json_get_typed(item, key, &type);

   switch (type)
   {
      case ValueString:
      case ValueStringRef:
         pgagroal_snprintf(buf, size, "%s", data != 0 ? (char*)data : "");
         return buf;
      case ValueInt64:
         pgagroal_snprintf(buf, size, "%" PRId64, (int64_t)data);
         return buf;
      case ValueDouble:
         pgagroal_snprintf(buf, size, "%g", pgagroal_value_to_double(data));
         return buf;
      case ValueBool:
         pgagroal_snprintf(buf, size, "%s", data ? "true" : "false");
         return buf;
      default:
         return NULL;
   }
}

static bool
has_workload(struct run* run, char* workload)
{
   for (int i = 0; i < run->number_of_workloads; i++)
   {
      if (!strcmp(run->workloads[i], workload))
      {
         return true;
      }
   }

   return false;
}

static double
median_of(struct run* run, char* workload, char* metric)
{
   double* values = calloc(run->number_of_iters, sizeof(double));
   double median;
   int n = run->number_of_iters;

   for (int i = 0; i < n; i++)
   {
      number_of((struct json*)pgagroal_json_get(run->iters[i], workload), metric, &values[i]);
   }
   qsort(values, n, sizeof(double), compare_doubles);

   if (n % 2 == 1)
   {
      median = values[n / 2];
   }
   else
   {
      median = (values[n / 2 - 1] + values[n / 2]) / 2.0;
   }

   free(values);

   return median;
}

static void
range_of(struct run* run, char* workload, char* metric, double* min, double* max)
{
   for (int i = 0; i < run->number_of_iters; i++)
   {
      double value = 0.0;

      number_of((struct json*)pgagroal_json_get(run->iters[i], workload), metric, &value);
      if (i == 0 || value < *min)
      {
         *min = value;
      }
      if (i == 0 || value > *max)
      {
         *max = value;
      }
   }
}

/* Like pgmoneta's classify(), plus: overlapping ranges are no change */
static void
classify(double base, double cand, bool higher_is_better, double band,
         double base_min, double base_max, double cand_min, double cand_max,
         char* out, size_t size)
{
   double ratio;

   if (fabs(cand - base) / base < band)
   {
      pgagroal_snprintf(out, size, "no change");
      return;
   }

   if (base_min <= cand_max && cand_min <= base_max)
   {
      pgagroal_snprintf(out, size, "no change (ranges overlap)");
      return;
   }

   ratio = higher_is_better ? cand / base : base / cand;
   if (ratio > 1.0)
   {
      pgagroal_snprintf(out, size, "%.2fx faster", ratio);
   }
   else
   {
      pgagroal_snprintf(out, size, "%.2fx slower", 1.0 / ratio);
   }
}

/* Compare a side with its previous run */
static void
drift_check(char* label, struct run* newest, char* previous_dir, double band,
            struct messages* warnings, struct messages* notes)
{
   struct run previous;
   char branch[MESSAGE_LENGTH];
   char stamp[MESSAGE_LENGTH];
   char prefix[MESSAGE_LENGTH];
   char* warning = NULL;
   bool shifted = false;

   memset(&previous, 0, sizeof(previous));
   string_of(newest->meta, "branch", branch, sizeof(branch));

   if (previous_dir == NULL)
   {
      add_message(notes, "%s was measured once; measure it again to check for drift.", branch);
      return;
   }

   pgagroal_snprintf(prefix, sizeof(prefix), "%s previous", label);
   if (load_run(prefix, previous_dir, &previous))
   {
      add_message(notes, "drift of %s not checked: %s", branch, integrity_error);
      free_run(&previous);
      return;
   }

   if (string_of(previous.meta, "timestamp", stamp, sizeof(stamp)) == NULL)
   {
      pgagroal_snprintf(stamp, sizeof(stamp), "?");
   }

   warning = pgagroal_format_and_append(NULL, "DRIFT: %s differs from its own previous run (%s):", branch, stamp);
   for (int w = 0; w < newest->number_of_workloads; w++)
   {
      char* workload = newest->workloads[w];

      if (!has_workload(&previous, workload))
      {
         continue;
      }

      for (int m = 0; metrics[m].name != NULL; m++)
      {
         double p = median_of(&previous, workload, metrics[m].name);
         double n = median_of(newest, workload, metrics[m].name);
         double pmin, pmax, nmin, nmax;
         char change[MESSAGE_LENGTH];
         char ps[64], ns[64];

         range_of(&previous, workload, metrics[m].name, &pmin, &pmax);
         range_of(newest, workload, metrics[m].name, &nmin, &nmax);
         classify(p, n, metrics[m].higher_is_better, band, pmin, pmax, nmin, nmax, change, sizeof(change));

         if (strncmp(change, "no change", strlen("no change")))
         {
            pgagroal_snprintf(ps, sizeof(ps), metrics[m].format, p);
            pgagroal_snprintf(ns, sizeof(ns), metrics[m].format, n);
            warning = pgagroal_format_and_append(warning, "\n  %s %s %s -> %s (%s)", workload, metrics[m].name, ps, ns, change);
            shifted = true;
         }
      }
   }
   warning = pgagroal_append(warning, "\n  Same code, different numbers: differences this small are not a result.");

   if (shifted)
   {
      add_message(warnings, "%s", warning);
   }
   else
   {
      add_message(notes, "%s agrees with its previous run (%s): no drift.", branch, stamp);
   }

   free(warning);
   free_run(&previous);
}

static void
warnings_for(struct run* base, struct run* cand, struct messages* warnings)
{
   char* keys[] = {"cpus", "os", "ev_backend", "postgres", NULL};
   char* whats[] = {"CPU count", "OS", "event backend", "PostgreSQL setup", NULL};
   char* labels[] = {"baseline", "candidate"};
   struct run* runs[] = {base, cand};
   char b[MESSAGE_LENGTH];
   char c[MESSAGE_LENGTH];

   for (int i = 0; keys[i] != NULL; i++)
   {
      if (string_of(base->meta, keys[i], b, sizeof(b)) != NULL &&
          string_of(cand->meta, keys[i], c, sizeof(c)) != NULL && strcmp(b, c))
      {
         add_message(warnings, "WARNING: %s differs (%s vs %s); results are not comparable.", whats[i], b, c);
      }
   }

   for (int i = 0; i < 2; i++)
   {
      enum value_type type = ValueNone;
      uintptr_t data;

      if (string_of(runs[i]->meta, "build_type", b, sizeof(b)) != NULL && strcmp(b, "Release"))
      {
         add_message(warnings, "WARNING: %s was built as %s, not Release.", labels[i], b);
      }

      data = pgagroal_json_get_typed(runs[i]->meta, "dirty", &type);
      if (type == ValueBool && data)
      {
         add_message(warnings, "WARNING: %s was measured on a dirty working tree.", labels[i]);
      }

      data = pgagroal_json_get_typed(runs[i]->meta, "pgagroal_errors", &type);
      if (type == ValueInt64 && (int64_t)data > 0)
      {
         add_message(warnings, "WARNING: pgagroal logged %" PRId64 " errors while measuring the %s.", (int64_t)data, labels[i]);
      }
   }
}

static void
notes_for(struct run* base, struct run* cand, struct messages* notes)
{
   char b[MESSAGE_LENGTH];
   char c[MESSAGE_LENGTH];
   bool b_other;
   bool c_other;

   if (string_of(base->meta, "note", b, sizeof(b)) != NULL && b[0] != '\0')
   {
      add_message(notes, "%s", b);
   }
   if (string_of(cand->meta, "note", c, sizeof(c)) != NULL && c[0] != '\0')
   {
      add_message(notes, "%s", c);
   }

   b_other = string_of(base->meta, "os", b, sizeof(b)) != NULL && strcmp(b, "Linux");
   c_other = string_of(cand->meta, "os", c, sizeof(c)) != NULL && strcmp(c, "Linux");
   if (b_other && c_other && strcmp(b, c))
   {
      if (strcmp(b, c) > 0)
      {
         add_message(notes, "Measured on %s, %s, not Linux: only the direction of a change counts.", c, b);
      }
      else
      {
         add_message(notes, "Measured on %s, %s, not Linux: only the direction of a change counts.", b, c);
      }
   }
   else if (b_other || c_other)
   {
      add_message(notes, "Measured on %s, not Linux: only the direction of a change counts.", b_other ? b : c);
   }
}

static void
describe(struct run* run, char* out, size_t size)
{
   char branch[MESSAGE_LENGTH];
   char commit[MESSAGE_LENGTH];
   char version[MESSAGE_LENGTH];

   string_of(run->meta, "branch", branch, sizeof(branch));
   string_of(run->meta, "commit", commit, sizeof(commit));

   if (string_of(run->meta, "pgagroal_version", version, sizeof(version)) != NULL && version[0] != '\0')
   {
      pgagroal_snprintf(out, size, "%s @ %s (pgagroal %s)", branch, commit, version);
   }
   else
   {
      pgagroal_snprintf(out, size, "%s @ %s", branch, commit);
   }
}

/* Add a message unless it is already there */
static void
add_message(struct messages* messages, char* fmt, ...)
{
   char buf[MESSAGE_LENGTH * 4];
   va_list ap;

   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);

   for (int i = 0; i < messages->size; i++)
   {
      if (!strcmp(messages->list[i], buf))
      {
         return;
      }
   }

   if (messages->size < MAX_MESSAGES)
   {
      messages->list[messages->size++] = strdup(buf);
   }
}

static void
free_messages(struct messages* messages)
{
   for (int i = 0; i < messages->size; i++)
   {
      free(messages->list[i]);
   }
   messages->size = 0;
}

static int
compare_doubles(const void* a, const void* b)
{
   double x = *(const double*)a;
   double y = *(const double*)b;

   return (x > y) - (x < y);
}

static int
compare_paths(const void* a, const void* b)
{
   return strcmp(*(char* const*)a, *(char* const*)b);
}
