all: mod demod ratetest

mod: mod.c bitops.h fskclk.h fsk.h compatmodem.h packetmodem.h
	gcc -g -o mod mod.c -lsndfile -lsamplerate -lfftw3 -lm

demod: demod.c bitops.h fskclk.h fsk.h compatmodem.h packetmodem.h
	gcc -g -o demod demod.c -lsndfile -lsamplerate -lfftw3 -lm

ratetest: ratetest.c bitops.h fskclk.h fsk.h compatmodem.h packetmodem.h
	gcc -g -o ratetest ratetest.c -lsndfile -lsamplerate -lfftw3 -lm

clean:
	rm -f mod
	rm -f demod
	rm -f ratetest
