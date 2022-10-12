# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2010-2014 Intel Corporation

APP = dperf 

# all source are stored in SRCS-y
SRCS-y := src/*.c 

# Build using pkg-config variables if possible
ifneq ($(shell pkg-config --exists libdpdk && echo 0),0)
$(error "no installation of DPDK found")
endif
ifneq ($(shell uname),Linux)
$(error This application can only operate in a linux environment)
endif

all: main
.PHONY: main 
main: build/${APP}

PKGCONF ?= pkg-config
PC_FILE := $(shell $(PKGCONF) --path libdpdk 2>/dev/null)
CFLAGS  += -O3 $(shell $(PKGCONF) --cflags libdpdk)
CFLAGS  += -DALLOW_EXPERIMENTAL_API
CFLAGS  += -std=gnu11
LDFLAGS += -lrt
LDFLAGS += -lnuma
LDFLAGS += -levent
APP_SHARED = $(shell $(PKGCONF) --libs libdpdk)

build/${APP}: $(SRCS-y) Makefile $(PC_FILE) | build
	$(CC) $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS) $(APP_SHARED)

build:
	@mkdir -p $@

.PHONY: install
install: $(APP)
	cp $(APP) /usr/local/bin

.PHONY: clean
clean:
	rm -f build/$(APP)
	test -d build && rmdir -p build || true
