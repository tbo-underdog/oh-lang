# PostgreSQL-over-TLS client (composition of std/tls.oh + std/pg.oh)

`pgtls.oh` layers the Postgres v3 protocol over the TLS 1.3 channel:
`connect` -> **SSLRequest** -> (server replies `S`) -> `tls_handshake` (x25519 +
Ed25519 CertificateVerify + optional pinning) -> Postgres startup + query carried over
`tls_send`/`tls_recv`.

## Status — protocol-correct, blocked on P-256 (HONEST)
Verified against a real TLS-enabled PostgreSQL 16: the client correctly sends the
SSLRequest, receives `S`, sends a TLS ClientHello, and the server responds — i.e. the
SSLRequest + TLS-over-socket + PG-over-TLS wiring is right.

It does **not** complete the handshake against default Postgres, for one concrete
reason: **Postgres mandates a classic ECDH curve (default `prime256v1` / P-256)** and
refuses `ssl_ecdh_curve='X25519'` (the server won't even start). This client offers
only x25519, so there is no common group -> the server sends a `handshake_failure`
alert. This is the same wall as talking to the open internet.

### The unlock: NIST P-256 ECDHE
Implementing P-256 ECDHE in `std/tls.oh` (and ECDSA-P256 / RSA for cert verification)
is the prerequisite for interoperating with Postgres and real-world TLS servers. That
is a from-scratch P-256 field + curve implementation (comparable in size to X25519),
the clearly-scoped next step. With it, this client completes unchanged.

The TLS layer itself (handshake, records, cert verify, pinning, app I/O) and the
Postgres layer are both independently verified — only the ECDHE group is the gap.
