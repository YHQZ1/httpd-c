#ifndef STATIC_FILES_H
#define STATIC_FILES_H

#include "http_parser.h"
#include "response.h"

// Resolves root to a canonical directory. Returns -1 if it isn't one.
int static_init(const char *root);

// Serves req->path from the root (a trailing '/' maps to index.html).
// Always fills resp: the file, or a 404/500 error.
void static_serve(const http_request *req, http_response *resp);

#endif // STATIC_FILES_H
