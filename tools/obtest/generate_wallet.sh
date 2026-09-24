#!/bin/bash
# Generate mTLS wallet (ca.pem, cert.pem, key.pem) for the current machine.
# Usage: ./generate_wallet.sh

set -e

WALLET_DIR="./wallet"
DAYS_VALID=365

# Auto-detect local IP (prefer non-loopback)
LOCAL_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
if [ -z "$LOCAL_IP" ]; then
    LOCAL_IP="127.0.0.1"
fi

echo "Generating wallet in $WALLET_DIR (IP: $LOCAL_IP)..."
rm -rf "$WALLET_DIR"
mkdir "$WALLET_DIR"

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# CA
openssl genrsa -out "$TMPDIR/ca.key" 4096 2>/dev/null
openssl req -x509 -new -nodes \
    -key "$TMPDIR/ca.key" \
    -sha256 -days $DAYS_VALID \
    -subj "/O=OceanBase/CN=OceanBase-CA" \
    -out "$WALLET_DIR/ca.pem"

# Node key and cert (SAN includes local IP + loopback)
openssl genrsa -out "$WALLET_DIR/key.pem" 2048 2>/dev/null
openssl req -new \
    -key "$WALLET_DIR/key.pem" \
    -subj "/O=OceanBase/CN=$LOCAL_IP" \
    -out "$TMPDIR/node.csr"

echo "subjectAltName=IP:$LOCAL_IP,IP:127.0.0.1" > "$TMPDIR/san.ext"

openssl x509 -req \
    -in "$TMPDIR/node.csr" \
    -CA "$WALLET_DIR/ca.pem" \
    -CAkey "$TMPDIR/ca.key" \
    -CAcreateserial \
    -extfile "$TMPDIR/san.ext" \
    -days $DAYS_VALID -sha256 \
    -out "$WALLET_DIR/cert.pem" 2>/dev/null

chmod 600 "$WALLET_DIR/key.pem"
chmod 644 "$WALLET_DIR/ca.pem" "$WALLET_DIR/cert.pem"

echo "Done:"
echo "  $WALLET_DIR/ca.pem"
echo "  $WALLET_DIR/cert.pem  (SAN: IP:$LOCAL_IP, IP:127.0.0.1)"
echo "  $WALLET_DIR/key.pem"
