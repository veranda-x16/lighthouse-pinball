# Lighthouse Pinball for Veranda
#   make        Lighthouse Pinball.cvt, against an installed Veranda SDK (SDK=...)
#   make test   the test build: a scripted game (SCRIPT=<script.bin of
#               host/sim bin>, the last record); SHOT=<tick>: stop there
#   make run    in the emulator (the SDK's vrun)
# OUT: where the results go (default ../build/veranda)
SDK      ?= ../build/sdk
OUT      ?= ../build/veranda
GEN      := $(OUT)/gen
export CC65_HOME ?= $(HOME)/.local/share/cc65
CC       := cl65 -t none --cpu 65C02 -Oirs -Cl -I $(SDK)/include --asm-include-dir $(SDK)/include -I $(GEN) -I ../src
SRC      := main.c sounds.c ../src/phys.c ../src/game.c ../src/trace.c $(GEN)/table.c
ASM      := x16.s $(if $(FIXC),,fix.s)
SRC      += $(if $(FIXC),../src/fix.c)
APP      := $(OUT)/Lighthouse Pinball.cvt
OBJ      := $(OUT)/obj
SCRIPT   ?=
SHOT     ?=

all: $(OUT)/pinball.cvt

$(GEN)/table.h $(GEN)/table.c: ../tools/mktable.py
	mkdir -p $(GEN)
	python3 -I -B ../tools/mktable.py $(GEN) > $(GEN)/mktable.log

# the table's picture in full colour (VERA tiles: Veranda's vr_pic, the SDK's mkwall.py --tiles)
$(GEN)/pic.h: ../tools/mkart.py ../tools/mktable.py
	mkdir -p $(GEN)
	python3 -I -B ../tools/mkart.py $(GEN) $(SDK) > $(GEN)/mkart.log

$(OUT)/pinball.bin: $(SRC) $(ASM) pbx.h ../src/pb.h ../src/trace.h $(GEN)/table.h $(GEN)/pic.h
	mkdir -p $(OBJ)
	rm -f $(OBJ)/*.o
	for f in $(SRC) $(ASM); do $(CC) $(CFLAGS) -c -o $(OBJ)/$$(basename $$f).o $$f || exit 1; done
	cl65 -t none -C $(SDK)/cfg/app-c.cfg -m $(OUT)/pinball.map -o $@ $(OBJ)/*.o $(SDK)/lib/veranda.lib

$(OUT)/pinball.cvt: $(OUT)/pinball.bin app.hdr
	cp app.hdr $(GEN)/app.hdr
	python3 -I -B $(SDK)/tools/vpack.py --load 0x0801 -o "$(APP)" $(GEN)/app.hdr $(OUT)/pinball.bin \
		$$(ls $(GEN)/pic.vram.[0-9]* | sort -t. -k3 -n) $(GEN)/sprites.1 $(GEN)/sprites.2 $(SCRIPT)
	cp "$(APP)" $@

test:
	rm -f $(OUT)/pinball.bin
	$(MAKE) OUT=$(OUT) CFLAGS="-DPB_TEST $(if $(SHOT),-DPB_SHOT=$(SHOT)) $(TESTFLAGS)" SCRIPT="$(SCRIPT)" all

run: all
	$(SDK)/tools/vrun "$(APP)"

clean:
	rm -f $(OUT)/pinball.bin $(OUT)/pinball.cvt $(OUT)/pinball.map $(OBJ)/*.o

.PHONY: all test run clean
