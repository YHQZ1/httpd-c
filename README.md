# httpd-c

A tiny HTTP/1.1 server in C, built from raw sockets — no libs, epoll event loop, hand-rolled parser. Learning the layer frameworks hide.

## Why

Backend work usually sits on top of Gin, Fastify, or FastAPI — this strips that away. Sockets, request parsing, connection handling, all done by hand to see what the frameworks are actually doing underneath.

## Status

v1 — blocking accept loop, one connection at a time, hardcoded response. No request parsing yet.

## Build

\```bash
make        # builds ./httpd-c
make run    # builds and runs on port 8080
make clean  # removes build artifacts
\```

## Usage

\```bash
./httpd-c [port]   # defaults to 8080
\```

Then in another terminal:

\```bash
curl localhost:8080
\```

## Roadmap

- [x] Raw socket setup, blocking accept loop, hardcoded response
- [ ] Incremental HTTP request line + header parsing
- [ ] Path-based routing
- [ ] Static file serving
- [ ] Concurrent connections via epoll
- [ ] Keep-alive support