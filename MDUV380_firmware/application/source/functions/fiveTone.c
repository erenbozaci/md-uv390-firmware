/*
 * 5 tone selective calling (encode only), this fork.
 *
 * The tones come from the AT1846S tone 1 generator (like the 1750 Hz burst and the CW ID), one after
 * the other with a fixed length per standard. A digit equal to the previous tone is replaced by the
 * repeat tone, as the standards ask.
 *
 * Use of this source code or binary releases for commercial purposes is strictly forbidden
 * (see the licence of the project).
 */

#include <string.h>
#include "main.h"
#include "functions/fiveTone.h"
#include "functions/settings.h"
#include "functions/trx.h"
#include "functions/ticks.h"

#define FIVETONE_TX_DELAY_MS     150U  // carrier before the first tone (transmitter and receiver squelch settle)
#define FIVETONE_CALL_TIMEOUT_MS 1500U // a call that never reached the TX screen (RX only, out of band...) is dropped

typedef struct
{
	uint16_t digits[10]; // Hz, digits 0..9
	uint16_t repeat;     // Hz, repeat tone
	uint16_t toneMs;
} fiveToneTable_t;

static const fiveToneTable_t tables[FIVETONE_NUM_STDS] =
{
	// ZVEI1
	{ { 2400, 1060, 1160, 1270, 1400, 1530, 1670, 1830, 2000, 2200 }, 2600, 70 },
	// ZVEI2
	{ { 2400, 1060, 1160, 1270, 1400, 1530, 1670, 1830, 2000, 2200 },  970, 70 },
	// CCIR
	{ { 1981, 1124, 1197, 1275, 1358, 1446, 1540, 1640, 1747, 1860 }, 2110, 100 },
	// EEA
	{ { 1981, 1124, 1197, 1275, 1358, 1446, 1540, 1640, 1747, 1860 }, 2110, 40 },
};

static const char *const standardNames[FIVETONE_NUM_STDS] = { "ZVEI1", "ZVEI2", "CCIR", "EEA" };

typedef enum
{
	CALL_IDLE = 0,
	CALL_PENDING, // waiting for the TX screen
	CALL_SENT     // code sent, the PTT is released, no EOT ID
} callState_t;

static volatile callState_t callState = CALL_IDLE;
static bool callStartRequest = false;
static uint32_t callStartTime;
static char callDigits[FIVETONE_DIGITS + 1];

// nonVolatileSettings.fiveTone[]: [0] = ID digits 1,2 (BCD), [1] = digits 3,4, [2] = digit 5 (high nibble),
// standard (bits 3..2), PTT ID mode (bits 1..0)
static uint8_t idNibble(int i)
{
	uint8_t b = nonVolatileSettings.fiveTone[i / 2];
	uint8_t n = ((i & 1) ? (b & 0x0F) : (b >> 4));

	return ((n > 9) ? 0 : n);
}

void fiveToneGetId(char *buf)
{
	for (int i = 0; i < FIVETONE_DIGITS; i++)
	{
		buf[i] = (char)('0' + idNibble(i));
	}
	buf[FIVETONE_DIGITS] = 0;
}

void fiveToneSetId(const char *digits)
{
	uint8_t n[6] = { 0 };

	for (int i = 0; (i < FIVETONE_DIGITS) && (digits[i] >= '0') && (digits[i] <= '9'); i++)
	{
		n[i] = (uint8_t)(digits[i] - '0');
	}

	settingsSet(nonVolatileSettings.fiveTone[0], (uint8_t)((n[0] << 4) | n[1]));
	settingsSet(nonVolatileSettings.fiveTone[1], (uint8_t)((n[2] << 4) | n[3]));
	settingsSet(nonVolatileSettings.fiveTone[2], (uint8_t)((n[4] << 4) | (nonVolatileSettings.fiveTone[2] & 0x0F)));
}

fiveToneStandard_t fiveToneGetStandard(void)
{
	return (fiveToneStandard_t)((nonVolatileSettings.fiveTone[2] >> 2) & 0x03);
}

void fiveToneSetStandard(fiveToneStandard_t std)
{
	settingsSet(nonVolatileSettings.fiveTone[2], (uint8_t)((nonVolatileSettings.fiveTone[2] & 0xF3) | ((std & 0x03) << 2)));
}

fiveTonePttId_t fiveToneGetPttId(void)
{
	return (fiveTonePttId_t)(nonVolatileSettings.fiveTone[2] & 0x03);
}

void fiveToneSetPttId(fiveTonePttId_t mode)
{
	settingsSet(nonVolatileSettings.fiveTone[2], (uint8_t)((nonVolatileSettings.fiveTone[2] & 0xFC) | (mode & 0x03)));
}

const char *fiveToneStandardName(fiveToneStandard_t std)
{
	return standardNames[std & 0x03];
}

static void waitMs(uint32_t ms)
{
	vTaskDelay((ms / portTICK_PERIOD_MS));
}

// Blocking, while transmitting in FM. justKeyedUp: wait for the carrier to settle first.
static void sendSequence(const char *digits, bool justKeyedUp)
{
	const fiveToneTable_t *t = &tables[fiveToneGetStandard()];
	uint16_t previous = 0;

	if (justKeyedUp)
	{
		waitMs(FIVETONE_TX_DELAY_MS);
	}

	for (const char *c = digits; *c != 0; c++)
	{
		if ((*c < '0') || (*c > '9'))
		{
			continue;
		}

		uint16_t f = t->digits[*c - '0'];

		if (f == previous)
		{
			f = t->repeat;
		}

		trxSetTone1(f);
		waitMs(t->toneMs);
		previous = f;
	}

	trxSetTone1(0); // back to the microphone
}

bool fiveToneCallStart(const char *digits)
{
	if ((callState != CALL_IDLE) || (digits[0] == 0) || (trxGetMode() != RADIO_MODE_ANALOG) || trxTransmissionEnabled)
	{
		return false;
	}

	strncpy(callDigits, digits, FIVETONE_DIGITS);
	callDigits[FIVETONE_DIGITS] = 0;
	callStartTime = ticksGetMillis();
	callStartRequest = true;
	callState = CALL_PENDING;

	return true;
}

bool fiveToneCallHoldsPtt(void)
{
	if ((callState == CALL_PENDING) && (trxTransmissionEnabled == false) && ((ticksGetMillis() - callStartTime) > FIVETONE_CALL_TIMEOUT_MS))
	{
		callState = CALL_IDLE; // never keyed up (RX only, out of band...)
		callStartRequest = false;
	}

	return (callState == CALL_PENDING);
}

bool fiveToneCallTakeStartRequest(void)
{
	bool r = callStartRequest;

	callStartRequest = false;
	return r;
}

void fiveToneTxStarted(void)
{
	if (callState == CALL_PENDING)
	{
		sendSequence(callDigits, true);
		callState = CALL_SENT; // the main loop stops holding the PTT, the TX screen ends the transmission
	}
	else if ((fiveToneGetPttId() == FIVETONE_PTTID_START) || (fiveToneGetPttId() == FIVETONE_PTTID_BOTH))
	{
		char id[FIVETONE_DIGITS + 1];

		fiveToneGetId(id);
		sendSequence(id, true);
	}
}

void fiveToneTxEnding(void)
{
	if (callState == CALL_SENT)
	{
		callState = CALL_IDLE; // a selective call has no EOT ID
	}
	else if ((fiveToneGetPttId() == FIVETONE_PTTID_END) || (fiveToneGetPttId() == FIVETONE_PTTID_BOTH))
	{
		char id[FIVETONE_DIGITS + 1];

		fiveToneGetId(id);
		sendSequence(id, false);
	}
}
