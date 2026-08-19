# Genix — C toolchain (render + rebuild). The live installer stays Python.
CC      ?= gcc
LDFLAGS ?=

# Ignore the host's CFLAGS. CachyOS (and similar) often export -march=native
# or x86-64-v3; glibc then refuses to run the binary on older chips
# (Celeron N4500, etc) with "CPU ISA level is lower than required".
# v2 is SSE4.2 — Jasper Lake / most laptops from ~2011 on.
# For this machine only: make MARCH=native
MARCH   ?= x86-64-v2
override CFLAGS := -std=c11 -O2 -Wall -Wextra -Wno-format-truncation -D_GNU_SOURCE -march=$(MARCH) $(EXTRA_CFLAGS)

SRC     := src
BUILD   := build

.PHONY: all clean install test

all: $(BUILD)/genix-render $(BUILD)/genix-rebuild

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/util.o: $(SRC)/util.c $(SRC)/util.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $(SRC)/util.c

$(BUILD)/toml.o: $(SRC)/toml.c $(SRC)/toml.h $(SRC)/util.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $(SRC)/toml.c

$(BUILD)/render.o: $(SRC)/render.c $(SRC)/render.h $(SRC)/toml.h $(SRC)/util.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $(SRC)/render.c

$(BUILD)/boot.o: $(SRC)/boot.c $(SRC)/boot.h $(SRC)/toml.h $(SRC)/util.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $(SRC)/boot.c

$(BUILD)/genix-render: $(SRC)/genix-render.c $(BUILD)/render.o $(BUILD)/toml.o $(BUILD)/util.o
	$(CC) $(CFLAGS) -o $@ $(SRC)/genix-render.c $(BUILD)/render.o $(BUILD)/toml.o $(BUILD)/util.o $(LDFLAGS)

$(BUILD)/genix-rebuild: $(SRC)/rebuild.c $(BUILD)/boot.o $(BUILD)/render.o $(BUILD)/toml.o $(BUILD)/util.o
	$(CC) $(CFLAGS) -o $@ $(SRC)/rebuild.c $(BUILD)/boot.o $(BUILD)/render.o $(BUILD)/toml.o $(BUILD)/util.o $(LDFLAGS)

clean:
	rm -rf $(BUILD)

install: all
	install -d $(DESTDIR)/usr/bin
	install -m755 $(BUILD)/genix-render $(DESTDIR)/usr/bin/genix-render
	install -m755 $(BUILD)/genix-rebuild $(DESTDIR)/usr/bin/genix-rebuild
	install -m755 bin/genix-install $(DESTDIR)/usr/bin/genix-install

test: all
	$(BUILD)/genix-render example/configuration.toml /tmp/genix-rend-test
	python3 -m py_compile lib/genix/installer.py
