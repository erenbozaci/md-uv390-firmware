/*
 * 5 tone selective calling (encode only), this fork.
 *
 * Use of this source code or binary releases for commercial purposes is strictly forbidden
 * (see the licence of the project).
 */
#ifndef _OPENGD77_FIVETONE_H_
#define _OPENGD77_FIVETONE_H_

#include <stdbool.h>
#include <stdint.h>

#define FIVETONE_DIGITS 5

typedef enum
{
	FIVETONE_STD_ZVEI1 = 0,
	FIVETONE_STD_ZVEI2,
	FIVETONE_STD_CCIR,
	FIVETONE_STD_EEA,
	FIVETONE_NUM_STDS
} fiveToneStandard_t;

typedef enum
{
	FIVETONE_PTTID_OFF = 0,
	FIVETONE_PTTID_START, // own ID when the PTT is pressed (BOT)
	FIVETONE_PTTID_END,   // own ID when the PTT is released (EOT)
	FIVETONE_PTTID_BOTH,
	FIVETONE_NUM_PTTIDS
} fiveTonePttId_t;

// Settings (stored in nonVolatileSettings.fiveTone[], 0 = defaults: ID 00000, ZVEI1, PTT ID off)
void fiveToneGetId(char *buf); // buf: FIVETONE_DIGITS + 1 chars
void fiveToneSetId(const char *digits);
fiveToneStandard_t fiveToneGetStandard(void);
void fiveToneSetStandard(fiveToneStandard_t std);
fiveTonePttId_t fiveToneGetPttId(void);
void fiveToneSetPttId(fiveTonePttId_t mode);
const char *fiveToneStandardName(fiveToneStandard_t std);

// Selective call: keys up (software PTT held by the main loop), the TX screen sends the code, then the
// PTT is released. FM channels only. Returns false when a call is already running or the code is empty.
bool fiveToneCallStart(const char *digits);
bool fiveToneCallHoldsPtt(void);      // main loop: keep the PTT down
bool fiveToneCallTakeStartRequest(void); // main loop: one-shot, generate the PTT button event

// Called by the TX screen (FM only, UI task, blocking for the tone sequence)
void fiveToneTxStarted(void);
void fiveToneTxEnding(void);

#endif
