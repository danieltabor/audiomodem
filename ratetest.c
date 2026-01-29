#include <sndfile.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

//#define FSKCALIBRATE_VERBOSE 1

#define BITOPS_IMPLEMENTATION
#define FSKCALIBRATE_IMPLEMENTATION
#define SRCFFT_IMPLEMENTATION
#define FSKCLK_IMPLEMENTATION
#define FSK_IMPLEMENTATION
#define OOK_IMPLEMENTATION
#define PSKCLK_IMPLEMENTATION
#define CORR_IMPLEMENTATION
#define PKT_IMPLEMENTATION
#define AUDIOMODEM_IMPLEMENTATION
#include "audiomodem.h"

#define DEFAULT_BITRATE 64
#define DEFAULT_BANDWIDTH 3000
#define DEFAULT_SYMBOL_COUNT 4
#define DEFAULT_FREQUENCY 1000
#define DEFAULT_TEST_SIZE 512
#define DEFAULT_NOISE_AMPLITUDE 0

typedef enum {OPT_NONE,OPT_FSK,OPT_FSKCLK,OPT_OOK,OPT_PSKCLK,OPT_CORRFSK,OPT_CORRPSK,OPT_CORRFPSKCLK} modemopt_t;

void usage(char* cmd) {
	char* filename = cmd+strlen(cmd);
	while( filename > cmd ) {
		if( *(filename-1) == '/' ) {
			break;
		}
		filename--;
	}
	printf("Usage: %s [-h] [-v] [-p] [-fsk | -fskclk | -ook | -pskclk | -cfsk | -cpsk | -cfpsk]\n",filename);
	printf("  [-s samplerate] [-r bitrate] [-bw bandwidth] [-c symbol_count] [-f frequency]\n");
	printf("  [-z test_size] [-n noise_amplitude]\n");
	printf("\n");
	printf("Defaults:\n");
	printf("  samplerate     : based on bandwidth\n");
	printf("  start_bitrate  : %d\n",DEFAULT_BITRATE);
	printf("  bandwidth      : %d\n",DEFAULT_BANDWIDTH);
	printf("  symbol_count     : %d\n",DEFAULT_SYMBOL_COUNT);
	printf("  frequency      : %d\n",DEFAULT_FREQUENCY);
	printf("  test_size      : %d\n",DEFAULT_TEST_SIZE);
	printf("  noise_amplitude: %0.1lf\n",(double)DEFAULT_NOISE_AMPLITUDE);
	printf("\n");
	exit(0);
}

int main(int argc, char** argv) {
	double *samples;
	sf_count_t samples_len;
	uint8_t *test_data = 0;
	double  *ota_samples = 0;
	size_t   ota_samples_len = 0;
	uint8_t *comp_data;
	size_t   comp_size;
	audiomodem_t *modem = 0;
	size_t max_bitrate = 0;
	size_t best_bitrate = 0;
	size_t next_bitrate;
	int verbose = 0;
	int use_pkt = 0;
	modemopt_t modemopt = OPT_NONE;
	size_t samplerate = 0;
	size_t bitrate = 0;
	size_t bandwidth = 0;
	size_t symbol_count = 0;
	size_t frequency = 0;
	size_t test_size = 0;
	double noise_amp = -1;
	int i = 1;
	size_t ii;
	struct timespec ts;
	
	while( i < argc ) {
		if( !strcmp(argv[i],"-h") ) {
			usage(argv[0]);
		}
		else if( !strcmp(argv[i],"-v") ) {
			verbose = 1;
		}
		else if( !strcmp(argv[i],"-p") ) {
			if( use_pkt ) {
				usage(argv[0]);
			}
			use_pkt = 1;
		}
		else if( !strcmp(argv[i],"-fsk") ) {
			if( modemopt != OPT_NONE ) {
				usage(argv[0]);
			}
			modemopt = OPT_FSK;
		}
		else if( !strcmp(argv[i],"-fskclk") ) {
			if( modemopt != OPT_NONE ) {
				usage(argv[0]);
			}
			modemopt = OPT_FSKCLK;
		}
		else if( !strcmp(argv[i],"-ook") ) {
			if( modemopt != OPT_NONE ) {
				usage(argv[0]);
			}
			modemopt = OPT_OOK;
		}
		else if( !strcmp(argv[i],"-pskclk") ) {
			if( modemopt != OPT_NONE ) {
				usage(argv[0]);
			}
			modemopt = OPT_PSKCLK;
		}
		else if( !strcmp(argv[i],"-cfsk") ) {
			if( modemopt != OPT_NONE ) {
				usage(argv[0]);
			}
			modemopt = OPT_CORRFSK;
		}
		else if( !strcmp(argv[i],"-cpsk") ) {
			if( modemopt != OPT_NONE ) {
				usage(argv[0]);
			}
			modemopt = OPT_CORRPSK;
		}
		else if( !strcmp(argv[i],"-cfpsk") ) {
			if( modemopt != OPT_NONE ) {
				usage(argv[0]);
			}
			modemopt = OPT_CORRFPSKCLK;
		}
		else if( !strcmp(argv[i],"-s") ) {
			++i;
			if( i >=argc || samplerate ) {
				usage(argv[0]);
			}
			samplerate = strtoul(argv[i],0,0);
			if( !samplerate ) {
				usage(argv[0]);
			}
		}
		else if( !strcmp(argv[i],"-r") ) {
			++i;
			if( i >= argc || bitrate ) {
				usage(argv[0]);
			}
			bitrate = strtoul(argv[i],0,0);
			if( !bitrate ) {
				usage(argv[0]);
			}
		}
		else if( !strcmp(argv[i],"-bw") ) {
			++i;
			if( i >= argc || bandwidth ) {
				usage(argv[0]);
			}
			bandwidth = strtoul(argv[i],0,0);
			if( !bandwidth ) {
				usage(argv[0]);
			}
		}
		else if( !strcmp(argv[i],"-c") ) {
			++i;
			if( i >= argc || symbol_count ) {
				usage(argv[0]);
			}
			symbol_count = strtoul(argv[i],0,0);
			if( !symbol_count ) {
				usage(argv[0]);
			}
		}
		else if( !strcmp(argv[i],"-f") ) {
			++i;
			if( i >= argc || frequency ) {
				usage(argv[0]);
			}
			frequency = strtoul(argv[i],0,0);
			if( !frequency ) {
				usage(argv[0]);
			}
		}
		else if( !strcmp(argv[i],"-z") ) {
			++i;
			if( i >= argc || test_size ) {
				usage(argv[0]);
			}
			test_size = strtoul(argv[i],0,0);
			if( !test_size ) {
				usage(argv[0]);
			}
		}
		else if( !strcmp(argv[i],"-n") ) {
			++i;
			if( i >= argc || noise_amp >= 0 ) {
				usage(argv[0]);
			}
			noise_amp = strtod(argv[i],0);
			if( noise_amp < 0 || noise_amp > 1.0 ) {
				usage(argv[0]);
			}
		}
		else {
			usage(argv[0]);
		}
		++i;
	}

	if( modemopt == OPT_NONE ) {
		usage(argv[0]);
	}
	if( !bitrate ) {
		bitrate = DEFAULT_BITRATE;
	}
	if( !bandwidth ) {
		bandwidth = DEFAULT_BANDWIDTH;
	}
	if( !symbol_count ) {
		symbol_count = DEFAULT_SYMBOL_COUNT;
	}
	if( !frequency ) {
		frequency = DEFAULT_FREQUENCY;
	}
	if( !test_size ) {
		test_size = DEFAULT_TEST_SIZE;
	}
	if( noise_amp < 0 ) {
		noise_amp = DEFAULT_NOISE_AMPLITUDE;
	}
	
	if( !samplerate ) {
		if( bandwidth <= 4000 ) {
			samplerate=8000;
		}
		else if( bandwidth <= 8000 ) {
			samplerate=16000;
		}
		else if( bandwidth <= 22050 ) {
			samplerate=44100;
		}
		else {
			printf("Bandwidth is too large\n");
			exit(0);
		}
	}
	else if( samplerate < bandwidth *2 ) {
		printf("Samplerate is too small for bandwidth\n");
	}
	
	//Generate random data
	if( clock_gettime(CLOCK_MONOTONIC,&ts) ) {
		printf("Failed to get time\n");
		exit(0);
	}
	srandom(ts.tv_nsec);
	test_data = (uint8_t*)malloc(sizeof(uint8_t)*test_size);
	if( !test_data ) {
		printf("Memory allocation failure\n");
		exit(0);
	}
	
	if( verbose ) {
		printf("Test data: ");
	}
	for( ii=0; ii<test_size; ii++ ) {
		test_data[ii] = (uint8_t)random();
		if( verbose ) {
			printf("%02x ",test_data[ii]);
		}
	}
	if( verbose ) {
		printf("\n");
	}
	
	for(;;) {
		printf("Testing %zu bps  ",bitrate);
		if( modemopt == OPT_FSKCLK ) {
			modem = audiomodem_fskclk_init(samplerate, bitrate, bandwidth, symbol_count);
		}
		else if( modemopt == OPT_FSK ) {
			modem = audiomodem_fsk_init(samplerate, bitrate, bandwidth, symbol_count);
		}
		else if( modemopt == OPT_OOK ) {
			modem = audiomodem_ook_init(samplerate,bitrate,bandwidth,frequency);
		}
		else if( modemopt == OPT_PSKCLK ) {
			modem = audiomodem_pskclk_init(samplerate,bitrate,bandwidth,frequency,symbol_count);
		}
		else if( modemopt == OPT_CORRFSK ) {
			modem = audiomodem_corrfsk_init(samplerate, bitrate, bandwidth, symbol_count);
		}
		else if( modemopt == OPT_CORRPSK ) {
			modem = audiomodem_corrpsk_init(samplerate,bitrate,frequency,symbol_count);
		}
		else if( modemopt == OPT_CORRFPSKCLK ) {
			modem = audiomodem_corrfpsk_init(samplerate, bitrate, bandwidth, symbol_count);
		}
		if( !modem ) {
			printf("Create modem ");
			goto bitrate_failed;
		}
		if( use_pkt ) {
			if( audiomodem_pkt_init(modem) ) {
				printf("Create packet framer ");
				goto bitrate_failed;
			}
		}
		if( verbose ) {
			audiomodem_printinfo(modem);
			audiomodem_set_verbose(modem,verbose);
		}

		if( audiomodem_modulate(modem, &samples, &samples_len, test_data, test_size) ) {
			printf("Modulate audio ");
			goto bitrate_failed;
		}
		
		//Generate preceedeing and trailing silence (1 second each)
		ota_samples_len = samples_len+2*samplerate;
		ota_samples = realloc(ota_samples,sizeof(double)*ota_samples_len);
		if( !ota_samples ) {
			printf("Memory allocation failure\n");
			exit(0);
		}
		
		/*
		for( ii=0; ii<samples_len; ii++ ) {
			ota_samples[ii] = samples[ii];
		}
		for( ; ii < ota_samples_len; ii++ ) {
			ota_samples[ii] = 0.0;
		}
		*/
		
		for( ii=0; ii<samplerate; ii++ ) {
			ota_samples[ii] = 0.0;
		}
		for( ; ii<samplerate+samples_len; ii++ ) {
			ota_samples[ii] = samples[ii-samplerate];
		}
		for( ; ii<ota_samples_len; ii++ ) {
			ota_samples[ii] = 0.0;
		}
		
		
		//Introduce noise
		if( noise_amp ) {
			//printf("Inject noise\n");
			for( ii=0; ii<ota_samples_len; ii++ ) {
				double noise_sample = ((double)(random()-0x40000000) / 0x40000000)*noise_amp;
				ota_samples[ii] = ota_samples[ii] + noise_sample;
				if( ota_samples[ii] > 1.0 ) {
					ota_samples[ii] = 1.0;
				}
				else if( ota_samples[ii] < -1.0 ) {
					ota_samples[ii] = -1.0;
				}
			}
		}
		
		if( audiomodem_demodulate(modem,&comp_data,&comp_size,ota_samples,ota_samples_len) ) {
			printf("Demodulate audio ");
			goto bitrate_failed;
		}
		
		if( test_size > comp_size ) {
			goto bitrate_failed;
		}
		for( ii=0; ii<test_size; ii++ ) {
			if( test_data[ii] != comp_data[ii] ) {
				//printf("bad @ %zu  test(%02x) != demod(%02x)\n",ii,test_data[ii],demod_data[ii]);
				goto bitrate_failed;
			}
		}
		
		if( modem ) {
			audiomodem_destroy(modem);
			modem = 0;
		}
		printf("OK\n");
		if( bitrate > best_bitrate ) {
			best_bitrate = bitrate;
		}
		if( !max_bitrate ) {
			//Keep doubling until failure
			next_bitrate = bitrate * 2;
		}
		else {
			//Go up halfway between here at the failure point
			next_bitrate = (max_bitrate+best_bitrate) / 2;
		}
		goto next;
		
		bitrate_failed:
		if( modem ) {
			audiomodem_destroy(modem);
			modem = 0;
		}
		printf("Failed\n");
		max_bitrate = bitrate;
		//go halfway down between here at the best success point
		next_bitrate = (max_bitrate + best_bitrate) / 2;
		
		next:
		if( next_bitrate == bitrate || next_bitrate == best_bitrate) {
			break;
		}
		bitrate = next_bitrate;
	}
	
	printf("Highest possible bitrate: %zu\n",best_bitrate);
	
	if( test_data ) {
		free(test_data);
	}
	if( ota_samples ) {
		free(ota_samples);
	}
	
	return 0;
}