//
// DMR Protocol Decoder (C) Copyright 2019 Graham J. Norbury
// 
// This file is part of OP25
// 
// OP25 is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
// 
// OP25 is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
// License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with OP25; see the file COPYING. If not, write to the Free
// Software Foundation, Inc., 51 Franklin Street, Boston, MA
// 02110-1301, USA.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <iostream>
#include <deque>
#include <errno.h>
#include <unistd.h>

#include "dmr_cai.h"

#include "bit_utils.h"
#include "dmr_const.h"
#include "hamming.h"

dmr_cai::dmr_cai(int debug) :
	d_shift_reg(0),
	d_chan(0),
	d_debug(debug)
{
	d_slot[0].set_debug(debug);
	d_slot[0].set_chan(0);
	d_slot[1].set_debug(debug);
	d_slot[1].set_chan(1);
	d_cach_sig.clear();
	memset(d_frame, 0, sizeof(d_frame));
}

dmr_cai::~dmr_cai() {
}

int
dmr_cai::load_frame(const uint8_t fr_sym[]) {
	dibits_to_bits(d_frame, fr_sym, FRAME_SIZE >> 1);
	extract_cach_fragment();
	d_slot[d_chan].load_slot(d_frame + 24);
	return d_chan;
}

void
dmr_cai::extract_cach_fragment() {
	static const int slot_ids[] = {0, 1, 0, 0, 1, 1, 0, 1};
	int tact, chan, lcss;
	uint8_t tactbuf[sizeof(cach_tact_bits)];

	for (size_t i=0; i<sizeof(cach_tact_bits); i++)
		tactbuf[i] = d_frame[CACH + cach_tact_bits[i]];
	tact = hamming_7_4_decode[load_i(tactbuf, 7)];
	chan = (tact>>2) & 1;
	lcss = tact & 3;
	d_shift_reg = (d_shift_reg << 1) + chan;
	d_chan = slot_ids[d_shift_reg & 7];

	switch(lcss) {
		case 0: // Single-fragment CSBK
			// TODO: do something useful
			break;
		case 1: // Begin Short_LC
			d_cach_sig.clear();
			for (size_t i=0; i<sizeof(cach_payload_bits); i++)
				d_cach_sig.push_back(d_frame[CACH + cach_payload_bits[i]]);
			break;
		case 2: // End Short_LC
			for (size_t i=0; i<sizeof(cach_payload_bits); i++)
				d_cach_sig.push_back(d_frame[CACH + cach_payload_bits[i]]);
			break;
		case 3: // Continue Short_LC
			for (size_t i=0; i<sizeof(cach_payload_bits); i++)
				d_cach_sig.push_back(d_frame[CACH + cach_payload_bits[i]]);
				decode_shortLC(d_cach_sig);
			break;
	}
	
}

bool
dmr_cai::decode_shortLC(bit_vector& cw)
{
	bit_vector slc(68, false);

	// deinterleave
	int i, src;
	for (i = 0; i < 67; i++) {
		src = (i * 4) % 67;
		slc[i] = cw[src];
	}
	slc[i] = cw[i];

	// apply error correction
	CHamming::decode17123(slc, 0);
	CHamming::decode17123(slc, 17);
	CHamming::decode17123(slc, 34);

	// parity check
	for (i = 0; i < 17; i++) {
		if (slc[i+51] != (slc[i+0] ^ slc[i+17] ^ slc[i+34]))
			return false;
	}

	// remove hamming and parity bits and leave only 36 bits of Short LC
	slc.erase(slc.begin()+46, slc.end());
	slc.erase(slc.begin()+29, slc.begin()+29+5);
	slc.erase(slc.begin()+12, slc.begin()+12+5);

	// TODO validate CRC8

	// extract useful data
	if (d_debug >= 10) {
		int slco, d0, d1, d2 = 0;
		for (i = 0; i < 4; i++) {
			slco <<= 1;
			slco |= slc[i];
		}
		for (i = 0; i < 8; i++) {
			d0 <<= 1;
			d0 |= slc[i+4];
		}
		for (i = 0; i < 8; i++) {
			d1 <<= 1;
			d1 |= slc[i+12];
		}
		for (i = 0; i < 8; i++) {
			d2 <<= 1;
			d2 |= slc[i+20];
		}
		
		if (d_debug >= 10)
			fprintf(stderr, "SLCO=0x%x, DATA=%02x %02x %02x\n", slco, d0, d1, d2);
	}

	return true;
}
