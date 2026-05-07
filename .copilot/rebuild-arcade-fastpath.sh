#!/bin/bash
set -eo pipefail

cd /mnt/d/Documents/GitHub/MisterFPGA/Main_MiSTer
source ./setup_default_toolchain.sh

common_flags=(
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
  -DPACKAGE_VERSION=\"1.3.3\"
  -DHAVE_LROUND
  -DHAVE_STDINT_H
  -DHAVE_STDLIB_H
  -DHAVE_SYS_PARAM_H
  -DENABLE_64_BIT_WORDS=0
  -D_FILE_OFFSET_BITS=64
  -D_LARGEFILE64_SOURCE
  -DVDATE=\"$(date +"%y%m%d")\"
  -Wall
  -Wextra
  -Wno-strict-aliasing
  -Wno-stringop-overflow
  -Wno-stringop-truncation
  -Wno-format-truncation
  -Wno-psabi
  -Wno-restrict
  -O3
  -c
)

link_flags=(
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

mkdir -p bin/support/arcade

echo "compiling user_io.cpp.o"
arm-none-linux-gnueabihf-gcc "${common_flags[@]}" -std=gnu++14 -Wno-class-memaccess -o bin/user_io.cpp.o -c user_io.cpp

echo "compiling support/arcade/mra_loader.cpp.o"
arm-none-linux-gnueabihf-gcc "${common_flags[@]}" -std=gnu++14 -Wno-class-memaccess -o bin/support/arcade/mra_loader.cpp.o -c support/arcade/mra_loader.cpp

echo "linking MiSTer"
mapfile -t object_files < <(find bin -type f -name '*.o' | sort)
arm-none-linux-gnueabihf-gcc -o bin/MiSTer "${object_files[@]}" "${link_flags[@]}"
cp bin/MiSTer bin/MiSTer.elf
arm-none-linux-gnueabihf-strip bin/MiSTer

echo "rebuilt arcade fast-path objects"
ls -l bin/user_io.cpp.o bin/support/arcade/mra_loader.cpp.o bin/MiSTer bin/MiSTer.elf