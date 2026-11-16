# Import Apple code signing certificate for CI.
# Required env: BUILD_CERTIFICATE_BASE64, P12_PASSWORD, KEYCHAIN_PASSWORD
# When BUILD_CERTIFICATE_BASE64 is empty, skip import; libseekdb-build.sh will use ad-hoc signing.
set -e
export CERTIFICATE_PATH=${CERTIFICATE_PATH:-$RUNNER_TEMP/build_certificate.p12}
export KEYCHAIN_PATH=${KEYCHAIN_PATH:-$RUNNER_TEMP/app-signing.keychain-db}

if [[ -z "${BUILD_CERTIFICATE_BASE64:-}" ]]; then
  echo "[BUILD] No certificate configured; skipping import (ad-hoc signing will be used)."
  exit 0
fi

echo -n "$BUILD_CERTIFICATE_BASE64" | base64 --decode -o "$CERTIFICATE_PATH"

security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN_PATH"
security set-keychain-settings -lut 21600 "$KEYCHAIN_PATH"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN_PATH"

security import "$CERTIFICATE_PATH" -P "$P12_PASSWORD" -A -t cert -f pkcs12 -k "$KEYCHAIN_PATH"
security list-keychain -d user -s "$KEYCHAIN_PATH"
