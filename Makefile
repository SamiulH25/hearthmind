CC ?= cc
CFLAGS ?= -O2 -march=native -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers
CFLAGS += $(shell pkg-config --cflags sdl3)
LDLIBS = $(shell pkg-config --libs sdl3) -lm

SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=build/%.o)

hearthmind: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.c src/game.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

run: hearthmind
	./hearthmind

clean:
	rm -rf build hearthmind

.PHONY: run clean
