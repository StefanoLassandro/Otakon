


CFLAGS = -O3 -lpng
CC = g++

.PHONY: all
all: bin/otakon docs/otakon_docs.pdf

.PHONY: docs
docs: docs/otakon_docs.pdf

bin:
	mkdir -p bin

bin/otakon: src/otakon.cpp | bin
	$(CC) ./src/otakon.cpp -o ./bin/otakon $(CFLAGS)

docs/otakon_docs.pdf:
	pandoc ./docs/otakon_docs.md -o ./docs/otakon_docs.pdf

clean:
	rm ./bin/otakon
