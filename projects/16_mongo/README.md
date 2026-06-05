# MongoDB client in Oh

Talks to a real MongoDB server over the OP_MSG wire protocol with BSON — raw sockets,
no libc. Inserts a document and finds it back.

## Layers
- **`std/bson.oh`** — BSON encode/decode (length-backpatched docs, typed elements,
  field lookup with nesting). Pure byte functions, **unit-tested** in `tests/35_bson.oh`.
- **`mongo.oh`** — the live client: OP_MSG framing + `insert` + `find`, parsing the
  nested `cursor.firstBatch[0]` reply.

## Run
```sh
docker run -p27017:27017 mongo:7        # or a local mongod
./compiler/oh std/core.oh std/net.oh std/bson.oh projects/16_mongo/mongo.oh -o mongo
./mongo                                  # -> alice
```

## Test
```sh
./projects/16_mongo/test.sh             # native
ARCH=arm64 ./projects/16_mongo/test.sh  # freestanding aarch64 under qemu
```
Uses an existing MongoDB on `:27017`, else `docker run mongo:7`; skips if neither.

## Verification status
- **Verified against real MongoDB 7, on x86-64 AND ARM64 (qemu):** insert `{name:"alice"}`
  then `find {name:"alice"}` returns `alice`, and `mongosh` confirms the document is
  actually stored.
- BSON encode/decode is additionally unit-tested against a known round-trip.

## Limits
- OP_MSG with a single body section; reads assume the reply fits one `recv` (fine for
  small result sets; production loops to read the full framed message).
- BSON types covered: int32, string, embedded doc, array, plus skip-over for double /
  int64 / ObjectId in replies. No AUTH/TLS, no cursor continuation (getMore), no
  aggregation. SET-of-fields and basic find are proven.
