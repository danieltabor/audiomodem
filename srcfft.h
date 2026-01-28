#ifndef __SRCFFT_H__
#define __SRCFFT_H__

#include <stdlib.h>
#include <samplerate.h>
#include <fftw3.h>

typedef enum{ SRCFFT_ERROR=-1, SRCFFT_RESULT=0, SRCFFT_NEED_MORE=1 } srcfft_status_t;

typedef struct {
	//Samplerate Conversion Internals
	SRC_STATE *src;
	float      srcratio;
	float     *srcin;
	size_t     srcinlen;
	size_t     srcinalloc;
	float     *srcout;
	size_t     fftalloc;
	size_t     srcoutlen;
	
	//FFT Internals
	fftw_plan     fftplan;
	double       *fftin;
	fftw_complex *fftout;
	double        thresh;
	double        norm_thresh;
	size_t        magalloc;
	
	//Syncronization
	size_t        sync_skip;
	
	//Results
	size_t        used_samples;
	size_t        maxbin;
	double        maxmag;
	size_t        minbin;
	double        minmag;
	double        avgmag;
	size_t        len;
	double       *mag;
	double       *norm;
	double       *ang;
	size_t        detectlen;
	size_t       *detect;
} srcfft_t;

srcfft_t        *srcfft_init(size_t input_samplerate, size_t input_size, size_t output_bandwidth, size_t output_size);
void             srcfft_destroy(srcfft_t *srcfft);
int              srcfft_reset(srcfft_t *srcfft);
void             srcfft_printresult(srcfft_t *srcfft);
int              srcfft_set_thresh(srcfft_t *srcfft, double thresh);
int              srcfft_set_norm_thresh(srcfft_t *srcfft, double thresh);
int              srcfft_sync(srcfft_t *srcfft, size_t skip_sampleslen);
srcfft_status_t  srcfft_process(srcfft_t *srcfft, double *samples, size_t sampleslen);

#endif //__SRCFFT_H__

#ifdef SRCFFT_IMPLEMENTATION
#undef SRCFFT_IMPLEMENTATION

#include <stdio.h>
#include <math.h>
#include <string.h>

srcfft_t *srcfft_init(size_t input_samplerate, size_t input_size, size_t output_bandwidth, size_t output_size) {
	srcfft_t *srcfft = 0;
	
	if( output_bandwidth > input_samplerate / 2 ) {
		//We can only reduce bandwidth, not create it
		goto srcfft_init_error;
	}
	
	srcfft = (srcfft_t*)malloc(sizeof(srcfft_t));
	if( !srcfft ) { goto srcfft_init_error; }
	memset(srcfft,0,sizeof(srcfft_t));
	
	//Samplerate Rate
	srcfft->srcratio =  (double)(output_bandwidth*2) / (double)input_samplerate;
	srcfft->src = src_new(SRC_SINC_MEDIUM_QUALITY,1,0);
	if( !srcfft->src ) { goto srcfft_init_error; }
	srcfft->srcinalloc = input_size;
	srcfft->srcin  = (float*)malloc(sizeof(float) * srcfft->srcinalloc);
	if( !srcfft->srcin ) { goto srcfft_init_error; }
	srcfft->fftalloc = input_size * srcfft->srcratio;
	if( srcfft->fftalloc == 0 ) { goto srcfft_init_error; }
	srcfft->srcout = (float*)malloc(sizeof(float) * srcfft->fftalloc);
	if( !srcfft->srcout ) { goto srcfft_init_error; }
	
	//FFT
	//Because these are real samples, only the first half of the fft results (DC->nyquist) will be valid
	//The second half (-nyquist->DC) will all be zero
	if( output_size == 0 ) {
		output_size = srcfft->fftalloc/2;
	}
	if( srcfft->fftalloc/2 < output_size ) {
		//The input_size is not large enough to produce the required output_size
		goto srcfft_init_error;
	}
	srcfft->fftin  = (double*)fftw_malloc(sizeof(double) * srcfft->fftalloc);
	if( !srcfft->fftin ) { goto srcfft_init_error; }
	srcfft->fftout = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * srcfft->fftalloc);
	if( !srcfft->fftout ) { goto srcfft_init_error; }
	srcfft->fftplan  = fftw_plan_dft_r2c_1d(srcfft->fftalloc, srcfft->fftin, srcfft->fftout,  FFTW_MEASURE);
	
	//Thresholds
	srcfft->thresh = -1;
	srcfft->norm_thresh = -1;
	
	//Results
	srcfft->magalloc = output_size;
	srcfft->mag = (double*)malloc(sizeof(double)* srcfft->magalloc );
	if( !srcfft->mag ) { goto srcfft_init_error; }
	srcfft->norm = (double*)malloc(sizeof(double)* srcfft->magalloc );
	if( !srcfft->norm ) { goto srcfft_init_error; }
	srcfft->detect = (size_t*)malloc(sizeof(size_t)* srcfft->magalloc );
	if( !srcfft->detect ) { goto srcfft_init_error; }
	srcfft->ang = (double*)malloc(sizeof(double)* srcfft->magalloc );
	if( !srcfft->ang ) { goto srcfft_init_error; }
	return srcfft;
	
	srcfft_init_error:
	srcfft_destroy(srcfft);
	return 0;
}

void srcfft_destroy(srcfft_t *srcfft) {
	if( srcfft ) {
		if( srcfft->src ) { src_delete(srcfft->src); }
		if( srcfft->srcin ) { free(srcfft->srcin); }
		if( srcfft->srcout ) { free(srcfft->srcout); }
		if( srcfft->fftin ) { fftw_free(srcfft->fftin); }
		if( srcfft->fftout ) { fftw_free(srcfft->fftout); }
		if( srcfft->mag ) { free(srcfft->mag); }
		if( srcfft->norm ) { free(srcfft->norm); }
		if( srcfft->detect ) { free(srcfft->detect); }
		memset(srcfft,0,sizeof(srcfft_t));
		free(srcfft);
	}
}

int srcfft_reset(srcfft_t *srcfft) {
	size_t i;
	if( !srcfft ) { return -1; }
	if( src_reset(srcfft->src) ) { return -1; }
	srcfft->used_samples = 0;
	srcfft->maxbin = 0;
	srcfft->maxmag = 0.0;
	srcfft->minbin = 0;
	srcfft->minmag = 0.0;
	for( i=0; i<srcfft->magalloc; i++ ) {
		srcfft->mag[i] = 0.0;
		srcfft->norm[i] = 0.0;
		srcfft->detect[i] = 0.0;
	}
	srcfft->len = 0;
	return 0;
}

void srcfft_printresult(srcfft_t *srcfft) {
	size_t i,j;
	if( srcfft ) {
		if( !srcfft->len ) {
			printf("No FFT\n");
		}
		else {
			if( srcfft->norm_thresh >= 0 ) {
				printf("norm: ");
				for( i=0; i<srcfft->len; i++ ) {
					for( j=0; j<srcfft->detectlen; j++ ) {
						if( srcfft->detect[j] == i ) {
							printf("[%03.1lf] ",srcfft->norm[i]);
							break;
						}
					}
					if( j==srcfft->detectlen ) {
						printf(" %03.1lf  ",srcfft->norm[i]);
					}
				}
				printf("\n");
			}
			else {
				printf("fft : ");
				for( i=0; i<srcfft->len; i++ ) {
					for( j=0; j<srcfft->detectlen; j++ ) {
						if( srcfft->detect[j] == i ) {
							printf("[%05.1lf] ",srcfft->mag[i]);
							break;
						}
					}
					if( j==srcfft->detectlen ) {
						printf(" %05.1lf  ",srcfft->mag[i]);
					}
				}
				printf("\n");
			}
		}
	}
}

int srcfft_set_thresh(srcfft_t *srcfft, double thresh) {
	if( !srcfft ) { return -1; }
	if( thresh < 0 ) { return -1; }
	srcfft->thresh = thresh;
	srcfft->norm_thresh = -1;
	return 0;
}


int srcfft_set_norm_thresh(srcfft_t *srcfft, double thresh) {
	if( !srcfft ) { return -1; }
	if( thresh < 0 || thresh > 1.0 ) { return -1; }
	srcfft->thresh = -1;
	srcfft->norm_thresh = thresh;
	return 0;
}

int srcfft_sync(srcfft_t *srcfft, size_t skip_sampleslen) {
	size_t i;
	if( !srcfft ) { return -1; }
	srcfft->sync_skip = srcfft->sync_skip + (size_t)((double)skip_sampleslen * srcfft->srcratio);
	return 0;
}

srcfft_status_t srcfft_process(srcfft_t *srcfft, double *samples, size_t sampleslen) {
	SRC_DATA src_data;
	size_t i;
	double mag;
	double ang;
	size_t binidx;
	
	if( !srcfft ) { goto srcfft_process_error; }
	if( !samples && sampleslen ) { goto srcfft_process_error; }
		
	srcfft->used_samples = 0;
	for(;;) {
		//Append samples to src input until we have enough to convert
		while( srcfft->srcinlen < srcfft->srcinalloc && srcfft->used_samples < sampleslen ) {
			srcfft->srcin[srcfft->srcinlen++] = (float)samples[srcfft->used_samples++];
		}
		
		//Check to see if we have enough samples to perform a conversion
		if( srcfft->srcinlen != srcfft->srcinalloc ) {
			return SRCFFT_NEED_MORE;
		}
		
		//Perform conversion
		src_data.data_in       = srcfft->srcin;
		src_data.input_frames  = srcfft->srcinlen;
		src_data.data_out      = srcfft->srcout + srcfft->srcoutlen;
		src_data.output_frames = srcfft->fftalloc - srcfft->srcoutlen;
		src_data.src_ratio     = srcfft->srcratio;
		src_data.end_of_input  = 0;
		if( src_process(srcfft->src, &src_data) ) {
			//printf("src_process failed\n");
			goto srcfft_process_error;
		}
		if( src_data.input_frames_used == srcfft->srcinlen ) {
			srcfft->srcinlen = 0;
		}
		else {
			for( i=src_data.input_frames_used; i<srcfft->srcinlen; i++ ) {
				srcfft->srcin[i-src_data.input_frames_used] = srcfft->srcin[i];
			}
			srcfft->srcinlen  = srcfft->srcinlen - src_data.input_frames_used;
		}
		srcfft->srcoutlen = srcfft->srcoutlen + src_data.output_frames_gen;
		
		//printf("Convert: input(%zu/%zu) output(%zu/%zu)\n",src_data.input_frames_used,src_data.input_frames,srcfft->srcoutlen,srcfft->fftalloc);
		
		//Skip samples in order to synchornize the FFT (if we've been asked to)
		if( srcfft->sync_skip ) {
			if( srcfft->sync_skip > srcfft->srcoutlen ) {
				srcfft->sync_skip = srcfft->sync_skip - srcfft->srcoutlen;
				srcfft->srcoutlen = 0;
			}
			else {
				for( i=srcfft->sync_skip; i<srcfft->srcoutlen; i++ ) {
					srcfft->srcout[i-srcfft->sync_skip] = srcfft->srcout[i];
				}
				srcfft->sync_skip = 0;
			}
		}
		
		//Check to see if we have enough samples to perform an FFT
		if( srcfft->srcoutlen != srcfft->fftalloc ) {
			continue;
		}
		//Ideally (if enough samples are provided and SRC isn't buffering anything
		//internally and we we didnt't skip any samples) this loop should only be 
		//executed once.
		break;
	}
	
	//Move resampled data to fft input
	for( i=0; i<srcfft->fftalloc; i++ ) {
		srcfft->fftin[i] = (double)srcfft->srcout[i];
	}
	srcfft->srcoutlen = 0;
	
	//Perform an FFT on audio
	fftw_execute(srcfft->fftplan);
	
	//For most configurations, the FFT will produce more
	//bins that the desired out.  We'll reduce the bins
	//by grouping and summing their magnitudes, so we need
	//to first zero out the destination array.
	for( i=0; i<srcfft->magalloc; i++ ) {
		srcfft->mag[i] = 0.0;
		srcfft->ang[i] = 0.0;
	}
	
	//Create FFT magnitudes, 
	//- reduce them into the desired number of output bins
	//- find max
	//- calculate average
	//printf("  offt: ");
	srcfft->maxmag = 0;
	srcfft->avgmag = 0;
	for( i=0; i<(srcfft->fftalloc/2); i++ ) {
		binidx = (size_t)((double)i * (double)srcfft->magalloc / (double)(srcfft->fftalloc/2));
		
		mag = sqrt(srcfft->fftout[i][0] * srcfft->fftout[i][0] + srcfft->fftout[i][1] * srcfft->fftout[i][1]);
		//printf("%02.1f[%zu] ",mag,binidx);
		mag = mag + srcfft->mag[binidx];
		if( isnan(mag) || isinf(mag) ) {
			goto srcfft_process_error;
		}
		
		srcfft->mag[binidx] = mag;
		if( mag > srcfft->maxmag ) {
			srcfft->maxmag = mag;
			srcfft->maxbin = binidx;
		}
		
		srcfft->avgmag = srcfft->avgmag + mag;
		
		ang = atan2(srcfft->fftout[i][1],srcfft->fftout[i][0]);
		ang = ang + srcfft->ang[binidx];
		if( isnan(ang) || isinf(ang) ) {
			goto srcfft_process_error;
		}
		while( ang < 0 ) {
			ang = ang + 2*M_PI;
		}
		while( ang >= 2*M_PI ) {
			ang = ang - (2*M_PI);
		}
		srcfft->ang[binidx] = ang;
	}
	srcfft->avgmag = srcfft->avgmag / srcfft->magalloc;
	//printf("\n");
	
	//Create normalized FFT magnitudes
	//- perform threshold detected
	//- find min
	srcfft->minmag = srcfft->maxmag;
	srcfft->detectlen = 0;
	for( i=0; i<srcfft->magalloc; i++ ) {
		mag = srcfft->mag[i];
		if( mag < srcfft->minmag ) {
			srcfft->minmag = mag;
			srcfft->minbin = i;
		}
		if( srcfft->thresh >= 0 && mag >= srcfft->thresh ) {
			srcfft->detect[srcfft->detectlen++] = i;
		}
		
		if( srcfft->maxmag == 0.0 ) {
			mag = 0.0;
		}
		else {
			mag = mag / srcfft->maxmag;
			if( srcfft->norm_thresh >= 0 && mag >= srcfft->norm_thresh ) {
				srcfft->detect[srcfft->detectlen++] = i;
			}
		}
		srcfft->norm[i] = mag;
	}
	
	srcfft->len = srcfft->magalloc;
	return SRCFFT_RESULT;
	
	srcfft_process_error:
	(void)srcfft_reset(srcfft);
	return SRCFFT_ERROR;
}


#endif //SRCFFT_IMPLEMENTATION