CC=gcc
CFLAGS=-Wall -pedantic

all: build test

build:
	$(CC) $(CFLAGS) -lpthread main.c -o ./rerun

test:
	$(foreach file, $(wildcard tests/*.exp), echo "Run tests from $(file):" && expect "$(file)" && echo || exit 1;)
