/*
 * Copyright (C) 2023-2025 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
 *
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

#ifndef _OPENGD77_MESSAGING_H_
#define _OPENGD77_MESSAGING_H_

#if !defined(PLATFORM_GD77S)

#include <stdint.h>
#include <stdbool.h>

#define MESSAGING_MAX_MESSAGES    16U // RAM-only ring buffer, oldest entries are dropped
#define MESSAGING_PEER_LEN        10U // APRS addressee is 9 chars max + terminator
#define MESSAGING_TEXT_LEN        68U // APRS message text is 67 chars max + terminator
#define MESSAGING_NUM_CANNED       8U

typedef enum
{
	MESSAGING_TRANSPORT_APRS = 0,
	MESSAGING_TRANSPORT_DMR
} messagingTransport_t;

typedef struct
{
	char     peer[MESSAGING_PEER_LEN];
	char     text[MESSAGING_TEXT_LEN];
	uint32_t time;       // dateTimeSecs, 0 if unknown
	uint8_t  transport;  // messagingTransport_t
	bool     outgoing;
	bool     unread;
} messagingEntry_t;

void messagingInit(void);
uint32_t messagingGetCount(void);
uint32_t messagingGetUnreadCount(void);
messagingEntry_t *messagingGetEntry(uint32_t index); // index 0 is the newest, NULL if out of range
void messagingMarkRead(uint32_t index);
void messagingDelete(uint32_t index);
void messagingDeleteAll(void);
const char *messagingGetCanned(uint32_t index);

// Store a received message and notify the user (beep + toast). Transport receive code
// (none yet) must call this from the main task, never from an interrupt.
void messagingReceive(messagingTransport_t transport, const char *peer, const char *text);

// DMR data receive. messagingDmrRxFrame() is called from the HR-C6000 interrupt handler with each
// good data frame (header or data block); it only reassembles into a static buffer.
// messagingTick() must be called regularly from the main loop to decode and store completed messages.
void messagingDmrRxFrame(uint8_t dataType, const uint8_t *data, uint8_t len);
void messagingTick(void);

// Send a private DMR text message to a DMR ID (digital channel required). The text goes out as a
// DMR data call driven by the HR-C6000; returns false if it can't be queued.
bool messagingSendDMR(uint32_t dstId, const char *text);

// Send through the APRS transport. On success the message is added to the store as outgoing.
bool messagingSendAPRS(const char *peer, const char *text);

#endif // !PLATFORM_GD77S

#endif // _OPENGD77_MESSAGING_H_
