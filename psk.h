#ifndef __PSK_H__
#define __PSK_H__

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "bitops.h"
#include "srcfft.h"

#define PSK_DEFAULT_VERBOSE     0
#define PSK_DEFAULT_THRESH      0.75

typedef enum{
	PSK_DEMOD_BASE_SEARCH,
	PSK_DEMOD_BASE_ACQUIRE,
	PSK_DEMOD_BASE_DETECTED,
	PSK_DEMOD_DATA_SEARCH,
	PSK_DEMOD_DATA_ACQUIRE,
	PSK_DEMOD_DATA_DETECTED,
} psk_demod_state_t;

typedef struct {
	int      verbose;
	size_t   samplerate;
	size_t   bitrate;
	size_t   bandwidth;
	double   frequency;
	size_t   bit_per_symbol;
	size_t   symbol_count;
	
	double   sym_freq;
	size_t   mod_samp_per_sym;
	size_t   demod_samp_per_fft;
	size_t   demod_fft_per_sym;
	double  *mod_samples;
	size_t   mod_sampleslen;
	
	srcfft_t *srcfft;
	psk_demod_state_t demod_state;
	size_t     demod_sync_loss;
	
	size_t     demod_capture_alloc;
	size_t     demod_capture_len;
	uint8_t   *demod_capture;
	
	size_t     demod_fftbin;
	double     demod_base_ang;
	double     demod_data_ang;
	size_t     demod_fft_count;
	uint8_t    demod_bytes[2];
	uint8_t    demod_bit_count;
	uint8_t   *demod_data;
	size_t     demod_datalen;
} psk_t;


psk_t *psk_init(size_t samplerate, size_t bitrate, size_t bandwidth, double frequency, size_t symbol_count);
void   psk_destroy(psk_t *modem);
int    psk_set_thresh(psk_t *modem, double thresh);
int    psk_set_verbose(psk_t *modem, int verbose);
void   psk_printinfo(psk_t *modem);
int    psk_modulate(psk_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen);
int    psk_demodulate(psk_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen);

#endif //__PSK_H__


#ifdef PSK_IMPLEMENTATION
#undef PSK_IMPLEMENTATION

psk_t *psk_init(size_t samplerate, size_t bitrate, size_t bandwidth, double frequency, size_t symbol_count) {
	psk_t *modem;
	size_t i;
	
	//Double check arguments
	if( samplerate < (bandwidth * 2) ) { return 0; }
	if( frequency * 2 > bandwidth ) { return 0; }
	if( symbol_count < 2 ) { return 0; }
	
	modem = (psk_t*)malloc(sizeof(psk_t));
	if( !modem ) { goto psk_init_error; }
	memset(modem,0,sizeof(psk_t));
	
	modem->verbose = PSK_DEFAULT_VERBOSE;
	
	modem->samplerate = samplerate;
	modem->bitrate = bitrate;
	modem->bandwidth = bandwidth;
	modem->frequency = frequency;

	modem->bit_per_symbol = 1;
	while( 1<<modem->bit_per_symbol < symbol_count ) {
		modem->bit_per_symbol++;
	}
	modem->symbol_count = (1 << modem->bit_per_symbol);
	
	//Samples to produce per clock cycle on modulation
	modem->sym_freq = ((double)bitrate / (double)modem->bit_per_symbol);
	modem->mod_samp_per_sym = (double)samplerate / modem->sym_freq;
	if( modem->mod_samp_per_sym < 2 ) { goto psk_init_error; }
	//Measure the signal every wave length
	modem->demod_samp_per_fft = samplerate / frequency;
	modem->demod_fft_per_sym = modem->mod_samp_per_sym / modem->demod_samp_per_fft;
	if( modem->demod_fft_per_sym < 1 ) { goto psk_init_error; }
	
	//Create the Samplerate converting FFT object
	modem->srcfft = srcfft_init(samplerate,modem->demod_samp_per_fft,bandwidth,0);
	if( !modem->srcfft ) { goto psk_init_error; }
	
	modem->demod_fftbin = modem->frequency * ((double)modem->srcfft->magalloc/(double)modem->bandwidth);
	if( psk_set_thresh(modem,PSK_DEFAULT_THRESH ) ) {
		goto psk_init_error;
	}
	
	modem->demod_state = PSK_DEMOD_BASE_SEARCH;
	
	return modem;
	
	psk_init_error:
	psk_destroy(modem);
	return 0;
}

void psk_destroy(psk_t *modem) {
	if( modem ) {
		if( modem->srcfft ) { srcfft_destroy(modem->srcfft); }
		if( modem->mod_samples ) { free(modem->mod_samples); }
		if( modem->demod_data ) { free(modem->demod_data); }
		memset(modem,0,sizeof(psk_t));
		free(modem);
	}
}


int psk_set_thresh(psk_t *modem, double thresh) {
	double *samples = 0;
	size_t  sampleslen;
	size_t  ii;
	size_t  j;
	srcfft_status_t result;
	
	if( !modem ) { goto psk_set_thresh_error; }
	
	sampleslen = modem->demod_samp_per_fft;
	samples = (double*)malloc(sizeof(double)*sampleslen);
	if( !samples ) { goto psk_set_thresh_error; }
	
	if( srcfft_reset(modem->srcfft) ) {
		goto psk_set_thresh_error;
	}
	
	ii = 0;
	do {
		for( j=0; j<sampleslen; j++ ) {
			samples[j] = sin(2*M_PI*modem->frequency*ii/modem->samplerate);
			ii++;
		}
		result = srcfft_process(modem->srcfft,samples,sampleslen);
	} while( result == SRCFFT_NEED_MORE );
	if( result == SRCFFT_ERROR ) {
		goto psk_set_thresh_error;
	}
	
	srcfft_set_thresh(modem->srcfft, modem->srcfft->mag[modem->demod_fftbin] * thresh);
	free(samples);
	return 0;
	
	psk_set_thresh_error:
	if( samples ) { free(samples); }
	return -1;
}

int psk_set_verbose(psk_t *modem, int verbose) {
	if( !modem ) { return -1; }
	modem->verbose = verbose;
	return 0;
}

void psk_printinfo(psk_t *modem) {
	size_t i;
	size_t j;
	printf("Tone Modem:\n");
	printf("  Verbose               : %d\n",modem->verbose);
	printf("  Samplerate            : %zu\n",modem->samplerate);
	printf("  Bitrate               : %zu bps\n",modem->bitrate);
	printf("  Bandwidth             : %zu Hz\n",modem->bandwidth);
	printf("  Samples per Symbol    : %zu\n",modem->mod_samp_per_sym);
	printf("  Demod Samples per FFT : %zu\n",modem->demod_samp_per_fft);
	printf("  Frequnecy             : %lf\n",modem->frequency);
}

int psk_modulate(psk_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen) {
	size_t symbol_idx;
	size_t symbol_count;
	size_t sample_count;
	size_t bit_idx;
	size_t ii;
	size_t mod_sampleslen;
	double *mod_samples;
	int sym;
	double ang;
	
	if( !modem ) { return -1; }
	if( !samples ) { return -1; }
	if( !sampleslen ) { return -1; }
	if( !data ) { return -1; }
	
	if( modem->verbose ) {
		printf("ook_modulate(...):\n");
		printf("  Data: ");
		for( ii=0; ii<datalen; ii++ ) {
			printf("%02x ",data[ii]);
		}
	}
	
	symbol_count = (datalen*8) / modem->bit_per_symbol;
	if( (datalen*8) % (modem->bit_per_symbol) ) {
		symbol_count++;
	}
	if( modem->verbose ) {
		printf("  Symbol count: %zu\n",symbol_count);
	}
	
	mod_sampleslen = modem->mod_samp_per_sym*2*symbol_count;
	mod_samples = (double*)realloc(modem->mod_samples,sizeof(double)*mod_sampleslen);
	if( !mod_samples ) {
		goto ook_modulate_error;
	}
	modem->mod_samples = mod_samples;
	modem->mod_sampleslen = mod_sampleslen;
	
	ii = 0;
	bit_idx = 0;
	//Generate one symbol of mark
	for( symbol_idx=0; symbol_idx<symbol_count; symbol_idx++ ) {
		for( sample_count=0; sample_count<modem->mod_samp_per_sym; sample_count++ ) {
			//Generate base tone (phase 0)
			mod_samples[ii] = sin(2*M_PI*modem->frequency*ii/modem->samplerate) *
							  sin(2*M_PI*modem->sym_freq*sample_count/modem->samplerate);
			ii++;
		}
		sym = getbits(data, datalen, bit_idx, modem->bit_per_symbol);
		bit_idx = bit_idx + modem->bit_per_symbol;
		
		ang = (2*M_PI) / (double)modem->symbol_count * sym;
		for( sample_count=0; sample_count<modem->mod_samp_per_sym; sample_count++ ) {
			//Generate base tone (phase 0)
			mod_samples[ii] = sin(2*M_PI*modem->frequency*ii/modem->samplerate + ang) *
							  sin(2*M_PI*modem->sym_freq*sample_count/modem->samplerate);
			ii++;
		}
	}
	
	*samples = mod_samples;
	*sampleslen = mod_sampleslen;
	return 0;
	
	ook_modulate_error:
	*samples = 0;
	*sampleslen = 0;
	return -1;
}


int psk_demodulate(psk_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen) {
	size_t   ii;
	size_t   j;
	int tone_detected;
	int sym;
	uint8_t *tmpdata;
	srcfft_status_t result;
	
	
	if( !modem ) { return -1; }
	if( !data ) { return -1; }
	if( !datalen ) { return -1; }
	if( !samples ) { return -1; }
	
	if( modem->verbose ) {
		printf("ook_demodulate(...)\n");
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
		//if( modem->verbose ) {
		//	srcfft_printresult(modem->srcfft);
		//}
		
		//Check for signal
		tone_detected = 0;
		for( j=0; j<modem->srcfft->detectlen; j++ ) {
			if( modem->srcfft->detect[j] == modem->demod_fftbin ) {
				tone_detected = 1;
				if( modem->verbose ) {
					printf("  Tone Detected with angle: %lf\n",modem->srcfft->ang[modem->demod_fftbin]);
				}
			}
		}
		
		if( !tone_detected ) {
			if( modem->demod_state != PSK_DEMOD_BASE_SEARCH ) {
				if( modem->demod_sync_loss >= modem->mod_samp_per_sym ) {
					if( modem->verbose ) {
						printf("  Sync lost\n");
					}
					modem->demod_state = PSK_DEMOD_BASE_SEARCH;
					modem->demod_fft_count = 0;
				}
			}
		}
		else {
			modem->demod_sync_loss = 0;
		}
		
		if( modem->demod_state == PSK_DEMOD_BASE_SEARCH ) {
			if( tone_detected ) {
				modem->demod_base_ang = modem->srcfft->ang[modem->demod_fftbin];
				modem->demod_fft_count = 1;
				modem->demod_state = PSK_DEMOD_BASE_ACQUIRE;
			}
		}
		else if( modem->demod_state == PSK_DEMOD_BASE_ACQUIRE ) {
			if( tone_detected ) {
				if( fabs(modem->srcfft->ang[modem->demod_fftbin] - modem->demod_base_ang) >
				      ((double)(2*M_PI) / (double)modem->symbol_count) ) {
					//Angle went off
					modem->demod_state = PSK_DEMOD_BASE_SEARCH;
					modem->demod_fft_count = 0;
				}
				else {
					if( modem->verbose ) {
						printf("      Base detected\n");
					}
					modem->demod_state = PSK_DEMOD_BASE_DETECTED;
					modem->demod_fft_count++;
				}
			}
			else {
				modem->demod_state = PSK_DEMOD_BASE_SEARCH;
				modem->demod_fft_count = 0;
			}
		}
		else if( modem->demod_state == PSK_DEMOD_BASE_DETECTED ) {
			modem->demod_fft_count++;
			if( modem->demod_fft_count >= modem->demod_fft_per_sym ) {
				modem->demod_state = PSK_DEMOD_DATA_SEARCH;
				modem->demod_fft_count = 0;
			}
			else if( tone_detected &&
			         fabs(modem->srcfft->ang[modem->demod_fftbin] - modem->demod_base_ang) >
			           ((double)(2*M_PI) / (double)modem->symbol_count) ) {
				//Phase changed dramatically change prematurely
				printf("Premature change from base\n");
				modem->demod_data_ang = modem->srcfft->ang[modem->demod_fftbin];
				modem->demod_state = PSK_DEMOD_DATA_ACQUIRE;
				modem->demod_fft_count = 1;
			}
		}
		else if( modem->demod_state == PSK_DEMOD_DATA_SEARCH ) {
			if( tone_detected ) {
				modem->demod_data_ang = modem->srcfft->ang[modem->demod_fftbin];
				modem->demod_state = PSK_DEMOD_DATA_ACQUIRE;
			}
		}
		else if( modem->demod_state == PSK_DEMOD_DATA_ACQUIRE ) {
			if( tone_detected ) {
				if( fabs(modem->srcfft->ang[modem->demod_fftbin] - modem->demod_data_ang) >
				    ((double)(2*M_PI) / (double)modem->symbol_count) ) {
					//Angle went off
					modem->demod_state = PSK_DEMOD_BASE_SEARCH;
					modem->demod_fft_count = 0;
				}
				else {
					if( modem->verbose ) {
						printf("      Data detected %lf / %lf\n",modem->demod_base_ang,modem->demod_data_ang);
					}
					modem->demod_state = PSK_DEMOD_DATA_DETECTED;
					modem->demod_fft_count++;
					
					sym = (int)( fabs(modem->demod_data_ang-modem->demod_base_ang) / ((double)(2*M_PI) / (double)modem->symbol_count) );
					putbits(modem->demod_bytes, sizeof(modem->demod_bytes), modem->demod_bit_count, modem->bit_per_symbol, sym);
					modem->demod_bit_count = modem->demod_bit_count + modem->bit_per_symbol;
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
			else {
				modem->demod_state = PSK_DEMOD_BASE_SEARCH;
				modem->demod_fft_count = 0;
			}
		}
		else if( modem->demod_state == PSK_DEMOD_DATA_DETECTED ) {
			modem->demod_fft_count++;
			if( modem->demod_fft_count >= modem->demod_fft_per_sym ) {
				modem->demod_state = PSK_DEMOD_DATA_SEARCH;
				modem->demod_fft_count = 0;
			}
			/*
			else if( tone_detected &&
			         fabs(modem->srcfft->ang[modem->demod_fftbin] - modem->demod_data_ang) >
			           ((double)(2*M_PI) / (double)modem->symbol_count) ) {
				//Phase changed dramatically change prematurely
				printf("Premature change from data\n");
				modem->demod_base_ang = modem->srcfft->ang[modem->demod_fftbin];
				modem->demod_state = PSK_DEMOD_BASE_ACQUIRE;
				modem->demod_fft_count = 1;
			}
			*/
		}
	}
	
	*data = modem->demod_data;
	*datalen = modem->demod_datalen;
	if( modem->verbose ) {
		printf("  Data: ");
		for( j=0; j<modem->demod_datalen; j++ ) {
			printf("%02x ",modem->demod_data[j]);
		}
		printf("\n");
	}
	return 0;
}

#endif //PSK_IMPLEMENTATION
