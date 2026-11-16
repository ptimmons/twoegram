# twoegram -- a client for OpenClaw agents on the Enhanced Apple //e with a
# CoGS card.
#
#   make            build TWOEGRAM.SYSTEM and A2PROBE.SYSTEM
#   make disk       the bootable ProDOS image build/TWOEGRAM-b<build>.po;
#                   URL= and KEY= add the probe's A2PROBE.CFG
#   make run        build, boot it in Virtual ][, print the screen
#   make shots      boot the disk and capture docs/images/*.png
#   make host-test  the C that has no Apple dependencies, run on the Mac:
#                   card protocol, conversation layout, keys, HMAC, replies,
#                   the session, the message store and history
#   make clean

TARGET  = apple2enh
CL65    = cl65
CFG     = cfg/twoegram.cfg
# -Or: optimise with register variables, no inlining; -Cl: static locals
COPT    = -Or -Cl

COMMON = \
	src/screen.c \
	src/kbd.c \
	src/status.c \
	src/tick.c \
	src/fmt.c \
	src/cogs.c \
	src/slot.c \
	src/blit.s \
	src/cogs_io.s

APP = \
	src/main.c \
	src/splash.c \
	src/help.c \
	src/net.c \
	src/convo.c \
	src/layout.c \
	src/msgs.c \
	src/link.c \
	src/setup.c \
	src/config.c \
	src/keystore.c \
	src/session.c \
	src/cogs_http.c \
	src/cogs_extra.c \
	src/hmac.c \
	src/proto.c \
	src/store.c \
	src/history.c \
	src/auxmem.s

# The milestone 2 probe: the protocol end to end against the dev server.
PROBE = \
	src/a2probe.c \
	src/session.c \
	src/cogs_http.c \
	src/cogs_extra.c \
	src/hmac.c \
	src/proto.c \
	src/disk.s

obj = $(patsubst src/%.s,build/%.o,$(patsubst src/%.c,build/%.o,$(1)))
HDRS = $(wildcard src/*.h)

BIN   = build/TWOEGRAM.SYSTEM
PRBIN = build/A2PROBE.SYSTEM

# Build numbers. BUILDNUM holds the number the next disk gets; it goes up
# by one after each disk is made, so no two disks share one. The number is
# in the disk's file name (a new name each time, so the card's disk cache
# can never serve an older image under it), in its volume name, and on
# screen in both programs (build/build.h, rewritten only when the number
# changes, so just the files that show it recompile).
BUILD    := $(shell cat BUILDNUM)
BUILD_ID := b$(shell printf %03d $(BUILD))
DISK      = build/TWOEGRAM-$(BUILD_ID).po
VOLUME    = TWOEGRAM.B$(shell printf %03d $(BUILD))
$(shell mkdir -p build; t='#define BUILD_ID "$(BUILD_ID)"'; [ "$$(cat build/build.h 2>/dev/null)" = "$$t" ] || echo "$$t" > build/build.h)

HOST_CC     = cc
HOST_CFLAGS = -std=c99 -Wall -Wextra -Werror -Isrc

.PHONY: all disk run shots host-test clean

all: $(BIN) $(PRBIN)

build build/host build/asm:
	mkdir -p $@

# C goes through assembly so tools/cc65check.py can look at it: this cc65
# has been seen to miscompile a pointer reload (see that script), and a
# recurrence must fail the build, not corrupt memory on the Apple.
build/%.o: src/%.c $(HDRS) tools/cc65check.py | build build/asm
	$(CL65) -t $(TARGET) $(COPT) -Ibuild -S -o build/asm/$*.s $<
	python3 -I tools/cc65check.py build/asm/$*.s > /dev/null || python3 -I tools/cc65check.py build/asm/$*.s
	$(CL65) -t $(TARGET) -c -o $@ build/asm/$*.s

build/splash.o build/help.o build/setup.o build/a2probe.o: build/build.h

build/%.o: src/%.s | build
	$(CL65) -t $(TARGET) -c -o $@ $<

# The heap left below the stack must hold cc65's 1K page-aligned ProDOS
# buffer once the program opens files: tools/memcheck.py fails the build
# if not.
$(BIN): $(call obj,$(APP) $(COMMON)) $(CFG) tools/memcheck.py
	$(CL65) -t $(TARGET) -C $(CFG) -m build/twoegram.map -Ln build/twoegram.lbl -o $@ $(call obj,$(APP) $(COMMON))
	@echo "TWOEGRAM.SYSTEM: $$(($$(wc -c < $@) - 58)) bytes"
	@python3 -I tools/memcheck.py build/twoegram.map $(CFG) || { rm -f $@; exit 1; }

$(PRBIN): $(call obj,$(PROBE) $(COMMON)) $(CFG) tools/memcheck.py
	$(CL65) -t $(TARGET) -C $(CFG) -m build/a2probe.map -Ln build/a2probe.lbl -o $@ $(call obj,$(PROBE) $(COMMON))
	@echo "A2PROBE.SYSTEM: $$(($$(wc -c < $@) - 58)) bytes"
	@python3 -I tools/memcheck.py build/a2probe.map $(CFG)

disk: $(DISK)

# TWOEGRAM.SYSTEM first, so ProDOS runs it at boot; A2PROBE.SYSTEM from
# the quit selector. With URL= and KEY= on the command line, A2PROBE.CFG
# goes on too (docs/PROBE.md): a test key for the dev server only.
#
# Given once, URL= and KEY= are kept in probe.cfg (gitignored, never
# committed: it holds a key) and every later disk carries them too.
$(DISK): $(BIN) $(PRBIN) tools/mkdisk.sh FORCE
	VOL=$(VOLUME) tools/mkdisk.sh $(DISK) $(BIN) $(PRBIN)
	@if [ -n "$(URL)" ] && [ -n "$(KEY)" ]; then \
	    printf 'URL=%s\nKEY=%s\n%s' "$(URL)" "$(KEY)" "$(if $(AGENT),AGENT=$(AGENT)\n,)" > probe.cfg; \
	    chmod 600 probe.cfg; \
	fi
	@if [ -f probe.cfg ]; then \
	    tr '\n' '\r' < probe.cfg | tools/ac -ptx $(DISK) A2PROBE.CFG; \
	    echo "A2PROBE.CFG: $$(sed -n 's/^URL=//p' probe.cfg), with a key (from probe.cfg)"; \
	    printf '# Twoegram: the gateway%ss address\rGATEWAY=%s\r' "'" "$$(sed -n 's/^URL=//p' probe.cfg)" | tools/ac -ptx $(DISK) TWOEGRAM.CFG; \
	    echo "TWOEGRAM.CFG: the same address, no key (the key stays on the card)"; \
	else \
	    echo "no A2PROBE.CFG (give URL= and KEY= once for the probe)"; \
	fi
	@echo $$(( $(BUILD) + 1 )) > BUILDNUM
	@echo "==> $(DISK): build $(BUILD_ID), volume /$(VOLUME)"

FORCE:

run: $(DISK)
	tools/vii.sh boot $(DISK)
	tools/vii.sh await TWOEGRAM 30
	tools/vii.sh screen

shots: $(DISK) tools/shots.sh
	tools/shots.sh $(DISK) docs/images

HOST_COGS = src/cogs.c src/cogs_extra.c src/fmt.c src/fmt_extra.c

build/host/test_cogs: tests/host/test_cogs.c $(HOST_COGS) $(HDRS) | build/host
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/host/test_cogs.c $(HOST_COGS)

build/host/test_layout: tests/host/test_layout.c src/layout.c $(HDRS) | build/host
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/host/test_layout.c src/layout.c

HOST_A2 = src/cogs.c src/cogs_extra.c src/cogs_http.c src/hmac.c src/proto.c src/session.c \
          src/keystore.c src/config.c src/fmt.c src/store.c src/history.c \
          tests/host/sha256.c tests/host/auxmodel.c

build/host/test_a2: tests/host/test_a2.c $(HOST_A2) $(HDRS) tests/host/sha256.h | build/host
	$(HOST_CC) $(HOST_CFLAGS) -Itests/host -o $@ tests/host/test_a2.c $(HOST_A2)

build/host/test_store: tests/host/test_store.c src/store.c tests/host/auxmodel.c $(HDRS) | build/host
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/host/test_store.c src/store.c tests/host/auxmodel.c

# The conversation screen itself, on test_a2.c's model card and gateway,
# with a text screen and scripted keys in place of the Apple's.
HOST_CONVO = $(HOST_A2) src/convo.c src/layout.c src/msgs.c src/link.c src/setup.c \
             src/help.c src/status.c src/screen.c

build/host/test_convo: tests/host/test_convo.c tests/host/test_a2.c $(HOST_CONVO) $(HDRS) build/build.h | build/host
	$(HOST_CC) $(HOST_CFLAGS) -Wno-unused-function -Itests/host -Ibuild -o $@ tests/host/test_convo.c $(HOST_CONVO)

host-test: build/host/test_cogs build/host/test_layout build/host/test_a2 build/host/test_store build/host/test_convo
	build/host/test_cogs
	build/host/test_layout
	build/host/test_a2
	build/host/test_store
	build/host/test_convo

clean:
	rm -rf build
