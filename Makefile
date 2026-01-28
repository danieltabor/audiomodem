all: mod demod ratetest

mod: mod.c bitops.h fskcalibrate.h srcfft.h fskclk.h fsk.h ook.h psk.h corr.h pkt.h audiomodem.h
	gcc -g -o mod mod.c -lsndfile -lsamplerate -lfftw3 -lm

demod: demod.c bitops.h fskcalibrate.h srcfft.h fskclk.h fsk.h ook.h psk.h corr.h pkt.h audiomodem.h
	gcc -g -o demod demod.c -lsndfile -lsamplerate -lfftw3 -lm

ratetest: ratetest.c bitops.h fskcalibrate.h srcfft.h fskclk.h fsk.h ook.h psk.h corr.h pkt.h audiomodem.h
	gcc -g -o ratetest ratetest.c -lsndfile -lsamplerate -lfftw3 -lm

clean:
	rm -f mod
	rm -f demod
	rm -f ratetest
