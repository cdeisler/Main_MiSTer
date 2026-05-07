#!/bin/bash
set -eo pipefail

cd /mnt/d/Documents/GitHub/MisterFPGA/Main_MiSTer
source ./setup_default_toolchain.sh

DFLAGS=(
  -I./
  -I./bin
  -I./lib/libco
  -I./lib/miniz
  -I./lib/md5
  -I./lib/lzma
  -I./lib/zstd/lib
  -I./lib/libchdr/include
  -I./lib/bluetooth
  -I./lib/serial_server/library
  -I./lib/httplib
  -D_7ZIP_ST
  '-DPACKAGE_VERSION="1.3.3"'
  -DHAVE_LROUND
  -DHAVE_STDINT_H
  -DHAVE_STDLIB_H
  -DHAVE_SYS_PARAM_H
  -DENABLE_64_BIT_WORDS=0
  -D_FILE_OFFSET_BITS=64
  -D_LARGEFILE64_SOURCE
  "-DVDATE=\"$(date +%y%m%d)\""
)

CFLAGS=(
  "${DFLAGS[@]}"
  -Wall
  -Wextra
  -Wno-strict-aliasing
  -Wno-stringop-overflow
  -Wno-stringop-truncation
  -Wno-format-truncation
  -Wno-psabi
  -Wno-restrict
  -c
  -O3
)

LFLAGS=(
  -lc
  -lstdc++
  -lm
  -lrt
  -Wl,--allow-shlib-undefined
  -Wl,-rpath-link,/usr/arm-linux-gnueabihf/lib
  -Llib/imlib2
  -lfreetype
  -lbz2
  -lpng16
  -lz
  -lImlib2
  -ldl
  -Llib/bluetooth
  -lbluetooth
  -lpthread
)

mkdir -p bin
rm -f bin/MiSTer bin/MiSTer.elf

echo rebuilding menu.cpp.o
arm-none-linux-gnueabihf-gcc "${CFLAGS[@]}" -std=gnu++14 -Wno-class-memaccess -o bin/menu.cpp.o -c menu.cpp
echo rebuilding main.cpp.o
arm-none-linux-gnueabihf-gcc "${CFLAGS[@]}" -std=gnu++14 -Wno-class-memaccess -o bin/main.cpp.o -c main.cpp
echo rebuilding http_server.cpp.o
arm-none-linux-gnueabihf-gcc "${CFLAGS[@]}" -std=gnu++14 -Wno-class-memaccess -o bin/http_server.cpp.o -c http_server.cpp

mapfile -t OBJECTS < <(find bin -name '*.o' | sort)
echo linking MiSTer
arm-none-linux-gnueabihf-gcc -o bin/MiSTer "${OBJECTS[@]}" "${LFLAGS[@]}"
cp bin/MiSTer bin/MiSTer.elf
arm-none-linux-gnueabihf-strip bin/MiSTer

ls -l bin/main.cpp.o bin/http_server.cpp.o bin/MiSTer bin/MiSTer.elf
strings bin/MiSTer | grep '202605' | tail -n 10 || true
