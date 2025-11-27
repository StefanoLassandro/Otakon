


CFLAGS = -O3 -lpng

.PHONY: all
all: bin/otakon

bin:
	mkdir -p bin

bin/otakon: src/otakon.cpp | bin
	g++ ./src/otakon.cpp -o ./bin/otakon $(CFLAGS)

clean:
	rm ./bin/otakon
