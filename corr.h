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
#ifndef __corr_H__
#define __corr_H__

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "bitops.h"

#define CORR_DEFAULT_VERBOSE     0
#define CORR_DEFAULT_THRESH      0.90

typedef enum{
	corr_DEMOD_SEARCH,
	corr_DEMOD_ACQUIRE,
	corr_DEMOD_DETECTED,
} corr_demod_state_t;

typedef struct {
	double* samples;
	size_t  len;
} corr_sym_t;

typedef struct {
	int         verbose;
	corr_sym_t *symbols;
	size_t      symbol_count;
	size_t      bit_per_sym;
	
	double     *symbol_thresh;

	double     *mod_samples;
	size_t      mod_sampleslen;

	double     *demod_buffer;
	size_t      demod_bufferalloc;
	size_t      demod_bufferoff;
	
	uint8_t     demod_bytes[2];
	uint8_t     demod_bit_count;
	uint8_t    *demod_data;
	size_t      demod_datalen;
} corr_t;


corr_t *corr_init(corr_sym_t *symbols, size_t symbol_count);
corr_t *corr_fsk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count);
corr_t *corr_psk_init(size_t samplerate, size_t bitrate, double frequency, size_t symbol_count);
void   corr_destroy(corr_t *modem);
int    corr_set_thresh(corr_t *modem, double thresh);
int    corr_set_verbose(corr_t *modem, int verbose);
void   corr_printinfo(corr_t *modem);
int    corr_modulate(corr_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen);
int    corr_demodulate(corr_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen);

#endif //__corr_H__


#ifdef CORR_IMPLEMENTATION
#undef CORR_IMPLEMENTATION

#define corr_OVERSAMPLE 4

corr_t *corr_init(corr_sym_t *symbols, size_t symbol_count) {
	corr_t *modem;
	size_t i;
	
	//Double check arguments
	if( !symbols ) { return 0; }
	if( symbol_count < 2 ) { return 0; }
	
	modem = (corr_t*)malloc(sizeof(corr_t));
	if( !modem ) { goto corr_init_error; }
	memset(modem,0,sizeof(corr_t));
	
	modem->verbose = CORR_DEFAULT_VERBOSE;
	modem->symbols = symbols;
	
	modem->bit_per_sym = 1;
	while( 1<<modem->bit_per_sym < symbol_count ) {
		modem->bit_per_sym++;
	}
	modem->symbol_count = (1 << modem->bit_per_sym);
	
	modem->symbol_thresh = (double*)malloc(sizeof(double)*modem->symbol_count);
	if( !modem->symbol_thresh ) { goto corr_init_error; }
	
	if( corr_set_thresh(modem,CORR_DEFAULT_THRESH) ) {
		goto corr_init_error;
	}
	
	modem->demod_bufferalloc = 0;
	for( i=0; i<modem->symbol_count; i++ ) {
		if( modem->demod_bufferalloc < modem->symbols[i].len ) {
			modem->demod_bufferalloc = modem->symbols[i].len;
		}
	}
	if( !modem->demod_bufferalloc ) {
		goto corr_init_error;
	}
	modem->demod_buffer = (double*)malloc(sizeof(double)*modem->demod_bufferalloc);
	if( !modem->demod_buffer ) {
		goto corr_init_error;
	}
	for( i=0; i<modem->demod_bufferalloc; i++ ) {
		modem->demod_buffer[i] = 0.0;
	}
	
	return modem;
	
	corr_init_error:
	corr_destroy(modem);
	return 0;
}

corr_t *corr_fsk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count) {
	corr_sym_t *symbols = 0;
	size_t ii;
	size_t j;
	size_t bit_per_sym;
	double freq_step;
	double freq;
	double sym_freq;
	size_t samp_per_sym;
		
	if( samplerate < bandwidth*2 ) { return 0; }
	if( tone_count < 2 ) { return 0; }
	
	symbols = (corr_sym_t*)malloc(sizeof(corr_sym_t)*tone_count);
	if( !symbols ) { goto corr_fsk_init_error; }
	memset(symbols,0,sizeof(corr_sym_t)*tone_count);
	
	bit_per_sym = 1;
	while( 1<<bit_per_sym < tone_count ) {
		bit_per_sym++;
	}
	tone_count = (1 << bit_per_sym);
	sym_freq = ((double)bitrate / (double)bit_per_sym);
	samp_per_sym = (double)samplerate / sym_freq;
	
	freq_step = bandwidth/tone_count;
	freq = freq_step/2;
	for( j=0; j<tone_count; j++ ) {
		symbols[j].samples = (double*)malloc(sizeof(double)*samp_per_sym);
		if( !symbols[j].samples ) {
			goto corr_fsk_init_error;
		}
		symbols[j].len = samp_per_sym;
		for( ii=0; ii<samp_per_sym; ii++ ) {
			symbols[j].samples[ii] = sin(2*M_PI*freq*ii/samplerate) *
			                         sin(2*M_PI*sym_freq*ii/samplerate);
		}
		freq = freq + freq_step;
	}
	
	return corr_init(symbols,tone_count);
	
	corr_fsk_init_error:
	if( symbols ) { 
		free(symbols); }
	return 0;
}

corr_t *corr_psk_init(size_t samplerate, size_t bitrate, double frequency, size_t symbol_count) {
	corr_sym_t *symbols = 0;
	size_t ii;
	size_t j;
	size_t bit_per_sym;
	double ang_step;
	double ang;
	double sym_freq;
	size_t samp_per_sym;
		
	if( samplerate < frequency*2 ) { return 0; }
	if( symbol_count < 2 ) { return 0; }
	
	symbols = (corr_sym_t*)malloc(sizeof(corr_sym_t)*symbol_count);
	if( !symbols ) { goto corr_psk_init_error; }
	memset(symbols,0,sizeof(corr_sym_t)*symbol_count);
	
	bit_per_sym = 1;
	while( 1<<bit_per_sym < symbol_count ) {
		bit_per_sym++;
	}
	symbol_count = (1 << bit_per_sym);
	sym_freq = ((double)bitrate / (double)bit_per_sym);
	samp_per_sym = (double)samplerate / sym_freq;
	
	ang_step = (2*M_PI)/symbol_count;
	ang = 0;
	for( j=0; j<symbol_count; j++ ) {
		symbols[j].samples = (double*)malloc(sizeof(double)*samp_per_sym);
		if( !symbols[j].samples ) {
			goto corr_psk_init_error;
		}
		symbols[j].len = samp_per_sym;
		for( ii=0; ii<samp_per_sym; ii++ ) {
			symbols[j].samples[ii] = sin(2*M_PI*frequency*ii/samplerate + ang) *
			                         sin(2*M_PI*sym_freq*ii/samplerate);
		}
		ang = ang + ang_step;
	}
	
	return corr_init(symbols,symbol_count);
	
	corr_psk_init_error:
	if( symbols ) { 
		free(symbols); }
	return 0;
}

corr_t *corr_fpsk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t symbol_count) {
	corr_sym_t *symbols = 0;
	size_t ii;
	size_t j,k;
	size_t bit_per_sym;
	double freq_step;
	double freq;
	double ang_step;
	double ang;
	double sym_freq;
	size_t samp_per_sym;
	size_t tone_count;
	size_t ang_count;
	size_t sym;
		
	if( samplerate < bandwidth*2 ) { return 0; }
	if( symbol_count < 2 ) { return 0; }
	
	symbols = (corr_sym_t*)malloc(sizeof(corr_sym_t)*symbol_count);
	if( !symbols ) { goto corr_fpsk_init_error; }
	memset(symbols,0,sizeof(corr_sym_t)*symbol_count);
	
	bit_per_sym = 1;
	while( 1<<bit_per_sym < symbol_count ) {
		bit_per_sym++;
	}
	symbol_count = (1 << bit_per_sym);
	
	if( symbol_count > 8 ) {
		ang_count = 4;
	}
	else if( symbol_count >= 4 ) {
		ang_count = 2;
	} else {
		ang_count = 1;
	}
	tone_count = symbol_count / ang_count;
	
	sym_freq = ((double)bitrate / (double)bit_per_sym) / 2;
	samp_per_sym = (double)samplerate / sym_freq;
	
	freq_step = bandwidth/tone_count;
	ang_step = (2*M_PI)/ang_count;
	ang = 0;
	sym = 0;
	for( j=0; j<ang_count; j++ ) {
		freq = freq_step/2;
		for( k=0; k<tone_count; k++ ) {
			symbols[sym].samples = (double*)malloc(sizeof(double)*samp_per_sym);
			if( !symbols[sym].samples ) {
				goto corr_fpsk_init_error;
			}
			symbols[sym].len = samp_per_sym;
			//printf("Symbol 0x%02x %0.1lf Hz %0.1lf rad\n",sym,freq,ang);
			for( ii=0; ii<samp_per_sym; ii++ ) {
				symbols[sym].samples[ii] = sin(2*M_PI*freq*ii/samplerate + ang) *
										   sin(2*M_PI*sym_freq*ii/samplerate);
			}
			freq = freq + freq_step;
			sym++;
		}
		ang = ang + ang_step;
	}
	
	return corr_init(symbols,symbol_count);
	
	corr_fpsk_init_error:
	if( symbols ) { 
		free(symbols); }
	return 0;
}

void corr_destroy(corr_t *modem) {
	if( modem ) {
		if( modem->symbols ) { free(modem->symbols); }
		if( modem->symbol_thresh ) { free(modem->symbol_thresh); }
		if( modem->mod_samples ) { free(modem->mod_samples); }
		if( modem->demod_buffer ) { free(modem->demod_buffer); }
		if( modem->demod_data ) { free(modem->demod_data); }
		memset(modem,0,sizeof(corr_t));
		free(modem);
	}
}


int corr_set_thresh(corr_t *modem, double thresh) {
	size_t i,ii;
	double corr;
	
	if( !modem ) { return -1; }
	if( thresh <= 0.0 || thresh > 1.0 ) { return -1; }
	
	for( i=0; i<modem->symbol_count; i++ ) {
		corr = 0.0;
		for( ii=0; ii<modem->symbols[i].len; ii++ ) {
			corr += modem->symbols[i].samples[ii] * modem->symbols[i].samples[ii];
		}
		modem->symbol_thresh[i] = corr * thresh;
	}
	
	return 0;
}

int corr_set_verbose(corr_t *modem, int verbose) {
	if( !modem ) { return -1; }
	modem->verbose = verbose;
	return 0;
}

void corr_printinfo(corr_t *modem) {
	size_t i;
	printf("Generic Corrleation Modem:\n");
	printf("  Verbose         :  %d\n",modem->verbose);
	printf("  Bits per Symbol : %zu\n",modem->bit_per_sym);
	printf("  Symbols(%zu)    :\n",modem->symbol_count);
	for( i=0; i<modem->symbol_count; i++ ) {
		printf("    0x%02lx: %zu samples\n",i,modem->symbols[i].len);
	}
}

int corr_modulate(corr_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen) {
	size_t symbol_idx;
	size_t symbol_count;
	int    sym;
	size_t bit_idx;
	size_t ii;
	size_t jj;
	size_t mod_sampleslen;
	double *mod_samples;
	
	if( !modem ) { return -1; }
	if( !samples ) { return -1; }
	if( !sampleslen ) { return -1; }
	if( !data ) { return -1; }
	
	if( modem->verbose ) {
		printf("corr_modulate(...):\n");
		printf("  Data: ");
		for( ii=0; ii<datalen; ii++ ) {
			printf("%02x ",data[ii]);
		}
	}
	
	symbol_count = (datalen*8) / modem->bit_per_sym;
	if( (datalen*8) % (modem->bit_per_sym) ) {
		symbol_count++;
	}
	
	if( modem->verbose ) {
		printf("  Symbol count: %zu\n",symbol_count);
	}
	
	ii = 0;
	bit_idx = 0;
	mod_sampleslen = 0;
	for( symbol_idx=0; symbol_idx<symbol_count; symbol_idx++ ) {
		//Get the next symbol bits
		sym = getbits(data, datalen, bit_idx, modem->bit_per_sym);
		if( modem->verbose ) {
			printf("  Symbol[%zu]=0x%02x modulated to %zu samples\n",symbol_idx,sym,modem->symbols[sym].len);
		}
		bit_idx = bit_idx + modem->bit_per_sym;
		
		//Allocate enough space for the next symbol
		mod_sampleslen = mod_sampleslen + modem->symbols[sym].len;
		mod_samples = (double*)realloc(modem->mod_samples,sizeof(double)*mod_sampleslen);
		if( !mod_samples ) {
			goto corr_modulate_error;
		}
		modem->mod_samples = mod_samples;
		modem->mod_sampleslen = mod_sampleslen;
		
		//Gererate the symbol
		for( jj=0; jj<modem->symbols[sym].len; jj++ ) {
			mod_samples[ii] = modem->symbols[sym].samples[jj];
			ii++;
		}
	}
	
	*samples = mod_samples;
	*sampleslen = mod_sampleslen;
	return 0;
	
	corr_modulate_error:
	*samples = 0;
	*sampleslen = 0;
	return -1;
}

int corr_demodulate(corr_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen) {
	size_t   ii;
	size_t   jj;
	size_t   k;
	size_t   next;
	size_t   off;
	uint8_t *tmpdata;
	int      sym;
	double   corr;
	double   norm;
	double   maxcorr;
	
	if( !modem ) { return -1; }
	if( !data ) { return -1; }
	if( !datalen ) { return -1; }
	if( !samples ) { return -1; }
	
	if( modem->verbose ) {
		printf("corr_demodulate(...)\n");
	}
	
	modem->demod_datalen = 0;
	
	ii = 0;
	while( ii < sampleslen ) {
		modem->demod_buffer[modem->demod_bufferoff] = samples[ii++];
		
		next = modem->demod_bufferoff+1;
		if( next >= modem->demod_bufferalloc ) {
			next = 0;
		}
		
		sym = -1;
		maxcorr = 0;
		for( k=0; k<modem->symbol_count; k++ ) {
			corr = 0.0;
			off = next;
			for( jj=0; jj<modem->symbols[k].len; jj++ ) {
				corr += modem->symbols[k].samples[jj] * modem->demod_buffer[off];
				if( ++off >= modem->demod_bufferalloc ) { off = 0; }
			}
			//Normalized based upon per-symbol thresholds
			norm = corr / modem->symbol_thresh[k];
			if( norm >= 1.0 &&
			    norm > maxcorr ) {
			    maxcorr = norm;
			    sym = k;
				if( modem->verbose ) {
					printf("  Possible symbol: 0x%02x %0.1lf / %0.1lf\n",sym,corr,modem->symbol_thresh[k]);
				}
			}
		}
		
		if( sym >= 0 ) {
			if( modem->verbose ) {
				printf("  Symbol: 0x%02x\n",sym);
			}
			putbits(modem->demod_bytes, sizeof(modem->demod_bytes), modem->demod_bit_count, modem->bit_per_sym, sym);
			modem->demod_bit_count = modem->demod_bit_count + modem->bit_per_sym;
			if( modem->demod_bit_count >= 8 ) {
				//Push a demodulated byte
				tmpdata = (uint8_t*)realloc(modem->demod_data,sizeof(uint8_t)*(modem->demod_datalen+1));
				if( !tmpdata ) {
					if( modem->verbose ) {
						printf("    Failed to reallocate data buffer\n");
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
			//Dump all of the samples used to create this correlation
			off = next;
			for( jj=0; jj<modem->symbols[sym].len; jj++ ) {
				modem->demod_buffer[off] = 0.0;
				if( ++off >= modem->demod_bufferalloc ) { off = 0; }
			}
		}
		
		modem->demod_bufferoff = next;
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


#endif //CORR_IMPLEMENTATION
