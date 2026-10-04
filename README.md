# httpd-c

A tiny HTTP/1.1 server in C, built from raw sockets: no libraries, an event loop (epoll on Linux, `poll()` elsewhere), and a hand-rolled incremental parser. Learning the layer frameworks hide.

## Why

Backend work usually sits on top of Gin, Fastify, or FastAPI. This strips that away: sockets, request parsing, connection handling, all done by hand to see what the frameworks are actually doing underneath.

## Status

Complete. Every roadmap item is implemented and covered by `make test`.

## Build

```bash
make        # builds ./httpd-c
make run    # builds and runs on port 8080
make test   # builds and runs tests/smoke.sh (needs bash + curl)
make clean  # removes build artifacts
```

## Usage

```bash
./httpd-c [port]          # port: argv > $PORT > 8080
curl localhost:8080
```

| Env var       | Default   | Meaning                                  |
| ------------- | --------- | ---------------------------------------- |
| `PORT`        | `8080`    | Listen port (overridden by `argv[1]`)    |
| `WWW_ROOT`    | `www`     | Directory served as static files         |
| `APP_VERSION` | `1.0.0`   | Reported by `/version` and `/metrics`    |

### Endpoints

| Path       | Description                                                          |
| ---------- | -------------------------------------------------------------------- |
| `/health`  | `{"status":"ok"}`, for liveness/readiness probes                     |
| `/version` | `{"version":"<APP_VERSION>"}`                                        |
| `/metrics` | Prometheus text format (see below)                                   |
| anything else | Static file from `$WWW_ROOT` (`/` maps to `index.html`)           |

Only `GET` and `HEAD` are supported (others get `405`).

### Metrics

| Metric                                  | Type      | Notes                                      |
| --------------------------------------- | --------- | ------------------------------------------ |
| `httpd_requests_total{code="…"}`        | counter   | Error rate = `5xx` / total                 |
| `httpd_request_duration_seconds`        | histogram | First request byte to response fully sent  |
| `httpd_connections_active`              | gauge     |                                            |
| `httpd_connections_total`               | counter   |                                            |
| `httpd_uptime_seconds`                  | gauge     |                                            |
| `httpd_build_info{version="…"}`         | gauge     | Always 1                                   |

### Logging and shutdown

- One access-log line per request on stdout: `ip:port "request line" status bytes duration`.
- `SIGTERM`/`SIGINT` start a graceful shutdown: stop accepting, close idle connections, let in-flight requests finish (with `Connection: close`), then exit. Gives up after 5 s.

## Layout

```
src/main.c          config, signals, startup
src/server.c        listener + event loop, accept, idle sweep, graceful drain
src/event.c         epoll / poll() backend behind one tiny API
src/conn.c          per-connection state machine (read, parse, respond, write)
src/http_parser.c   incremental request-head parser (zero-copy slices)
src/router.c        /health, /version, /metrics, else static files
src/static_files.c  file serving with traversal protection
src/response.c      response building / serialisation
src/metrics.c       counters + Prometheus rendering
tests/smoke.sh      end-to-end checks over real sockets
```

## Limits (by design)

This is a learning project, not a production server.

- No request bodies: a request with `Content-Length > 0` is answered and the connection closed; `Transfer-Encoding` gets `501`.
- Static files are read fully into memory (8 MiB cap); no range requests, caching headers, or compression.
- IPv4 only, no TLS, single-threaded.
- Request head limited to 16 KiB and 64 headers.

## Roadmap

- [x] Raw socket setup, blocking accept loop, hardcoded response
- [x] Incremental HTTP request line + header parsing
- [x] Path-based routing
- [x] Static file serving
- [x] Concurrent connections via epoll
- [x] Keep-alive support
