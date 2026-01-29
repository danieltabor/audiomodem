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
#include <stdio.h>
#include <sndfile.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define BITOPS_IMPLEMENTATION
#define CORR_IMPLEMENTATION
#define PKT_IMPLEMENTATION
#include "corr.h"
#include "pkt.h"

void usage(char* cmd) {
	char* filename = cmd+strlen(cmd);
	while( filename > cmd ) {
		if( *(filename-1) == '/' ) {
			break;
		}
		filename--;
	}
	printf("Usage: %s [-h] [-v] [-s symbol.wav]\n",filename);
	printf("  [-mod | -demod] [-n noise_amplitude]\n");
	printf("  -i inpath -o outpath\n");
	printf("\n");
	exit(0);
}

int main(int argc, char** argv) {
	SNDFILE    *sndfile;
	SF_INFO     sfinfo;
	corr_sym_t *syms = 0;
	size_t      symslen = 0;
	size_t      i;
	int         verbose = 0;
	int         use_pkt = 0;
	int         do_demod = -1;
	char       *inpath = 0;
	char       *outpath = 0;
	double      noise_amp = 0.0;
	corr_t     *corr;
	pkt_t      *pkt;
	int         fd;
	uint8_t    *data;
	size_t      datalen;
	pktdata_t  *pkts;
	size_t      pktslen;
	
	i=1;
	while( i<argc ) {
		if( !strcmp(argv[i],"-h") ) {
			usage(argv[0]);
		}
		else if( !strcmp(argv[i],"-v") ) {
			if( verbose ) {
				usage(argv[0]);
			}
			verbose = 1;
		}
		else if( !strcmp(argv[i],"-p") ) {
			if( use_pkt ) {
				usage(argv[0]);
			}
			use_pkt = 1;
		}
		else if( !strcmp(argv[i],"-s") ) {
			if( ++i >= argc ) {
				usage(argv[0]);
			}
			memset(&sfinfo,0,sizeof(SF_INFO));
			sndfile = sf_open(argv[i],SFM_READ,&sfinfo);
			if( !sndfile ) { printf("Failed to open %s\n",argv[i]); return -1;}
			symslen++;
			syms = realloc(syms,sizeof(corr_sym_t)*symslen);
			if( !syms ) { printf("Malloc error\n"); return -1; }
			syms[symslen-1].len = sfinfo.frames;
			syms[symslen-1].samples = malloc(sizeof(double)*sfinfo.frames);
			if( !syms[symslen-1].samples ) { printf("Malloc error\n"); return -1; }
			if( sf_readf_double(sndfile,syms[symslen-1].samples,sfinfo.frames) != sfinfo.frames ) {
				printf("Read error\n"); return -1;
			}
			sf_close(sndfile);
		}
		else if( !strcmp(argv[i],"-mod") ) {
			if( do_demod != -1 ) {
				usage(argv[0]);
			}
			do_demod = 0;
		}
		else if( !strcmp(argv[i],"-demod") ) {
			if( do_demod != -1 ) {
				usage(argv[0]);
			}
			do_demod = 1;
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
			if( ++i >= argc || inpath != 0 ) {
				usage(argv[0]);
			}
			inpath = argv[i];
		}
		else if( !strcmp(argv[i],"-o") ) {
			if( ++i >= argc || outpath != 0 ) {
				usage(argv[0]);
			}
			outpath = argv[i];
		}
		else {
			usage(argv[0]);
		}
		++i;
	}
	
	if( do_demod == -1 ) {
		usage(argv[0]);
	}
	if( inpath == 0 ) {
		usage(argv[0]);
	}
	if( outpath == 0 ) {
		usage(argv[0]);
	}
	if( symslen == 0 ) {
		usage(argv[0]);
	}
	
	pkt = pkt_init();
	if( !pkt ) { printf("Failed to create packet handler\n"); return -1; }
	corr = corr_init(syms,symslen);
	if( !corr ) { printf("Failed ot create modem\n"); return -1; }
	
	if( do_demod ) {
		double  samples[0xffff];
		size_t  sampleslen;
		int done = 0;
		memset(&sfinfo,0,sizeof(SF_INFO));
		sndfile = sf_open(inpath,SFM_READ,&sfinfo);
		if( !sndfile ) { printf("Failed to open: %s\n",inpath); return -1; }
		fd = open(outpath,O_WRONLY|O_TRUNC|O_CREAT,0666);
		if( fd < 0 ) { printf("Failed to open: %s\n",outpath); return -1; }
		while( !done ) {
			sampleslen = sf_readf_double(sndfile,samples,sizeof(samples)/sizeof(double));
			if( sampleslen < 0 ) {
				printf("Read Error\n");
				return -1;
			}
			if( !sampleslen ) {
				done = 1;
				//Generate an extra second to flush out the demodulator
				for( i=0; i<sfinfo.samplerate; i++ ) {
					samples[i] = 0.0;
				}
				sampleslen = sfinfo.samplerate;
			}
			if( corr_demodulate(corr, &data, &datalen, samples, sampleslen) ) {
				printf("Demodulation failed\n");
				return -1;
			}
			if( pkt_rx(pkt,&pkts,&pktslen,data,datalen) ) {
				printf("Packet Rx failed\n");
				return -1;
			}
			for( i=0; i<pktslen; i++ ) {
				if( write(fd,pkts[i].data,pkts[i].len) != pkts[i].len ) {
					printf("File Write error\n"); 
					return -1;
				}
			}
		}
		close(fd);
		sf_close(sndfile);
	}
	else {
		uint8_t  buffer[0xffff];
		size_t   bufferlen;
		double  *samples;
		size_t   sampleslen;
		memset(&sfinfo,0,sizeof(SF_INFO));
		sfinfo.samplerate = 8000;
		sfinfo.channels=1;
		sfinfo.format=SF_FORMAT_WAV|SF_FORMAT_PCM_16;
		sfinfo.sections=1;
		sfinfo.seekable=1;
		sndfile = sf_open(outpath,SFM_WRITE,&sfinfo);
		if( !sndfile ) {
			printf("Failed to open %s\n",outpath);
			return -1;
		}
		fd = open(inpath,O_RDONLY);
		if( fd < 0 ) {
			printf("Failed to open: %s\n",outpath);
			return -1;
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
		
		for(;;) {
			bufferlen = read(fd,buffer,sizeof(buffer));
			if( bufferlen < 0 ) {
				printf("Read Error\n");
				return -1;
			}
			if( !bufferlen ) { break; }
			if( pkt_tx(pkt, &data, &datalen, buffer, bufferlen) ) {
				printf("Packet Tx failed\n");
				return -1;
			}
			if( corr_modulate(corr,&samples,&sampleslen,data,datalen) ) {
				printf("Modulation failed\n");
				return -1;
			}
			if( noise_amp > 0.0 ) {
				for( i=0; i<sampleslen; i++ ) {
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
			if( sf_writef_double(sndfile,samples,sampleslen) != sampleslen ) {
				printf("Write error\n");
				return -1;
			}
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
		
		close(fd);
		sf_close(sndfile);
	}
	corr_destroy(corr);
	pkt_destroy(pkt);
	return 0;
}