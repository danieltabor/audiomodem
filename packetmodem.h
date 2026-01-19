#ifndef __PACKETMODEM_H__
#define __PACKETMODEM_H__

#include "bitops.h"

#define PACKETMODEM_DEFAULT_SYNC        0xC9A5
#define PACKETMODEM_DEFAULT_SYNCLEN     2

typedef struct {
	size_t   redundancy;
	uint8_t *sync;
	size_t   synclen;
	uint8_t *mask;
	size_t   masklen;
	
	uint8_t *tx_pkt;
	size_t   tx_pktlen;
	
	int      rx_synced;
	uint8_t *rx_sync;
	size_t   rx_synclen;
	uint8_t *rx_buf;
	size_t   rx_buflen;
	size_t   rx_bitoff;
	uint8_t *rx_pkt;
	size_t   rx_pktlen;
} packetmodem_t;

packetmodem_t *packetmodem_init(size_t redundancy);
void           packetmodem_destroy(packetmodem_t *modem);
int            packetmodem_set_sync(packetmodem_t *modem, uint8_t *sync, size_t synclen);
int            packetmodem_set_mask(packetmodem_t *modem, uint8_t *mask, size_t masklen);
int            packetmodem_tx(packetmodem_t *modem, uint8_t **pktdata, size_t *pktdatalen, uint8_t *rawdata, size_t rawdatalen);
int            packetmodem_rx(packetmodem_t *modem, uint8_t **rxdata, size_t *rxdatalen, uint8_t *rawdata, size_t rawdatalen);

#endif //__PACKETMODEM_H__

#ifdef PACKETMODEM_IMPLEMENTATION
#undef PACKETMODEM_IMPLEMENTATION

packetmodem_t *packetmodem_init(size_t redundancy) {
	packetmodem_t *modem;
	
	if( redundancy == 0 ) { redundancy = 1; }
	if( redundancy % 2 == 0 ) { return 0; }
	
	modem = (packetmodem_t*)malloc(sizeof(packetmodem_t));
	if( !modem ) { return 0; }
	memset(modem,0,sizeof(packetmodem_t));

	modem->redundancy = redundancy;
	
	modem->sync = (uint8_t*)malloc(sizeof(uint8_t)*1);
	if( !modem->sync ) { goto packetmodem_init_error; }
	modem->sync[0] = 0xC9;
	modem->synclen = 1;
	
	modem->rx_sync = (uint8_t*)malloc(sizeof(uint8_t)*1);
	if( !modem->rx_sync ) { goto packetmodem_init_error; }
	memset(modem->rx_sync,0,1);
	modem->rx_sync[0] = 0;
	modem->rx_synclen = 1;
	
	modem->rx_buf = (uint8_t*)malloc(sizeof(uint8_t)*modem->redundancy);
	if( ! modem->rx_buf ) { goto packetmodem_init_error; };
	
	modem->mask = (uint8_t*)malloc(sizeof(uint8_t)*2);
	if( !modem->mask ) { goto packetmodem_init_error; }
	modem->mask[0] = 0x5A;
	modem->mask[1] = 0xA5;
	modem->masklen = 2;

	return modem;
		
	packetmodem_init_error:
	packetmodem_destroy(modem);
	return 0;
}

void packetmodem_destroy(packetmodem_t *modem) {
	if( modem ) {
		if( modem->sync ) { free(modem->sync); }
		if( modem->rx_sync ) { free(modem->rx_sync); }
		if( modem->rx_buf ) { free(modem->rx_buf); }
		if( modem->mask ) { free(modem->mask); }
		if( modem->tx_pkt ) { free(modem->tx_pkt); }
		if( modem->rx_pkt ) { free(modem->rx_pkt); }
		free(modem);
	}
}

int packetmodem_set_sync(packetmodem_t *modem, uint8_t *sync, size_t synclen) {
	uint8_t *tmp;
	if( !modem ) { return -1; }
	tmp = (uint8_t*)realloc(modem->sync,sizeof(uint8_t)*synclen);
	if( !tmp ) { return -1; }
	modem->sync = tmp;
	modem->synclen = synclen;
	memcpy(modem->sync,sync,synclen);
	
	tmp = (uint8_t*)realloc(modem->rx_sync,sizeof(uint8_t)*synclen);
	if( !tmp ) { return -1; }
	modem->rx_sync = tmp;
	modem->rx_synclen = synclen;
	memset(modem->rx_sync,0,synclen);
	
	modem->rx_synced = 0;
	return 0;
}

int packetmodem_set_mask(packetmodem_t *modem, uint8_t *mask, size_t masklen) {
	uint8_t *tmp;
	if( !modem ) { return -1; }
	tmp = (uint8_t*)realloc(modem->mask,sizeof(uint8_t)*masklen);
	if( !tmp ) { return -1; }
	modem->mask = tmp;
	modem->masklen = masklen;
	memcpy(modem->mask,mask,masklen);
	return 0;
}

int packetmodem_tx(packetmodem_t *modem, uint8_t **pktdata, size_t *pktdatalen, uint8_t *rawdata, size_t rawdatalen) {
	uint8_t *tmp;
	size_t i,j;
	size_t dst;
	int bit;
	uint8_t pktlen16[2];
	size_t pktalloc;
	
	if( !modem ) { return -1; }
	if( !pktdata ) { return -1; }
	if( !pktdatalen ) { return -1; }
	if( !rawdata ) { return -1; }
	if( rawdatalen > 0xffff ) { return -1; }
	
	pktalloc = (modem->synclen+2+rawdatalen)*modem->redundancy;
	tmp = realloc(modem->tx_pkt,pktalloc);
	if( !tmp ) { return -1; }
	modem->tx_pkt = tmp;
	modem->tx_pktlen = pktalloc;
	memset(modem->tx_pkt,0,pktalloc);
	
	//Pack the redundant bits of the sync
	dst = 0;
	for( i=0; i<modem->synclen*8; i++ ) {
		bit = getbits(modem->sync, modem->synclen, i, 1);
		for( j=0; j<modem->redundancy; j++ ) {
			putbits(modem->tx_pkt, modem->tx_pktlen, dst++, 1, bit);
		}
	}
	pktlen16[0] = (rawdatalen>>8)&0xff;
	pktlen16[1] = (rawdatalen>>0)&0xff;
	//Pack the redundant bits of packet length
	for( i=0; i<16; i++ ) {
		bit = getbits(pktlen16,2,i,1);
		for( j=0; j<modem->redundancy; j++ ) {
			putbits(modem->tx_pkt, modem->tx_pktlen, dst++, 1, bit);
		}
	}
	//Pack the redundant bits of the data
	for( i=0; i<rawdatalen*8; i++ ) {
		bit = getbits(rawdata, rawdatalen, i, 1);
		for( j=0; j<modem->redundancy; j++ ) {
			putbits(modem->tx_pkt, modem->tx_pktlen, dst++, 1, bit);
		}
	}
	//Apply the mask (after the sync)
	for( i=modem->synclen*modem->redundancy, j=0; i<modem->tx_pktlen; i++, j++ ) {
		modem->tx_pkt[i] = modem->tx_pkt[i] ^ modem->mask[j%modem->masklen];
	}
	
	//Return the packed data
	*pktdata = modem->tx_pkt;
	*pktdatalen = modem->tx_pktlen;
	return 0;
}

int packetmodem_rx(packetmodem_t *modem, uint8_t **rxdata, size_t *rxdatalen, uint8_t *rawdata, size_t rawdatalen) {
	size_t bitoff;
	size_t rawoff;
	size_t i;
	int bit;
	int bit_votes;
	int mask;
	uint8_t *tmp;
	uint16_t pktlen16;
	
	if( !modem ) { return -1; }
	if( !rxdata ) { return -1; }
	if( !rxdatalen ) { return -1; }
	if( !rawdata ) { return -1; }
	
	rawoff = 0;
	while( rawoff < rawdatalen ) {
		//Move redudancy worth of bytes into initial buffer
		while( modem->rx_buflen < modem->redundancy && rawoff < rawdatalen ) {
			modem->rx_buf[modem->rx_buflen++] = rawdata[rawoff++];
		}
	
		if( modem->rx_buflen == modem->redundancy ) {
			//Process bits in rx_buf
			bitoff = 0;
			while( bitoff < modem->rx_buflen*8 ) {
				//Pull redundant bits and vote
				bit_votes = 0;
				for( i=0; i<modem->redundancy; i++ ) {
					bit = getbits(modem->rx_buf, modem->rx_buflen, bitoff++, 1);
					if( modem->rx_synced ) {
						//Apply the mask for all bits after the sync
						mask = getbits(modem->mask,modem->masklen,(modem->rx_bitoff*3+i)%(modem->masklen*8),1);
						bit = bit ^ mask;
					}
					bit_votes = bit_votes + bit;
				}
				if( bit_votes > modem->redundancy/2 ) {
					bit = 1;
				}
				else {
					bit = 0;
				}
				
				if( !modem->rx_synced ) {
					//Try to find the sync
					shiftbits(modem->rx_sync,modem->rx_synclen,1);
					putbits(modem->rx_sync,modem->rx_synclen,modem->rx_synclen*8-1,1,bit);
					//printf("Sync: %02x\n",modem->rx_sync[0]);
					if( !memcmp(modem->rx_sync, modem->sync, modem->synclen) ) {
						//printf("Found sync pattern\n");
						modem->rx_synced = 1;
						modem->rx_bitoff = 0;
						tmp = (uint8_t*)realloc(modem->rx_pkt,2);
						if( !tmp ) { return -1; }
						modem->rx_pkt = tmp;
						modem->rx_pktlen = 2;
						memset(modem->rx_pkt,0,2);
						memset(modem->rx_sync,0,modem->rx_synclen);
					}
				}
				else {
					//Put the bit in the rx_pkt buffer
					putbits(modem->rx_pkt,modem->rx_pktlen,modem->rx_bitoff++,1,bit);
					if( modem->rx_bitoff == 16 ) {
						//Read the packet size and realloc the buffer
						pktlen16 = (modem->rx_pkt[0]<<8) | (modem->rx_pkt[1]);
						tmp = (uint8_t*)realloc(modem->rx_pkt,2+pktlen16);
						if( !tmp ) { return -1; }
						modem->rx_pkt = tmp;
						modem->rx_pktlen = 2+pktlen16;
						memset(modem->rx_pkt+2,0,pktlen16);
					}
					else if( modem->rx_bitoff >= modem->rx_pktlen*8 ) {
						//We've finish reading the complete packet
						//Set the return variable and forget anything else
						*rxdata = modem->rx_pkt+2;
						*rxdatalen = modem->rx_pktlen-2;
						modem->rx_synced = 0;
						modem->rx_buflen = 0;
						return 0;
					}
				}
			}
			modem->rx_buflen = 0;
		}
	}
	
	*rxdata = 0;
	*rxdatalen = 0;
	return 0;
}

#endif //PACKETMODEM_IMPLEMENTATION