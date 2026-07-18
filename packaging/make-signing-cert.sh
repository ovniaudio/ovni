#!/bin/bash
# Create a stable, self-signed CODE SIGNING identity in the login keychain.
# This replaces ad-hoc signing so macOS TCC (Screen Recording) keeps the grant
# across rebuilds — the app's designated requirement becomes cert-based (stable),
# not cdhash-based (changes every build).
set -euo pipefail

CERT_NAME="SUPERNOVA Local"
KEYCHAIN="$HOME/Library/Keychains/login.keychain-db"

echo "=== openssl ==="
openssl version

# Already present? (idempotent)
if security find-certificate -c "$CERT_NAME" "$KEYCHAIN" >/dev/null 2>&1; then
  echo "Identity '$CERT_NAME' already exists in login keychain — skipping creation."
  exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
CONF="$WORK/csr.cnf"
cat > "$CONF" <<'EOF'
[req]
distinguished_name = dn
x509_extensions = v3
prompt = no
[dn]
CN = SUPERNOVA Local
[v3]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = critical,codeSigning
EOF

echo "=== generating key + self-signed cert (10y, codeSigning EKU) ==="
openssl req -x509 -newkey rsa:2048 -sha256 -days 3650 -nodes \
  -keyout "$WORK/key.pem" -out "$WORK/cert.pem" -config "$CONF"

echo "=== bundling into pkcs12 ==="
openssl pkcs12 -export -legacy \
  -inkey "$WORK/key.pem" -in "$WORK/cert.pem" \
  -out "$WORK/id.p12" -passout pass:supernova -name "$CERT_NAME"

echo "=== importing into login keychain (-A: codesign can use the key without prompts) ==="
security import "$WORK/id.p12" -k "$KEYCHAIN" -P supernova \
  -T /usr/bin/codesign -T /usr/bin/security -A

echo "=== present now? ==="
security find-certificate -c "$CERT_NAME" "$KEYCHAIN" >/dev/null && echo "OK: cert in keychain"
echo "--- find-identity (all, incl. untrusted policy) ---"
security find-identity -p codesigning "$KEYCHAIN" | grep -i "$CERT_NAME" || \
  security find-identity "$KEYCHAIN" | grep -i "$CERT_NAME" || echo "(not listed by find-identity)"
