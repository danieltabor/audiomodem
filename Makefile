ALL_HEADERS = \
	bitops.h \
	fskcalibrate.h \
	srcfft.h fskclk.h \
	fsk.h \
	ook.h \
	psk.h \
	corr.h \
	pkt.h \
	audiomodem.h

ALL_LIBS = \
	-lsndfile \
	-lsamplerate \
	-lfftw3 \
	-lm

all: mod demod ratetest generic

mod: mod.c $(ALL_HEADERS)
	gcc -g -o mod mod.c $(ALL_LIBS)

demod: demod.c $(ALL_HEADERS)
	gcc -g -o demod demod.c $(ALL_LIBS)

ratetest: ratetest.c $(ALL_HEADERS)
	gcc -g -o ratetest ratetest.c $(ALL_LIBS)

generic: generic.c bitops.h corr.h pkt.h
	gcc -g -o generic generic.c -lsndfile -lm

clean:
	rm -f mod
	rm -f demod
	rm -f ratetest
	rm -f generic
