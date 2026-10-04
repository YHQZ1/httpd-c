#ifndef RESPONSE_H
#define RESPONSE_H

#include <stddef.h>

typedef struct
{
  int status;
  const char *content_type;
  const char *allow;      // optional Allow header (for 405)
  const char *body;       // may point at a literal, owned_body, or small
  size_t body_len;
  char *owned_body;       // heap body, freed by response_free
  char small[64];         // inline storage for short error bodies
} http_response;

void response_init(http_response *r);
void response_free(http_response *r);

// Literal body: not copied, must outlive the response.
void response_set_static(http_response *r, int status, const char *type,
                         const char *text);
// Takes ownership of a malloc'd body.
void response_set_owned(http_response *r, int status, const char *type,
                        char *body, size_t len);
// "<code> <reason>\n" as text/plain.
void response_error(http_response *r, int status);

const char *http_status_text(int status);

// Builds the full wire response (head + body) in one malloc'd buffer.
// For HEAD, the body is omitted but Content-Length still reflects it.
char *response_serialize(const http_response *r, int keep_alive, int head_only,
                         size_t *out_len);

#endif // RESPONSE_H
