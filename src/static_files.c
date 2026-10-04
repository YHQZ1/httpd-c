#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "httpd.h"
#include "static_files.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char root_real[PATH_MAX];
static size_t root_len;

static const struct
{
  const char *ext;
  const char *type;
} mime_types[] = {
    {".html", "text/html; charset=utf-8"},
    {".htm", "text/html; charset=utf-8"},
    {".css", "text/css; charset=utf-8"},
    {".js", "text/javascript; charset=utf-8"},
    {".json", "application/json"},
    {".txt", "text/plain; charset=utf-8"},
    {".svg", "image/svg+xml"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".ico", "image/x-icon"},
};

static const char *mime_for(const char *path)
{
  const char *dot = strrchr(path, '.');
  if (dot)
  {
    for (size_t i = 0; i < sizeof(mime_types) / sizeof(mime_types[0]); i++)
    {
      if (strcmp(dot, mime_types[i].ext) == 0)
      {
        return mime_types[i].type;
      }
    }
  }
  return "application/octet-stream";
}

int static_init(const char *root)
{
  struct stat st;
  if (!realpath(root, root_real) || stat(root_real, &st) < 0 ||
      !S_ISDIR(st.st_mode))
  {
    return -1;
  }
  root_len = strlen(root_real);
  return 0;
}

static int has_dotdot_segment(const char *path)
{
  for (const char *p = path; *p;)
  {
    const char *seg = p;
    while (*p && *p != '/')
      p++;
    if (p - seg == 2 && seg[0] == '.' && seg[1] == '.')
      return 1;
    if (*p == '/')
      p++;
  }
  return 0;
}

static void not_found(http_response *resp) { response_error(resp, 404); }

void static_serve(const http_request *req, http_response *resp)
{
  if (has_dotdot_segment(req->path))
  {
    not_found(resp);
    return;
  }

  char candidate[PATH_MAX];
  size_t plen = strlen(req->path);
  int n = snprintf(candidate, sizeof(candidate), "%s%s%s", root_real,
                   req->path, req->path[plen - 1] == '/' ? "index.html" : "");
  if (n < 0 || (size_t)n >= sizeof(candidate))
  {
    not_found(resp);
    return;
  }

  // Canonicalise, then make sure symlinks didn't lead us out of the root.
  char real[PATH_MAX];
  if (!realpath(candidate, real) || strncmp(real, root_real, root_len) != 0 ||
      (real[root_len] != '/' && real[root_len] != '\0'))
  {
    not_found(resp);
    return;
  }

  int fd = open(real, O_RDONLY);
  if (fd < 0)
  {
    not_found(resp);
    return;
  }
  struct stat st;
  if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode))
  {
    close(fd);
    not_found(resp);
    return;
  }
  if (st.st_size > MAX_STATIC_FILE)
  {
    close(fd);
    response_error(resp, 500);
    return;
  }

  const char *type = mime_for(real);
  size_t size = (size_t)st.st_size;

  if (req->method.n == 4 && memcmp(req->method.p, "HEAD", 4) == 0)
  {
    // Headers only: report the length without reading the file.
    close(fd);
    resp->status = 200;
    resp->content_type = type;
    resp->body = NULL;
    resp->body_len = size;
    return;
  }

  char *body = malloc(size ? size : 1);
  if (!body)
  {
    close(fd);
    response_error(resp, 500);
    return;
  }
  size_t got = 0;
  while (got < size)
  {
    ssize_t r = read(fd, body + got, size - got);
    if (r < 0 && errno == EINTR)
      continue;
    if (r <= 0)
      break;
    got += (size_t)r;
  }
  close(fd);
  if (got != size)
  {
    free(body);
    response_error(resp, 500);
    return;
  }
  response_set_owned(resp, 200, type, body, size);
}
