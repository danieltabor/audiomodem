#ifndef __PKT_H__
#define __PKT_H__

#include "bitops.h"

#define PKT_DEFAULT_VERBOSE     0
#define PKT_DEFAULT_REDUNDANCY  1
#define PKT_DEFAULT_SYNC_0      0xC9
#define PKT_DEFAULT_SYNC_1      0x3F
#define PKT_DEFAULT_MASK_0      0x5A
#define PKT_DEFAULT_MASK_1      0xA5

typedef struct {
	uint8_t *data;
	size_t   len;
} pktdata_t;

typedef struct {
	int      verbose;
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
	
	uint8_t    *rx_data;
	size_t      rx_datalen;
	pktdata_t  *rx_pkts;
	size_t      rx_pktslen;
} pkt_t;

pkt_t *pkt_init();
void   pkt_destroy(pkt_t *pkt);
int    pkt_set_redundancy(pkt_t *pkt, size_t redundancy);
int    pkt_set_sync(pkt_t *pkt, uint8_t *sync, size_t synclen);
int    pkt_set_mask(pkt_t *pkt, uint8_t *mask, size_t masklen);
int    pkt_set_verbose(pkt_t *pkt, int verbose);
int    pkt_tx(pkt_t *pkt, uint8_t **pktdata, size_t *pktdatalen, uint8_t *rawdata, size_t rawdatalen);
int    pkt_rx(pkt_t *pkt, pktdata_t **rxpkts, size_t *rxpktslen, uint8_t *rawdata, size_t rawdatalen);

#endif //__PKT_H__

#ifdef PKT_IMPLEMENTATION
#undef PKT_IMPLEMENTATION

pkt_t *pkt_init() {
	pkt_t *pkt;
	
	pkt = (pkt_t*)malloc(sizeof(pkt_t));
	if( !pkt ) { return 0; }
	memset(pkt,0,sizeof(pkt_t));

	pkt->verbose    = PKT_DEFAULT_VERBOSE;
	pkt->redundancy = PKT_DEFAULT_REDUNDANCY;
	
	pkt->sync = (uint8_t*)malloc(sizeof(uint8_t)*2);
	if( !pkt->sync ) { goto pkt_init_error; }
	pkt->sync[0] = PKT_DEFAULT_SYNC_0;
	pkt->sync[1] = PKT_DEFAULT_SYNC_1;
	pkt->synclen = 2;
	
	pkt->rx_sync = (uint8_t*)malloc(sizeof(uint8_t)*pkt->synclen);
	if( !pkt->rx_sync ) { goto pkt_init_error; }
	memset(pkt->rx_sync,0,2);
	pkt->rx_synclen = pkt->synclen;
	
	pkt->rx_buf = (uint8_t*)malloc(sizeof(uint8_t)*pkt->redundancy);
	if( ! pkt->rx_buf ) { goto pkt_init_error; };
	
	pkt->mask = (uint8_t*)malloc(sizeof(uint8_t)*2);
	if( !pkt->mask ) { goto pkt_init_error; }
	pkt->mask[0] = PKT_DEFAULT_MASK_0;
	pkt->mask[1] = PKT_DEFAULT_MASK_1;
	pkt->masklen = 2;

	return pkt;
		
	pkt_init_error:
	pkt_destroy(pkt);
	return 0;
}

void pkt_destroy(pkt_t *pkt) {
	size_t i;
	if( pkt ) {
		if( pkt->sync ) { free(pkt->sync); }
		if( pkt->rx_sync ) { free(pkt->rx_sync); }
		if( pkt->rx_buf ) { free(pkt->rx_buf); }
		if( pkt->mask ) { free(pkt->mask); }
		if( pkt->tx_pkt ) { free(pkt->tx_pkt); }
		if( pkt->rx_data ) { free(pkt->rx_data); }
		if( pkt->rx_pkts ) {
			for( i=0; i<pkt->rx_pktslen; i++ ) {
				if( pkt->rx_pkts[i].data ) { 
					free(pkt->rx_pkts[i].data-2);
				}
			}
		}
		free(pkt);
	}
}

int pkt_set_redundancy(pkt_t *pkt, size_t redundancy) {
	uint8_t *tmp;
	
	if( !pkt ) { return -1; }
	if( redundancy == 0 ) { redundancy = 1; }
	if( redundancy % 2 == 0 ) { return 0; }
	
	pkt->redundancy = redundancy;
	tmp = (uint8_t*)realloc(pkt->rx_buf,sizeof(uint8_t)*pkt->redundancy);
	if( !tmp ) { return -1; }
	pkt->rx_buf = tmp;
}

int pkt_set_sync(pkt_t *pkt, uint8_t *sync, size_t synclen) {
	uint8_t *tmp;
	if( !pkt ) { return -1; }
	tmp = (uint8_t*)realloc(pkt->sync,sizeof(uint8_t)*synclen);
	if( !tmp ) { return -1; }
	pkt->sync = tmp;
	pkt->synclen = synclen;
	memcpy(pkt->sync,sync,synclen);
	
	tmp = (uint8_t*)realloc(pkt->rx_sync,sizeof(uint8_t)*synclen);
	if( !tmp ) { return -1; }
	pkt->rx_sync = tmp;
	pkt->rx_synclen = synclen;
	memset(pkt->rx_sync,0,synclen);
	
	pkt->rx_synced = 0;
	return 0;
}

int pkt_set_mask(pkt_t *pkt, uint8_t *mask, size_t masklen) {
	uint8_t *tmp;
	if( !pkt ) { return -1; }
	tmp = (uint8_t*)realloc(pkt->mask,sizeof(uint8_t)*masklen);
	if( !tmp ) { return -1; }
	pkt->mask = tmp;
	pkt->masklen = masklen;
	memcpy(pkt->mask,mask,masklen);
	return 0;
}

int pkt_set_verbose(pkt_t *pkt, int verbose) {
	if( !pkt ) { return -1; }
	pkt->verbose = verbose;
	return 0;
}

int pkt_tx(pkt_t *pkt, uint8_t **pktdata, size_t *pktdatalen, uint8_t *rawdata, size_t rawdatalen) {
	uint8_t *tmp;
	size_t i,j;
	size_t dst;
	int bit;
	uint8_t pktlen16[2];
	size_t pktalloc;
	
	if( !pkt ) { return -1; }
	if( !pktdata ) { return -1; }
	if( !pktdatalen ) { return -1; }
	if( !rawdata ) { return -1; }
	if( rawdatalen > 0xffff ) { return -1; }
	
	if( pkt->verbose ) {
		printf("pkt_tx(...):\n");
		printf("  Raw: ");
		for( i=0; i<rawdatalen; i++ ) {
			printf("%02x ",rawdata[i]);
		}
		printf("\n");
	}
	
	pktalloc = (pkt->synclen+2+rawdatalen)*pkt->redundancy;
	tmp = realloc(pkt->tx_pkt,pktalloc);
	if( !tmp ) { return -1; }
	pkt->tx_pkt = tmp;
	pkt->tx_pktlen = pktalloc;
	memset(pkt->tx_pkt,0,pktalloc);
	
	//Pack the redundant bits of the sync
	dst = 0;
	for( i=0; i<pkt->synclen*8; i++ ) {
		bit = getbits(pkt->sync, pkt->synclen, i, 1);
		for( j=0; j<pkt->redundancy; j++ ) {
			putbits(pkt->tx_pkt, pkt->tx_pktlen, dst++, 1, bit);
		}
	}
	pktlen16[0] = (rawdatalen>>8)&0xff;
	pktlen16[1] = (rawdatalen>>0)&0xff;
	//Pack the redundant bits of packet length
	for( i=0; i<16; i++ ) {
		bit = getbits(pktlen16,2,i,1);
		for( j=0; j<pkt->redundancy; j++ ) {
			putbits(pkt->tx_pkt, pkt->tx_pktlen, dst++, 1, bit);
		}
	}
	//Pack the redundant bits of the data
	for( i=0; i<rawdatalen*8; i++ ) {
		bit = getbits(rawdata, rawdatalen, i, 1);
		for( j=0; j<pkt->redundancy; j++ ) {
			putbits(pkt->tx_pkt, pkt->tx_pktlen, dst++, 1, bit);
		}
	}
	//Apply the mask (after the sync)
	for( i=pkt->synclen*pkt->redundancy, j=0; i<pkt->tx_pktlen; i++, j++ ) {
		pkt->tx_pkt[i] = pkt->tx_pkt[i] ^ pkt->mask[j%pkt->masklen];
	}
	
	if( pkt->verbose ) {
		printf("  Pkt[%zu]: ",pkt->tx_pktlen);
		for( i=0; i<pkt->tx_pktlen; i++ ) {
			printf("%02x ",pkt->tx_pkt[i]);
		}
		printf("\n");
	}
	
	//Return the packed data
	*pktdata = pkt->tx_pkt;
	*pktdatalen = pkt->tx_pktlen;
	return 0;
}

int pkt_rx(pkt_t *pkt, pktdata_t **rx, size_t *rxlen, uint8_t *rawdata, size_t rawdatalen) {
	size_t bitoff;
	size_t rawoff;
	size_t i;
	int bit;
	int bit_votes;
	int mask;
	uint8_t *tmp;
	uint16_t pktlen16;
	
	if( !pkt ) { return -1; }
	if( !rx ) { return -1; }
	if( !rxlen ) { return -1; }
	if( !rawdata && rawdatalen ) { return -1; }
	
	*rx = 0;
	*rxlen = 0;
	
	if( pkt->verbose ) {
		printf("pkt_rx(...):\n");
		printf("  Raw: ");
		for( i=0; i<rawdatalen; i++ ) {
			printf("%02x ",rawdata[i]);
		}
		printf("\n");
	}
	
	//Free up any previously returned packets
	for( i=0; i<pkt->rx_pktslen; i++ ) {
		if( pkt->rx_pkts[i].data ) {
			free(pkt->rx_pkts[i].data-2);
		}
	}
	pkt->rx_pktslen = 0;
	
	rawoff = 0;
	while( rawoff < rawdatalen ) {
		//Move redudancy worth of bytes into initial buffer
		while( pkt->rx_buflen < pkt->redundancy && rawoff < rawdatalen ) {
			pkt->rx_buf[pkt->rx_buflen++] = rawdata[rawoff++];
		}
	
		if( pkt->rx_buflen == pkt->redundancy ) {
			//Process bits in rx_buf
			bitoff = 0;
			while( bitoff < pkt->rx_buflen*8 ) {
				//Pull redundant bits and vote
				bit_votes = 0;
				for( i=0; i<pkt->redundancy; i++ ) {
					bit = getbits(pkt->rx_buf, pkt->rx_buflen, bitoff++, 1);
					if( pkt->rx_synced ) {
						//Apply the mask for all bits after the sync
						mask = getbits(pkt->mask,pkt->masklen,(pkt->rx_bitoff*pkt->redundancy+i)%(pkt->masklen*8),1);
						bit = bit ^ mask;
					}
					bit_votes = bit_votes + bit;
				}
				//Reduce the votes down to a singel bit
				if( bit_votes > pkt->redundancy/2 ) {
					bit = 1;
				}
				else {
					bit = 0;
				}
				
				if( !pkt->rx_synced ) {
					//Try to find the sync
					shiftbits(pkt->rx_sync,pkt->rx_synclen,1);
					putbits(pkt->rx_sync,pkt->rx_synclen,pkt->rx_synclen*8-1,1,bit);
					if( pkt->verbose ) {
						printf("  Testing Sync: ");
						for( i=0; i<pkt->synclen; i++ ) {
							printf("%02x ",pkt->rx_sync[i]);
						}
						printf("\n");
					}
					if( !memcmp(pkt->rx_sync, pkt->sync, pkt->synclen) ) {
						if( pkt->verbose ) {
							printf("  Found Sync\n");
						}
						pkt->rx_synced = 1;
						pkt->rx_bitoff = 0;
						tmp = (uint8_t*)realloc(pkt->rx_data,2);
						if( !tmp ) { return -1; }
						pkt->rx_data = tmp;
						pkt->rx_datalen = 2;
						memset(pkt->rx_data,0,2);
						memset(pkt->rx_sync,0,pkt->rx_synclen);
					}
				}
				else {
					//Put the bit in the rx_pkt buffer
					putbits(pkt->rx_data,pkt->rx_datalen,pkt->rx_bitoff++,1,bit);
					if( pkt->rx_bitoff == 16 ) {
						//Read the packet size and realloc the buffer
						pktlen16 = (pkt->rx_data[0]<<8) | (pkt->rx_data[1]);
						if( pkt->verbose ) {
							printf("  Packet Length: %u\n",pktlen16);
						}
						tmp = (uint8_t*)realloc(pkt->rx_data,2+pktlen16);
						if( !tmp ) { return -1; }
						pkt->rx_data = tmp;
						pkt->rx_datalen = 2+pktlen16;
						memset(pkt->rx_data+2,0,pktlen16);
					}
					else if( pkt->rx_bitoff >= pkt->rx_datalen*8 ) {
						//We've finish reading the complete packet
						//Append it to the result set
						pkt->rx_pktslen++;
						pkt->rx_pkts = realloc(pkt->rx_pkts,sizeof(pktdata_t)*pkt->rx_pktslen);
						if( !pkt->rx_pkts ) { return -1; }
						pkt->rx_pkts[pkt->rx_pktslen-1].data = pkt->rx_data+2;
						pkt->rx_pkts[pkt->rx_pktslen-1].len  = pkt->rx_datalen-2;
						pkt->rx_data = 0;
						pkt->rx_datalen = 0;
						pkt->rx_synced = 0;
						if( pkt->verbose ) {
							printf("  Pkt[%zu]: ",pkt->rx_pkts[pkt->rx_pktslen-1].len);
							for( i=0; i<pkt->rx_pkts[pkt->rx_pktslen-1].len ; i++ ) {
								printf("%02x ",pkt->rx_pkts[pkt->rx_pktslen-1].data[i]);
							}
							printf("\n");
						}
					}
				}
			}
			pkt->rx_buflen = 0;
		}
	}
	
	if( pkt->rx_pktslen ) {
		*rx = pkt->rx_pkts;
		*rxlen = pkt->rx_pktslen;
	}
	return 0;
}

#endif //PKT_IMPLEMENTATION