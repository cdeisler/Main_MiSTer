# makefile to fail if any command in pipe is failed.
SHELL = /bin/bash -o pipefail

MAKEFLAGS += "-j $(shell nproc)"

# using gcc version 10.2.1
BASE    = arm-none-linux-gnueabihf

CC      = $(BASE)-gcc
LD      = $(BASE)-ld
STRIP   = $(BASE)-strip

ifeq ($(V),1)
	Q :=
else
	Q := @
endif

INCLUDE	= -I./
INCLUDE	+= -I./bin
INCLUDE	+= -I./lib/libco
INCLUDE	+= -I./lib/miniz
INCLUDE	+= -I./lib/md5
INCLUDE += -I./lib/lzma
INCLUDE += -I./lib/zstd/lib
INCLUDE += -I./lib/libchdr/include
INCLUDE += -I./lib/bluetooth
INCLUDE += -I./lib/serial_server/library
INCLUDE += -I./lib/httplib

BUILDDIR = bin
BUILD_FILE := build_number.txt
VERSION_HEADER := $(BUILDDIR)/version.h
APP_VERSION_MAJOR ?= 1
APP_VERSION_MINOR ?= 0
APP_VERSION_PATCH ?= 0
BUILD_HASH := $(shell git rev-parse --short=12 HEAD 2>/dev/null || echo nogit)
BUILD_DIRTY := $(shell test -n "$$(git status --porcelain --untracked-files=no 2>/dev/null)" && echo -dirty || true)
BUILD_META = $(BUILDDIR)/build_meta.h

ifeq ($(MAKE_RESTARTS),)
BUILD_DATE := $(shell date -u +"%Y-%m-%dT%H:%M:%SZ")
else
BUILD_DATE := $(shell current=$$(sed -n 's/^#define APP_BUILD_DATE "\([^"]\+\)"/\1/p' $(VERSION_HEADER) 2>/dev/null | head -n 1 | tr -d '\r\n'); \
	if [ -n "$$current" ]; then \
		echo $$current; \
	else \
		date -u +"%Y-%m-%dT%H:%M:%SZ"; \
	fi)
endif

ifeq ($(MAKE_RESTARTS),)
BUILD_NUMBER := $(shell file_current=$$(tr -d '\r\n' < $(BUILD_FILE) 2>/dev/null); \
	meta_current=$$(sed -n 's/^#define BUILD_NUMBER "\([0-9]\+\)"/\1/p' $(BUILD_META) 2>/dev/null | head -n 1 | tr -d '\r\n'); \
	git_current=$$(git rev-list --count HEAD 2>/dev/null | tr -d '\r\n'); \
	current=0; \
	for candidate in $$file_current $$meta_current $$git_current; do \
		if [[ $$candidate =~ ^[0-9]+$$ ]] && [ $$candidate -gt $$current ]; then \
			current=$$candidate; \
		fi; \
	done; \
	echo $$((current + 1)))
else
BUILD_NUMBER := $(shell current=$$(tr -d '\r\n' < $(BUILD_FILE) 2>/dev/null); \
	if [[ $$current =~ ^[0-9]+$$ ]]; then \
		echo $$current; \
	else \
		echo 1; \
	fi)
endif

ifeq ($(strip $(BUILD_NUMBER)),)
BUILD_NUMBER := 1
endif

BUILD_STAMP := $(BUILD_NUMBER)
APP_BUILD_VERSION := $(APP_VERSION_MAJOR).$(APP_VERSION_MINOR).$(APP_VERSION_PATCH).$(BUILD_NUMBER)

PRJ = MiSTer
C_SRC =   $(wildcard *.c) \
          $(wildcard ./lib/miniz/*.c) \
          $(wildcard ./lib/md5/*.c) \
          $(wildcard ./lib/lzma/*.c) \
					$(wildcard ./lib/zstd/lib/common/*.c) \
					$(wildcard ./lib/zstd/lib/decompress/*.c) \
          $(wildcard ./lib/libchdr/*.c) \
          lib/libco/arm.c

CPP_SRC = $(wildcard *.cpp) \
          $(wildcard ./lib/serial_server/library/*.cpp) \
          $(wildcard ./support/*/*.cpp)

IMG =     $(wildcard *.png)

IMLIB2_LIB  = -Llib/imlib2 -lfreetype -lbz2 -lpng16 -lz -lImlib2

OBJ	= $(C_SRC:%.c=$(BUILDDIR)/%.c.o) $(CPP_SRC:%.cpp=$(BUILDDIR)/%.cpp.o) $(IMG:%.png=$(BUILDDIR)/%.png.o)
DEP	= $(C_SRC:%.c=$(BUILDDIR)/%.c.d) $(CPP_SRC:%.cpp=$(BUILDDIR)/%.cpp.d)

DFLAGS	= $(INCLUDE) -D_7ZIP_ST -DPACKAGE_VERSION=\"1.3.3\" -DHAVE_LROUND -DHAVE_STDINT_H -DHAVE_STDLIB_H -DHAVE_SYS_PARAM_H -DENABLE_64_BIT_WORDS=0 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
CFLAGS	= $(DFLAGS) -Wall -Wextra -Wno-strict-aliasing -Wno-stringop-overflow -Wno-stringop-truncation -Wno-format-truncation -Wno-psabi -Wno-restrict -c
LFLAGS	= -lc -lstdc++ -lm -lrt -Wl,--allow-shlib-undefined -Wl,-rpath-link,/usr/arm-linux-gnueabihf/lib $(IMLIB2_LIB) -ldl -Llib/bluetooth -lbluetooth -lpthread

OUTPUT_FILTER = sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

ifneq ($(DEBUG),1)
	CFLAGS += -O3
else
	CFLAGS += -O0 -g -fomit-frame-pointer
endif

ifeq ($(PROFILING),1)
	DFLAGS += -DPROFILING
endif

$(BUILDDIR)/$(PRJ): $(OBJ) $(BUILD_META)
	$(Q)$(info $@)
	$(Q)$(CC) -o $@ $(OBJ) $(LFLAGS)
	$(Q)cp $@ $@.elf
ifneq ($(DEBUG),1)
	$(Q)$(STRIP) $@
endif

.PHONY: clean
clean:
	$(Q)rm -rf bin

.PHONY: FORCE
FORCE:


$(BUILD_FILE): FORCE
	$(Q)printf '%s\n' '$(BUILD_NUMBER)' > $@.tmp
	$(Q)if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm -f $@.tmp; fi

$(VERSION_HEADER): $(BUILD_FILE) FORCE
	@mkdir -p $(dir $@)
	$(Q)printf '#pragma once\n#define APP_VERSION_MAJOR %s\n#define APP_VERSION_MINOR %s\n#define APP_VERSION_PATCH %s\n#define APP_BUILD_NUMBER %s\n#define APP_BUILD_NUMBER_STR "%s"\n#define APP_BUILD_VERSION "%s"\n#define APP_BUILD_HASH "%s%s"\n#define APP_BUILD_DATE "%s"\n' '$(APP_VERSION_MAJOR)' '$(APP_VERSION_MINOR)' '$(APP_VERSION_PATCH)' '$(BUILD_NUMBER)' '$(BUILD_NUMBER)' '$(APP_BUILD_VERSION)' '$(BUILD_HASH)' '$(BUILD_DIRTY)' '$(BUILD_DATE)' > $@.tmp
	$(Q)if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm -f $@.tmp; fi
	$(Q)printf '#pragma once\n#define BUILD_NUMBER "%s"\n#define BUILD_VERSION "%s"\n#define BUILD_STAMP "%s"\n#define BUILD_HASH "%s%s"\n#define BUILD_DATE "%s"\n' '$(BUILD_NUMBER)' '$(APP_BUILD_VERSION)' '$(BUILD_STAMP)' '$(BUILD_HASH)' '$(BUILD_DIRTY)' '$(BUILD_DATE)' > $(BUILD_META).tmp
	$(Q)if ! cmp -s $(BUILD_META).tmp $(BUILD_META); then mv $(BUILD_META).tmp $(BUILD_META); else rm -f $(BUILD_META).tmp; fi

$(BUILD_META): $(VERSION_HEADER)

$(BUILDDIR)/%.c.o: %.c
	$(Q)$(info $<)
	$(Q)$(CC) $(CFLAGS) -std=gnu99 -o $@ -c $< 2>&1 | $(OUTPUT_FILTER)

$(BUILDDIR)/%.cpp.o: %.cpp
	$(Q)$(info $<)
	$(Q)$(CC) $(CFLAGS) -std=gnu++14 -Wno-class-memaccess -o $@ -c $< 2>&1 | $(OUTPUT_FILTER)

$(BUILDDIR)/%.png.o: %.png
	$(Q)$(info $<)
	$(Q)$(LD) -r -b binary -o $@ $< 2>&1 | $(OUTPUT_FILTER)

ifneq ($(MAKECMDGOALS), clean)
-include $(DEP)
endif
$(BUILDDIR)/%.c.d: %.c
	@mkdir -p $(dir $(BUILDDIR)/$*)
	$(Q)$(info $< >> $@)
	$(Q)$(CC) $(DFLAGS) -MM $< -MT $@ -MT $(BUILDDIR)/$*.c.o -MF $@ 2>&1 | $(OUTPUT_FILTER)

$(BUILDDIR)/%.cpp.d: %.cpp
	@mkdir -p $(dir $(BUILDDIR)/$*)
	$(Q)$(info $< >> $@)
	$(Q)$(CC) $(DFLAGS) -MM $< -MT $@ -MT $(BUILDDIR)/$*.cpp.o -MF $@ 2>&1 | $(OUTPUT_FILTER)

$(BUILDDIR)/main.cpp.d: $(VERSION_HEADER)
$(BUILDDIR)/http_server.cpp.d: $(VERSION_HEADER)
$(BUILDDIR)/main.cpp.o: $(VERSION_HEADER)
$(BUILDDIR)/http_server.cpp.o: $(VERSION_HEADER)

# Ensure correct time stamp
$(BUILDDIR)/main.cpp.o: $(filter-out $(BUILDDIR)/main.cpp.o, $(OBJ))
