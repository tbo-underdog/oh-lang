# Calculator API — in Oh

A multi-route HTTP API: raw syscalls, no libc, no framework. Serves an HTML form
(submit button / Enter) and computes on the server.

## Routes
| route | does |
|---|---|
| `GET /` | HTML form with **add** / **divide** submit buttons |
| `GET /status` | health check → `ok` |
| `GET /add?a=&b=` | returns `a + b` (handles negatives) |
| `GET /divide?a=&b=` | returns `a / b`, or `error: division by zero` |

## Run
```sh
./compiler/oh std/core.oh std/str.oh projects/14_calc_api/api.oh -o api
./api                       # listens on :8091
# then in a browser: http://localhost:8091/   (type two numbers, hit a button)
# or:
curl 'localhost:8091/add?a=3&b=4'        # -> 7
curl 'localhost:8091/divide?a=7&b=0'     # -> error: division by zero
```

Freestanding (zero-dependency static binary) and ARM64 builds work the same way —
link `tooling/start_<arch>.s tooling/rt.ll` (see `test.sh`).

## Test
```sh
./projects/14_calc_api/test.sh             # native
ARCH=arm64 ./projects/14_calc_api/test.sh  # freestanding aarch64 under qemu
```

## How it works
- `main`: socket → setsockopt(REUSEADDR) → bind(:8091) → listen → accept loop.
- `handle`: reads the request, routes on the request line via `starts(req, "GET /…")`.
- `getparam(req,"a=")`: `sfind` locates the query key, `atoi` parses the value
  (negatives supported) from `&req[i+2]`.
- Responses use `Connection: close` with no `Content-Length` — the body ends at
  connection close, so there's nothing to pre-compute.
