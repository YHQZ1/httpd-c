#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "metrics.h"

static const int codes[] = {200, 400, 404, 405, 408, 414, 431, 500, 501, 505};
#define N_CODES (sizeof(codes) / sizeof(codes[0]))

static const double bounds[] = {0.0005, 0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1, 5};
#define N_BOUNDS (sizeof(bounds) / sizeof(bounds[0]))

static unsigned long long by_code[N_CODES + 1]; // last slot: any other code
static unsigned long long bucket[N_BOUNDS + 1]; // per-bucket, last slot is +Inf
static unsigned long long req_count;
static double req_sum;
static unsigned long long conns_total;
static long conns_active;
static time_t started;
static char version[64];

void metrics_init(const char *v)
{
  started = time(NULL);
  snprintf(version, sizeof(version), "%s", v);
  for (char *c = version; *c; c++)
  {
    if (*c == '"' || *c == '\\' || *c == '\n')
    {
      *c = '_'; // keep the label value valid
    }
  }
}

void metrics_conn_opened(void)
{
  conns_total++;
  conns_active++;
}

void metrics_conn_closed(void) { conns_active--; }

void metrics_observe_request(int status, double seconds)
{
  size_t i = 0;
  while (i < N_CODES && codes[i] != status)
  {
    i++;
  }
  by_code[i]++;

  size_t b = 0;
  while (b < N_BOUNDS && seconds > bounds[b])
  {
    b++;
  }
  bucket[b]++;
  req_count++;
  req_sum += seconds;
}

static void app(char *buf, size_t cap, size_t *off, const char *fmt, ...)
{
  if (*off >= cap)
  {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + *off, cap - *off, fmt, ap);
  va_end(ap);
  if (n > 0)
  {
    *off += (size_t)n;
  }
}

char *metrics_render(size_t *len)
{
  const size_t cap = 8192;
  char *buf = malloc(cap);
  if (!buf)
  {
    return NULL;
  }
  size_t o = 0;

  app(buf, cap, &o,
      "# HELP httpd_requests_total HTTP requests served, by status code.\n"
      "# TYPE httpd_requests_total counter\n");
  for (size_t i = 0; i < N_CODES; i++)
  {
    app(buf, cap, &o, "httpd_requests_total{code=\"%d\"} %llu\n", codes[i],
        by_code[i]);
  }
  app(buf, cap, &o, "httpd_requests_total{code=\"other\"} %llu\n",
      by_code[N_CODES]);

  app(buf, cap, &o,
      "# HELP httpd_request_duration_seconds Time from first request byte to "
      "response fully written.\n"
      "# TYPE httpd_request_duration_seconds histogram\n");
  unsigned long long cum = 0;
  for (size_t b = 0; b < N_BOUNDS; b++)
  {
    cum += bucket[b];
    app(buf, cap, &o, "httpd_request_duration_seconds_bucket{le=\"%g\"} %llu\n",
        bounds[b], cum);
  }
  app(buf, cap, &o,
      "httpd_request_duration_seconds_bucket{le=\"+Inf\"} %llu\n"
      "httpd_request_duration_seconds_sum %.6f\n"
      "httpd_request_duration_seconds_count %llu\n",
      req_count, req_sum, req_count);

  app(buf, cap, &o,
      "# HELP httpd_connections_active Currently open client connections.\n"
      "# TYPE httpd_connections_active gauge\n"
      "httpd_connections_active %ld\n"
      "# HELP httpd_connections_total Client connections accepted.\n"
      "# TYPE httpd_connections_total counter\n"
      "httpd_connections_total %llu\n"
      "# HELP httpd_uptime_seconds Seconds since the server started.\n"
      "# TYPE httpd_uptime_seconds gauge\n"
      "httpd_uptime_seconds %ld\n"
      "# HELP httpd_build_info Build information; value is always 1.\n"
      "# TYPE httpd_build_info gauge\n"
      "httpd_build_info{version=\"%s\"} 1\n",
      conns_active, conns_total, (long)(time(NULL) - started), version);

  *len = o < cap ? o : cap - 1;
  return buf;
}
