#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stddef.h>

#include "httpd.h"

#define HTTP_MAX_HEADERS 64
#define HTTP_MAX_PATH 1024

// Non-owning slice into the connection's read buffer.
typedef struct
{
  const char *p;
  size_t n;
} str_view;

typedef struct
{
  str_view name;
  str_view value;
} http_header;

typedef struct
{
  str_view method;
  str_view target;
  str_view query;     // after '?', not decoded
  char path[HTTP_MAX_PATH]; // percent-decoded path
  int version_minor;  // HTTP/1.<n>
  http_header headers[HTTP_MAX_HEADERS];
  size_t n_headers;
  int keep_alive;
  int has_body;           // Content-Length > 0 (we don't read bodies)
  int transfer_encoding;  // Transfer-Encoding present (unsupported)
} http_request;

typedef enum
{
  HTTP_PARSE_OK = 0,
  HTTP_PARSE_INCOMPLETE,
  HTTP_PARSE_BAD_REQUEST,   // 400
  HTTP_PARSE_BAD_VERSION,   // 505
  HTTP_PARSE_URI_TOO_LONG,  // 414
  HTTP_PARSE_TOO_LARGE      // 431
} http_parse_status;

// Parses one request head from buf[0..len). The buffer may hold a partial
// request (INCOMPLETE: call again after more bytes arrive) or several
// pipelined ones (OK: *consumed says how many bytes the first one used).
// req holds pointers into buf, so it is only valid until buf changes.
http_parse_status http_parse_request(const char *buf, size_t len,
                                     http_request *req, size_t *consumed);

int sv_eq(str_view v, const char *s);
int sv_ieq(str_view v, const char *s); // ASCII case-insensitive

#endif // HTTP_PARSER_H
