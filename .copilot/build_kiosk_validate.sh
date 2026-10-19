#!/bin/bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
source ./setup_default_toolchain.sh
make "$@" BUILDDIR=bin HTTP_SERVER=0 KIOSK_MODE=1 bin/MiSTer
if arm-none-linux-gnueabihf-nm -C bin/MiSTer.elf | grep 'http_server_' > /dev/null; then
  echo "kiosk binary unexpectedly contains HTTP server symbols" >&2
  exit 1
fi
arm-none-linux-gnueabihf-readelf -h bin/MiSTer
