# twoegram -- a client for OpenClaw agents on the Enhanced Apple //e with a
# CoGS card.
#
#   make            build TWOEGRAM.SYSTEM
#   make disk       the bootable ProDOS image build/TWOEGRAM.po
#   make run        build, boot it in Virtual ][, print the screen
#   make shots      boot the disk and capture docs/images/*.png
#   make host-test  the C that has no Apple dependencies, run on the Mac:
#                   card protocol and conversation layout
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
	src/demo.c

obj = $(patsubst src/%.s,build/%.o,$(patsubst src/%.c,build/%.o,$(1)))
HDRS = $(wildcard src/*.h)

BIN   = build/TWOEGRAM.SYSTEM
DISK  = build/TWOEGRAM.po

HOST_CC     = cc
HOST_CFLAGS = -std=c99 -Wall -Wextra -Werror -Isrc

.PHONY: all disk run shots host-test clean

all: $(BIN)

build build/host build/asm:
	mkdir -p $@

# C goes through assembly so tools/cc65check.py can look at it: this cc65
# has been seen to miscompile a pointer reload (see that script), and a
# recurrence must fail the build, not corrupt memory on the Apple.
build/%.o: src/%.c $(HDRS) tools/cc65check.py | build build/asm
	$(CL65) -t $(TARGET) $(COPT) -S -o build/asm/$*.s $<
	python3 -I tools/cc65check.py build/asm/$*.s > /dev/null || python3 -I tools/cc65check.py build/asm/$*.s
	$(CL65) -t $(TARGET) -c -o $@ build/asm/$*.s

build/%.o: src/%.s | build
	$(CL65) -t $(TARGET) -c -o $@ $<

# The heap left below the stack must hold cc65's 1K page-aligned ProDOS
# buffer once the program opens files: tools/memcheck.py fails the build
# if not.
$(BIN): $(call obj,$(APP) $(COMMON)) $(CFG) tools/memcheck.py
	$(CL65) -t $(TARGET) -C $(CFG) -m build/twoegram.map -Ln build/twoegram.lbl -o $@ $(call obj,$(APP) $(COMMON))
	@echo "TWOEGRAM.SYSTEM: $$(($$(wc -c < $@) - 58)) bytes"
	@python3 -I tools/memcheck.py build/twoegram.map $(CFG) || { rm -f $@; exit 1; }

disk: $(DISK)

$(DISK): $(BIN) tools/mkdisk.sh
	tools/mkdisk.sh $(DISK) $(BIN)

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

host-test: build/host/test_cogs build/host/test_layout
	build/host/test_cogs
	build/host/test_layout

clean:
	rm -rf build
