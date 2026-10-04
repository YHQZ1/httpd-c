#include <string.h>

#include "http_parser.h"

int sv_eq(str_view v, const char *s)
{
  size_t n = strlen(s);
  return v.n == n && memcmp(v.p, s, n) == 0;
}

static char lower(char c)
{
  return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

int sv_ieq(str_view v, const char *s)
{
  if (v.n != strlen(s))
  {
    return 0;
  }
  for (size_t i = 0; i < v.n; i++)
  {
    if (lower(v.p[i]) != lower(s[i]))
    {
      return 0;
    }
  }
  return 1;
}

static int is_tchar(unsigned char c)
{
  if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
  {
    return 1;
  }
  return c != 0 && strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

static int is_ctl(unsigned char c)
{
  return (c < 0x20 && c != '\t') || c == 0x7f;
}

static int has_ctl(const char *p, const char *end)
{
  for (; p < end; p++)
  {
    if (is_ctl((unsigned char)*p))
    {
      return 1;
    }
  }
  return 0;
}

// Index of the "\r\n\r\n" that ends the header block, or (size_t)-1.
static size_t find_head_end(const char *buf, size_t len)
{
  for (size_t i = 0; i + 3 < len; i++)
  {
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' &&
        buf[i + 3] == '\n')
    {
      return i;
    }
  }
  return (size_t)-1;
}

static const char *find_crlf(const char *p, const char *end)
{
  for (; p + 1 < end; p++)
  {
    if (p[0] == '\r' && p[1] == '\n')
    {
      return p;
    }
  }
  return NULL;
}

static int hex_val(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

// 0 ok, -1 malformed escape or NUL, -2 too long.
static int decode_path(const char *s, size_t n, char *out, size_t cap)
{
  size_t o = 0;
  for (size_t i = 0; i < n; i++)
  {
    unsigned char c = (unsigned char)s[i];
    if (c == '%')
    {
      if (i + 2 >= n)
        return -1;
      int hi = hex_val(s[i + 1]);
      int lo = hex_val(s[i + 2]);
      if (hi < 0 || lo < 0)
        return -1;
      c = (unsigned char)(hi * 16 + lo);
      if (c == 0)
        return -1;
      i += 2;
    }
    if (o + 1 >= cap)
      return -2;
    out[o++] = (char)c;
  }
  out[o] = '\0';
  return 0;
}

static str_view trim(const char *p, const char *end)
{
  while (p < end && (*p == ' ' || *p == '\t'))
    p++;
  while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
    end--;
  str_view v = {p, (size_t)(end - p)};
  return v;
}

// Parses a Content-Length value. Returns -1 if it isn't a plain number.
static long long parse_length(str_view v)
{
  if (v.n == 0 || v.n > 12)
    return -1;
  long long x = 0;
  for (size_t i = 0; i < v.n; i++)
  {
    if (v.p[i] < '0' || v.p[i] > '9')
      return -1;
    x = x * 10 + (v.p[i] - '0');
  }
  return x;
}

// Scans a comma-separated Connection header for close / keep-alive.
static void scan_connection(str_view v, int *close_tok, int *keepalive_tok)
{
  const char *p = v.p;
  const char *end = v.p + v.n;
  while (p <= end)
  {
    const char *comma = memchr(p, ',', (size_t)(end - p));
    const char *tok_end = comma ? comma : end;
    str_view t = trim(p, tok_end);
    if (sv_ieq(t, "close"))
      *close_tok = 1;
    else if (sv_ieq(t, "keep-alive"))
      *keepalive_tok = 1;
    if (!comma)
      break;
    p = comma + 1;
  }
}

http_parse_status http_parse_request(const char *buf, size_t len,
                                     http_request *req, size_t *consumed)
{
  size_t end = find_head_end(buf, len);
  if (end == (size_t)-1)
  {
    return len >= HTTP_MAX_HEADER_BYTES ? HTTP_PARSE_TOO_LARGE
                                        : HTTP_PARSE_INCOMPLETE;
  }
  if (end + 4 > HTTP_MAX_HEADER_BYTES)
  {
    return HTTP_PARSE_TOO_LARGE;
  }

  memset(req, 0, sizeof(*req));
  const char *block_end = buf + end + 2; // start of the final blank line

  // --- request line: METHOD SP target SP HTTP/1.x
  const char *line_end = find_crlf(buf, block_end);
  if (!line_end || has_ctl(buf, line_end))
    return HTTP_PARSE_BAD_REQUEST;

  const char *sp1 = memchr(buf, ' ', (size_t)(line_end - buf));
  if (!sp1 || sp1 == buf)
    return HTTP_PARSE_BAD_REQUEST;
  const char *sp2 = memchr(sp1 + 1, ' ', (size_t)(line_end - sp1 - 1));
  if (!sp2 || sp2 == sp1 + 1)
    return HTTP_PARSE_BAD_REQUEST;

  req->method.p = buf;
  req->method.n = (size_t)(sp1 - buf);
  for (size_t i = 0; i < req->method.n; i++)
  {
    if (!is_tchar((unsigned char)buf[i]))
      return HTTP_PARSE_BAD_REQUEST;
  }

  req->target.p = sp1 + 1;
  req->target.n = (size_t)(sp2 - sp1 - 1);
  if (req->target.p[0] != '/')
    return HTTP_PARSE_BAD_REQUEST;
  if (req->target.n > 2048)
    return HTTP_PARSE_URI_TOO_LONG;

  str_view version = {sp2 + 1, (size_t)(line_end - sp2 - 1)};
  if (sv_eq(version, "HTTP/1.1"))
    req->version_minor = 1;
  else if (sv_eq(version, "HTTP/1.0"))
    req->version_minor = 0;
  else if (version.n == 8 && memcmp(version.p, "HTTP/", 5) == 0)
    return HTTP_PARSE_BAD_VERSION;
  else
    return HTTP_PARSE_BAD_REQUEST;

  const char *q = memchr(req->target.p, '?', req->target.n);
  size_t path_len = q ? (size_t)(q - req->target.p) : req->target.n;
  if (q)
  {
    req->query.p = q + 1;
    req->query.n = (size_t)(req->target.p + req->target.n - (q + 1));
  }
  int d = decode_path(req->target.p, path_len, req->path, sizeof(req->path));
  if (d == -1)
    return HTTP_PARSE_BAD_REQUEST;
  if (d == -2)
    return HTTP_PARSE_URI_TOO_LONG;

  // --- headers
  const char *p = line_end + 2;
  while (p < block_end)
  {
    line_end = find_crlf(p, block_end + 2);
    if (!line_end)
      return HTTP_PARSE_BAD_REQUEST;
    if (*p == ' ' || *p == '\t' || has_ctl(p, line_end)) // obs-fold / control chars
      return HTTP_PARSE_BAD_REQUEST;

    const char *colon = memchr(p, ':', (size_t)(line_end - p));
    if (!colon || colon == p)
      return HTTP_PARSE_BAD_REQUEST;
    for (const char *c = p; c < colon; c++)
    {
      if (!is_tchar((unsigned char)*c)) // also rejects "Name : value"
        return HTTP_PARSE_BAD_REQUEST;
    }
    if (req->n_headers >= HTTP_MAX_HEADERS)
      return HTTP_PARSE_TOO_LARGE;

    http_header *h = &req->headers[req->n_headers++];
    h->name.p = p;
    h->name.n = (size_t)(colon - p);
    h->value = trim(colon + 1, line_end);

    p = line_end + 2;
  }

  // --- semantics we care about
  int saw_host = 0, close_tok = 0, keepalive_tok = 0;
  long long content_length = -1;
  for (size_t i = 0; i < req->n_headers; i++)
  {
    const http_header *h = &req->headers[i];
    if (sv_ieq(h->name, "host"))
    {
      if (++saw_host > 1)
        return HTTP_PARSE_BAD_REQUEST;
    }
    else if (sv_ieq(h->name, "content-length"))
    {
      long long v = parse_length(h->value);
      if (v < 0 || (content_length >= 0 && content_length != v))
        return HTTP_PARSE_BAD_REQUEST;
      content_length = v;
    }
    else if (sv_ieq(h->name, "transfer-encoding"))
    {
      req->transfer_encoding = 1;
    }
    else if (sv_ieq(h->name, "connection"))
    {
      scan_connection(h->value, &close_tok, &keepalive_tok);
    }
  }
  if (req->version_minor == 1 && !saw_host)
    return HTTP_PARSE_BAD_REQUEST;

  req->keep_alive = req->version_minor == 1 ? 1 : keepalive_tok;
  if (close_tok)
    req->keep_alive = 0;
  req->has_body = content_length > 0;

  *consumed = end + 4;
  return HTTP_PARSE_OK;
}
