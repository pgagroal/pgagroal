/*
 * oversubscription_test.c - Oversubscription / wait-queue stress test for pgagroal.
 *
 * Spawns N concurrent libpq clients against a pgagroal instance whose
 * max_connections is smaller than N (e.g. 200 clients vs 100 slots) and
 * verifies the wait-queue contract:
 *
 *   1. Admission: at most max_connections clients may hold a pool slot at any
 *      instant (exact client-side in-flight counter; optionally cross-checked
 *      by sampling pg_stat_activity directly on PostgreSQL).
 *   2. Absorption (burst): when wait_queue_timeout is generous, clients beyond
 *      the pool capacity must be SERVED (queued, not rejected).
 *   3. Timeout contract (burst): when holders outlast wait_queue_timeout,
 *      exactly --expect-failures clients must fail with the pool-full/timeout
 *      error.
 *   4. FIFO (stagger): clients that had to wait must be admitted in enqueue
 *      order (--require-fifo turns the report into a hard assertion).
 *   5. No starvation (churn): every client must keep making progress while
 *      the pool is oversubscribed.
 *
 * Build:   make                          (needs libpq headers, e.g. libpq-dev)
 *          make asan                     (AddressSanitizer build)
 *
 * Example: 200 clients against max_connections = 100, every client holds its
 * slot 0.5 s; all 200 must be served, occupancy must never exceed 100:
 *
 *   ./oversubscription_test --port 2345 --user mohammad --dbname postgres \
 *       --clients 200 --max-connections 100 --hold 0.5 --mode burst \
 *       --activity-dsn "host=localhost port=5432 user=mohammad dbname=postgres"
 *
 * More scenarios in run_examples.sh.
 *
 * Exit code: 0 = all configured assertions passed
 *            1 = an assertion failed (report printed)
 *            2 = usage / environment error
 *
 * Notes:
 *   - libpq is used from one thread per client (thread-safe: one PGconn per
 *     thread). Thread-per-client is fine up to a few thousand clients.
 *   - The client-side "in flight" counter spans PQconnectdb() success through
 *     PQfinish(); while a client is in flight it holds exactly one pool slot,
 *     so peak in flight > max_connections means the pool leaked a slot.
 *   - libpq's connect_timeout caps how long a client waits INSIDE the queue;
 *     keep it well above pgagroal's wait_queue_timeout (default here: 120 s)
 *     so timeouts are produced by pgagroal, not by libpq.
 *   - The pg_stat_activity sampler counts every non-idle backend on the
 *     PostgreSQL instance; run it on an otherwise idle instance.
 *   - Ctrl-C cleanly stops churn mode; in burst mode it takes effect after
 *     the current connection attempts return.
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <libpq-fe.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define MAX_ERR_KINDS 16
#define MAX_CLIENTS   4096
#define ERR_MSG_CAP   192

// enums

enum { CLS_NONE = 0, CLS_POOL_FULL, CLS_LIBPQ_TIMEOUT, CLS_REFUSED, CLS_OTHER };
enum { ST_PENDING = 0, ST_SERVED, ST_FAILED };
enum { MODE_BURST = 0, MODE_STAGGER, MODE_CHURN };
enum { SAMPLER_OFF = 0, SAMPLER_RUNNING, SAMPLER_UNAVAILABLE, SAMPLER_ERROR };

static const char *cls_name[] = { "none", "pool-full", "libpq-timeout", "refused", "other" };

// barrier

#ifdef __APPLE__
/* pthread_barrier() is not available on macOS; small mutex/cond fallback. */
typedef struct my_barrier
{
  pthread_mutex_t mu;
  pthread_cond_t cv;
  unsigned threshold;
  unsigned count;
  unsigned generation;
} my_barrier_t;

static int
my_barrier_init (my_barrier_t *b, void *attr, unsigned n)
{
  (void)attr;
  if (pthread_mutex_init (&b->mu, NULL) != 0)
    return -1;
  if (pthread_cond_init (&b->cv, NULL) != 0)
  {
    pthread_mutex_destroy (&b->mu);
    return -1;
  }
  b->threshold = n;
  b->count = 0;
  b->generation = 0;
  return 0;
}

static int
my_barrier_wait (my_barrier_t *b)
{
  unsigned gen;
  pthread_mutex_lock (&b->mu);
  gen = b->generation;
  if (++b->count == b->threshold)
  {
    b->count = 0;
    b->generation++;
    pthread_cond_broadcast (&b->cv);
    pthread_mutex_unlock (&b->mu);
    return 0;
  }
  while (gen == b->generation)
    pthread_cond_wait (&b->cv, &b->mu);
  pthread_mutex_unlock (&b->mu);
  return 0;
}

static int
my_barrier_destroy (my_barrier_t *b)
{
  pthread_cond_destroy (&b->cv);
  pthread_mutex_destroy (&b->mu);
  return 0;
}
#else
typedef pthread_barrier_t my_barrier_t;

static int
my_barrier_init (my_barrier_t *b, void *attr, unsigned n)
{
  return pthread_barrier_init (b, (const pthread_barrierattr_t *) attr, n);
}

static int
my_barrier_wait (my_barrier_t *b)
{
  return pthread_barrier_wait (b);
}

static int
my_barrier_destroy (my_barrier_t *b)
{
  return pthread_barrier_destroy (b);
}
#endif

// types

typedef struct
{
  /* connection */
  char host[128];
  int port;
  char user[128];
  char password[128];
  char dbname[128];
  char conninfo[1024];
  int have_conninfo;
  int connect_timeout;
  /* load */
  int mode;
  long clients;
  double hold;
  double duration;
  long stagger_ms;
  double churn_sleep;
  /* assertions */
  long max_connections;
  int have_max;
  long expect_failures;
  int have_expect;
  int require_fifo;
  double fifo_eps;
  double queued_threshold;
  double starve_limit;
  long min_iters;
  /* sampler */
  char activity_dsn[1024];
  int have_activity;
  long sample_ms;
  int verbose;
} cfg_t;

typedef struct
{
  double t_start;
  double t_connected;
  double t_finished;
  double wait;    /* served: connect time incl. queue wait */
  double ttf;     /* failed: time to failure */
  double max_gap; /* churn: largest gap between successive completions */
  int status;
  int cls;
  int iters;
  int connect_attempts;
  int starved;
  char err[ERR_MSG_CAP];
} result_t;

typedef struct
{
  int idx;
  my_barrier_t *bar;
} warg_t;

// globals

static cfg_t g_cfg;
static result_t *g_results;
static double g_t0;

static atomic_long g_in_flight;
static atomic_long g_peak_in_flight;
static atomic_long g_peak_active;
static atomic_long g_samples;
static atomic_int g_sampler_state;
static atomic_int g_done;

static volatile sig_atomic_t g_stop;

static pthread_mutex_t g_err_mu = PTHREAD_MUTEX_INITIALIZER;

typedef struct
{
  char msg[ERR_MSG_CAP];
  long count;
  int cls;
} err_kind_t;

static err_kind_t g_errs[MAX_ERR_KINDS];
static int g_nerrs;

// helpers

static double
now_s (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
}

static void
msleep (long ms)
{
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  while (nanosleep (&ts, &ts) != 0 && errno == EINTR)
  {
  }
}

static const char *
istrstr (const char *haystack, const char *needle)
{
  size_t nl;
  if (haystack == NULL || needle == NULL)
    return NULL;
  nl = strlen (needle);
  if (nl == 0)
    return haystack;
  for (; *haystack; haystack++)
  {
    size_t i = 0;
    while (i < nl && tolower ((unsigned char) haystack[i]) == tolower ((unsigned char) needle[i]))
      i++;
    if (i == nl)
      return haystack;
  }
  return NULL;
}

static void
sanitize_msg (char *dst, size_t cap, const char *src)
{
  size_t i, j = 0;
  if (src != NULL)
  {
    for (i = 0; src[i] != 0 && j < cap - 1; i++)
      dst[j++] = (src[i] == '\n' || src[i] == '\r') ? ' ' : src[i];
  }
  dst[j] = 0;
  while (j > 0 && dst[j - 1] == ' ')
    dst[--j] = 0;
}

static int
classify_err (const char *msg)
{
  if (msg == NULL || msg[0] == 0)
    return CLS_OTHER;
  if (istrstr (msg, "pool is full") || istrstr (msg, "connection pool") || istrstr (msg, "wait queue"))
    return CLS_POOL_FULL;
  if (istrstr (msg, "timeout expired") || istrstr (msg, "timed out"))
    return CLS_LIBPQ_TIMEOUT;
  if (istrstr (msg, "connection refused"))
    return CLS_REFUSED;
  return CLS_OTHER;
}

static void
record_error (const char *raw, int cls)
{
  char clean[ERR_MSG_CAP];
  int i;
  sanitize_msg (clean, sizeof (clean), raw);
  pthread_mutex_lock (&g_err_mu);
  for (i = 0; i < g_nerrs; i++)
  {
    if (strcmp (g_errs[i].msg, clean) == 0)
    {
      g_errs[i].count++;
      pthread_mutex_unlock (&g_err_mu);
      return;
    }
  }
  if (g_nerrs < MAX_ERR_KINDS)
  {
    snprintf (g_errs[g_nerrs].msg, ERR_MSG_CAP, "%s", clean);
    g_errs[g_nerrs].count = 1;
    g_errs[g_nerrs].cls = cls;
    g_nerrs++;
  }
  pthread_mutex_unlock (&g_err_mu);
}

static void
in_flight_add (int d)
{
  long v = atomic_fetch_add (&g_in_flight, d) + d;
  long p = atomic_load (&g_peak_in_flight);
  while (v > p && !atomic_compare_exchange_weak (&g_peak_in_flight, &p, v))
  {
    /* p was reloaded by the failed CAS */
  }
}

static void
ci_escape (char *dst, size_t cap, const char *src)
{
  size_t i, j = 0;
  for (i = 0; src[i] != 0 && j < cap - 2; i++)
  {
    if (src[i] == '\'' || src[i] == '\\')
      dst[j++] = '\\';
    dst[j++] = src[i];
  }
  dst[j] = 0;
}

static void
build_conninfo (char *dst, size_t cap)
{
  if (g_cfg.have_conninfo)
  {
    snprintf (dst, cap, "%s", g_cfg.conninfo);
    if (istrstr (dst, "connect_timeout") == NULL)
    {
      size_t len = strlen (dst);
      snprintf (dst + len, cap - len, " connect_timeout=%d", g_cfg.connect_timeout);
    }
    return;
  }
  {
    char eh[256], eu[256], ep[256], ed[256];
    size_t len;
    ci_escape (eh, sizeof (eh), g_cfg.host);
    ci_escape (eu, sizeof (eu), g_cfg.user);
    ci_escape (ep, sizeof (ep), g_cfg.password);
    ci_escape (ed, sizeof (ed), g_cfg.dbname);
    snprintf (dst, cap, "host='%s' port='%d' user='%s' dbname='%s' connect_timeout=%d",
              eh, g_cfg.port, eu, ed, g_cfg.connect_timeout);
    if (g_cfg.password[0] != 0)
    {
      len = strlen (dst);
      snprintf (dst + len, cap - len, " password='%s'", ep);
    }
  }
}

static int
dcmp (const void *a, const void *b)
{
  double x = *(const double *) a, y = *(const double *) b;
  return (x > y) - (x < y);
}

/* a is sorted ascending, n > 0; nearest-rank percentile */
static double
pct_of (const double *a, int n, double p)
{
  int i;
  if (n <= 0)
    return 0.0;
  i = (int) (p / 100.0 * (n - 1) + 0.5);
  if (i < 0)
    i = 0;
  if (i >= n)
    i = n - 1;
  return a[i];
}

static void
on_sigint (int sig)
{
  (void) sig;
  g_stop = 1;
}

// workers

static void
fail_with (result_t *r, PGconn *c, double t_start)
{
  r->status = ST_FAILED;
  r->cls = classify_err (PQerrorMessage (c));
  sanitize_msg (r->err, sizeof (r->err), PQerrorMessage (c));
  r->ttf = now_s () - t_start;
  record_error (r->err, r->cls);
  if (c != NULL)
    PQfinish (c);
}

/* burst / stagger: one connection, hold the slot --hold seconds, release. */
static void
run_once (warg_t *w)
{
  result_t *r = &g_results[w->idx];
  char ci[1200];
  char sql[64];
  PGconn *c;
  PGresult *res;
  double t_start, t_conn, t_end;
  int ok;

  build_conninfo (ci, sizeof (ci));

  if (g_stop)
  {
    r->status = ST_FAILED;
    r->cls = CLS_OTHER;
    snprintf (r->err, ERR_MSG_CAP, "canceled before connecting");
    record_error (r->err, r->cls);
    return;
  }

  t_start = now_s ();
  r->t_start = t_start;
  r->connect_attempts = 1;

  c = PQconnectdb (ci);
  if (c == NULL)
  {
    r->status = ST_FAILED;
    r->cls = CLS_OTHER;
    snprintf (r->err, ERR_MSG_CAP, "PQconnectdb returned NULL (out of memory)");
    r->ttf = now_s () - t_start;
    record_error (r->err, r->cls);
    return;
  }
  if (PQstatus (c) != CONNECTION_OK)
  {
    fail_with (r, c, t_start);
    return;
  }

  t_conn = now_s ();
  r->t_connected = t_conn;
  r->wait = t_conn - t_start;
  r->status = ST_SERVED;

  in_flight_add (1);
  snprintf (sql, sizeof (sql), "SELECT pg_sleep(%.3f)", g_cfg.hold);
  res = PQexec (c, sql);
  ok = (res != NULL && PQresultStatus (res) == PGRES_TUPLES_OK);
  if (res != NULL)
    PQclear (res);
  in_flight_add (-1);

  t_end = now_s ();
  r->t_finished = t_end;

  if (!ok)
  {
    r->status = ST_FAILED;
    r->cls = CLS_OTHER;
    sanitize_msg (r->err, sizeof (r->err), PQerrorMessage (c));
    record_error (r->err, r->cls);
  }
  PQfinish (c);
}

static void *
worker_burst (void *arg)
{
  warg_t *w = arg;
  my_barrier_wait (w->bar);
  run_once (w);
  return NULL;
}

static void *
worker_stagger (void *arg)
{
  run_once ((warg_t *) arg);
  return NULL;
}

/* churn: connect / query / disconnect in a loop until the deadline. */
static void *
worker_churn (void *arg)
{
  warg_t *w = arg;
  result_t *r = &g_results[w->idx];
  char ci[1200];
  char sql[64];
  double deadline, last;

  build_conninfo (ci, sizeof (ci));
  if (g_cfg.churn_sleep > 0)
    snprintf (sql, sizeof (sql), "SELECT pg_sleep(%.3f)", g_cfg.churn_sleep);
  else
    snprintf (sql, sizeof (sql), "SELECT 1");

  deadline = g_t0 + g_cfg.duration;
  last = now_s ();

  while (!g_stop && now_s () < deadline)
  {
    PGconn *c = PQconnectdb (ci);
    double ts = now_s ();

    r->connect_attempts++;

    if (c == NULL)
    {
      r->status = ST_FAILED;
      r->cls = CLS_OTHER;
      snprintf (r->err, ERR_MSG_CAP, "PQconnectdb returned NULL (out of memory)");
      record_error (r->err, r->cls);
      break;
    }
    if (PQstatus (c) != CONNECTION_OK)
    {
      int cls = classify_err (PQerrorMessage (c));
      if (cls == CLS_POOL_FULL)
      {
        /* expected transient while oversubscribed; back off and retry */
        sanitize_msg (r->err, sizeof (r->err), PQerrorMessage (c));
        record_error (r->err, cls);
        PQfinish (c);
        msleep (20);
        continue;
      }
      r->status = ST_FAILED;
      r->cls = cls;
      sanitize_msg (r->err, sizeof (r->err), PQerrorMessage (c));
      r->ttf = ts - g_t0;
      record_error (r->err, cls);
      PQfinish (c);
      break;
    }

    in_flight_add (1);
    {
      PGresult *res = PQexec (c, sql);
      int ok = (res != NULL && PQresultStatus (res) == PGRES_TUPLES_OK);
      if (res != NULL)
        PQclear (res);
      in_flight_add (-1);
      PQfinish (c);
      if (!ok)
      {
        r->status = ST_FAILED;
        r->cls = CLS_OTHER;
        sanitize_msg (r->err, sizeof (r->err), "query failed after connecting");
        record_error (r->err, r->cls);
        break;
      }
    }

    {
      double tdone = now_s ();
      double gap = tdone - last;
      if (gap > r->max_gap)
        r->max_gap = gap;
      last = tdone;
    }
    r->iters++;
    r->status = ST_SERVED;
  }

  if (r->status != ST_FAILED && r->iters == 0)
  {
    r->status = ST_FAILED;
    r->cls = CLS_POOL_FULL;
    r->starved = 1;
    snprintf (r->err, ERR_MSG_CAP, "never admitted: 0 of %d attempts succeeded in %.1fs",
              r->connect_attempts, g_cfg.duration);
    record_error (r->err, r->cls);
  }
  return NULL;
}

// sampler

static void *
sampler_main (void *arg)
{
  char ci[1100];
  PGconn *c;
  size_t len;

  (void) arg;
  snprintf (ci, sizeof (ci) - 1, "%s", g_cfg.activity_dsn);
  len = strlen (ci);
  snprintf (ci + len, sizeof (ci) - len, " connect_timeout=5 application_name='oversubscription_sampler'");

  c = PQconnectdb (ci);
  if (c == NULL || PQstatus (c) != CONNECTION_OK)
  {
    record_error (c != NULL ? PQerrorMessage (c) : "sampler: PQconnectdb returned NULL", CLS_OTHER);
    atomic_store (&g_sampler_state, SAMPLER_UNAVAILABLE);
    if (c != NULL)
      PQfinish (c);
    return NULL;
  }
  atomic_store (&g_sampler_state, SAMPLER_RUNNING);

  while (!atomic_load (&g_done) && !g_stop)
  {
    PGresult *r = PQexec (c,
        "SELECT count(*) FROM pg_stat_activity"
        " WHERE pid <> pg_backend_pid() AND state <> 'idle'");
    if (r == NULL || PQresultStatus (r) != PGRES_TUPLES_OK)
    {
      record_error (PQerrorMessage (c), CLS_OTHER);
      atomic_store (&g_sampler_state, SAMPLER_ERROR);
      if (r != NULL)
        PQclear (r);
      break;
    }
    {
      long v = atol (PQgetvalue (r, 0, 0));
      long p = atomic_load (&g_peak_active);
      while (v > p && !atomic_compare_exchange_weak (&g_peak_active, &p, v))
      {
      }
      atomic_fetch_add (&g_samples, 1);
    }
    PQclear (r);
    msleep (g_cfg.sample_ms);
  }
  PQfinish (c);
  return NULL;
}

// usage

static void
usage (FILE *out)
{
  fprintf (out,
      "pgagroal oversubscription / wait-queue stress test\n\n"
      "Usage: oversubscription_test [options]\n\n"
      "Connection:\n"
      "  -H, --host HOST          pgagroal host (default localhost)\n"
      "  -p, --port PORT          pgagroal port (default 2345)\n"
      "  -U, --user USER          user name (default: none, libpq default)\n"
      "  -w, --password PASS      password (or use PGPASSWORD/.pgpass)\n"
      "  -d, --dbname DB          database name (default postgres)\n"
      "  -C, --conninfo STR       full libpq conninfo; overrides the options above\n"
      "  --connect-timeout S      libpq cap on the whole connect (incl. queue wait);\n"
      "                           default 120 s. Keep it well above wait_queue_timeout.\n\n"
      "Load:\n"
      "  -M, --mode MODE          burst | stagger | churn (default burst)\n"
      "  -c, --clients N          concurrent clients (default 200)\n"
      "  -s, --hold SECONDS       slot hold time per client in burst/stagger (0.25)\n"
      "  -T, --duration SECONDS   churn mode run length (default 30)\n"
      "  --churn-sleep SECONDS    optional pg_sleep inside each churn transaction (0)\n"
      "  --stagger MS             launch spacing between clients in stagger mode (50)\n\n"
      "Assertions:\n"
      "  -m, --max-connections N  pgagroal max_connections; enables the occupancy\n"
      "                           invariant (peak in-flight must never exceed N)\n"
      "  --expect-failures K      exact number of clients that must fail (default 0;\n"
      "                           used for the wait_queue_timeout contract)\n"
      "  --require-fifo           stagger mode: fail if admission order deviates\n"
      "                           from enqueue order\n"
      "  --fifo-eps S             tolerance for admission-order comparison (0)\n"
      "  --queued-threshold S     waits >= S count as 'queued' for FIFO/stats (0.1)\n"
      "  --starve-limit S         churn: max allowed per-client completion gap (10)\n"
      "  --min-iters N            churn: minimum iterations per client (default 1)\n\n"
      "Observability:\n"
      "  --activity-dsn DSN       libpq conninfo DIRECTLY to PostgreSQL (bypassing\n"
      "                           pgagroal) to sample active backends and verify\n"
      "                           occupancy server-side\n"
      "  --sample-ms MS           sampler interval (default 200)\n"
      "  -v, --verbose            reserved for progress output\n"
      "  -h, --help               this help\n\n"
      "Exit codes: 0 all assertions passed, 1 assertion failed, 2 usage/env error\n");
}

// main

int
main (int argc, char *argv[])
{
  int opt, opt_idx, ret = 2;
  char conninfo[1200];
  my_barrier_t bar;
  my_barrier_t *barp = NULL;
  pthread_t *threads = NULL;
  warg_t *wargs = NULL;
  pthread_t sampler;
  int have_sampler = 0;
  long i, served = 0, failed = 0, queued = 0;
  long cls_count[5] = { 0, 0, 0, 0, 0 };
  double *waits = NULL;
  double *ttfs = NULL;
  int n_waits = 0, n_ttfs = 0;
  long peak_in_flight, peak_active;
  int check_occ = 0, check_fail = 0, check_fifo = 0, check_churn = 0, check_sampler = 0;
  int occ_ok = 1, fail_ok = 1, fifo_ok = 1, churn_ok = 1, sampler_ok = 1;
  double wait_min = 0, wait_p50 = 0, wait_p90 = 0, wait_p99 = 0, wait_max = 0;
  double ttf_med = 0, ttf_max = 0;
  long fifo_pairs = 0, fifo_inv = 0;
  int *qidx = NULL;
  int n_q = 0;
  long iters_total = 0, iters_min = 0, iters_max = 0;
  double gap_max = 0;
  struct sigaction sa;

  static struct option long_options[] = {
    { "host", required_argument, 0, 'H' },
    { "port", required_argument, 0, 'p' },
    { "user", required_argument, 0, 'U' },
    { "password", required_argument, 0, 'w' },
    { "dbname", required_argument, 0, 'd' },
    { "conninfo", required_argument, 0, 'C' },
    { "connect-timeout", required_argument, 0, 0 },
    { "mode", required_argument, 0, 'M' },
    { "clients", required_argument, 0, 'c' },
    { "hold", required_argument, 0, 's' },
    { "duration", required_argument, 0, 'T' },
    { "churn-sleep", required_argument, 0, 0 },
    { "stagger", required_argument, 0, 0 },
    { "max-connections", required_argument, 0, 'm' },
    { "expect-failures", required_argument, 0, 0 },
    { "require-fifo", no_argument, 0, 0 },
    { "fifo-eps", required_argument, 0, 0 },
    { "queued-threshold", required_argument, 0, 0 },
    { "starve-limit", required_argument, 0, 0 },
    { "min-iters", required_argument, 0, 0 },
    { "activity-dsn", required_argument, 0, 0 },
    { "sample-ms", required_argument, 0, 0 },
    { "verbose", no_argument, 0, 'v' },
    { "help", no_argument, 0, 'h' },
    { NULL, 0, NULL, 0 }
  };

  /* defaults */
  memset (&g_cfg, 0, sizeof (g_cfg));
  snprintf (g_cfg.host, sizeof (g_cfg.host), "localhost");
  g_cfg.port = 2345;
  snprintf (g_cfg.dbname, sizeof (g_cfg.dbname), "postgres");
  g_cfg.connect_timeout = 120;
  g_cfg.mode = MODE_BURST;
  g_cfg.clients = 200;
  g_cfg.hold = 0.25;
  g_cfg.duration = 30.0;
  g_cfg.stagger_ms = 50;
  g_cfg.queued_threshold = 0.10;
  g_cfg.fifo_eps = 0.0;
  g_cfg.starve_limit = 10.0;
  g_cfg.min_iters = 1;
  g_cfg.sample_ms = 200;

  atomic_init (&g_in_flight, 0);
  atomic_init (&g_peak_in_flight, 0);
  atomic_init (&g_peak_active, 0);
  atomic_init (&g_samples, 0);
  atomic_init (&g_sampler_state, SAMPLER_OFF);
  atomic_init (&g_done, 0);

  while ((opt = getopt_long (argc, argv, "H:p:U:w:d:C:M:c:s:T:m:vh", long_options, &opt_idx)) != -1)
  {
    switch (opt)
    {
      case 0:
        if (strcmp (long_options[opt_idx].name, "connect-timeout") == 0)
          g_cfg.connect_timeout = atoi (optarg);
        else if (strcmp (long_options[opt_idx].name, "churn-sleep") == 0)
          g_cfg.churn_sleep = atof (optarg);
        else if (strcmp (long_options[opt_idx].name, "stagger") == 0)
          g_cfg.stagger_ms = atol (optarg);
        else if (strcmp (long_options[opt_idx].name, "expect-failures") == 0)
        {
          g_cfg.expect_failures = atol (optarg);
          g_cfg.have_expect = 1;
        }
        else if (strcmp (long_options[opt_idx].name, "require-fifo") == 0)
          g_cfg.require_fifo = 1;
        else if (strcmp (long_options[opt_idx].name, "fifo-eps") == 0)
          g_cfg.fifo_eps = atof (optarg);
        else if (strcmp (long_options[opt_idx].name, "queued-threshold") == 0)
          g_cfg.queued_threshold = atof (optarg);
        else if (strcmp (long_options[opt_idx].name, "starve-limit") == 0)
          g_cfg.starve_limit = atof (optarg);
        else if (strcmp (long_options[opt_idx].name, "min-iters") == 0)
          g_cfg.min_iters = atol (optarg);
        else if (strcmp (long_options[opt_idx].name, "activity-dsn") == 0)
        {
          snprintf (g_cfg.activity_dsn, sizeof (g_cfg.activity_dsn), "%s", optarg);
          g_cfg.have_activity = 1;
        }
        else if (strcmp (long_options[opt_idx].name, "sample-ms") == 0)
          g_cfg.sample_ms = atol (optarg);
        break;
      case 'H': snprintf (g_cfg.host, sizeof (g_cfg.host), "%s", optarg); break;
      case 'p': g_cfg.port = atoi (optarg); break;
      case 'U': snprintf (g_cfg.user, sizeof (g_cfg.user), "%s", optarg); break;
      case 'w': snprintf (g_cfg.password, sizeof (g_cfg.password), "%s", optarg); break;
      case 'd': snprintf (g_cfg.dbname, sizeof (g_cfg.dbname), "%s", optarg); break;
      case 'C':
        snprintf (g_cfg.conninfo, sizeof (g_cfg.conninfo), "%s", optarg);
        g_cfg.have_conninfo = 1;
        break;
      case 'M':
        if (strcmp (optarg, "burst") == 0)
          g_cfg.mode = MODE_BURST;
        else if (strcmp (optarg, "stagger") == 0)
          g_cfg.mode = MODE_STAGGER;
        else if (strcmp (optarg, "churn") == 0)
          g_cfg.mode = MODE_CHURN;
        else
        {
          fprintf (stderr, "ERROR: unknown mode '%s' (burst|stagger|churn)\n", optarg);
          return 2;
        }
        break;
      case 'c': g_cfg.clients = atol (optarg); break;
      case 's': g_cfg.hold = atof (optarg); break;
      case 'T': g_cfg.duration = atof (optarg); break;
      case 'm':
        g_cfg.max_connections = atol (optarg);
        g_cfg.have_max = 1;
        break;
      case 'v': g_cfg.verbose = 1; break;
      case 'h': usage (stdout); return 0;
      default: usage (stderr); return 2;
    }
  }

  /* validation */
  if (g_cfg.clients < 1 || g_cfg.clients > MAX_CLIENTS)
  {
    fprintf (stderr, "ERROR: --clients must be in [1, %d]\n", MAX_CLIENTS);
    return 2;
  }
  if (g_cfg.hold < 0 || g_cfg.duration <= 0 || g_cfg.stagger_ms < 0 || g_cfg.sample_ms < 10)
  {
    fprintf (stderr, "ERROR: invalid load parameters\n");
    return 2;
  }
  if (g_cfg.have_max && g_cfg.max_connections < 1)
  {
    fprintf (stderr, "ERROR: --max-connections must be >= 1\n");
    return 2;
  }
  if (g_cfg.connect_timeout < 1)
  {
    fprintf (stderr, "ERROR: --connect-timeout must be >= 1 (seconds)\n");
    return 2;
  }

  build_conninfo (conninfo, sizeof (conninfo));

  g_results = calloc ((size_t) g_cfg.clients, sizeof (result_t));
  threads = calloc ((size_t) g_cfg.clients, sizeof (pthread_t));
  wargs = calloc ((size_t) g_cfg.clients, sizeof (warg_t));
  if (g_results == NULL || threads == NULL || wargs == NULL)
  {
    fprintf (stderr, "ERROR: out of memory\n");
    return 2;
  }

  printf ("=== pgagroal oversubscription test ===\n");
  printf ("mode=%s clients=%ld",
          g_cfg.mode == MODE_BURST ? "burst" : g_cfg.mode == MODE_STAGGER ? "stagger" : "churn",
          g_cfg.clients);
  if (g_cfg.mode == MODE_CHURN)
    printf (" duration=%.1fs churn_sleep=%.3fs", g_cfg.duration, g_cfg.churn_sleep);
  else
    printf (" hold=%.3fs", g_cfg.hold);
  if (g_cfg.mode == MODE_STAGGER)
    printf (" stagger=%ldms", g_cfg.stagger_ms);
  printf ("\n");
  if (g_cfg.have_conninfo)
    printf ("target=<conninfo provided, %zu chars>\n", strlen (g_cfg.conninfo));
  else
    printf ("target=%s:%d db=%s user=%s\n", g_cfg.host, g_cfg.port, g_cfg.dbname, g_cfg.user);
  printf ("max_connections=%s expect_failures=%s activity_sampler=%s\n",
          g_cfg.have_max ? "set" : "NOT SET (occupancy check skipped)",
          g_cfg.have_expect ? "set" : "0 (default)",
          g_cfg.have_activity ? "on" : "off");
  printf ("\n");

  memset (&sa, 0, sizeof (sa));
  sa.sa_handler = on_sigint;
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);

  if (g_cfg.mode == MODE_BURST)
  {
    if (my_barrier_init (&bar, NULL, (unsigned) g_cfg.clients) != 0)
    {
      fprintf (stderr, "ERROR: barrier init failed\n");
      return 2;
    }
    barp = &bar;
  }

  if (g_cfg.have_activity)
  {
    if (pthread_create (&sampler, NULL, sampler_main, NULL) != 0)
    {
      fprintf (stderr, "WARNING: cannot start sampler thread\n");
      g_cfg.have_activity = 0;
    }
    else
    {
      have_sampler = 1;
    }
  }

  g_t0 = now_s ();

  for (i = 0; i < g_cfg.clients; i++)
  {
    wargs[i].idx = (int) i;
    wargs[i].bar = barp;
    void *(*fn) (void *) = g_cfg.mode == MODE_BURST ? worker_burst :
                           g_cfg.mode == MODE_STAGGER ? worker_stagger : worker_churn;
    if (pthread_create (&threads[i], NULL, fn, &wargs[i]) != 0)
    {
      /* Not safe to join: burst workers would block on the barrier forever.
       * Report and leave; the OS reclaims everything. */
      fprintf (stderr, "ERROR: pthread_create failed for client %ld (%s)\n", i, strerror (errno));
      fprintf (stderr, "       check 'ulimit -u' or reduce --clients\n");
      fflush (NULL);
      _exit (2);
    }
    if (g_cfg.mode == MODE_STAGGER && g_cfg.stagger_ms > 0)
      msleep (g_cfg.stagger_ms);
  }

  for (i = 0; i < g_cfg.clients; i++)
    pthread_join (threads[i], NULL);

  atomic_store (&g_done, 1);
  if (have_sampler)
    pthread_join (sampler, NULL);
  if (g_cfg.mode == MODE_BURST)
    my_barrier_destroy (&bar);

  for (i = 0; i < g_cfg.clients; i++)
  {
    result_t *r = &g_results[i];
    if (r->status == ST_SERVED)
      served++;
    else if (r->status == ST_FAILED)
    {
      failed++;
      if (r->cls >= 0 && r->cls <= 4)
        cls_count[r->cls]++;
    }
  }

  waits = calloc ((size_t) (served > 0 ? served : 1), sizeof (double));
  ttfs = calloc ((size_t) (failed > 0 ? failed : 1), sizeof (double));
  if (waits == NULL || ttfs == NULL)
  {
    fprintf (stderr, "ERROR: out of memory\n");
    ret = 2;
    goto cleanup;
  }

  for (i = 0; i < g_cfg.clients; i++)
  {
    result_t *r = &g_results[i];
    if (r->status == ST_SERVED)
    {
      if (g_cfg.mode == MODE_CHURN)
        iters_total += r->iters;
      else
      {
        waits[n_waits++] = r->wait;
        if (r->wait >= g_cfg.queued_threshold)
          queued++;
      }
    }
    else if (r->status == ST_FAILED)
      ttfs[n_ttfs++] = r->ttf;
  }

  if (n_waits > 0)
  {
    qsort (waits, (size_t) n_waits, sizeof (double), dcmp);
    wait_min = waits[0];
    wait_p50 = pct_of (waits, n_waits, 50);
    wait_p90 = pct_of (waits, n_waits, 90);
    wait_p99 = pct_of (waits, n_waits, 99);
    wait_max = waits[n_waits - 1];
  }
  if (n_ttfs > 0)
  {
    qsort (ttfs, (size_t) n_ttfs, sizeof (double), dcmp);
    ttf_med = pct_of (ttfs, n_ttfs, 50);
    ttf_max = ttfs[n_ttfs - 1];
  }

  /* FIFO analysis (stagger mode) */
  if (g_cfg.mode == MODE_STAGGER)
  {
    qidx = calloc ((size_t) g_cfg.clients, sizeof (int));
    if (qidx != NULL)
    {
      for (i = 0; i < g_cfg.clients; i++)
      {
        if (g_results[i].status == ST_SERVED && g_results[i].wait >= g_cfg.queued_threshold)
          qidx[n_q++] = (int) i;
      }
      /* sort queued clients by start (enqueue) time; insertion sort is fine */
      for (i = 1; i < n_q; i++)
      {
        int key = qidx[i];
        long j = i - 1;
        while (j >= 0 && g_results[qidx[j]].t_start > g_results[key].t_start)
        {
          qidx[j + 1] = qidx[j];
          j--;
        }
        qidx[j + 1] = key;
      }
      for (i = 0; i < n_q; i++)
      {
        long j;
        for (j = i + 1; j < n_q; j++)
        {
          fifo_pairs++;
          if (g_results[qidx[i]].t_connected > g_results[qidx[j]].t_connected + g_cfg.fifo_eps)
            fifo_inv++;
        }
      }
    }
  }

  /* churn aggregates */
  if (g_cfg.mode == MODE_CHURN)
  {
    iters_min = -1;
    for (i = 0; i < g_cfg.clients; i++)
    {
      result_t *r = &g_results[i];
      if (iters_min < 0 || r->iters < iters_min)
        iters_min = r->iters;
      if (r->iters > iters_max)
        iters_max = r->iters;
      if (r->max_gap > gap_max)
        gap_max = r->max_gap;
    }
    if (iters_min < 0)
      iters_min = 0;
  }

  peak_in_flight = atomic_load (&g_peak_in_flight);
  peak_active = atomic_load (&g_peak_active);

  printf ("---- results ----\n");
  if (g_cfg.mode == MODE_CHURN)
  {
    double goodput = g_cfg.duration > 0 ? (double) iters_total / g_cfg.duration : 0;
    printf ("iterations total   : %ld (%.1f tx/s)\n", iters_total, goodput);
    printf ("per client min/max : %ld / %ld (min required %ld)\n", iters_min, iters_max, g_cfg.min_iters);
    printf ("max per-client gap : %.3fs (starve limit %.3fs)\n", gap_max, g_cfg.starve_limit);
  }
  else
  {
    printf ("served             : %ld / %ld\n", served, g_cfg.clients);
    printf ("failed             : %ld  (pool-full=%ld libpq-timeout=%ld refused=%ld other=%ld)\n",
            failed, cls_count[CLS_POOL_FULL], cls_count[CLS_LIBPQ_TIMEOUT], cls_count[CLS_REFUSED],
            cls_count[CLS_OTHER]);
    printf ("queued (wait>=%.2fs): %ld\n", g_cfg.queued_threshold, queued);
    if (n_waits > 0)
      printf ("connect wait, s    : min=%.3f p50=%.3f p90=%.3f p99=%.3f max=%.3f\n", wait_min, wait_p50,
              wait_p90, wait_p99, wait_max);
    if (n_ttfs > 0)
      printf ("time-to-failure, s : p50=%.3f max=%.3f\n", ttf_med, ttf_max);
  }

  printf ("---- invariants ----\n");
  if (g_cfg.have_max)
  {
    check_occ = 1;
    occ_ok = peak_in_flight <= g_cfg.max_connections;
    printf ("peak in flight     : %ld / %ld max_connections          %s\n", peak_in_flight,
            g_cfg.max_connections, occ_ok ? "PASS" : "FAIL");
  }
  else
  {
    printf ("peak in flight     : %ld (pass --max-connections to assert)\n", peak_in_flight);
  }

  if (have_sampler)
  {
    int st = atomic_load (&g_sampler_state);
    if (st == SAMPLER_RUNNING)
    {
      if (g_cfg.have_max)
      {
        check_sampler = 1;
        sampler_ok = peak_active <= g_cfg.max_connections;
        printf ("peak active (pg)   : %ld / %ld (%ld samples)          %s\n", peak_active,
                g_cfg.max_connections, atomic_load (&g_samples), sampler_ok ? "PASS" : "FAIL");
      }
      else
      {
        printf ("peak active (pg)   : %ld (%ld samples)\n", peak_active, atomic_load (&g_samples));
      }
    }
    else
    {
      printf ("activity sampler   : %s (see errors below)\n",
              st == SAMPLER_UNAVAILABLE ? "could not connect" : "error mid-run");
    }
  }

  printf ("---- contract ----\n");
  {
    long expected = g_cfg.have_expect ? g_cfg.expect_failures : 0;
    if (g_cfg.mode != MODE_CHURN)
    {
      check_fail = 1;
      fail_ok = (failed == expected);
      printf ("failures           : expected %ld, actual %ld%s\n", expected, failed,
              fail_ok ? "" : "  <-- MISMATCH");
    }
    if (g_cfg.mode == MODE_STAGGER)
    {
      check_fifo = 1;
      printf ("fifo inversions    : %ld / %ld queued-pairs", fifo_inv, fifo_pairs);
      if (n_q > 0)
      {
        int k, printed = 0;
        printf (" (order: ");
        for (k = 0; k < n_q && printed < 20; k++, printed++)
          printf ("%s#%d", k > 0 ? "," : "", qidx[k] + 1);
        if (n_q > printed)
          printf (",...");
        printf (")");
      }
      printf ("\n");
      fifo_ok = (fifo_inv == 0) || !g_cfg.require_fifo;
    }
    if (g_cfg.mode == MODE_CHURN)
    {
      int other_failed = 0, under_min = 0, gap_viol = 0;
      check_churn = 1;
      for (i = 0; i < g_cfg.clients; i++)
      {
        result_t *r = &g_results[i];
        if (r->status == ST_FAILED && r->cls != CLS_POOL_FULL)
          other_failed++;
        if (r->iters < g_cfg.min_iters)
          under_min++;
        if (r->max_gap > g_cfg.starve_limit)
          gap_viol++;
      }
      churn_ok = (other_failed == 0) && (under_min == 0) && (gap_viol == 0);
      printf ("churn health       : other-failures=%d under-min-iters=%d gap-violations=%d  %s\n",
              other_failed, under_min, gap_viol, churn_ok ? "PASS" : "FAIL");
    }
  }

  if (g_nerrs > 0)
  {
    int k;
    printf ("---- errors seen ----\n");
    for (k = 0; k < g_nerrs && k < 8; k++)
      printf ("  [%4ldx %s] %.*s\n", g_errs[k].count, cls_name[g_errs[k].cls], 110, g_errs[k].msg);
    if (g_nerrs > 8)
      printf ("  ... and %d more distinct messages\n", g_nerrs - 8);
  }

  {
    int all_ok = (!check_occ || occ_ok) && (!check_fail || fail_ok) && (!check_fifo || fifo_ok) &&
                 (!check_churn || churn_ok) && (!check_sampler || sampler_ok);
    printf ("OVERALL: %s\n", all_ok ? "PASS" : "FAIL");
    ret = all_ok ? 0 : 1;
  }

  if (g_stop)
    printf ("(interrupted)\n");

cleanup:
  free (waits);
  free (ttfs);
  free (qidx);
  free (g_results);
  free (threads);
  free (wargs);
  return ret;
}
