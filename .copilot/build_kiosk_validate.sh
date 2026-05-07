#!/bin/bash
set -eo pipefail
cd /mnt/d/Documents/GitHub/MisterFPGA/Main_MiSTer
source ./setup_default_toolchain.sh
rm -f bin/menu.cpp.o bin/main.cpp.o bin/MiSTer bin/MiSTer.elf
for pass in $(seq 1 20); do
  echo "build pass $pass"
  make -j8 bin/MiSTer
  if [ -f bin/MiSTer ]; then
    exit 0
  fi
done
echo "build did not produce bin/MiSTer after repeated make passes" >&2
exit 1
