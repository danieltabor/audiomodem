#ifndef __COMPATMODEM_H__
#define __COMPATMODEM_H__

#include "fskclk.h"
#include "fsk.h"

typedef enum {
	COMPAT_FSKCLK,
	COMPAT_FSK,
} compatmodem_type_t;

typedef struct {
	compatmodem_type_t type;
	union {
		fskclk_t *fskclk;
		fsk_t    *fsk;
	};
} compatmodem_t;

compatmodem_t *compatmodem_fskclk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count);
compatmodem_t *compatmodem_fsk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count);
void           compatmodem_destroy(compatmodem_t *modem);
int            compatmodem_set_thresh(compatmodem_t *modem, double thresh);
int            compatmodem_set_verbose(compatmodem_t *modem, int verbose);
void           compatmodem_printinfo(compatmodem_t *modem);
int            compatmodem_modulate(compatmodem_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen);
int            compatmodem_demodulate(compatmodem_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen);

#endif //__COMPATMODEM_H__

#ifdef COMPATMODEM_IMPLEMENTATION
#undef COMPATMODEM_IMPLEMENTATION

compatmodem_t *compatmodem_fskclk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count) {
	compatmodem_t *modem;
	
	modem = malloc(sizeof(compatmodem_t));
	if( !modem ) { return 0; }
	
	modem->type = COMPAT_FSKCLK;
	modem->fskclk = fskclk_init(samplerate,bitrate,bandwidth,tone_count);
	if( !modem->fskclk ) { compatmodem_destroy(modem); return 0; }
	return modem;
}

compatmodem_t *compatmodem_fsk_init(size_t samplerate, size_t bitrate, size_t bandwidth, size_t tone_count) {
	compatmodem_t *modem;
	
	modem = malloc(sizeof(compatmodem_t));
	if( !modem ) { return 0; }
	
	modem->type = COMPAT_FSK;
	modem->fsk = fsk_init(samplerate,bitrate,bandwidth,tone_count);
	if( !modem->fsk ) { compatmodem_destroy(modem); return 0; }
	return modem;
}

void compatmodem_destroy(compatmodem_t *modem) {
	if( modem ) {
		if( modem->type == COMPAT_FSKCLK ) {
			fskclk_destroy(modem->fskclk);
		}
		else if( modem->type == COMPAT_FSK ) {
			fsk_destroy(modem->fsk);
		}
		memset(modem,0,sizeof(compatmodem_t));
		free(modem);
	}
}

int compatmodem_set_thresh(compatmodem_t *modem, double thresh) {
	if( !modem ) { return -1; }
	if( modem->type == COMPAT_FSKCLK ) {
		return fskclk_set_thresh(modem->fskclk,thresh);
	}
	else if( modem->type == COMPAT_FSK ) {
		return fsk_set_thresh(modem->fsk,thresh);
	}
	else {
		return -1;
	}
}

int compatmodem_set_verbose(compatmodem_t *modem, int verbose) {
	if( !modem ) { return -1; }
	if( modem->type == COMPAT_FSKCLK ) {
		return fskclk_set_verbose(modem->fskclk,verbose);
	}
	else if( modem->type == COMPAT_FSK ) {
		return fsk_set_verbose(modem->fsk,verbose);
	}
	else {
		return -1;
	}
}

void compatmodem_printinfo(compatmodem_t *modem) {
	if( modem ) {
		if( modem->type == COMPAT_FSKCLK ) {
			fskclk_printinfo(modem->fskclk);
		}
		else if( modem->type == COMPAT_FSK ) {
			fsk_printinfo(modem->fsk);
		}
	}
}

int compatmodem_modulate(compatmodem_t *modem, double **samples, size_t *sampleslen, uint8_t *data, size_t datalen) {
	if( !modem ) { return -1; }
	if( modem->type == COMPAT_FSKCLK ) {
		return fskclk_modulate(modem->fskclk,samples,sampleslen,data,datalen);
	}
	else if( modem->type == COMPAT_FSK ) {
		return fsk_modulate(modem->fsk,samples,sampleslen,data,datalen);
	}
	else {
		return -1;
	}
}

int compatmodem_demodulate(compatmodem_t *modem, uint8_t **data, size_t *datalen, double *samples, size_t sampleslen) {
	if( !modem ) { return -1; }
	if( modem->type == COMPAT_FSKCLK ) {
		return fskclk_demodulate(modem->fskclk,data,datalen,samples,sampleslen);
	}
	else if( modem->type == COMPAT_FSK ) {
		return fsk_demodulate(modem->fsk,data,datalen,samples,sampleslen);
	}
	else {
		return -1;
	}
}

#endif //COMPATMODEM_IMPLEMENTATION