#!/bin/bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
profile=${1:-kiosk}
if [ "$#" -gt 0 ]; then shift; fi
case "$profile" in
    kiosk) http_server=0; kiosk_mode=1 ;;
    normal) http_server=1; kiosk_mode=0 ;;
    *) echo "Usage: bash scripts/build-mister-local.sh [kiosk|normal] [make arguments]" >&2; exit 1 ;;
esac
source ./setup_default_toolchain.sh
build_dir="bin/$profile"
mkdir -p -- "$build_dir"
if [ ! -f "$build_dir/build_number.txt" ]; then
    printf '0\n' > "$build_dir/build_number.txt"
fi
make "$@" BUILDDIR="$build_dir" BUILD_FILE="$build_dir/build_number.txt" \
    HTTP_SERVER="$http_server" KIOSK_MODE="$kiosk_mode" "$build_dir/MiSTer"
grep -qx "#define MISTER_HTTP_SERVER $http_server" "$build_dir/build_features.h"
grep -qx "#define MISTER_KIOSK_MODE $kiosk_mode" "$build_dir/build_features.h"
if [ "$profile" = kiosk ]; then
    if arm-none-linux-gnueabihf-nm -C "$build_dir/MiSTer.elf" | grep 'http_server_' > /dev/null; then
        echo "kiosk binary unexpectedly contains HTTP server symbols" >&2
        exit 1
    fi
fi
arm-none-linux-gnueabihf-readelf -h "$build_dir/MiSTer"