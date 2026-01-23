#ifndef __AUDIOMODEM_H__
#define __AUDIOMODEM_H__

#include "fskclk.h"
#include "fsk.h"
#include "ook.h"
#include "pkt.h"

typedef enum {
	COMPAT_NONE,
	COMPAT_FSKCLK,
	COMPAT_FSK,
	COMPAT_OOK,
} audiomodem_type_t;

typedef struct {
	audiomodem_type_t type;
	union {
		fskclk_t *fskclk;
		fsk_t    *fsk;
		ook_t    *ook;
	};
	pkt_t *pkt;
} audiomodem_t;

audiomodem_t *audiomodem_fskclk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count);
audiomodem_t *audiomodem_fsk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count);
audiomodem_t *audiomodem_ook_init(size_t samplerate, size_t bitrate, double freq);
int           audiomodem_pkt_init(audiomodem_t *modem);
void          audiomodem_destroy(audiomodem_t *modem);
int           audiomodem_set_thresh(audiomodem_t *modem, double thresh);
int           audiomodem_set_verbose(audiomodem_t *modem, int verbose);
void          audiomodem_printinfo(audiomodem_t *modem);
int           audiomodem_modulate(audiomodem_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen);
int           audiomodem_demodulate(audiomodem_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen);

#endif //__AUDIOMODEM_H__

#ifdef AUDIOMODEM_IMPLEMENTATION
#undef AUDIOMODEM_IMPLEMENTATION

audiomodem_t *audiomodem_fskclk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count) {
	audiomodem_t *modem;
	
	modem = malloc(sizeof(audiomodem_t));
	if( !modem ) { return 0; }
	
	modem->type = COMPAT_FSKCLK;
	modem->fskclk = fskclk_init(samplerate,bitrate,bandwidth,tone_count);
	if( !modem->fskclk ) { audiomodem_destroy(modem); return 0; }
	modem->pkt = 0;
	return modem;
}

audiomodem_t *audiomodem_fsk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count) {
	audiomodem_t *modem;
	
	modem = malloc(sizeof(audiomodem_t));
	if( !modem ) { return 0; }
	
	modem->type = COMPAT_FSK;
	modem->fsk = fsk_init(samplerate,bitrate,bandwidth,tone_count);
	if( !modem->fsk ) { audiomodem_destroy(modem); return 0; }
	modem->pkt = 0;
	return modem;
}

audiomodem_t *audiomodem_ook_init(size_t samplerate, size_t bitrate, double freq) {
	audiomodem_t *modem;
	
	modem = malloc(sizeof(audiomodem_t));
	if( !modem ) { return 0; }
	
	modem->type = COMPAT_OOK;
	modem->ook = ook_init(samplerate,bitrate,freq);
	if( !modem->ook ) { audiomodem_destroy(modem); return 0; }
	modem->pkt = 0;
	return modem;
}

int audiomodem_pkt_init(audiomodem_t *modem) {
	if( !modem ) { return -1; }
	modem->pkt = pkt_init();
	if( !modem->pkt ) { return -1; }
	return 0;
}

void audiomodem_destroy(audiomodem_t *modem) {
	if( modem ) {
		if( modem->type == COMPAT_FSKCLK ) {
			fskclk_destroy(modem->fskclk);
		}
		else if( modem->type == COMPAT_FSK ) {
			fsk_destroy(modem->fsk);
		}
		else if( modem->type == COMPAT_OOK ) {
			ook_destroy(modem->ook);
		}
		if( modem->pkt ) {
			pkt_destroy(modem->pkt);
		}
		memset(modem,0,sizeof(audiomodem_t));
		free(modem);
	}
}

int audiomodem_set_thresh(audiomodem_t *modem, double thresh) {
	if( !modem ) { return -1; }
	if( modem->type == COMPAT_FSKCLK ) {
		return fskclk_set_thresh(modem->fskclk,thresh);
	}
	else if( modem->type == COMPAT_FSK ) {
		return fsk_set_thresh(modem->fsk,thresh);
	}
	else if( modem->type == COMPAT_OOK ) {
		return ook_set_thresh(modem->ook,thresh);
	}
	else {
		return -1;
	}
}

int audiomodem_set_verbose(audiomodem_t *modem, int verbose) {
	if( !modem ) { return -1; }
	if( modem->type == COMPAT_FSKCLK ) {
		if( fskclk_set_verbose(modem->fskclk,verbose) ) {
			return -1;
		}
	}
	else if( modem->type == COMPAT_FSK ) {
		if( fsk_set_verbose(modem->fsk,verbose) ) { 
			return -1;
		}
	}
	else if( modem->type == COMPAT_OOK ) {
		if( ook_set_verbose(modem->ook,verbose) ) { 
			return -1;
		}
	}
	else {
		return -1;
	}
	if( modem->pkt ) {
		if( pkt_set_verbose(modem->pkt,verbose) ) {
			return -1;
		}
	}
	return 0;
}

void audiomodem_printinfo(audiomodem_t *modem) {
	if( modem ) {
		if( modem->type == COMPAT_FSKCLK ) {
			fskclk_printinfo(modem->fskclk);
		}
		else if( modem->type == COMPAT_FSK ) {
			fsk_printinfo(modem->fsk);
		}
		else if( modem->type == COMPAT_OOK ) {
			ook_printinfo(modem->ook);
		}
	}
}

int audiomodem_modulate(audiomodem_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen) {
	uint8_t *mod_data;
	size_t   mod_datalen;
	
	if( !modem ) { return -1; }
	
	if( modem->pkt ) {
		if( pkt_tx(modem->pkt,&mod_data,&mod_datalen,data,datalen) ) {
			return -1;
		}
	} else {
		mod_data = data;
		mod_datalen = datalen;
	}
	
	if( modem->type == COMPAT_FSKCLK ) {
		return fskclk_modulate(modem->fskclk,samples,sampleslen,mod_data,mod_datalen);
	}
	else if( modem->type == COMPAT_FSK ) {
		return fsk_modulate(modem->fsk,samples,sampleslen,mod_data,mod_datalen);
	}
	else if( modem->type == COMPAT_OOK ) {
		return ook_modulate(modem->ook,samples,sampleslen,mod_data,mod_datalen);
	}
	else {
		return -1;
	}
}

int audiomodem_demodulate(audiomodem_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen) {
	uint8_t *demod_data;
	size_t   demod_datalen;
	
	if( !modem ) { return -1; }
	
	if( modem->type == COMPAT_FSKCLK ) {
		if( fskclk_demodulate(modem->fskclk,&demod_data,&demod_datalen,samples,sampleslen) ) {
			return -1;
		}
	}
	else if( modem->type == COMPAT_FSK ) {
		if( fsk_demodulate(modem->fsk,&demod_data,&demod_datalen,samples,sampleslen) ) {
			return -1;
		}
	}
	else if( modem->type == COMPAT_OOK ) {
		if( ook_demodulate(modem->ook,&demod_data,&demod_datalen,samples,sampleslen) ) {
			return -1;
		}
	}
	else {
		return -1;
	}
	
	if( modem->pkt ) {
		if( pkt_rx(modem->pkt,data,datalen,demod_data,demod_datalen) ) {
			return -1;
		}
	} else {
		*data = demod_data;
		*datalen = demod_datalen;
	}
	return 0;
}

#endif //AUDIOMODEM_IMPLEMENTATION