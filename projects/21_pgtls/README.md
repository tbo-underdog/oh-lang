# Secure PostgreSQL-over-TLS in Oh

An Oh program runs SQL against a real PostgreSQL database over an encrypted,
server-authenticated TLS 1.3 channel — composing std/tls.oh + std/pg.oh + std/p256.oh.

## Flow
`connect` -> **SSLRequest** -> server `S` -> **`tls_handshake`** (P-256 *or* x25519
ECDHE, AES-128-GCM, **Ed25519 CertificateVerify**, optional pinning) -> Postgres
startup + `SELECT` carried over `tls_send`/`tls_recv`.

## Verified
Against real PostgreSQL 16 with `ssl=on` (default `ssl_ecdh_curve=prime256v1`, Ed25519
server cert), on x86-64 AND aarch64 (qemu):
```
TLS established to postgres (cert verified)
postgres authenticated
query over TLS -> pgtls-ok
```
`./test.sh` (+ `ARCH=arm64`) stands up the TLS Postgres and runs it.

## What made it work
Postgres mandates a classic ECDH curve (P-256). The TLS client now offers **both**
P-256 and x25519 groups (std/p256.oh provides P-256 ECDHE) and uses whichever the
server selects, so it interoperates with Postgres and other P-256-only servers.

## Security
Server is authenticated (Ed25519 CertificateVerify; forged sigs rejected). Trust
anchoring = pinning (load_pin) for known servers. Remaining for arbitrary CAs:
ECDSA-P256/RSA cert signatures + X.509 chain validation. Auth here is trust (Postgres
SCRAM-over-TLS is a further compose of std/scram with this channel).
