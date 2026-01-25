#ifndef __FSKCALIBRATE_H__
#define __FSKCALIBRATE_H__

#include <stdlib.h>
#include "srcfft.h"

int fskcalibrate(double *freqs, size_t freqslen, srcfft_t *srcfft, size_t samplerate, size_t bandwidth, double percent_thresh);

#endif //__FSKCALIBRATE_H__

#ifdef FSKCALIBRATE_IMPLEMENTATION
#undef FSKCALIBRATE_IMPLEMENTATION

#include <math.h>

int fskcalibrate(double *freqs, size_t freqslen, srcfft_t *srcfft, size_t samplerate, size_t bandwidth, double percent_thresh) {
	//Adjust the tone frequencies for this specific configuration 
	//to ensure that the pure symbol tones will be received in 
	//the correct FFT bins with the greast value
	size_t ii;
	size_t k;
	size_t fftbin;
	
	size_t sampleslen = 0;
	double *samples = 0;
	
	double freq_step;
	double min_freq;
	double     freq;
	double max_freq;
	
	double maxmag;
	double maxmagfreq;
	
	double threshmag = -1;
	srcfft_status_t result;
	
	if( !srcfft ) { goto fskcalibrate_error; }
	if( !freqs || freqslen < 1 ) { goto fskcalibrate_error; }
	if( bandwidth < 2 ) { goto fskcalibrate_error; }
	
	#if (FSKCALIBRATE_VERBOSE)
		printf("fskcalibrate(...)\n");
	#endif
	
	sampleslen = srcfft->srcinalloc;
	samples = (double*)malloc(sizeof(double)*sampleslen);
	if( !samples ) {
		#if (FSKCALIBRATE_VERBOSE)
			printf("  malloc failed");
		#endif
		goto fskcalibrate_error;
	}
	
	freq_step = (double)bandwidth / (double)freqslen;
	for( fftbin=0; fftbin<freqslen; fftbin++ ) {
		min_freq = ( fftbin    * freq_step)+1;
		max_freq = ((fftbin+1) * freq_step)-1;
		freq = min_freq;
		
		maxmag = 0.0;
		maxmagfreq = 0.0;
		
		#if (FSKCALIBRATE_VERBOSE)
			printf("  Calibrating for bin %zu %0.1lf Hz - %0.1lf Hz\n",fftbin,min_freq,max_freq);
		#endif
		
		//Check every frequency (at 1Hz steps) within the FFT bin
		//and decide the best frequency to use to get the best
		//result from the FFT
		for( freq=min_freq; freq<=max_freq; freq++ ) {
			if( srcfft_reset(srcfft) ) {
				#if (FSKCALIBRATE_VERBOSE)
					printf("    Failed to reset srcfft\n");
				#endif
				goto fskcalibrate_error;
			}
			
			ii = 0;
			do {
				//Generate Tone
				for( k=0; k<sampleslen; k++ ) {
					samples[k] = percent_thresh*sin(2*M_PI*freq*ii/samplerate);
					ii++;
				}
				//Execute FFT
				result = srcfft_process(srcfft,samples,sampleslen);
				if( result == SRCFFT_ERROR || 
				    result == SRCFFT_NEED_MORE && srcfft->used_samples != sampleslen ) {
					#if (FSKCALIBRATE_VERBOSE)
						printf("    Failed to process samples\n");
					#endif
					goto fskcalibrate_error;
				}
			} while( result == SRCFFT_NEED_MORE );
			
			#if (FSKCALIBRATE_VERBOSE)
				printf("  %06.1lf Hz ",freq);
				srcfft_printresult(srcfft);
			#endif
			
			if( srcfft->maxbin != fftbin ) {
				//This is a bad freq, don't consider it
				continue;
			}
			if( srcfft->maxmag > maxmag ) {
				maxmag = srcfft->maxmag;
				maxmagfreq = freq;
			}
		}
		
		if( maxmagfreq == 0.0 ) {
			#if (FSKCALIBRATE_VERBOSE)
				printf("    Failed to determine a frequency for bin %zu\n",fftbin);
			#endif
			goto fskcalibrate_error;
		}
		
		#if (FSKCALIBRATE_VERBOSE)
			printf("  Best Frequency: %0.1lf Hz @ %0.1lf\n",maxmagfreq,maxmag);
		#endif
		
		//Configure new/adjusted frequency
		freqs[fftbin] = maxmagfreq;
		
		//Update (maybe) detection threshhold
		//threshold is the smallest of the maximum magitudes
		if( threshmag < 0 || maxmag < threshmag ) {
			threshmag = maxmag;
		}
	}
	
	#if (FSKCALIBRATE_VERBOSE)
		printf("  Detection Threshold %lf\n",threshmag);
	#endif
	if( srcfft_set_thresh(srcfft,percent_thresh*threshmag) ) {
		#if (FSKCALIBRATE_VERBOSE)
			printf("  Failed to set srcfft threshold\n");
		#endif
		goto fskcalibrate_error;
	}
	
	
	if( samples ) { free(samples); }
	if( srcfft_reset(srcfft) ) {
		#if (FSKCALIBRATE_VERBOSE)
			printf("  Failed to reset srcfft\n");
		#endif
		goto fskcalibrate_error;
	}
	return 0;
	
	fskcalibrate_error:
	if( samples ) { free(samples); }
	(void)srcfft_reset(srcfft);
	return -1;
}

#endif //FSKCALIBRATE_IMPLEMENTATION
