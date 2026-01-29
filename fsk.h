/*
 * Copyright (c) 2026, Daniel Tabor
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __FSK_H__
#define __FSK_H__

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "bitops.h"
#include "srcfft.h"

#define FSK_DEFAULT_VERBOSE     0
#define FSK_DEFAULT_THRESH      0.50

typedef enum{
	FSK_DEMOD_SEARCH,
	FSK_DEMOD_ACQUIRE,
	FSK_DEMOD_DETECTED,
} fsk_demod_state_t;

typedef struct {
	int      verbose;
	size_t   samplerate;
	size_t   bitrate;
	size_t   bandwidth;
	size_t   bit_per_tone;
	size_t   tone_count;
	double  *tones;
	
	size_t   mod_samp_per_sym;
	size_t   demod_samp_per_fft;
	double  *mod_samples;
	size_t   mod_sampleslen;
	double   sym_freq;
	
	srcfft_t *srcfft;
	fsk_demod_state_t demod_state;
	
	size_t     demod_sync_loss;
	size_t     demod_fft_skip;
	double     demod_thresh;
	size_t     demod_databin;
	
	uint8_t    demod_bytes[2];
	uint8_t    demod_bit_count;
	uint8_t   *demod_data;
	size_t     demod_datalen;
} fsk_t;


fsk_t *fsk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count);
void   fsk_destroy(fsk_t *modem);
int    fsk_set_thresh(fsk_t *modem, double thresh);
int    fsk_set_verbose(fsk_t *modem, int verbose);
void   fsk_printinfo(fsk_t *modem);
int    fsk_modulate(fsk_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen);
int    fsk_demodulate(fsk_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen);

#endif //__FSK_H__


#ifdef FSK_IMPLEMENTATION
#undef FSK_IMPLEMENTATION

#define FSK_OVERSAMPLE 4

fsk_t *fsk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count) {
	fsk_t *modem;
	size_t i;
	
	//Double check arguments
	if( samplerate < (bandwidth * 2) ) { return 0; }
	if( tone_count < 2 ) { return 0; }
	
	modem = (fsk_t*)malloc(sizeof(fsk_t));
	if( !modem ) { goto fsk_init_error; }
	memset(modem,0,sizeof(fsk_t));
	
	modem->verbose = FSK_DEFAULT_VERBOSE;
	
	modem->bit_per_tone = 1;
	while( 1<<modem->bit_per_tone < tone_count ) {
		modem->bit_per_tone++;
	}
	modem->tone_count = (1 << modem->bit_per_tone);
	
	modem->samplerate = samplerate;
	modem->bitrate = bitrate;
	modem->bandwidth = bandwidth;
	
	modem->tones = (double*)malloc(sizeof(double)*modem->tone_count);
	if( !modem->tones ) { goto fsk_init_error; }
	
	//Samples to produce per clock cycle on modulation
	modem->sym_freq = ((double)bitrate / (double)modem->bit_per_tone) / 2;
	modem->mod_samp_per_sym = (double)samplerate / modem->sym_freq;
	if( modem->mod_samp_per_sym < 2 ) { goto fsk_init_error; }
	//Make sure that we are measureing the signal fast enough to 
	//catch as least FSK_OVERSAMPLE measurements of both the 
	//clk and the data
	modem->demod_samp_per_fft = modem->mod_samp_per_sym/FSK_OVERSAMPLE;
	
	//Create the Samplerate converting FFT object
	modem->srcfft = srcfft_init(samplerate,modem->demod_samp_per_fft,bandwidth,modem->tone_count);
	if( !modem->srcfft ) { goto fsk_init_error; }
	modem->demod_thresh = FSK_DEFAULT_THRESH;
	
	//Calculate optimal frequencies to use
	if( fskcalibrate(modem->tones, modem->tone_count, modem->srcfft, 
	                 modem->samplerate, modem->bandwidth, modem->demod_thresh) ) {
		goto fsk_init_error;
	}
	
	modem->demod_state = FSK_DEMOD_SEARCH;
	
	return modem;
	
	fsk_init_error:
	fsk_destroy(modem);
	return 0;
}

void fsk_destroy(fsk_t *modem) {
	if( modem ) {
		if( modem->tones ) { free(modem->tones); }
		if( modem->srcfft ) { srcfft_destroy(modem->srcfft); }
		if( modem->mod_samples ) { free(modem->mod_samples); }
		if( modem->demod_data ) { free(modem->demod_data); }
		memset(modem,0,sizeof(fsk_t));
		free(modem);
	}
}


int fsk_set_thresh(fsk_t *modem, double thresh) {
	if( !modem ) { return -1; }
	modem->demod_thresh = thresh;
	if( fskcalibrate(modem->tones, modem->tone_count, modem->srcfft, 
	                 modem->samplerate, modem->bandwidth, modem->demod_thresh) ) {
		return -1;
	}
	return 0;
}

int fsk_set_verbose(fsk_t *modem, int verbose) {
	if( !modem ) { return -1; }
	modem->verbose = verbose;
	return 0;
}

void fsk_printinfo(fsk_t *modem) {
	size_t i;
	size_t j;
	printf("FSK Modem:\n");
	printf("  Verbose               :  %d\n",modem->verbose);
	printf("  Samplerate            : %zu\n",modem->samplerate);
	printf("  Bitrate               : %zu bps\n",modem->bitrate);
	printf("  Bandwidth             : %zu Hz\n",modem->bandwidth);
	printf("  Bits per Symbol       : %zu\n",modem->bit_per_tone);
	printf("  Samples per Symbol    : %zu\n",modem->mod_samp_per_sym);
	printf("  Demod Samples per FFT : %zu\n",modem->demod_samp_per_fft);
	printf("  Tones(%zu):\n",modem->tone_count);
	for( i=0; i<modem->tone_count; i++ ) {
		printf("    0x%02lx: %04.1lf Hz\n",i,modem->tones[i]);
	}
}

int fsk_modulate(fsk_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen) {
	size_t symbol_idx;
	size_t symbol_count;
	size_t sample_count;
	size_t bit_idx;
	size_t ii;
	size_t mod_sampleslen;
	double *mod_samples;
	int sym;
	double amp;
	
	if( !modem ) { return -1; }
	if( !samples ) { return -1; }
	if( !sampleslen ) { return -1; }
	if( !data ) { return -1; }
	
	if( modem->verbose ) {
		printf("fsk_modulate(...):\n");
		printf("  Data: ");
		for( ii=0; ii<datalen; ii++ ) {
			printf("%02x ",data[ii]);
		}
	}
	
	symbol_count = (datalen*8) / modem->bit_per_tone;
	if( (datalen*8) % (modem->bit_per_tone) ) {
		symbol_count++;
	}
	
	if( modem->verbose ) {
		printf("  Symbol count: %zu\n",symbol_count);
	}
	
	mod_sampleslen = modem->mod_samp_per_sym*symbol_count;
	mod_samples = (double*)realloc(modem->mod_samples,sizeof(double)*mod_sampleslen);
	if( !mod_samples ) {
		goto fsk_modulate_error;
	}
	modem->mod_samples = mod_samples;
	modem->mod_sampleslen = mod_sampleslen;
	
	ii = 0;
	bit_idx = 0;
	for( symbol_idx=0; symbol_idx<symbol_count; symbol_idx++ ) {
		//Generate a bit of data
		sym = getbits(data, datalen, bit_idx, modem->bit_per_tone);
		if( modem->verbose ) {
			printf("  Symbol[%zu]=0x%02x modulated to frequency %04.1lf Hz\n",symbol_idx,sym,modem->tones[sym]);
		}
		bit_idx = bit_idx + modem->bit_per_tone;
		for( sample_count=0; sample_count<modem->mod_samp_per_sym; sample_count++ ) {
			mod_samples[ii] = sin(2*M_PI*modem->tones[sym]*ii/modem->samplerate) *
			                  sin(2*M_PI*modem->sym_freq*ii/modem->samplerate);
			ii++;
		}
	}
	
	*samples = mod_samples;
	*sampleslen = mod_sampleslen;
	return 0;
	
	fsk_modulate_error:
	*samples = 0;
	*sampleslen = 0;
	return -1;
}

int fsk_demodulate(fsk_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen) {
	size_t   ii;
	uint8_t *tmpdata;
	int      sym;
	srcfft_status_t result;
	
	if( !modem ) { return -1; }
	if( !data ) { return -1; }
	if( !datalen ) { return -1; }
	if( !samples ) { return -1; }
	
	if( modem->verbose ) {
		printf("fsk_demodulate(...)\n");
	}
	
	modem->demod_datalen = 0;
	*data = modem->demod_data;
	*datalen = modem->demod_datalen;
	
	ii = 0;
	while( ii < sampleslen ) {
		result = srcfft_process(modem->srcfft,samples+ii,sampleslen-ii);
		ii = ii + modem->srcfft->used_samples;
		if( result == SRCFFT_ERROR ) { 
			if( modem->verbose ) {
				printf("  FFT failed\n");
				return -1; 
			}
		}
		else if( result == SRCFFT_NEED_MORE ) {
			continue;
		}
		if( modem->verbose ) {
			srcfft_printresult(modem->srcfft);
		}
		
		//Check for signal
		if( !modem->srcfft->detectlen ) {
			//Not a definate peak
			if( modem->demod_state != FSK_DEMOD_SEARCH ) {
				modem->demod_sync_loss++;
				if( modem->demod_sync_loss >= FSK_OVERSAMPLE ) {
					if( modem->verbose ) {
						printf("  Sync lost\n");
					}
					modem->demod_state = FSK_DEMOD_SEARCH;
				}
			}
			continue;
		}
		
		modem->demod_sync_loss = 0;
		sym = modem->srcfft->maxbin;
		if( modem->demod_state == FSK_DEMOD_SEARCH ) {
			//First sample of a new data
			modem->demod_state = FSK_DEMOD_ACQUIRE;
			modem->demod_databin = sym;
		}
		else if( modem->demod_state == FSK_DEMOD_ACQUIRE ) {
			//Possible second sample of data
			if( modem->demod_databin != sym ) {
				//Not the data we expected, see if we get this one twice
				modem->demod_state = FSK_DEMOD_ACQUIRE;
				modem->demod_databin = sym;
			}
			else {
				//Detected data - reset state to look for next symbol
				modem->demod_state = FSK_DEMOD_DETECTED;
				modem->demod_fft_skip = FSK_OVERSAMPLE - 2;
				if( modem->verbose ) {
					printf("  Found data 0x%02x\n",sym);
				}
				
				putbits(modem->demod_bytes, sizeof(modem->demod_bytes), modem->demod_bit_count, modem->bit_per_tone, sym);
				modem->demod_bit_count = modem->demod_bit_count + modem->bit_per_tone;
				if( modem->demod_bit_count >= 8 ) {
					//Push a demodulated byte
					tmpdata = (uint8_t*)realloc(modem->demod_data,sizeof(uint8_t)*(modem->demod_datalen+1));
					if( !tmpdata ) {
						if( modem->verbose ) {
							printf("      Failed to reallocate data buffer\n");
						}
						return -1;
					}
					modem->demod_data = tmpdata;
					modem->demod_data[modem->demod_datalen] = modem->demod_bytes[0];
					modem->demod_datalen++;
					modem->demod_bytes[0] = modem->demod_bytes[1];
					modem->demod_bytes[1] = 0;
					modem->demod_bit_count = modem->demod_bit_count - 8;
				}
			}
		}
		else if( modem->demod_state == FSK_DEMOD_DETECTED ) {
			if( modem->demod_databin != sym ) {
				//Changed before we expected
				modem->demod_fft_skip = 0;
				modem->demod_state = FSK_DEMOD_ACQUIRE;
				modem->demod_databin = sym;
			}
			else {
				modem->demod_fft_skip--;
				if( !modem->demod_fft_skip ) {
					modem->demod_state = FSK_DEMOD_SEARCH;
				}
			}
		}
	}
	*data = modem->demod_data;
	*datalen = modem->demod_datalen;
	if( modem->verbose ) {
		printf("  Data: ");
		for( ii=0; ii<modem->demod_datalen; ii++ ) {
			printf("%02x ",modem->demod_data[ii]);
		}
		printf("\n");
	}
	return 0;
}


#endif //FSK_IMPLEMENTATION
