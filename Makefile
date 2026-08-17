# Genix — C toolchain (render + rebuild). The live installer stays Python.
CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-format-truncation -D_GNU_SOURCE
LDFLAGS ?=

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
