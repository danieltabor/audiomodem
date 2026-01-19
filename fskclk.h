#ifndef __FSKCLK_H__
#define __FSKCLK_H__

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include <samplerate.h>
#include <fftw3.h>

#include "bitops.h"

#define FSKCLK_DEFAULT_VERBOSE     0
#define FSKCLK_DEFAULT_THRESH      0.90

typedef enum{
	FSKCLK_DEMOD_CLK_SEARCH, 
	FSKCLK_DEMOD_CLK_ACQUIRE,
	FSKCLK_DEMOD_CLK_DETECTED,
	FSKCLK_DEMOD_DATA_ACQUIRE,
	FSKCLK_DEMOD_DATA_DETECTED,
} fskclk_demod_state_t;

typedef struct {
	int      verbose;
	size_t   samplerate;
	size_t   bitrate;
	size_t   bandwidth;
	size_t   bit_per_tone;
	size_t   tone_count;
	double  *tones;
	size_t  *tonesidx;
	size_t   clkidx;
	
	size_t   mod_samp_per_sym;
	size_t   demod_samp_per_sym;
	double  *mod_samples;
	size_t   mod_sampleslen;
	double   sym_freq;
	
	fskclk_demod_state_t demod_state;
	SRC_STATE *demod_src;
	float     *demod_srcin;
	size_t     demod_srcinlen;
	size_t     demod_srcin_alloc;
	float     *demod_srcout;
	size_t     demod_srcout_alloc;
	size_t     demod_srcoutlen;
	
	size_t     demod_bins;
	fftw_plan  demod_plan;
	double    *demod_fftin;
	fftw_complex *demod_fftout;
	double    *demod_fftmag;
	size_t     demod_sync_loss;
	double     demod_thresh;
	size_t     demod_databin;
	
	uint8_t    demod_bytes[2];
	uint8_t    demod_bit_count;
	uint8_t   *demod_data;
	size_t     demod_datalen;
} fskclk_t;


fskclk_t *fskclk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count);
void      fskclk_destroy(fskclk_t *modem);
int       fskclk_set_thresh(fskclk_t *modem, double thresh);
int       fskclk_set_verbose(fskclk_t *modem, int verbose);
void      fskclk_printinfo(fskclk_t *modem);
int       fskclk_modulate(fskclk_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen);
int       fskclk_demodulate(fskclk_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen);

#endif //__FSKCLK_H__


#ifdef FSKCLK_IMPLEMENTATION
#undef FSKCLK_IMPLEMENTATION

fskclk_t *fskclk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count) {
	fskclk_t *modem;
	double freq_step;
	double freq;
	size_t i;
	
	//Double check arguments
	if( samplerate < (bandwidth * 2) ) { return 0; }
	if( tone_count < 2 ) { return 0; }
	
	modem = (fskclk_t*)malloc(sizeof(fskclk_t));
	if( !modem ) { goto fskclk_init_error; }
	memset(modem,0,sizeof(fskclk_t));
	
	modem->verbose = FSKCLK_DEFAULT_VERBOSE;
	
	modem->bit_per_tone = 1;
	while( 1<<modem->bit_per_tone < tone_count ) {
		modem->bit_per_tone++;
	}
	modem->tone_count = (1 << modem->bit_per_tone)+1;
	
	modem->samplerate = samplerate;
	modem->bitrate = bitrate;
	modem->bandwidth = bandwidth;
	
	modem->tones = (double*)malloc(sizeof(double)*modem->tone_count);
	if( !modem->tones ) { goto fskclk_init_error; }
	modem->tonesidx = (size_t*)malloc(sizeof(size_t)*modem->tone_count-1);
	
	//Calculate the frequencies used
	freq_step = bandwidth / modem->tone_count;
	freq = freq_step / 2;
	for( i=0; i<modem->tone_count; i++ ) {
		modem->tones[i] = freq;
		freq = freq + freq_step;
		if( i < modem->tone_count/2 ) {
			modem->tonesidx[i] = i;
		}
		else if( i > modem->tone_count/2 ) {
			modem->tonesidx[i-1] = i;
		}
		else {
			modem->clkidx = i;
		}
	}
	
	//Samples to produce per clock cycle on modulation
	modem->sym_freq = ((double)bitrate / (double)modem->bit_per_tone);
	modem->mod_samp_per_sym = (double)samplerate / modem->sym_freq;
	if( modem->mod_samp_per_sym < 2 ) { goto fskclk_init_error; }
	modem->demod_samp_per_sym = (double)(bandwidth*2) / modem->sym_freq;
	if( modem->demod_samp_per_sym < modem->tone_count*6 ) { goto fskclk_init_error; }
	
	
	//Samplerate conveter
	modem->demod_src = src_new(SRC_SINC_MEDIUM_QUALITY,1,0);
	if( !modem->demod_src ) { goto fskclk_init_error; }
	//Make sure we are analyzing chunks small enough to guarantee that we
	//will see a clock for at least 2 clean ffts and dat for at least 2 clean ffts
	modem->demod_srcin_alloc = modem->mod_samp_per_sym / 8;
	modem->demod_srcin  = (float*)malloc(sizeof(float) * modem->demod_srcin_alloc);
	if( !modem->demod_srcin ) { goto fskclk_init_error; }
	modem->demod_srcout_alloc = modem->demod_samp_per_sym / 8;
	modem->demod_srcout = (float*)malloc(sizeof(float) * modem->demod_srcout_alloc);
	if( !modem->demod_srcout ) { goto fskclk_init_error; }
	
	//FFT
	//Because these are real samples, only the first half of the fft results (DC->nyquist) will be valid
	//The second half (-nyquist->DC) will all be zero
	modem->demod_bins = modem->demod_srcout_alloc / 2;
	if( modem->demod_bins < modem->tone_count ) {
		//The FFT resolution isn't good enough to differentiate tones
		//printf("FFT resolution is too small %zu < %zu\n",modem->demod_bins,modem->tone_count);
		goto fskclk_init_error;
	}
	modem->demod_fftin  = (double*)fftw_malloc(sizeof(double) * modem->demod_srcout_alloc);
	if( !modem->demod_fftin ) { goto fskclk_init_error; }
	modem->demod_fftout = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * modem->demod_srcout_alloc);
	if( !modem->demod_fftout ) { goto fskclk_init_error; }
	modem->demod_plan   = fftw_plan_dft_r2c_1d(modem->demod_srcout_alloc, modem->demod_fftin, modem->demod_fftout,  FFTW_MEASURE);
	modem->demod_fftmag = (double*)malloc(sizeof(double)* modem->tone_count);
	if( !modem->demod_fftmag ) { goto fskclk_init_error; }
	modem->demod_thresh = FSKCLK_DEFAULT_THRESH;
	
	modem->demod_state = FSKCLK_DEMOD_CLK_SEARCH;
	
	return modem;
	
	fskclk_init_error:
	fskclk_destroy(modem);
	return 0;
}

void fskclk_destroy(fskclk_t *modem) {
	if( modem ) {
		if( modem->tones ) { free(modem->tones); }
		if( modem->mod_samples ) { free(modem->mod_samples); }
		if( modem->demod_src ) { src_delete(modem->demod_src); }
		if( modem->demod_srcin ) { free(modem->demod_srcin); }
		if( modem->demod_srcout ) { free(modem->demod_srcout); }
		if( modem->demod_fftin ) { fftw_free(modem->demod_fftin); }
		if( modem->demod_fftout ) { fftw_free(modem->demod_fftout); }
		if( modem->demod_fftmag ) { free(modem->demod_fftmag); }
		if( modem->demod_data ) { free(modem->demod_data); }
		memset(modem,0,sizeof(fskclk_t));
		free(modem);
	}
}


int fskclk_set_thresh(fskclk_t *modem, double thresh) {
	if( !modem ) { return -1; }
	modem->demod_thresh = thresh;
	return 0;
}

int fskclk_set_verbose(fskclk_t *modem, int verbose) {
	if( !modem ) { return -1; }
	modem->verbose = verbose;
	return 0;
}

void fskclk_printinfo(fskclk_t *modem) {
	size_t i;
	size_t j;
	printf("Tone Modem:\n");
	printf("  Verbose           :  %d\n",modem->verbose);
	printf("  Samplerate        : %zu\n",modem->samplerate);
	printf("  Bitrate           : %zu bps\n",modem->bitrate);
	printf("  Bandwidth         : %zu Hz\n",modem->bandwidth);
	printf("  Bits per Symbol   : %zu\n",modem->bit_per_tone);
	printf("  Samples per Symbol: %zu\n",modem->mod_samp_per_sym);
	printf("  Tones(%zu):\n",modem->tone_count);
	for( i=0; i<modem->tone_count; i++ ) {
		if( i==modem->clkidx ) {
			printf("    Clk : %04.1lf Hz\n",modem->tones[i]);
		}
		else {
			for( j=0; j<modem->tone_count-1; j++ ) {
				if( i==modem->tonesidx[j] ) {
					printf("    0x%02lx: %04.1lf Hz\n",j,modem->tones[i]);
				}
			}
		}
	}
}

int fskclk_modulate(fskclk_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen) {
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
		printf("tonemode_modulate(...):\n");
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
		goto fskclk_modulate_sync_error;
	}
	modem->mod_samples = mod_samples;
	modem->mod_sampleslen = mod_sampleslen;
	
	ii = 0;
	bit_idx = 0;
	for( symbol_idx=0; symbol_idx<symbol_count; symbol_idx++ ) {
		//Generate a half-bit of clk
		for( sample_count=0; sample_count<modem->mod_samp_per_sym/2; sample_count++ ) {
			mod_samples[ii] = sin(2*M_PI*modem->tones[modem->clkidx]*ii/modem->samplerate) *
			                  sin(2*M_PI*modem->sym_freq*ii/modem->samplerate);
			ii++;
		}
		//Generate a half-bit of data
		sym = getbits(data, datalen, bit_idx, modem->bit_per_tone);
		if( modem->verbose ) {
			printf("  Symbol[%zu]=0x%02x modulated to frequency %04.1lf Hz\n",symbol_idx,sym,modem->tones[modem->tonesidx[sym]]);
		}
		bit_idx = bit_idx + modem->bit_per_tone;
		for( ; sample_count<modem->mod_samp_per_sym; sample_count++ ) {
			mod_samples[ii] = sin(2*M_PI*modem->tones[modem->tonesidx[sym]]*ii/modem->samplerate) *
			                  sin(2*M_PI*modem->sym_freq*ii/modem->samplerate);
			ii++;
		}
	}
	
	*samples = mod_samples;
	*sampleslen = mod_sampleslen;
	return 0;
	
	fskclk_modulate_sync_error:
	*samples = 0;
	*sampleslen = 0;
	return -1;
}


int fskclk_demodulate(fskclk_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen) {
	SRC_DATA src_data;
	size_t   ii;
	size_t   i;
	double   mag;
	ssize_t  maxbin;
	double   maxmag;
	size_t   peak_count;
	uint8_t *tmpdata;
	size_t   toneidx;
	int      sym;
	
	if( !modem ) { return -1; }
	if( !data ) { return -1; }
	if( !datalen ) { return -1; }
	if( !samples ) { return -1; }
	
	if( modem->verbose ) {
		printf("fskclk_demodulate(...)\n");
	}
	
	modem->demod_datalen = 0;
	*data = modem->demod_data;
	*datalen = modem->demod_datalen;
	
	src_data.src_ratio = (double)(modem->bandwidth*2) / (double)modem->samplerate;
	src_data.end_of_input = 0;
	
	ii = 0;
	while( ii < sampleslen ) {
		//Resample input to match the nyquist rate of the bandwidth
		while( modem->demod_srcinlen < modem->demod_srcin_alloc && ii < sampleslen ) {
			modem->demod_srcin[modem->demod_srcinlen] = (float)samples[ii];
			modem->demod_srcinlen++;
			ii++;
		}
		src_data.data_in       = modem->demod_srcin;
		src_data.input_frames  = modem->demod_srcinlen;
		src_data.data_out      = modem->demod_srcout;
		src_data.output_frames = modem->demod_srcout_alloc - modem->demod_srcoutlen;
		if( src_process(modem->demod_src, &src_data) ) { 
			if( modem->verbose ) {
				printf("  Samplerate conversion failed\n");
				return -1; 
			}
		}
		modem->demod_srcinlen  = modem->demod_srcinlen - src_data.input_frames_used;
		modem->demod_srcoutlen = modem->demod_srcoutlen + src_data.output_frames_gen;
		
		if( modem->demod_srcoutlen < modem->demod_srcout_alloc ) {
			//Unable to perform one last FFT
			continue;
		}
		
		//Perform an FFT on resampled audio
		for( i=0; i<modem->demod_srcoutlen; i++ ) {
			modem->demod_fftin[i] = (double)modem->demod_srcout[i];
		}
		modem->demod_srcoutlen = 0;
		fftw_execute(modem->demod_plan);
		
		//Reduce FFT magnitudes
		for( i=0; i<modem->tone_count; i++ ) {
			modem->demod_fftmag[i] = 0;
		}
		maxbin = -1;
		maxmag = -1;
		for( i=0; i<modem->demod_bins; i++ ) {
			mag = sqrt(modem->demod_fftout[i][0] * modem->demod_fftout[i][0] + modem->demod_fftout[i][1] * modem->demod_fftout[i][1]);
			toneidx = i * modem->tone_count / modem->demod_bins;
			modem->demod_fftmag[toneidx] += mag;
			if( maxmag < 0 || modem->demod_fftmag[toneidx] > maxmag ) {
				maxbin = toneidx;
				maxmag = modem->demod_fftmag[toneidx];
			}
		}
		//Normalize FFT magnitudes
		if( modem->verbose ) {
			printf("  fft : ");
		}
		peak_count = 0;
		for( i=0; i<modem->tone_count; i++ ) {
			if( maxmag == 0 ) {
				modem->demod_fftmag[i] = 0;
			} else {
				modem->demod_fftmag[i] = modem->demod_fftmag[i] / maxmag;
			}
			if( modem->demod_fftmag[i] >= modem->demod_thresh ) {
				peak_count++;
			}
			if( modem->verbose ) {
				printf("%1.2lf ",modem->demod_fftmag[i]);
			}
		}
		if( modem->verbose ) {
			printf("\n");
			printf("  Max: %zd  Thresh: %1.2lf  Peaks: %zu\n",maxbin,modem->demod_thresh,peak_count);
		}
		
		//Check for a single peak
		if( peak_count != 1 ) {
			//Not a definate peak
			if( modem->demod_state != FSKCLK_DEMOD_CLK_SEARCH ) {
				modem->demod_sync_loss = modem->demod_sync_loss + modem->demod_srcout_alloc;
				//printf("  Loosing sync %zu / %zu\n",modem->demod_sync_loss,modem->demod_samp_per_sym);
				if( modem->demod_sync_loss >= modem->demod_samp_per_sym ) {
					if( modem->verbose ) {
						printf("  Sync lost\n");
					}
					modem->demod_state = FSKCLK_DEMOD_CLK_SEARCH;
				}
			}
			continue;
		} else {
			modem->demod_sync_loss = 0;
		}
		
		if( maxbin == modem->clkidx ) {
			//Clock Tone
			if( modem->demod_state == FSKCLK_DEMOD_CLK_ACQUIRE ) {
				modem->demod_state = FSKCLK_DEMOD_CLK_DETECTED;
				if( modem->verbose ) {
					printf("  Found clock\n");
				}
				modem->demod_databin = maxbin;
			}
			else if( modem->demod_state == FSKCLK_DEMOD_CLK_DETECTED ) {
				//Subsequent sample of our detected clock
			}
			else {
				//First sample of a new clock
				modem->demod_state = FSKCLK_DEMOD_CLK_ACQUIRE;
			}
		}
		else {
			//Data Tone
			if( modem->demod_state == FSKCLK_DEMOD_CLK_ACQUIRE ) {
				//Confused clock detected, reset
				modem->demod_state = FSKCLK_DEMOD_CLK_SEARCH;
			}
			else if( modem->demod_state == FSKCLK_DEMOD_CLK_DETECTED ) {
				//First sample of a new data
				modem->demod_state = FSKCLK_DEMOD_DATA_ACQUIRE;
				modem->demod_databin = maxbin;
			}
			else if( modem->demod_state == FSKCLK_DEMOD_DATA_ACQUIRE ) {
				//Possible second sample of data
				if( modem->demod_databin != maxbin ) {
					//Not the data we expected
					modem->demod_state = FSKCLK_DEMOD_CLK_SEARCH;
				}
				else {
					//Detected data
					modem->demod_state = FSKCLK_DEMOD_DATA_DETECTED;
					
					//Find the symbol for this tone
					for( i=0; i<modem->tone_count-1; i++ ) {
						if( modem->tonesidx[i] == maxbin ) {
							sym = (int)i;
							break;
						}
					}
			
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
			else {
				//Either a subsequent sample of a detected data,
				//or a spurious tone when searching for clock.
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


#endif //FSKCLK_IMPLEMENTATION
