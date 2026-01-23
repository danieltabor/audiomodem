#ifndef __OOK_H__
#define __OOK_H__

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include <samplerate.h>
#include <fftw3.h>

#include "bitops.h"

#define OOK_DEFAULT_VERBOSE      0
#define OOK_DEFAULT_THRESH       0.90
#define OOK_FFT_BINS             10
#define OOK_REDUNDANCY            3


typedef enum{
	OOK_DEMOD_SEARCH,
	OOK_DEMOD_IDLE_ACQUIRE,
	OOK_DEMOD_IDLE_DETECTED,
	OOK_DEMOD_START_ACQUIRE,
	OOK_DEMOD_DATA
} ook_demod_state_t;

typedef struct {
	int      verbose;
	size_t   samplerate;
	size_t   bitrate;
	double   frequency;
	
	size_t   mod_samp_per_sym;
	double  *mod_samples;
	size_t   mod_sampleslen;
	
	ook_demod_state_t demod_state;
	
	size_t     demod_sync_skip;
	
	size_t     demod_bins;
	size_t     demod_freq_bin;
	fftw_plan  demod_plan;
	double    *demod_fftin;
	size_t     demod_fftinlen;
	size_t     demod_fftin_alloc;
	
	fftw_complex *demod_fftout;
	double    *demod_fftmag;
	size_t     demod_sync_loss;
	double     demod_norm_thresh;
	double     demod_thresh;
	
	uint8_t    demod_sym_vote;
	uint8_t    demod_sym_vote_count;
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
	
	/*
	//Samplerate conveter
	modem->demod_src = src_new(SRC_SINC_MEDIUM_QUALITY,1,0);
	if( !modem->demod_src ) { goto fskclk_init_error; }
	//Make sure we are analyzing chunks small enough to guarantee that we
	//will see a clock for at least 2 clean ffts and dat for at least 2 clean ffts
	modem->demod_srcin_alloc = modem->mod_mod_samp_per_sym / 8;
	modem->demod_srcin  = (float*)malloc(sizeof(float) * modem->demod_srcin_alloc);
	if( !modem->demod_srcin ) { goto ook_init_error; }
	modem->demod_srcout_alloc = modem->demod_mod_samp_per_sym / 8;
	modem->demod_srcout = (float*)malloc(sizeof(float) * modem->demod_srcout_alloc);
	if( !modem->demod_srcout ) { goto ook_init_error; }
	*/
	
	//Make sure we look at enough samples to get at least 
	//OOK_REDUNDANCY measurements for each symbol
	modem->demod_fftin_alloc = modem->mod_samp_per_sym / (OOK_REDUNDANCY+2);
	
	//FFT
	//Because these are real samples, only the first half of the fft results (DC->nyquist) will be valid
	//The second half (-nyquist->DC) will all be zero
	modem->demod_bins = modem->demod_fftin_alloc / 2;
	if( modem->demod_bins < 1 ) {
		//Too small run any meaningful FFT
		goto ook_init_error;
	}
	
	modem->demod_freq_bin = (size_t)(modem->frequency * (double)modem->demod_bins / ((double)modem->samplerate / 2.0));
	modem->demod_fftin  = (double*)fftw_malloc(sizeof(double) * modem->demod_fftin_alloc);
	if( !modem->demod_fftin ) { goto ook_init_error; }
	modem->demod_fftout = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * modem->demod_fftin_alloc);
	if( !modem->demod_fftout ) { goto ook_init_error; }
	modem->demod_plan   = fftw_plan_dft_r2c_1d(modem->demod_fftin_alloc, modem->demod_fftin, modem->demod_fftout,  FFTW_MEASURE);
	modem->demod_fftmag = (double*)malloc(sizeof(double)* modem->demod_bins);
	if( !modem->demod_fftmag ) { goto ook_init_error; }
	
	modem->demod_state = OOK_DEMOD_SEARCH;
	
	if( ook_set_thresh(modem,OOK_DEFAULT_THRESH) ) {
		goto ook_init_error;
	}
	
	return modem;
	
	ook_init_error:
	ook_destroy(modem);
	return 0;
}

void ook_destroy(ook_t *modem) {
	if( modem ) {
		if( modem->mod_samples ) { free(modem->mod_samples); }
		if( modem->demod_fftin ) { fftw_free(modem->demod_fftin); }
		if( modem->demod_fftout ) { fftw_free(modem->demod_fftout); }
		if( modem->demod_fftmag ) { free(modem->demod_fftmag); }
		if( modem->demod_data ) { free(modem->demod_data); }
		memset(modem,0,sizeof(ook_t));
		free(modem);
	}
}


int ook_set_thresh(ook_t *modem, double thresh) {
	size_t i;
	double mag;
	double maxmag;
	size_t maxbin;
	
	if( !modem ) { return -1; }
	if( thresh == modem->demod_norm_thresh ) { return 0; }
	
	//Execute an FFT on a pure tone to set non-normalized threshold
	for( i=0; i<modem->demod_fftin_alloc; i++ ) {
		modem->demod_fftin[i] = sin(2.0*M_PI*modem->frequency*i/modem->samplerate);
	}
	modem->demod_fftinlen = 0;
	fftw_execute(modem->demod_plan);
	
	maxmag = -1;
	for( i=0; i<modem->demod_bins; i++ ) {
		mag = sqrt(modem->demod_fftout[i][0] * modem->demod_fftout[i][0] + modem->demod_fftout[i][1] * modem->demod_fftout[i][1]);
		if( isnan(mag) || isinf(mag) ) {
			continue;
		}
		if( mag > maxmag ) {
			maxmag = mag;
			maxbin = i;
		}
	}
	
	if( maxmag <= 0 ) {
		return -1;
	}
	
	modem->demod_norm_thresh = thresh;
	modem->demod_thresh = maxmag * thresh;
	modem->demod_freq_bin = maxbin;
	
	return 0;
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
	size_t   i;
	double   mag;
	int      tone_detected;
	uint8_t *tmpdata;
	int      bit;
	
	
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
		//Skip samples to synchonize to a symbol
		//if( modem->demod_sync_skip && modem->verbose ) {
		//	printf("  Skip %zu of %zu samples\n",modem->demod_sync_skip,sampleslen-ii);
		//}
		//while( modem->demod_sync_skip && ii < sampleslen ) {
		//	ii++;
		//	modem->demod_sync_skip--;
		//}
		
		//Move samples into fft input
		while( modem->demod_fftinlen < modem->demod_fftin_alloc && ii < sampleslen ) {
			modem->demod_fftin[modem->demod_fftinlen] = samples[ii];
			modem->demod_fftinlen++;
			ii++;
		}
		
		if( modem->demod_fftinlen < modem->demod_fftin_alloc ) {
			//Unable to perform one last FFT
			if( modem->verbose ) {
				printf("  Need more samples: %zu / %zu\n",modem->demod_fftinlen,modem->demod_fftin_alloc);
			}
			break;
		}
		//modem->demod_fftinlen = 0;
		
		fftw_execute(modem->demod_plan);
		
		//Produce FFT magnitudes
		if( modem->verbose ) {
			printf("  fft : ");
		}
		for( i=0; i<modem->demod_bins; i++ ) {
			mag = sqrt(modem->demod_fftout[i][0] * modem->demod_fftout[i][0] + modem->demod_fftout[i][1] * modem->demod_fftout[i][1]);
			modem->demod_fftmag[i] = mag;
			if( modem->verbose ) {
				printf("%1.2lf ",modem->demod_fftmag[i]);
			}
		}
		if( modem->verbose ) {
			printf("\n");
		}
		
		if(  modem->demod_fftmag[modem->demod_freq_bin] >= modem->demod_thresh ||
		    (modem->demod_freq_bin < modem->demod_bins-1 &&
			 modem->demod_fftmag[modem->demod_freq_bin+1] >= modem->demod_thresh) ) {
			tone_detected = 1;
		}
		else {
			tone_detected = 0;
		}
		if( modem->verbose ) {
			printf("    Mag: %1.2lf Thresh: %1.2lf Detected: %d\n",modem->demod_fftmag[modem->demod_freq_bin],modem->demod_thresh,tone_detected);
		}
		
		if( modem->demod_sync_skip ) {
			if( modem->verbose ) {
				printf("  Skip %zu of %zu samples\n",modem->demod_sync_skip,modem->demod_fftin_alloc);
			}
			while( modem->demod_sync_skip && modem->demod_fftinlen ) {
				for( i=0; i<modem->demod_fftinlen-1; i++ ) {
					modem->demod_fftin[i] = modem->demod_fftin[i+1];
				}
				modem->demod_fftinlen--;
				modem->demod_sync_skip--;
			}
			continue;
		}
		modem->demod_fftinlen = 0;
		
		//Check for a single peak
		if( !tone_detected && modem->demod_state != OOK_DEMOD_SEARCH ) {
			modem->demod_sync_loss = modem->demod_sync_loss + modem->demod_fftin_alloc;
			if( modem->demod_sync_loss >= modem->mod_samp_per_sym ) {
				if( modem->verbose ) {
					printf("      Sync lost\n");
				}
				modem->demod_state = OOK_DEMOD_SEARCH;
			}
		}
		if( tone_detected ) {
			modem->demod_sync_loss = 0;
		}
		
		if( modem->demod_state == OOK_DEMOD_SEARCH ) {
			if( tone_detected ) {
				//First sample (idle)
				modem->demod_state = OOK_DEMOD_IDLE_ACQUIRE;
				modem->demod_sym_vote = tone_detected;
				modem->demod_sym_vote_count = 1;
			}
		}
		else if( modem->demod_state == OOK_DEMOD_IDLE_ACQUIRE ) {
			modem->demod_sym_vote = modem->demod_sym_vote + tone_detected;
			modem->demod_sym_vote_count++;
			if( modem->demod_sym_vote_count == OOK_REDUNDANCY ) {
				if( modem->demod_sym_vote == OOK_REDUNDANCY ) {
					//Solid tone detected
					if( modem->verbose ) {
						printf("      Sync detected\n");
					}
					modem->demod_state = OOK_DEMOD_IDLE_DETECTED;
				} 
				else {
					modem->demod_state = OOK_DEMOD_SEARCH;
				}
				modem->demod_sym_vote = 0;
				modem->demod_sym_vote_count = 0;
			}
		}
		else if( modem->demod_state == OOK_DEMOD_IDLE_DETECTED ) {
			if( !tone_detected ) {
				//First sample (start)
				modem->demod_state = OOK_DEMOD_START_ACQUIRE;
				modem->demod_sym_vote = tone_detected;
				modem->demod_sym_vote_count = 1;
			}
		}
		else if( modem->demod_state == OOK_DEMOD_START_ACQUIRE ) {
			modem->demod_sym_vote = modem->demod_sym_vote + tone_detected;
			modem->demod_sym_vote_count++;
			if( modem->demod_sym_vote_count == OOK_REDUNDANCY ) {
				if( modem->demod_sym_vote > 0 ) {
					//False Start
					modem->demod_state = OOK_DEMOD_IDLE_DETECTED;
				}
				else {
					//Solid non-tone detect
					if( modem->verbose ) {
						printf("      Start Byte detected\n");
					}
					modem->demod_state = OOK_DEMOD_DATA;
					modem->demod_sync_loss = 0;
					modem->demod_sync_skip  = round(modem->mod_samp_per_sym * 2 / (OOK_REDUNDANCY+2));
					modem->demod_byte = 0;
				}
				modem->demod_sym_vote = 0;
				modem->demod_sym_vote_count = 0;
			}
		}
		else if( modem->demod_state == OOK_DEMOD_DATA ) {
			modem->demod_sym_vote = modem->demod_sym_vote + tone_detected;
			modem->demod_sym_vote_count++;
			if( modem->verbose ) { printf("Bit: %d %d\n",modem->demod_sym_vote,modem->demod_sym_vote_count); }
			if( modem->demod_sym_vote_count == OOK_REDUNDANCY ) {
				if( modem->demod_sym_vote > (OOK_REDUNDANCY/2) ) {
					bit = 0;
				}
				else {
					bit = 1;
				}
				modem->demod_sym_vote = 0;
				modem->demod_sym_vote_count = 0;
				if( modem->verbose ) {
					printf("    Bit: %d\n",bit);
				}
				modem->demod_byte = (modem->demod_byte >> 1) | (bit<<7);
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
				}
				else {
					modem->demod_state = OOK_DEMOD_DATA;
					modem->demod_sync_loss = 0;
					modem->demod_sync_skip  = round(modem->mod_samp_per_sym * 2 / (OOK_REDUNDANCY+2));
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


#endif //OOK_IMPLEMENTATION
