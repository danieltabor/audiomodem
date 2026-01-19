#include <sndfile.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

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

void usage(char* cmd) {
	char* filename = cmd+strlen(cmd);
	while( filename > cmd ) {
		if( *(filename-1) == '/' ) {
			break;
		}
		filename--;
	}
	printf("Usage: %s [-h] [-v] [-p] [-clk] [-r bitrate] [-bw bandwidth] [-t tone_count] -o output.wav [-i inpath | -m \"message\"]\n",filename);
	printf("\n");
	printf("Defaults:\n");
	printf("  bitrate : %d\n",DEFAULT_BITRATE);
	printf("  bandwidth: %d\n",DEFAULT_BANDWIDTH);
	printf("  tone_count: %d\n",DEFAULT_TONE_COUNT);
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
	uint8_t *rawdata = 0;
	size_t rawdata_len = 0;
	uint8_t *data = 0;
	size_t data_len = 0;
	compatmodem_t *modem = 0;;
	packetmodem_t *pkt = 0;
	char *outpath = 0;
	char *inpath = 0;
	int fd;
	int verbose = 0;
	int use_pkt = 0;
	int use_clk = 0;
	size_t bitrate = 0;
	size_t bandwidth = 0;
	size_t tone_count = 0;
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
		else if( !strcmp(argv[i],"-o") ) {
			++i;
			if( i >= argc || outpath ) {
				usage(argv[0]);
			}
			outpath = argv[i];
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
			rawdata = (uint8_t*)argv[i];
			rawdata_len = strlen(argv[i]);
		}
		else {
			usage(argv[0]);
		}
		++i;
	}

	if( !outpath ) {
		usage(argv[0]);
	}
	if( !inpath && !rawdata ) {
		usage(argv[0]);
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
	
	if( bandwidth <= 4000 ) {
		sfinfo.samplerate=8000;
	}
	else if( bandwidth <= 8000 ) {
		sfinfo.samplerate=16000;
	}
	else if( bandwidth <= 22050 ) {
		sfinfo.samplerate=44100;
	}
	else {
		printf("Bandwidth is too large\n");
		exit(0);
	}
	sfinfo.channels=1;
	sfinfo.format=SF_FORMAT_WAV|SF_FORMAT_PCM_16;
	sfinfo.sections=1;
	sfinfo.seekable=1;
	sndfile = sf_open(outpath,SFM_WRITE,&sfinfo);
	if( !sndfile ) {
		printf("Failed to open %s\n",outpath);
		exit(0);
	}
	
	if( use_clk ) {
		modem = compatmodem_fskclk_init(sfinfo.samplerate, bitrate, bandwidth, tone_count);
	}
	else {
		modem = compatmodem_fsk_init(sfinfo.samplerate, bitrate, bandwidth, tone_count);
	}
	if( !modem ) {
		printf("Failed to create modem\n");
		exit(0);
	}
	if( verbose ) {
		compatmodem_printinfo(modem);
		compatmodem_set_verbose(modem,verbose);
	}
	
	if( use_pkt ) {
		pkt = packetmodem_init(3);
		if( !pkt ) {
			printf("Failed to create packet modem\n");
			exit(0);
		}
	}
	
	if( inpath ) {
		fd = open(inpath,O_RDONLY);
		if( fd < 0 ) {
			printf("Failed to open %s\n",inpath);
			exit(0);
		}
		rawdata = (uint8_t*)malloc(sizeof(uint8_t)*1024);
		if( !rawdata ) {
			printf("Memory allocation failed\n");
			exit(0);
		}
		for(;;) {
			readlen = read(fd,rawdata,1024);
			if( readlen <= 0 ) { break; }
			rawdata_len = (size_t)readlen;
			if( use_pkt ) {
				if( packetmodem_tx(pkt,&data,&data_len,rawdata,rawdata_len) ) {
					printf("Failed to generate packet\n");
					exit(0);
				}
			}
			else {
				data = rawdata;
				data_len = rawdata_len;
			}
			if( compatmodem_modulate(modem, &samples, &samples_len, data, data_len) ) {
				printf("Failed to generate audio\n");
				exit(0);
			}
			sf_writef_double(sndfile,samples,samples_len);
		}
		free(data);
	}
	else {
		if( use_pkt ) {
			if( packetmodem_tx(pkt,&data,&data_len,rawdata,rawdata_len) ) {
				printf("Failed to generate packet\n");
				exit(0);
			}
		}
		else {
			data = rawdata;
			data_len = rawdata_len;
		}
		if( compatmodem_modulate(modem, &samples, &samples_len, data, data_len) ) {
			printf("Failed to generate audio\n");
			exit(0);
		}
		sf_writef_double(sndfile,samples,samples_len);
	}
	
	if( modem ) {
		compatmodem_destroy(modem);
	}
	if( pkt ) {
		packetmodem_destroy(pkt);
	}
	sf_close(sndfile);
	return 0;
}