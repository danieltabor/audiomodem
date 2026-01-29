# audiomodem

This is a set of data modem libraries, and demonstration wrapper programs, that I wrote primarily for my own education.

Each modem is provided as a header only library.  It can be included in multiple source files, however, the actual implmenetation needs to be included in a single compiled file.  For example:
```
#define BITOPS_IMPLEMENTATION
#define FSK_IMPLEMENTATION
#include<fsk.h>
```

The bitops library is used by several of the modems.  It does not need to individual included but the `XXX_IMPLEMENTATION` tag needs to be defined before the inclusion of the first modem that uses it.

## Modems Libraries:
- fskclk

  A Frequency Shift Keying modem that utilizes a clock (a frequency used to mark the beginning of each symbol).  This library is FFT based.
  
- fsk

  A Frequency Shift Keying modem that does not utilize a clock.  This library is FFT based.
  
- ook

  An On-Off Keying modem.  It's function is based upon a serial UART with 8N1 settings, were Vcc is the existence of a tone, and Gnd is the absence of a tone.  This library is FFT based.
  
- pskclk

  A Phase Shift Keying modem that utilize a clock (a tone with a "base" frequency for each symobl paired with tone with a phase shift from the base).  This library is FFT based.
  
- corr

  A Generic correlation based modem.  This modem modulates using arbitary audio snipets and demodulated by performing correlations against all of the arbitrary snipets.  The `corr` modem provides some initialization functions that allow it to work like other modems: fsk, psk, and fpsk (which shifts both frequency and phase).

Each modem provdes a standard API interface:

`XXX_t *XXX_init(...);`

Initialize the modem based upon the specific configuration.  Most modems will require a samplerate for input/output audio, as well as a bandwidth.  The bandwidth is used to determine the range of possible symbol frequencies for FSK, and the resampling ratio used during demodulation prior to performing an FFT.  Modems that utilize a single frequency, will require that frequnency as an input.  Most modems will require a bitrate.  All of the modems, except `ook` can utilize a variable set of symbols, to allow 1, 2, or more bits per symbol, that must be specified.  The return value is an `XXX_t` pointer that was allocated by the initialize function.

`void   XXX_destroy(XXX_t *modem);`

Destroy the modem, and free up all allocated memory.

`int    XXX_set_thresh(XXX_t *modem, double thresh);`

Set the signal detection threshold for the modem.  This threshold must be within (0.0-1.0] and is a percetage of the maximum magnitude produce by each symbol during calibration. 

`int    XXX_set_verbose(XXX_t *modem, int verbose);`

Enable/Display debug console prints.  Modems can be quite verbose when demodulating. 

`void   XXX_printinfo(XXX_t *modem);`

Print basic modem configuration information to the console.

`int    XXX_modulate(XXX_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen);`

Modulate the data bytes in data/datalen to an array of samples in samples/sampleslen.  The allocation and freeing of the audio is handled by the modem.  The allocated buffer of samples will be reused (and possibly moved) by subsequent modulations.

`int    XXX_demodulate(XXX_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen);`

Demodulate the audio in samples/sampleslen to an array of bytes in data/datalen.  The allocation and freeing of the data buffer is handled by the modem.  The allocated buffer of data will be reused (and possibly moved) by subsequent demodulations.

## Additonal Libraries:
- srcfft
  
  This library utilizes libsamplerate and libfftw3 to provide an abstraction to resampling audio to a specified bandwidth and producing FFTs with a specified number of bins from input sample of a specified length.
  
- pkt

  This library provides a packet handling capability to aid data handling for the modem.  It provides a synchronization header with packet length, data whitening, and redundancy.

- fskcalibrate

  This library provides a single function that looks at finds optimal frequencies for FFT based FSK modems.

- bitops

  This library provides convience functions for dealing with data on a per-bit basis.

- audiomodem

  This library provides a wrapper around all of the other libraries.  It provides a mechanism for 

## Demonstration Programs:
- mod

  Modulate data to WAV files.  The `cfsk`, `cpsk`, and `cfpsk` options all use the `corr` modem. 
  ```
  Usage: mod [-h] [-v] [-p] [-fsk | -fskclk | -ook | -pskclk | -cfsk | -cpsk | -cfpsk]
  [-s samplerate] [-r bitrate] [-bw bandwidth] [-c symbol_count] [-f frequency]
  [-i inpath | -m "message"] -o output.wav
  
  Defaults:
    samplerate: based on bandwidth
    bitrate   : 64
    bandwidth : 3000
    symbol_count: 4
    frequency : 1000
  ```
  
- demod

   Demodulate data in WAV files.  The `cfsk`, `cpsk`, and `cfpsk` options all use the `corr` modem. 
  ```
  Usage: demod [-h] [-v] [-p] [-fsk | -fskclk | -ook | -pskclk | -cfsk | -cpsk | -cfpsk]
  [-r bitrate] [-bw bandwidth] [-c symbol_count] [-f frequency]
  -i input.wav [-o outpath]
  
  Defaults:
    bitrate : 64
    bandwidth: 3000
    symbol_count: 4
    frequency: 100
  ```

- ratetest

  Try to find the maximum datarate of a modem, with or without using the `pkt` library, `-p`, and with optional added noise.
  ```
  Usage: ratetest [-h] [-v] [-p] [-fsk | -fskclk | -ook | -pskclk | -cfsk | -cpsk | -cfpsk]
    [-s samplerate] [-r bitrate] [-bw bandwidth] [-c symbol_count] [-f frequency]
    [-z test_size] [-n noise_amplitude]
  
  Defaults:
    samplerate     : based on bandwidth
    start_bitrate  : 64
    bandwidth      : 3000
    symbol_count     : 4
    frequency      : 1000
    test_size      : 512
    noise_amplitude: 0.0
  ```
  
- generic

  Utilizes the generic capabilities of the `corr` modem by using specfied WAV files for symbols.
  ```
  Usage: generic [-h] [-v] [-s symbol.wav]
    [-mod | -demod] -i inpath -o outpath
  ```
