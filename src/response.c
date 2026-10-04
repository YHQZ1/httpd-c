#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "httpd.h"
#include "response.h"

const char *http_status_text(int status)
{
  switch (status)
  {
  case 200: return "OK";
  case 400: return "Bad Request";
  case 404: return "Not Found";
  case 405: return "Method Not Allowed";
  case 408: return "Request Timeout";
  case 414: return "URI Too Long";
  case 431: return "Request Header Fields Too Large";
  case 500: return "Internal Server Error";
  case 501: return "Not Implemented";
  case 505: return "HTTP Version Not Supported";
  default:  return "Unknown";
  }
}

void response_init(http_response *r)
{
  memset(r, 0, sizeof(*r));
  r->status = 200;
  r->content_type = "text/plain; charset=utf-8";
}

void response_free(http_response *r)
{
  free(r->owned_body);
  r->owned_body = NULL;
  r->body = NULL;
}

void response_set_static(http_response *r, int status, const char *type,
                         const char *text)
{
  r->status = status;
  r->content_type = type;
  r->body = text;
  r->body_len = strlen(text);
}

void response_set_owned(http_response *r, int status, const char *type,
                        char *body, size_t len)
{
  r->status = status;
  r->content_type = type;
  r->owned_body = body;
  r->body = body;
  r->body_len = len;
}

void response_error(http_response *r, int status)
{
  r->status = status;
  r->content_type = "text/plain; charset=utf-8";
  int n = snprintf(r->small, sizeof(r->small), "%d %s\n", status,
                   http_status_text(status));
  r->body = r->small;
  r->body_len = n > 0 ? (size_t)n : 0;
}

char *response_serialize(const http_response *r, int keep_alive, int head_only,
                         size_t *out_len)
{
  char date[64];
  time_t now = time(NULL);
  struct tm tm;
  gmtime_r(&now, &tm);
  strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", &tm);

  char head[512];
  int hl = snprintf(head, sizeof(head),
                    "HTTP/1.1 %d %s\r\n"
                    "Date: %s\r\n"
                    "Server: httpd-c\r\n"
                    "Content-Type: %s\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: %s\r\n",
                    r->status, http_status_text(r->status), date,
                    r->content_type, r->body_len,
                    keep_alive ? "keep-alive" : "close");
  if (hl < 0 || (size_t)hl >= sizeof(head))
  {
    return NULL;
  }
  if (keep_alive)
  {
    hl += snprintf(head + hl, sizeof(head) - (size_t)hl,
                   "Keep-Alive: timeout=%d\r\n", IDLE_TIMEOUT_SEC);
  }
  if (r->allow)
  {
    hl += snprintf(head + hl, sizeof(head) - (size_t)hl, "Allow: %s\r\n",
                   r->allow);
  }
  if ((size_t)hl + 2 > sizeof(head))
  {
    return NULL;
  }
  head[hl++] = '\r';
  head[hl++] = '\n';

  size_t body_send = head_only ? 0 : r->body_len;
  char *out = malloc((size_t)hl + body_send);
  if (!out)
  {
    return NULL;
  }
  memcpy(out, head, (size_t)hl);
  if (body_send > 0)
  {
    memcpy(out + hl, r->body, body_send);
  }
  *out_len = (size_t)hl + body_send;
  return out;
}
