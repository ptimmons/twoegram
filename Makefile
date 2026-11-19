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
	src/prefs.c \
	src/lock.c \
	src/overlay.c \
	src/save.c \
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
SETUP = build/TWOEGRAM.SETUP
PREFS = build/TWOEGRAM.PREFS
SAVE  = build/TWOEGRAM.SAVE
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

.PHONY: all disk release run shots convo-shots charset host-test hermes-sdk hermes-test hermes-vectors relay-test relay-dist clean

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
	@cp $@.1 $(SETUP)
	@echo "TWOEGRAM.SETUP: $$(wc -c < $(SETUP)) bytes, adding a gateway"
	@cp $@.3 $(PREFS)
	@echo "TWOEGRAM.PREFS: $$(wc -c < $(PREFS)) bytes, settings"
	@cp $@.2 $(SAVE)
	@echo "TWOEGRAM.SAVE: $$(wc -c < $(SAVE)) bytes, the saving overlay"

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
#
# TWOEGRAM.CFG gets the GATEWAY= lines from probe.cfg when it has any
# (the real gateways, the one in use first, each a key name and an
# address: make disk GATEWAY="claw1 http://192.168.1.20:8417" sets one,
# kept like URL=; edit probe.cfg for several), else the probe's URL
# (the dev server), with no key name, which set-up then asks for.
$(DISK): $(BIN) $(PRBIN) tools/mkdisk.sh FORCE
	VOL=$(VOLUME) tools/mkdisk.sh $(DISK) $(BIN) $(SETUP) $(PREFS) $(SAVE) $(PRBIN)
	@if [ -n "$(GATEWAY)" ] && [ -f probe.cfg ]; then \
	    grep -v '^GATEWAY=' probe.cfg > probe.cfg.new; \
	    printf 'GATEWAY=%s\n' "$(GATEWAY)" >> probe.cfg.new; \
	    mv probe.cfg.new probe.cfg; chmod 600 probe.cfg; \
	fi
	@if [ -n "$(URL)" ] && [ -n "$(KEY)" ]; then \
	    printf 'URL=%s\nKEY=%s\n%s' "$(URL)" "$(KEY)" "$(if $(AGENT),AGENT=$(AGENT)\n,)" > probe.cfg; \
	    chmod 600 probe.cfg; \
	fi
	@if [ -f probe.cfg ]; then \
	    tr '\n' '\r' < probe.cfg | tools/ac -ptx $(DISK) A2PROBE.CFG; \
	    echo "A2PROBE.CFG: $$(sed -n 's/^URL=//p' probe.cfg), with a key (from probe.cfg)"; \
	    if grep -q '^GATEWAY=' probe.cfg; then gws=$$(grep '^GATEWAY=' probe.cfg); \
	    else gws="GATEWAY=$$(sed -n 's/^URL=//p' probe.cfg)"; fi; \
	    { echo '# Twoegram: the gateways, the one in use first'; echo "$$gws"; } | \
	        tr '\n' '\r' | tools/ac -ptx $(DISK) TWOEGRAM.CFG; \
	    echo "TWOEGRAM.CFG: $$(echo "$$gws" | sed 's/^GATEWAY=//' | paste -sd ';' -), no key (the key stays on the card)"; \
	else \
	    echo "no A2PROBE.CFG (give URL= and KEY= once for the probe)"; \
	fi
	@echo $$(( $(BUILD) + 1 )) > BUILDNUM
	@echo "==> $(DISK): build $(BUILD_ID), volume /$(VOLUME)"

FORCE:

# The release: build/release/ holds the disk (TWOEGRAM.SYSTEM and
# A2PROBE.SYSTEM, volume /TWOEGRAM, and no .CFG file: no test key, no
# address; adding a gateway asks), the plugin package (npm pack, only
# the files OpenClaw loads), and their SHA-256 sums. The build number
# goes up as for any disk, so the release's is its own.
VERSION := $(shell sed -n 's/.*TWOEGRAM_VERSION "\(.*\)".*/\1/p' src/ui.h)
REL      = build/release

release: $(BIN) $(PRBIN) tools/mkdisk.sh FORCE
	rm -rf $(REL)
	mkdir -p $(REL)
	VOL=TWOEGRAM tools/mkdisk.sh $(REL)/TWOEGRAM-$(VERSION).po $(BIN) $(SETUP) $(PREFS) $(SAVE) $(PRBIN)
	@if tools/ac -ll $(REL)/TWOEGRAM-$(VERSION).po | grep -q '\.CFG'; then \
	    echo "release: a .CFG file is on the release disk" >&2; exit 1; \
	fi
	cd plugin && npm pack --pack-destination ../$(REL) >/dev/null
	cd $(REL) && shasum -a 256 *.po *.tgz > SHA256SUMS
	@echo $$(( $(BUILD) + 1 )) > BUILDNUM
	@echo "==> $(REL): Twoegram $(VERSION) (build $(BUILD_ID)) and the a2 plugin"
	@cat $(REL)/SHA256SUMS

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
HOST_CONVO = $(HOST_A2) src/convo.c src/layout.c src/msgs.c src/link.c src/setup.c src/prefs.c src/lock.c src/overlay.c src/save.c \
             src/help.c src/status.c src/screen.c

build/host/test_convo: tests/host/test_convo.c tests/host/test_a2.c $(HOST_CONVO) $(HDRS) build/build.h | build/host
	$(HOST_CC) $(HOST_CFLAGS) -Wno-unused-function -Itests/host -Ibuild -o $@ tests/host/test_convo.c $(HOST_CONVO)

# The manual's pictures of the conversation (docs/MANUAL.md): the real
# screen code on the model gateway, with a demo conversation, drawn with
# the glyphs captured from Virtual ][ (tools/charset). `make shots` takes
# the ones the emulator can show itself.
build/host/shots: tests/host/shots.c tests/host/test_a2.c $(HOST_CONVO) $(HDRS) build/build.h | build/host
	$(HOST_CC) $(HOST_CFLAGS) -Wno-unused-function -Itests/host -Ibuild -o $@ tests/host/shots.c $(HOST_CONVO)

convo-shots: build/host/shots tools/render.py tools/charset/charset.png
	mkdir -p build/shots
	build/host/shots
	python3 -I tools/render.py tools/charset/charset.png build/shots/*.scr docs/images

# CHARSET.SYSTEM shows every screen code; its Virtual ][ screenshot is
# tools/charset/charset.png. Only needed again if that is lost.
charset: build/charset.po
build/charset.po: tools/charset/charset.c src/screen.c src/blit.s $(CFG)
	$(CL65) -t $(TARGET) -Or -Isrc -C $(CFG) -o build/CHARSET.SYSTEM tools/charset/charset.c src/screen.c src/blit.s
	VOL=CHARSET tools/mkdisk.sh $@ build/CHARSET.SYSTEM
	tools/vii.sh boot $@
	tools/vii.sh await "CHARSET READY" 30
	sleep 1
	tools/vii.sh snap tools/charset/charset.png

host-test: build/host/test_cogs build/host/test_layout build/host/test_a2 build/host/test_store build/host/test_convo
	build/host/test_cogs
	build/host/test_layout
	build/host/test_a2
	build/host/test_store
	build/host/test_convo

# The Hermes Agent plugin (hermes/README.md). Its adapter tests run against
# Hermes's own source, at the commit the gateway runs.
HERMES_COMMIT = ae2a81ccc00e9b7c06301523e5bb09608397149c
HERMES_PY = .sdk/hermes-venv/bin/python

hermes-sdk:
	test -d .sdk/hermes-agent/.git || (mkdir -p .sdk && git init -q .sdk/hermes-agent && \
	  git -C .sdk/hermes-agent remote add origin https://github.com/NousResearch/hermes-agent.git)
	git -C .sdk/hermes-agent fetch -q --depth 1 origin $(HERMES_COMMIT)
	git -C .sdk/hermes-agent checkout -q FETCH_HEAD
	test -x $(HERMES_PY) || python3 -m venv .sdk/hermes-venv
	$(HERMES_PY) -c 'import tomllib; [print(d) for d in tomllib.load(open(".sdk/hermes-agent/pyproject.toml","rb"))["project"]["dependencies"] if not d.startswith("browser-harness")]' > .sdk/hermes-deps.txt
	$(HERMES_PY) -m pip install -q -r .sdk/hermes-deps.txt pytest pytest-asyncio

hermes-test:
	cd hermes && ../$(HERMES_PY) -m pytest -q -p no:cacheprovider

# The relay (relay/, docs/RELAY.md). Its tests run in the Hermes test
# environment (pytest), but need nothing of Hermes. relay-dist: one folder
# for the LXC, with the modules it shares with the Hermes plugin copied in.
RELAY_VERSION := $(shell sed -n 's/^VERSION = "\(.*\)"/\1/p' relay/twoegram_relay/__init__.py)
RELAY_SHARED   = __init__.py protocol.py store.py server.py text.py tables.py

relay-test:
	cd relay && ../$(HERMES_PY) -m pytest -q -p no:cacheprovider

relay-dist:
	rm -rf build/twoegram-relay build/twoegram-relay-$(RELAY_VERSION).tgz
	mkdir -p build/twoegram-relay/twoegram build/twoegram-relay/twoegram_relay
	cp relay/twoegram-relay relay/install.sh relay/twoegram-relay.service relay/README.md build/twoegram-relay/
	cp relay/twoegram_relay/*.py build/twoegram-relay/twoegram_relay/
	cd hermes/twoegram && cp $(RELAY_SHARED) ../../build/twoegram-relay/twoegram/
	cd build && COPYFILE_DISABLE=1 tar -czf twoegram-relay-$(RELAY_VERSION).tgz --uid 0 --gid 0 twoegram-relay
	@echo "==> build/twoegram-relay-$(RELAY_VERSION).tgz"

hermes-vectors:
	node hermes/tools/vectors.ts > hermes/tests/vectors.json
	python3 hermes/tools/tables.py

clean:
	rm -rf build
