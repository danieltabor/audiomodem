#include <sndfile.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define BITOPS_IMPLEMENTATION
#define FSKCALIBRATE_IMPLEMENTATION
#define SRCFFT_IMPLEMENTATION
#define FSKCLK_IMPLEMENTATION
#define FSK_IMPLEMENTATION
#define OOK_IMPLEMENTATION
#define PSK_IMPLEMENTATION
#define PKT_IMPLEMENTATION
#define AUDIOMODEM_IMPLEMENTATION
#include "audiomodem.h"

#define DEFAULT_BITRATE 64
#define DEFAULT_BANDWIDTH 3000
#define DEFAULT_SYMBOL_COUNT 4
#define DEFAULT_FREQUENCY 1000

void usage(char* cmd) {
	char* filename = cmd+strlen(cmd);
	while( filename > cmd ) {
		if( *(filename-1) == '/' ) {
			break;
		}
		filename--;
	}
	printf("Usage: %s [-h] [-v] [-p] [-fsk | -fskclk | -ook | -psk] [-r bitrate]\n",filename);
	printf("  [-bw bandwidth] [-c symbol_count] [-f frequency] -o output.wav\n");
	printf("  [-i inpath | -m \"message\"]\n");
	printf("\n");
	printf("Defaults:\n");
	printf("  bitrate : %d\n",DEFAULT_BITRATE);
	printf("  bandwidth: %d\n",DEFAULT_BANDWIDTH);
	printf("  symbol_count: %d\n",DEFAULT_SYMBOL_COUNT);
	printf("  frequency: %d\n",DEFAULT_FREQUENCY);
	printf("\n");
	exit(0);
}

int main(int argc, char** argv) {
	SNDFILE* sndfile;
	SF_INFO sfinfo;
	sf_count_t frames;
	double *samples;
	sf_count_t samples_len;
	uint8_t *data;
	size_t data_len;
	audiomodem_t *modem = 0;
	char *outpath = 0;
	char *inpath = 0;
	int fd;
	int verbose = 0;
	int use_pkt = 0;
	audiomodem_type_t modem_type = COMPAT_NONE;
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
			if( modem_type != COMPAT_NONE ) {
				usage(argv[0]);
			}
			modem_type = COMPAT_FSK;
		}
		else if( !strcmp(argv[i],"-fskclk") ) {
			if( modem_type != COMPAT_NONE ) {
				usage(argv[0]);
			}
			modem_type = COMPAT_FSKCLK;
		}
		else if( !strcmp(argv[i],"-ook") ) {
			if( modem_type != COMPAT_NONE ) {
				usage(argv[0]);
			}
			modem_type = COMPAT_OOK;
		}
		else if( !strcmp(argv[i],"-psk") ) {
			if( modem_type != COMPAT_NONE ) {
				usage(argv[0]);
			}
			modem_type = COMPAT_PSK;
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
		else if( !strcmp(argv[i],"-i") ) {
			++i;
			if( i >= argc || inpath ) {
				usage(argv[0]);
			}
			inpath = argv[i];
		}
		else {
			usage(argv[0]);
		}
		++i;
	}

	if( modem_type == COMPAT_NONE ) {
		usage(argv[0]);
	}
	if( !inpath ) {
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
	
	memset(&sfinfo,0,sizeof(SF_INFO));
	sndfile = sf_open(inpath,SFM_READ,&sfinfo);
	
	if( !sndfile ) {
		printf("Failed to open %s\n",inpath);
		exit(0);
	}
	
	if( outpath ) {
		fd = open(outpath,O_WRONLY|O_CREAT|O_TRUNC,0666);
		if( fd < 0 ) {
			printf("Failed to open %s\n",outpath);
		}
	}
	
	if( modem_type == COMPAT_FSKCLK ) {
		modem = audiomodem_fskclk_init(sfinfo.samplerate, bitrate, bandwidth, symbol_count);
	}
	else if( modem_type == COMPAT_FSK ) {
		modem = audiomodem_fsk_init(sfinfo.samplerate, bitrate, bandwidth, symbol_count);
	}
	else if( modem_type == COMPAT_OOK ) {
		modem = audiomodem_ook_init(sfinfo.samplerate,bitrate,bandwidth,frequency);
	}
	else if( modem_type == COMPAT_PSK ) {
		modem = audiomodem_psk_init(sfinfo.samplerate,bitrate,bandwidth,frequency,symbol_count);
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
	if( verbose ) {
		audiomodem_printinfo(modem);
		audiomodem_set_verbose(modem,verbose);
	}
	
	samples = (double*)malloc(sizeof(double)*sfinfo.samplerate);
	if( !samples ) {
		printf("Memory allocation failed\n");
		exit(0);
	}
	frames = 0;
	while( frames < sfinfo.frames + sfinfo.samplerate ) {
		if( frames >= sfinfo.frames  ) {
			//Give an extra second of silence to let the modem flush
			for( i=0; i<sfinfo.samplerate; i++ ) {
				samples[i] = 0;
			}
			samples_len = sfinfo.samplerate;
		}
		else {
			//Read upto a second of audio
			samples_len = sf_readf_double(sndfile,samples,sfinfo.samplerate);
		}
		if( audiomodem_demodulate(modem, &data, &data_len, samples, samples_len) ) {
			printf("Failed to demodulate\n");
			exit(0);
		}
		
		if( outpath ) {
			write(fd,data,data_len);
		}
		else {
			for( i=0; i<data_len; i++ ) {
				if( data[i] >= 0x20 && data[i] <= 0x7E ) {
					printf("%c",(char)data[i]);
				}
				else {
					printf("[%02x]",data[i]);
				}
			}
		}
		frames = frames + samples_len;
	}
	
	if( modem ) {
		audiomodem_destroy(modem);
	}
	free(samples);
	sf_close(sndfile);
	return 0;
}