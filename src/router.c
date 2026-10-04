#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "metrics.h"
#include "router.h"
#include "static_files.h"

typedef void (*route_fn)(const http_request *, http_response *);

static const char *app_version = "unknown";

static void h_health(const http_request *req, http_response *resp)
{
  (void)req;
  response_set_static(resp, 200, "application/json", "{\"status\":\"ok\"}\n");
}

static void h_version(const http_request *req, http_response *resp)
{
  (void)req;
  size_t cap = strlen(app_version) + 32;
  char *body = malloc(cap);
  if (!body)
  {
    response_error(resp, 500);
    return;
  }
  int n = snprintf(body, cap, "{\"version\":\"%s\"}\n", app_version);
  response_set_owned(resp, 200, "application/json", body, (size_t)n);
}

static void h_metrics(const http_request *req, http_response *resp)
{
  (void)req;
  size_t len = 0;
  char *body = metrics_render(&len);
  if (!body)
  {
    response_error(resp, 500);
    return;
  }
  response_set_owned(resp, 200, "text/plain; version=0.0.4; charset=utf-8",
                     body, len);
}

static const struct
{
  const char *path;
  route_fn fn;
} routes[] = {
    {"/health", h_health},
    {"/version", h_version},
    {"/metrics", h_metrics},
};

void router_init(const char *version) { app_version = version; }

void router_handle(const http_request *req, http_response *resp)
{
  if (req->transfer_encoding)
  {
    response_error(resp, 501); // chunked request bodies aren't supported
    return;
  }
  if (!sv_eq(req->method, "GET") && !sv_eq(req->method, "HEAD"))
  {
    response_error(resp, 405);
    resp->allow = "GET, HEAD";
    return;
  }

  for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
  {
    if (strcmp(req->path, routes[i].path) == 0)
    {
      routes[i].fn(req, resp);
      return;
    }
  }
  static_serve(req, resp);
}
