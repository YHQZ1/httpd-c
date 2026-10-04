#ifndef METRICS_H
#define METRICS_H

#include <stddef.h>

// Process-wide counters, rendered in the Prometheus text exposition format.
// Single-threaded: no locking.

void metrics_init(const char *version);
void metrics_conn_opened(void);
void metrics_conn_closed(void);
void metrics_observe_request(int status, double seconds);

// Returns a malloc'd body (caller frees), or NULL on failure.
char *metrics_render(size_t *len);

#endif // METRICS_H
