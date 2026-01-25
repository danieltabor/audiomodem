#ifndef __OOK_H__
#define __OOK_H__

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "bitops.h"
#include "srcfft.h"

#define OOK_DEFAULT_VERBOSE      0
#define OOK_DEFAULT_THRESH       0.50

typedef enum{
	OOK_DEMOD_SEARCH,
	OOK_DEMOD_IDLE_ACQUIRE,
	OOK_DEMOD_IDLE_DETECTED,
	OOK_DEMOD_START_ACQUIRE,
	OOK_DEMOD_SKIP,
	OOK_DEMOD_DATA,
	OOK_DEMOD_BIT_READY,
} ook_demod_state_t;

typedef struct {
	int      verbose;
	size_t   samplerate;
	size_t   bitrate;
	double   frequency;
	
	size_t   mod_samp_per_sym;
	size_t   demod_samp_per_fft;
	double  *mod_samples;
	size_t   mod_sampleslen;
	
	srcfft_t *srcfft;
	ook_demod_state_t demod_state;
	
	size_t     demod_sync_loss;
	size_t     demod_fft_skip;
	
	int        demod_sym;
	uint8_t    demod_byte;
	uint8_t    demod_bit_count;
	uint8_t   *demod_data;
	size_t     demod_datalen;
} ook_t;


ook_t *ook_init(size_t samplerate, size_t bitrate, double frequency);
void   ook_destroy(ook_t *modem);
int    ook_set_thresh(ook_t *modem, double thresh);
int    ook_set_verbose(ook_t *modem, int verbose);
void   ook_printinfo(ook_t *modem);
int    ook_modulate(ook_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen);
int    ook_demodulate(ook_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen);

#endif //__OOK_H__


#ifdef OOK_IMPLEMENTATION
#undef OOK_IMPLEMENTATION

#define OOK_OVERSAMPLE 4

ook_t *ook_init(size_t samplerate, size_t bitrate, double frequency) {
	ook_t *modem;
	
	//Double check arguments
	if( samplerate <= (frequency * 2) ) { return 0; }
	
	modem = (ook_t*)malloc(sizeof(ook_t));
	if( !modem ) { goto ook_init_error; }
	memset(modem,0,sizeof(ook_t));
	
	modem->verbose = OOK_DEFAULT_VERBOSE;
	modem->samplerate = samplerate;
	modem->bitrate = bitrate;
	modem->frequency = frequency;
	
	modem->mod_samp_per_sym = (double)samplerate / (double)bitrate;
	//Make sure that we are measureing the signal fast enough to 
	//catch as least FSK_OVERSAMPLE measurements of both the 
	//clk and the data
	modem->demod_samp_per_fft = ((double)samplerate / (double)bitrate )/ (double)OOK_OVERSAMPLE;
	
	//Create the Samplerate converting FFT object
	modem->srcfft = srcfft_init(samplerate,modem->demod_samp_per_fft,samplerate/2,1);
	if( !modem->srcfft ) { goto ook_init_error; }
	if( ook_set_thresh(modem,OOK_DEFAULT_THRESH) ) {
		goto ook_init_error;
	}
	
	modem->demod_state = OOK_DEMOD_SEARCH;
	
	return modem;
	
	ook_init_error:
	ook_destroy(modem);
	return 0;
}

void ook_destroy(ook_t *modem) {
	if( modem ) {
		if( modem->srcfft) { srcfft_destroy(modem->srcfft); }
		if( modem->mod_samples ) { free(modem->mod_samples); }
		if( modem->demod_data ) { free(modem->demod_data); }
		memset(modem,0,sizeof(ook_t));
		free(modem);
	}
}


int ook_set_thresh(ook_t *modem, double thresh) {
	size_t ii;
	size_t k;
	size_t sampleslen = 0;
	double *samples = 0;
	srcfft_status_t result;
	
	if( modem->verbose ) {
		printf("ook_set_thresh(...)\n");
	}
	
	sampleslen = modem->srcfft->srcinalloc;
	samples = (double*)malloc(sizeof(double)*sampleslen);
	if( !samples ) {
		if( modem->verbose ) {
			printf("  malloc failed");
		}
		goto ook_set_thresh_error;
	}
	
	if( srcfft_reset(modem->srcfft) ) {
		if( modem->verbose ) {
			printf("    Failed to reset srcfft\n");
		}
		goto ook_set_thresh_error;
	}
	
	ii = 0;
	do {
		//Generate Tone
		for( k=0; k<sampleslen; k++ ) {
			samples[k] = thresh*sin(2*M_PI*modem->frequency*ii/modem->samplerate);
			ii++;
		}
		//Execute FFT
		result = srcfft_process(modem->srcfft,samples,sampleslen);
		if( result == SRCFFT_ERROR || 
			result == SRCFFT_NEED_MORE && modem->srcfft->used_samples != sampleslen ) {
			if( modem->verbose ) {
				printf("  Failed to process samples\n");
			}
			goto ook_set_thresh_error;
		}
	} while( result == SRCFFT_NEED_MORE );
	
	if( srcfft_set_thresh(modem->srcfft,thresh*modem->srcfft->maxmag) ) {
		goto ook_set_thresh_error;
	}
	
	if( samples ) { free(samples); }
	if( srcfft_reset(modem->srcfft) ) {
		if( modem->verbose ) {
			printf("  Failed to reset srcfft\n");
		}
		goto ook_set_thresh_error;
	}
	return 0;
	
	ook_set_thresh_error:
	if( samples ) { free(samples); }
	(void)srcfft_reset(modem->srcfft);
	return -1;
}

int ook_set_verbose(ook_t *modem, int verbose) {
	if( !modem ) { return -1; }
	modem->verbose = verbose;
	return 0;
}

void ook_printinfo(ook_t *modem) {
	size_t i;
	size_t j;
	printf("Tone Modem:\n");
	printf("  Verbose            :  %d\n",modem->verbose);
	printf("  Samplerate         : %zu\n",modem->samplerate);
	printf("  Bitrate            : %zu bps\n",modem->bitrate);
	printf("  Samples per Symbol : %zu\n",modem->mod_samp_per_sym);
	printf("  Samples per FFT    : %zu\n",modem->demod_samp_per_fft);
	printf("  Frequency          : %0.1lf Hz\n",modem->frequency);
}

int ook_modulate(ook_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen) {
	size_t byte_idx;
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
		printf("ook_modulate(...):\n");
		printf("  Data: ");
		for( ii=0; ii<datalen; ii++ ) {
			printf("%02x ",data[ii]);
		}
	}
	
	//To sync receive star
	//  1 symbol of tone (idle)
	//All bytes will have
	//  1 symbol of none (start bit)
	//  8 symboles of tone/none (data)
	//  1 symbol of tone (stop)
	symbol_count = 11 + ((datalen-1)*10);
	if( modem->verbose ) {
		printf("  Symbol count: %zu\n",symbol_count);
	}
	
	mod_sampleslen = modem->mod_samp_per_sym*symbol_count;
	mod_samples = (double*)realloc(modem->mod_samples,sizeof(double)*mod_sampleslen);
	if( !mod_samples ) {
		goto ook_modulate_error;
	}
	modem->mod_samples = mod_samples;
	modem->mod_sampleslen = mod_sampleslen;
	
	ii = 0;
	//Generate one symbol of idle
	if( modem->verbose ) {
		printf("-\n");
	}
	for( sample_count=0; sample_count<modem->mod_samp_per_sym; sample_count++ ) {
		mod_samples[ii] = sin(2*M_PI*modem->frequency*ii/modem->samplerate);
		ii++;
	}
	for( byte_idx=0; byte_idx<datalen; byte_idx++ ) {
		//Generate start bit
		if( modem->verbose ) {
			printf("_");
		}
		for( sample_count=0; sample_count<modem->mod_samp_per_sym; sample_count++ ) {
			mod_samples[ii] = 0;
			ii++;
		}
		//Generate data bits
		for( bit_idx=0; bit_idx<8; bit_idx++ ) {
			sym = (data[byte_idx]>>bit_idx)&1;
			if( sym ) {
				if( modem->verbose ) {
					printf("_");
				}
				for( sample_count=0; sample_count<modem->mod_samp_per_sym; sample_count++ ) {
					mod_samples[ii] = 0;
					ii++;
				}
			}
			else {
				if( modem->verbose ) {
					printf("-");
				}
				for( sample_count=0; sample_count<modem->mod_samp_per_sym; sample_count++ ) {
					mod_samples[ii] = sin(2*M_PI*modem->frequency*ii/modem->samplerate);
					ii++;
				}
			}
		}
		//Generate Stop bit
		if( modem->verbose ) {
			printf("-\n");
		}
		for( sample_count=0; sample_count<modem->mod_samp_per_sym; sample_count++ ) {
			mod_samples[ii] = sin(2*M_PI*modem->frequency*ii/modem->samplerate);
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


int ook_demodulate(ook_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen) {
	size_t   ii;
	int tone_detected;
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
		if( modem->verbose ) {
			srcfft_printresult(modem->srcfft);
		}
		
		//Check for signal
		tone_detected = modem->srcfft->detectlen;
		
		if( modem->demod_state == OOK_DEMOD_SEARCH ) {
			if( tone_detected ) {
				//First sample (idle)
				modem->demod_state = OOK_DEMOD_IDLE_ACQUIRE;
			}
		}
		else if( modem->demod_state == OOK_DEMOD_IDLE_ACQUIRE ) {
			if( tone_detected ) {
				modem->demod_state = OOK_DEMOD_IDLE_DETECTED;
				if( modem->verbose ) {
					printf("      Sync detected\n");
				}
			}
			else {
				//False Sync
				modem->demod_state = OOK_DEMOD_SEARCH;
			}
		}
		else if( modem->demod_state == OOK_DEMOD_IDLE_DETECTED ) {
			if( !tone_detected ) {
				//First sample (start)
				modem->demod_state = OOK_DEMOD_START_ACQUIRE;
			}
		}
		else if( modem->demod_state == OOK_DEMOD_START_ACQUIRE ) {
			if( tone_detected ) {
				//False Start
				modem->demod_state = OOK_DEMOD_SEARCH;
				if( modem->verbose ) {
					printf("      False Sync\n");
				}
			}
			else {
				if( modem->verbose ) {
					printf("      Start Byte detected\n");
				}
				modem->demod_state = OOK_DEMOD_SKIP;
				modem->demod_fft_skip = OOK_OVERSAMPLE - 2;
				modem->demod_sym = 1;
			}
		}
		else if( modem->demod_state == OOK_DEMOD_SKIP ) {
			if( ( tone_detected &&  modem->demod_sym) ||
			    (!tone_detected && !modem->demod_sym) ) {
				//Change sooner than expected
				if( tone_detected ) {
					modem->demod_sym = 0;
				}
				else {
					modem->demod_sym = 1;
				}
				modem->demod_state = OOK_DEMOD_BIT_READY;
				if( modem->verbose ) {
					printf("    Early Change\n");
				}
			}
			else {
				modem->demod_fft_skip--;
				if( !modem->demod_fft_skip ) {
					modem->demod_state = OOK_DEMOD_DATA;
				}
				if( modem->verbose ) {
					printf("    Ignored (%zu left)\n",modem->demod_fft_skip);
				}
			}
		}
		else if( modem->demod_state == OOK_DEMOD_DATA ) {
			if( tone_detected ) {
				modem->demod_sym = 0;
			}
			else {
				modem->demod_sym = 1;
			}
			modem->demod_state = OOK_DEMOD_BIT_READY;
		}
		
		
		if( modem->demod_state == OOK_DEMOD_BIT_READY ) {
			if( modem->verbose ) {
				printf("    Bit: %d\n",modem->demod_sym);
			}
			modem->demod_byte = (modem->demod_byte >> 1) | (modem->demod_sym<<7);
			modem->demod_bit_count++;
			if( modem->demod_bit_count >= 8 ) {
				//Push a demodulated byte
				if( modem->verbose ) {
					printf("    Data Byte: %02x\n",modem->demod_byte);
				}
				tmpdata = (uint8_t*)realloc(modem->demod_data,sizeof(uint8_t)*(modem->demod_datalen+1));
				if( !tmpdata ) {
					if( modem->verbose ) {
						printf("      Failed to reallocate data buffer\n");
					}
					return -1;
				}
				modem->demod_data = tmpdata;
				modem->demod_data[modem->demod_datalen] = modem->demod_byte;
				modem->demod_datalen++;
				modem->demod_byte = 0;
				modem->demod_bit_count = 0;

				modem->demod_state = OOK_DEMOD_SEARCH;
				modem->demod_sync_loss = 0;
				modem->demod_fft_skip = 0;
			}
			else {
				modem->demod_state = OOK_DEMOD_SKIP;
				modem->demod_sync_loss = 0;
				modem->demod_fft_skip = OOK_OVERSAMPLE - 1;
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


#endif //OOK_IMPLEMENTATION
