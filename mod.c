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
#include <sndfile.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

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

typedef enum {OPT_NONE,OPT_FSK,OPT_FSKCLK,OPT_OOK,OPT_PSKCLK,OPT_CORRFSK,OPT_CORRPSK,OPT_CORRFPSK} modemopt_t;

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
	printf("  [-n noise_amplitude] [-i inpath | -m \"message\"] -o output.wav\n");
	printf("\n");
	printf("Defaults:\n");
	printf("  samplerate: based on bandwidth\n");
	printf("  bitrate   : %d\n",DEFAULT_BITRATE);
	printf("  bandwidth : %d\n",DEFAULT_BANDWIDTH);
	printf("  symbol_count: %d\n",DEFAULT_SYMBOL_COUNT);
	printf("  frequency : %d\n",DEFAULT_FREQUENCY);
	printf("\n");
	exit(0);
}

int main(int argc, char** argv) {
	SNDFILE* sndfile;
	SF_INFO sfinfo;
	sf_count_t frames;
	double *samples;
	sf_count_t samples_len;
	ssize_t readlen;
	uint8_t *data = 0;
	size_t data_len = 0;
	audiomodem_t *modem = 0;;
	char *outpath = 0;
	char *inpath = 0;
	int fd;
	int verbose = 0;
	int use_pkt = 0;
	double noise_amp = 0.0;
	modemopt_t modemopt = OPT_NONE;
	size_t samplerate = 0;
	size_t bitrate = 0;
	size_t bandwidth = 0;
	size_t symbol_count = 0;
	size_t frequency = 0;
	int i = 1;
	
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
			modemopt = OPT_CORRFPSK;
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
		else if( !strcmp(argv[i],"-o") ) {
			++i;
			if( i >= argc || outpath ) {
				usage(argv[0]);
			}
			outpath = argv[i];
		}
		else if( !strcmp(argv[i],"-n") ) {
			++i;
			if( i >= argc || noise_amp > 0.0 ) {
				usage(argv[0]);
			}
			noise_amp = strtod(argv[i],0);
			if( noise_amp <= 0.0 || noise_amp > 1.0 ) {
				usage(argv[0]);
			}
		}
		else if( !strcmp(argv[i],"-i") ) {
			++i;
			if( i >= argc || inpath || data ) {
				usage(argv[0]);
			}
			inpath = argv[i];
		}
		else if( !strcmp(argv[i],"-m") ) {
			++i;
			if( i >= argc || inpath || data ) {
				usage(argv[0]);
			}
			data = (uint8_t*)argv[i];
			data_len = strlen(argv[i]);
		}
		else {
			usage(argv[0]);
		}
		++i;
	}

	if( modemopt == OPT_NONE ) {
		usage(argv[0]);
	}
	if( !outpath ) {
		usage(argv[0]);
	}
	if( !inpath && !data ) {
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
	
	sfinfo.samplerate = samplerate;
	sfinfo.channels=1;
	sfinfo.format=SF_FORMAT_WAV|SF_FORMAT_PCM_16;
	sfinfo.sections=1;
	sfinfo.seekable=1;
	sndfile = sf_open(outpath,SFM_WRITE,&sfinfo);
	if( !sndfile ) {
		printf("Failed to open %s\n",outpath);
		exit(0);
	}
	
	if( modemopt == OPT_FSKCLK ) {
		modem = audiomodem_fskclk_init(sfinfo.samplerate, bitrate, bandwidth, symbol_count);
	}
	else if( modemopt == OPT_FSK ) {
		modem = audiomodem_fsk_init(sfinfo.samplerate, bitrate, bandwidth, symbol_count);
	}
	else if( modemopt == OPT_OOK ) {
		modem = audiomodem_ook_init(sfinfo.samplerate,bitrate,bandwidth,frequency);
	}
	else if( modemopt == OPT_PSKCLK ) {
		modem = audiomodem_pskclk_init(sfinfo.samplerate,bitrate,bandwidth,frequency,symbol_count);
	}
	else if( modemopt == OPT_CORRFSK ) {
		modem = audiomodem_corrfsk_init(sfinfo.samplerate, bitrate, bandwidth, symbol_count);
	}
	else if( modemopt == OPT_CORRPSK ) {
		modem = audiomodem_corrpsk_init(sfinfo.samplerate,bitrate,frequency,symbol_count);
	}
	else if( modemopt == OPT_CORRFPSK ) {
		modem = audiomodem_corrfpsk_init(sfinfo.samplerate, bitrate, bandwidth, symbol_count);
	}
	if( !modem ) {
		printf("Failed to create modem\n");
		exit(0);
	}
	if( use_pkt ) {
		if( audiomodem_pkt_init(modem) ) {
			printf("Failed to create packet framer\n");
			exit(0);
		}
	}
	audiomodem_set_verbose(modem,verbose);
	if( verbose ) {
		audiomodem_printinfo(modem);
	}
	
	if( noise_amp > 0.0 ) {
		//Seed random generator
		struct timespec ts;
		if( clock_gettime(CLOCK_MONOTONIC,&ts) ) {
			printf("Failed to get time\n");
			exit(0);
		}
		srandom(ts.tv_nsec);

		//Generate 1 second of noise
		for( i=0; i<8000; i++ ) {
			double noise_sample = ((double)(random()-0x40000000) / 0x40000000)*noise_amp;
			if( sf_writef_double(sndfile,&noise_sample,1) != 1 ) {
				printf("Write error\n");
				return -1;
			}
		}
	}
	
	if( inpath ) {
		fd = open(inpath,O_RDONLY);
		if( fd < 0 ) {
			printf("Failed to open %s\n",inpath);
			exit(0);
		}
		data = (uint8_t*)malloc(sizeof(uint8_t)*1024);
		if( !data ) {
			printf("Memory allocation failed\n");
			exit(0);
		}
		for(;;) {
			readlen = read(fd,data,1024);
			if( readlen <= 0 ) { break; }
			data_len = (size_t)readlen;
			if( audiomodem_modulate(modem, &samples, &samples_len, data, data_len) ) {
				printf("Failed to generate audio\n");
				exit(0);
			}
			if( noise_amp > 0.0 ) {
				for( i=0; i<samples_len; i++ ) {
					double noise_sample = ((double)(random()-0x40000000) / 0x40000000)*noise_amp;
					samples[i] = samples[i] + noise_sample;
					if( samples[i] > 1.0 ) {
						samples[i] = 1.0;
					}
					else if( samples[i] < -1.0 ) {
						samples[i] = -1.0;
					}
				}
			}
			sf_writef_double(sndfile,samples,samples_len);
		}
		free(data);
	}
	else {
		if( audiomodem_modulate(modem, &samples, &samples_len, data, data_len) ) {
			printf("Failed to generate audio\n");
			exit(0);
		}
		if( noise_amp > 0.0 ) {
			for( i=0; i<samples_len; i++ ) {
				double noise_sample = ((double)(random()-0x40000000) / 0x40000000)*noise_amp;
				samples[i] = samples[i] + noise_sample;
				if( samples[i] > 1.0 ) {
					samples[i] = 1.0;
				}
				else if( samples[i] < -1.0 ) {
					samples[i] = -1.0;
				}
			}
		}
		sf_writef_double(sndfile,samples,samples_len);
	}
	
	if( noise_amp > 0.0 ) {
		//Generate 1 second of noise
		for( i=0; i<8000; i++ ) {
			double noise_sample = ((double)(random()-0x40000000) / 0x40000000)*noise_amp;
			if( sf_writef_double(sndfile,&noise_sample,1) != 1) {
				printf("Write error\n");
				return -1;
			}
		}
	}
	
	if( modem ) {
		audiomodem_destroy(modem);
	}
	sf_close(sndfile);
	return 0;
}