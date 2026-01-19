#ifndef __BITOPS_H__
#define __BITOPS_H__

int  getbits(uint8_t *data, size_t datalen, size_t bit_idx, size_t bit_count);
void putbits(uint8_t *data, size_t datalen, size_t bit_idx, size_t bit_count, int bits);
void shiftbits(uint8_t *data, size_t datatlen, size_t left_shift);

#endif //__BITOPS_H__

#ifdef BITOPS_IMPLEMENTATION
#undef BITOPS_IMPLEMENTATION

int getbits(uint8_t *data, size_t datalen, size_t bit_idx, size_t bit_count) {
	int bits = 0;
	size_t byte_offset = bit_idx / 8;
	size_t bit_offset  = bit_idx % 8;
	size_t i;
	
	for( i=0; i<bit_count; i++ ) {
		if( bit_offset == 8 ) {
			bit_offset = 0;
			byte_offset++;
		}
		if( byte_offset < datalen ) {
			bits = bits | ((data[byte_offset] >> (7-bit_offset))&1) << (bit_count-i-1);
		}
		bit_offset++;
	}
	
	return bits;
}

void putbits(uint8_t *data, size_t datalen, size_t bit_idx, size_t bit_count, int bits) {
	size_t byte_offset = bit_idx / 8;
	size_t bit_offset  = bit_idx % 8;
	size_t i;
	
	for( i=0; i<bit_count; i++ ) {
		if( bit_offset == 8 ) {
			bit_offset = 0;
			byte_offset++;
		}
		
		if( byte_offset < datalen ) {
			data[byte_offset] = data[byte_offset] | (((bits >> (bit_count-i-1))&1) << (7-bit_offset));
		}
		bit_offset++;
	}
}

void shiftbits(uint8_t *data, size_t datalen, size_t left_shift) {
	size_t i;
	while( left_shift >= 8 ) {
		for( i=0; i<datalen-1; i++ ) {
			data[i] = data[i+1];
		}
		data[datalen-1] = 0;
		left_shift = left_shift-8;
	}
	if( left_shift ) {
		for( i=0; i<datalen-1; i++ ) {
			data[i] = data[i] << left_shift;
			data[i] = data[i] | (data[i+1] >> (8-left_shift));
		}
		data[i] = data[i] << left_shift;
	}
}

#endif //BITOPS_IMPLEMENTATION