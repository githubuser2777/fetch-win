ifeq ($(origin CC),default)
  ifeq ($(OS),Windows_NT)
    CC = gcc
  else
    CC = cc
  endif
endif

ifeq ($(OS),Windows_NT)
  VERSION := $(shell type VERSION 2>nul)
  ifeq ($(strip $(VERSION)),)
    VERSION := 0.1.0
  endif
  EXE_EXT = .exe
  RUN_PREFIX =
  RM = cmd /c del /f /q 2>nul
else
  VERSION := $(shell cat VERSION 2>/dev/null || echo 0.1.0)
  EXE_EXT =
  RUN_PREFIX = ./
  RM = rm -f
endif

CFLAGS ?= -O2
PREFIX ?= /usr/local
LDFLAGS ?=
LDLIBS = -lm
CODENAME ?= Overclocked ASCII

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
UNAME_M := $(shell uname -m 2>/dev/null || echo x86_64)
ifeq ($(UNAME_S),Darwin)
  LDLIBS += -framework IOKit -framework CoreFoundation
endif
ifeq ($(OS),Windows_NT)
  LDLIBS += -lws2_32
  CFLAGS += -I tests/include
endif

SRCS = fetch.c src/core/common.c src/renderer/renderer.c

fetch: $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -DFETCH_VERSION='"$(VERSION)"' -DFETCH_CODENAME='"$(CODENAME)"' -DFETCH_ARCH='"$(UNAME_M)"' -DFETCH_OS='"$(UNAME_S)"' -I. -o $@ $^ $(LDLIBS)

install: fetch
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 fetch $(DESTDIR)$(PREFIX)/bin/fetch

clean:
	-$(RM) fetch fetch.exe test_baseline test_baseline.exe test_phase1 test_phase1.exe

test: test_baseline$(EXE_EXT) test_phase1$(EXE_EXT)
	$(RUN_PREFIX)test_baseline$(EXE_EXT)
	$(RUN_PREFIX)test_phase1$(EXE_EXT)

test_baseline$(EXE_EXT): tests/test_baseline.c $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -DFETCH_VERSION='"$(VERSION)"' -DFETCH_CODENAME='"$(CODENAME)"' -DFETCH_ARCH='"$(UNAME_M)"' -DFETCH_OS='"$(UNAME_S)"' -I. -I tests/include tests/test_baseline.c src/core/common.c src/renderer/renderer.c -o $@ $(LDLIBS)

test_phase1$(EXE_EXT): tests/test_phase1.c src/core/common.c src/renderer/renderer.c
	$(CC) $(CFLAGS) $(LDFLAGS) -I. tests/test_phase1.c src/core/common.c src/renderer/renderer.c -o $@ -lm

.PHONY: install clean test
