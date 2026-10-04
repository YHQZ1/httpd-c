#ifndef ROUTER_H
#define ROUTER_H

#include "http_parser.h"
#include "response.h"

// version is reported by /version; must outlive the process (not copied).
void router_init(const char *version);

// Fills resp for a parsed request: built-in routes, else static files.
void router_handle(const http_request *req, http_response *resp);

#endif // ROUTER_H
