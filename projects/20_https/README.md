# HTTPS client in Oh (uses std/tls.oh)

`https.oh` is a tiny HTTPS GET built on the reusable **`std/tls.oh`** library:
allocate the buffers + a `Tls`, set `t.fd` to a connected socket, then
`tls_handshake(t)` -> `tls_send(t,...)` / `tls_recv(t,...)`.

`std/tls.oh` is the TLS 1.3 client (x25519 + AES-128-GCM + SHA-256) factored into a
library: full handshake, Ed25519 CertificateVerify, optional pinning (`load_pin`),
and application-data send/recv over the established channel.

Verified against `openssl s_server -tls1_3 -www` on x86-64 AND aarch64 (qemu):
`handshake ok` / `HTTP/1.0 200`. `./test.sh` (+ `ARCH=arm64`).

Same security boundary as projects/19_tls (CertificateVerify + pinning; no CA-chain /
RSA / ECDSA / hostname / expiry).
