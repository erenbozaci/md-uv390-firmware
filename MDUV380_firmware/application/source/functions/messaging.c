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

#if !defined(PLATFORM_GD77S)

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "functions/messaging.h"
#include "functions/aprs.h"
#include "functions/sound.h"
#include "functions/trx.h"
#include "hardware/HR-C6000.h"
#include "functions/ticks.h"
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"

static messagingEntry_t messages[MESSAGING_MAX_MESSAGES];
static uint32_t messagesCount = 0;
static uint32_t nextSeq = 1;

// ---- DMR data receive -------------------------------------------------------------------------
// Frame types as reported by HR-C6000 register 0x51[7:4] (HR-C6000 manual, table 5.5)
#define DMR_DATA_TYPE_HEADER      0x6U
#define DMR_DATA_TYPE_RATE_1_2    0x7U // 12 bytes per block
#define DMR_DATA_TYPE_RATE_3_4    0x8U // 18 bytes per block
#define DMR_DATA_TYPE_RATE_1      0xAU // 24 bytes per block

#define DMR_DPF_UNCONFIRMED       0x2U // data packet header, unconfirmed delivery
#define DMR_DPF_CONFIRMED         0x3U // data packet header, confirmed delivery (2 bytes of SN/CRC9 per block)
#define DMR_RX_MAX_BLOCKS         12U
#define DMR_RX_MAX_BYTES          (DMR_RX_MAX_BLOCKS * 24U)
#define DMR_RX_CRC32_LEN          4U

typedef struct
{
	uint32_t src;
	uint32_t dst;
	bool     group;
	uint8_t  pad;      // pad octet count from the header (bytes at the end of the data to ignore)
	uint16_t len;
	uint8_t  data[DMR_RX_MAX_BYTES];
} dmrRxMessage_t;

static struct
{
	bool     active;
	bool     confirmed;
	uint8_t  blocksLeft;
	dmrRxMessage_t msg;
} dmrRx;

static dmrRxMessage_t dmrRxMailbox;
static volatile bool dmrRxMailboxReady = false;

void messagingDmrRxFrame(uint8_t dataType, const uint8_t *data, uint8_t len)
{
	if (dataType == DMR_DATA_TYPE_HEADER)
	{
		uint8_t dpf = (data[0] & 0x0F);
		uint8_t blocks = (data[8] & 0x7F);

		dmrRx.active = false;

		if ((len < 10U) || ((dpf != DMR_DPF_UNCONFIRMED) && (dpf != DMR_DPF_CONFIRMED)) || (blocks == 0U) || (blocks > DMR_RX_MAX_BLOCKS))
		{
			return;
		}

		dmrRx.msg.group = ((data[0] & 0x80) != 0);
		dmrRx.msg.pad = (data[1] & 0x0F);
		dmrRx.msg.dst = ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 8) | data[4];
		dmrRx.msg.src = ((uint32_t)data[5] << 16) | ((uint32_t)data[6] << 8) | data[7];

		// Only keep messages addressed to us (private) or to the talkgroup currently in use (group)
		if (dmrRx.msg.dst != (dmrRx.msg.group ? (trxTalkGroupOrPcId & 0xFFFFFFU) : trxDMRID))
		{
			return;
		}

		dmrRx.confirmed = (dpf == DMR_DPF_CONFIRMED);
		dmrRx.blocksLeft = blocks;
		dmrRx.msg.len = 0;
		dmrRx.active = true;
	}
	else if (dmrRx.active && ((dataType == DMR_DATA_TYPE_RATE_1_2) || (dataType == DMR_DATA_TYPE_RATE_3_4) || (dataType == DMR_DATA_TYPE_RATE_1)))
	{
		uint8_t skip = (dmrRx.confirmed ? 2U : 0U);

		if ((len > skip) && ((dmrRx.msg.len + (len - skip)) <= DMR_RX_MAX_BYTES))
		{
			memcpy(&dmrRx.msg.data[dmrRx.msg.len], &data[skip], (len - skip));
			dmrRx.msg.len += (len - skip);
		}
		else
		{
			dmrRx.active = false;
			return;
		}

		if (--dmrRx.blocksLeft == 0U)
		{
			dmrRx.active = false;

			// Last block ends with the CRC-32 of the whole message (already verified by the chip)
			if ((dmrRx.msg.len > DMR_RX_CRC32_LEN) && (dmrRxMailboxReady == false))
			{
				dmrRx.msg.len -= DMR_RX_CRC32_LEN;
				if (dmrRx.msg.pad < dmrRx.msg.len)
				{
					dmrRx.msg.len -= dmrRx.msg.pad;
				}
				memcpy(&dmrRxMailbox, &dmrRx.msg, sizeof(dmrRxMailbox));
				__DMB(); // the message must be fully written before the main loop sees the flag
				dmrRxMailboxReady = true;
			}
		}
	}
}

static bool isPrintable(uint8_t c)
{
	return ((c >= 0x20U) && (c <= 0x7EU));
}

// The text encoding differs between radio brands (UTF-16LE, or 8 bit text, possibly after IP/UDP and
// vendor headers), so rather than parse each format pick the longest printable run in either encoding.
static size_t dmrExtractText(const uint8_t *data, size_t len, char *out, size_t outSize)
{
	size_t bestLen = 0;
	size_t bestStart = 0;
	size_t bestStride = 1;

	for (size_t stride = 1; stride <= 2; stride++)
	{
		for (size_t i = 0; i < len; )
		{
			size_t n = 0;

			while (((i + (n * stride) + (stride - 1U)) < len) && isPrintable(data[i + (n * stride)]) && ((stride == 1U) || (data[i + (n * stride) + 1U] == 0U)))
			{
				n++;
			}

			// Anything shorter than 2 chars is just noise from binary headers
			if ((n >= 2U) && (n > bestLen))
			{
				bestLen = n;
				bestStart = i;
				bestStride = stride;
			}
			i += ((n > 0U) ? (n * stride) : 1U);
		}
	}

	if (bestLen > (outSize - 1U))
	{
		bestLen = (outSize - 1U);
	}

	for (size_t i = 0; i < bestLen; i++)
	{
		out[i] = (char)data[bestStart + (i * bestStride)];
	}
	out[bestLen] = 0;

	return bestLen;
}

#define DMR_TX_WATCHDOG_MS  10000U // a data job holds the software PTT, never let it do so forever

void messagingTick(void)
{
	static uint32_t dmrTxActiveSince = 0;

	if (HRC6000DataTxIsActive())
	{
		if (dmrTxActiveSince == 0U)
		{
			dmrTxActiveSince = ticksGetMillis();
		}
		else if ((ticksGetMillis() - dmrTxActiveSince) > DMR_TX_WATCHDOG_MS)
		{
			HRC6000DataTxCancel();
			dmrTxActiveSince = 0;
		}
	}
	else
	{
		dmrTxActiveSince = 0;
	}

	if (dmrRxMailboxReady)
	{
		__DMB(); // flag read before the message data
		char peer[MESSAGING_PEER_LEN];
		char text[MESSAGING_TEXT_LEN];

		snprintf(peer, sizeof(peer), "%u", (unsigned int)dmrRxMailbox.src);

		if (dmrExtractText(dmrRxMailbox.data, dmrRxMailbox.len, text, sizeof(text)) > 0U)
		{
			messagingReceive(MESSAGING_TRANSPORT_DMR, peer, text);
		}

		dmrRxMailboxReady = false;
	}
}

static const char *const cannedMessages[MESSAGING_NUM_CANNED] =
{
	"QSL",
	"73",
	"QRZ?",
	"On my way",
	"Call me",
	"QRV, go ahead",
	"Wait 5 min",
	"QSY"
};

static void messagingCopyUpper(char *dst, size_t dstSize, const char *src)
{
	size_t i = 0;

	for (; (src[i] != 0) && (i < (dstSize - 1U)); i++)
	{
		dst[i] = (char)toupper((unsigned char)src[i]);
	}
	dst[i] = 0;
}

static void messagingAdd(messagingTransport_t transport, const char *peer, const char *text, bool outgoing)
{
	// Index 0 is the newest entry: shift everything down, dropping the oldest when full.
	uint32_t keep = ((messagesCount < MESSAGING_MAX_MESSAGES) ? messagesCount : (MESSAGING_MAX_MESSAGES - 1U));

	memmove(&messages[1], &messages[0], (keep * sizeof(messagingEntry_t)));

	messagingEntry_t *e = &messages[0];

	memset(e, 0, sizeof(messagingEntry_t));
	messagingCopyUpper(e->peer, sizeof(e->peer), peer);
	strncpy(e->text, text, (sizeof(e->text) - 1U));
	e->time = (uint32_t)uiDataGlobal.dateTimeSecs;
	e->seq = nextSeq++;
	e->transport = (uint8_t)transport;
	e->outgoing = outgoing;
	e->unread = (outgoing == false);

	messagesCount = (keep + 1U);
}

void messagingInit(void)
{
	messagesCount = 0;
}

uint32_t messagingGetCount(void)
{
	return messagesCount;
}

uint32_t messagingGetUnreadCount(void)
{
	uint32_t n = 0;

	for (uint32_t i = 0; i < messagesCount; i++)
	{
		if (messages[i].unread)
		{
			n++;
		}
	}
	return n;
}

messagingEntry_t *messagingGetEntry(uint32_t index)
{
	return ((index < messagesCount) ? &messages[index] : NULL);
}

messagingEntry_t *messagingGetEntryBySeq(uint32_t seq, uint32_t *index)
{
	for (uint32_t i = 0; i < messagesCount; i++)
	{
		if (messages[i].seq == seq)
		{
			if (index != NULL)
			{
				*index = i;
			}
			return &messages[i];
		}
	}
	return NULL;
}

void messagingMarkRead(uint32_t index)
{
	if (index < messagesCount)
	{
		messages[index].unread = false;
	}
}

void messagingDelete(uint32_t index)
{
	if (index < messagesCount)
	{
		memmove(&messages[index], &messages[index + 1U], ((messagesCount - index - 1U) * sizeof(messagingEntry_t)));
		messagesCount--;
	}
}

void messagingDeleteAll(void)
{
	messagesCount = 0;
}

const char *messagingGetCanned(uint32_t index)
{
	return cannedMessages[index % MESSAGING_NUM_CANNED];
}

void messagingReceive(messagingTransport_t transport, const char *peer, const char *text)
{
	char buf[SCREEN_LINE_BUFFER_SIZE];

	messagingAdd(transport, peer, text, false);

	snprintf(buf, sizeof(buf), "MSG %s", messages[0].peer);
	uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 3000, buf, true);
	soundSetMelody(MELODY_PRIVATE_CALL);
}

bool messagingSendDMR(uint32_t dstId, const char *text)
{
	uint8_t payload[(MESSAGING_TEXT_LEN - 1U) * 2U];
	char peer[MESSAGING_PEER_LEN];
	size_t chars = strlen(text);

	if ((trxGetMode() != RADIO_MODE_DIGITAL) || (dstId == 0U) || (dstId > 0xFFFFFFU) || (chars == 0U))
	{
		return false;
	}

	if (chars > (MESSAGING_TEXT_LEN - 1U))
	{
		chars = (MESSAGING_TEXT_LEN - 1U);
	}

	// UTF-16LE text
	for (size_t i = 0; i < chars; i++)
	{
		payload[i * 2U] = (uint8_t)text[i];
		payload[(i * 2U) + 1U] = 0;
	}

	if (HRC6000DataTxStart(dstId, payload, (uint16_t)(chars * 2U)) == false)
	{
		return false;
	}

	snprintf(peer, sizeof(peer), "%u", (unsigned int)dstId);
	messagingAdd(MESSAGING_TRANSPORT_DMR, peer, text, true);
	return true;
}

bool messagingSendAPRS(const char *peer, const char *text)
{
	char upperPeer[MESSAGING_PEER_LEN];

	messagingCopyUpper(upperPeer, sizeof(upperPeer), peer); // APRS addressees are upper case

	if (aprsMessageSend(upperPeer, text))
	{
		messagingAdd(MESSAGING_TRANSPORT_APRS, peer, text, true);
		return true;
	}
	return false;
}

#endif // !PLATFORM_GD77S
