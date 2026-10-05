/*
 * Copyright (C) 2019 Kai Ludwig, DG4KLU
 *           (C) 2020-2025 Roger Clark, VK3KYY / G4KYF
 *
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden. This includes, without limitation,
 *    incorporation in a commercial product or incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "dmr_codec/codec.h"
#include "functions/voicePrompts.h"
#include <string.h>

static uint16_t bitbuffer_encode[72];

// The AMBE codec lives at fixed addresses in the donor firmware image (bit 0 set = Thumb).
// Call it through C function pointers: modern GCC/ld refuse direct BL to absolute addresses.
// Argument layout (r0-r3, then stack) mirrors the original hand-written assembly calls.
typedef void (*ambeDecodeFn_t)(volatile uint8_t *waveOut, int numSamples, uint16_t *bitbuffer, int zero,
		int stack0, int stage, uint8_t *state);
typedef void (*ambeEncodeFn_t)(uint16_t *bitbuffer, int zero, volatile uint8_t *waveIn, int numSamples,
		int stack0, int stage, int stateLen, uint8_t *state);
typedef void (*ambeEncodeEccFn_t)(uint16_t *bitbufferIn, uint16_t *bitbufferOut, int zero, uint8_t *state);

#define ambeDecode    ((ambeDecodeFn_t)AMBE_DECODE)
#define ambeEncode    ((ambeEncodeFn_t)AMBE_ENCODE)
#define ambeEncodeEcc ((ambeEncodeEccFn_t)AMBE_ENCODE_ECC)

void codecDecode(uint8_t *indata_ptr, int numbBlocks)
{
	uint16_t bitbuffer_decode[49];

	for (int idx = 0; idx < numbBlocks; idx++)
	{
		initFrame(indata_ptr, bitbuffer_decode);
		indata_ptr += 9;

		for (int stage = 0; stage < 2; stage++)
		{
			soundSetupBuffer();// this just sets currentWaveBuffer
			ambeDecode(currentWaveBuffer, 80, bitbuffer_decode, 0, 0, stage, ambebuffer_decode);
			soundStoreBuffer();
		}
	}
}

void codecEncodeBlock(uint8_t *outdata_ptr)
{
	memset((uint8_t *)outdata_ptr, 0, 9);// fills with zeros
	memset(bitbuffer_encode, 0, sizeof(bitbuffer_encode));

	for (int stage = 0; stage < 2; stage++)
	{
		soundRetrieveBuffer();// gets currentWaveBuffer pointer used as input to the encoder
		ambeEncode(bitbuffer_encode, 0, currentWaveBuffer, 80, (stage == 0) ? 0x1840 : 0x0800, stage, 0x2000, ambebuffer_encode);
	}

	ambeEncodeEcc(bitbuffer_encode, bitbuffer_encode, 0, ambebuffer_encode_ecc);

	for (int i = 0; i < 72; i++)
	{
		if (bitbuffer_encode[i] & 1)
		{
			outdata_ptr[i >> 3] |= 128 >> (i & 7);
		}
	}
}
