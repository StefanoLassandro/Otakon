


CFLAGS = -O2 -lpng


all:
	g++ ./src/otakon.cpp -o ./bin/otakon $(CFLAGS)

clear:
	rm ./bin/otakon
