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