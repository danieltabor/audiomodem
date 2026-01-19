#include <sndfile.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define BITOPS_IMPLEMENTATION
#include "bitops.h"

#define FSKCLK_IMPLEMENTATION
#include "fskclk.h"

#define FSK_IMPLEMENTATION
#include "fsk.h"

#define COMPATMODEM_IMPLEMENTATION
#include "compatmodem.h"

#define PACKETMODEM_IMPLEMENTATION
#include "packetmodem.h"


#define DEFAULT_BITRATE 64
#define DEFAULT_BANDWIDTH 3000
#define DEFAULT_TONE_COUNT 4
#define DEFAULT_TEST_SIZE 512
#define DEFAULT_NOISE_AMPLITUDE 0

void usage(char* cmd) {
	char* filename = cmd+strlen(cmd);
	while( filename > cmd ) {
		if( *(filename-1) == '/' ) {
			break;
		}
		filename--;
	}
	printf("Usage: %s [-h] [-v] [-p] [-clk] [-r start_bitrate] [-bw bandwidth] [-t tone_count] [-s test_size] [-n noise_amplitude]\n",filename);
	printf("\n");
	printf("Defaults:\n");
	printf("  start_bitrate: %d\n",DEFAULT_BITRATE);
	printf("  bandwidth: %d\n",DEFAULT_BANDWIDTH);
	printf("  tone_count: %d\n",DEFAULT_TONE_COUNT);
	printf("  test_size: %d\n",DEFAULT_TEST_SIZE);
	printf("  noise_amplitude: %0.1lf\n",(double)DEFAULT_NOISE_AMPLITUDE);
	printf("\n");
	exit(0);
}

int main(int argc, char** argv) {
	double *samples;
	sf_count_t samples_len;
	uint8_t *test_data = 0;
	uint8_t *mod_data;
	size_t   mod_size;
	uint8_t *demod_data;
	size_t  demod_size;
	uint8_t *comp_data;
	size_t   comp_size;
	compatmodem_t *modem = 0;
	packetmodem_t *pkt = 0;
	size_t samplerate;
	double *tmp;
	size_t max_bitrate = 0;
	size_t best_bitrate = 0;
	size_t next_bitrate;
	int verbose = 0;
	int use_pkt = 0;
	int use_clk = 0;
	size_t bitrate = 0;
	size_t bandwidth = 0;
	size_t tone_count = 0;
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
		else if( !strcmp(argv[i],"-clk") ) {
			if( use_clk ) {
				usage(argv[0]);
			}
			use_clk = 1;
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
		else if( !strcmp(argv[i],"-t") ) {
			++i;
			if( i >= argc || tone_count ) {
				usage(argv[0]);
			}
			tone_count = strtoul(argv[i],0,0);
			if( !tone_count ) {
				usage(argv[0]);
			}
		}
		else if( !strcmp(argv[i],"-s") ) {
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

	if( !bitrate ) {
		bitrate = DEFAULT_BITRATE;
	}
	if( !bandwidth ) {
		bandwidth = DEFAULT_BANDWIDTH;
	}
	if( !tone_count ) {
		tone_count = DEFAULT_TONE_COUNT;
	}
	if( !test_size ) {
		test_size = DEFAULT_TEST_SIZE;
	}
	if( noise_amp < 0 ) {
		noise_amp = DEFAULT_NOISE_AMPLITUDE;
	}
	
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
		if( use_clk ) {
			modem = compatmodem_fskclk_init(samplerate, bitrate, bandwidth, tone_count);
		}
		else {
			modem = compatmodem_fsk_init(samplerate, bitrate, bandwidth, tone_count);
		}
		if( !modem ) {
			printf("Create modem ");
			goto bitrate_failed;
		}
		if( verbose ) {
			compatmodem_printinfo(modem);
			compatmodem_set_verbose(modem,verbose);
		}
		if( use_pkt ) {
			pkt = packetmodem_init(3);
			if( !pkt ) {
				printf("Create packetmodem ");
				goto bitrate_failed;
			}
			if( packetmodem_tx(pkt,&mod_data,&mod_size,test_data,test_size) ) {
				printf("Generate packet ");
				goto bitrate_failed;
			}
		}
		else {
			mod_data = test_data;
			mod_size = test_size;
		}
		if( compatmodem_modulate(modem, &samples, &samples_len, mod_data, mod_size) ) {
			printf("Modulate audio ");
			goto bitrate_failed;
		}
		
		//Generate preceedeing and trailing silence (1 second each)
		tmp = realloc(samples,sizeof(double)*(samples_len+2*samplerate));
		if( !tmp ) {
			printf("Memory allocation failure\n");
			exit(0);
		}
		samples = tmp;
		if( use_clk ) {
			modem->fskclk->mod_samples = tmp;
		}
		else {
			modem->fsk->mod_samples = tmp;
		}
		ii=(samples_len+2*samplerate);
		do {
			ii--;
			if( ii > samples_len+samplerate ) {
				samples[ii] = 0.0;
			}
			else if( ii > samplerate ) {
				samples[ii] = samples[ii-samplerate];
			}
			else {
				samples[ii] = 0.0;
			}
		} while(ii!=0);
		samples_len = samples_len+samplerate;
		
		//Introduce noise
		if( noise_amp ) {
			//printf("Inject noise\n");
			for( ii=0; ii<samples_len; ii++ ) {
				samples[ii] = samples[ii] + ((double)(random()-0x40000000) / 0x40000000)*noise_amp;
				if( samples[ii] > 1.0 ) {
					samples[ii] = 1.0;
				}
				else if( samples[ii] < -1.0 ) {
					samples[ii] = -1.0;
				}
			}
		}
		if( compatmodem_demodulate(modem, &demod_data, &demod_size, samples, samples_len) ) {
			printf("Demodulate audio ");
			goto bitrate_failed;
		}
		if( use_pkt ) {
			if( packetmodem_rx(pkt,&comp_data,&comp_size,demod_data,demod_size) ) {
				printf("Receive packet ");
				
			}
		}
		else {
			comp_data = demod_data;
			comp_size = demod_size;
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
		
		if( pkt ) {
			packetmodem_destroy(pkt);
			pkt = 0;
		}
		if( modem ) {
			compatmodem_destroy(modem);
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
		if( pkt ) {
			packetmodem_destroy(pkt);
			pkt = 0;
		}
		if( modem ) {
			compatmodem_destroy(modem);
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
	
	free(test_data);
	return 0;
}