# TLS 1.3 client in Oh

A from-scratch TLS 1.3 client (x25519 + AES-128-GCM + SHA-256), no libc. Completes a
real handshake with `openssl s_server -tls1_3`, **verifies the server's
CertificateVerify signature**, and exchanges encrypted application data.

## Crypto (all from scratch, vector-verified — see tests/37-49)
SHA-256/512, HMAC, HKDF (+ TLS Expand-Label), AES-128/256, AES-GCM, X25519, Ed25519.

## Flow
ClientHello -> ServerHello (x25519 key_share) -> ECDHE shared secret -> key schedule
(handshake + traffic secrets) -> decrypt server flight (EncryptedExtensions/Certificate/
CertificateVerify/Finished) -> **extract the leaf's Ed25519 key from the cert and verify
the CertificateVerify signature over the transcript** -> verify server Finished -> send
client Finished -> app keys -> send HTTP request -> decrypt HTTP response.

## Verified
Against real `openssl s_server` (Ed25519 cert), on x86-64 AND aarch64 (qemu):
`certificate verified (ed25519)` / `server Finished verified` / `HTTP/1.0 200`.
`./test.sh` (+ `ARCH=arm64`).

## Security status (honest)
- **CertificateVerify IS verified** — this cryptographically proves the peer holds the
  private key matching the certificate it presented (Ed25519 signature over the
  handshake transcript). Forged signatures are rejected (see tests/49_ed25519).
- **Trust anchoring is NOT done.** The client does not validate that the presented
  certificate is issued by a trusted CA (no chain building / root store), nor does it
  pin a key or check hostname/expiry. So an active MITM presenting its *own* valid cert
  would currently be accepted. Closing this needs either certificate pinning (compare
  the cert's key to a known-good one) or X.509 chain validation to a trust store.
- **Ed25519 server certs only.** Real-world CAs mostly use RSA / ECDSA-P256; those
  signature schemes (and X.509/ASN.1 chain parsing) are not implemented.
- Cipher suite fixed to TLS_AES_128_GCM_SHA256; group fixed to x25519; no session
  resumption / 0-RTT; no record-size/fragmentation edge handling beyond the basics.
