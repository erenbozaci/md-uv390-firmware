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

#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiDualScreen.h"
#include "user_interface/uiLocalisation.h"
#include "functions/trx.h"

#if defined(HAS_DUAL_WATCH_OPTIONS)

// "Dual screen": like the Anytone radios, two rows, A on top and B below. Each row is independently a VFO
// (row A = VFO A, row B = VFO B) or a channel of the current zone. One row is active: the radio is tuned to
// it and the normal VFO / channel screen of that row is the one running; the other row is only drawn.
// Up/Down select the other row, the red key switches the active row between VFO and channel (the rotary,
// as always, changes the frequency or the channel).
//
// State: nonVolatileSettings.dualWatchOptions bits DUALWATCH_LINE_A_CHANNEL / _LINE_B_CHANNEL / _ACTIVE_B.
// Each channel row has its own zone and index in that zone, kept in RAM (the active row is always the
// current zone's last used channel, the other row keeps what it had).

static int16_t slotIndex[2] = { -1, -1 };
static int16_t slotZone[2] = { -1, -1 };

static uint16_t opts(void)
{
	return (uint16_t)nonVolatileSettings.dualWatchOptions;
}

static void setOpts(uint16_t value)
{
	if (value != opts())
	{
		settingsSet(nonVolatileSettings.dualWatchOptions, (int16_t)value);
	}
}

static int activeLine(void)
{
	return ((opts() & DUALWATCH_ACTIVE_B) ? 1 : 0);
}

static void setActiveLine(int line)
{
	setOpts(line ? (opts() | DUALWATCH_ACTIVE_B) : (opts() & ~DUALWATCH_ACTIVE_B));
}

static bool lineIsChannel(int line)
{
	return ((opts() & (line ? DUALWATCH_LINE_B_CHANNEL : DUALWATCH_LINE_A_CHANNEL)) != 0U);
}

static void setLineType(int line, bool channel)
{
	uint16_t mask = (line ? DUALWATCH_LINE_B_CHANNEL : DUALWATCH_LINE_A_CHANNEL);

	setOpts(channel ? (opts() | mask) : (opts() & ~mask));
}

static bool zoneUsable(void)
{
	return (currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone > 0);
}

// Index in zone 'z' to use for a channel row (0 / first in use when the stored one is not valid any more)
static int16_t validIndexInZone(const CodeplugZone_t *z, int16_t idx)
{
	if (CODEPLUG_ZONE_IS_ALLCHANNELS(*z))
	{
		if ((idx >= 1) && codeplugAllChannelsIndexIsInUse(idx))
		{
			return idx;
		}

		for (int16_t i = 1; i <= z->NOT_IN_CODEPLUGDATA_highestIndex; i++)
		{
			if (codeplugAllChannelsIndexIsInUse(i))
			{
				return i;
			}
		}
		return 1;
	}

	return (((idx >= 0) && (idx < z->NOT_IN_CODEPLUGDATA_numChannelsInZone)) ? idx : 0);
}

static int16_t channelNumberForIndex(const CodeplugZone_t *z, int16_t idx)
{
	return (CODEPLUG_ZONE_IS_ALLCHANNELS(*z) ? idx : z->channels[idx]);
}

// The zone a channel row belongs to (the current one until the row has its own)
static int16_t zoneOfLine(int line)
{
	return ((slotZone[line] >= 0) ? slotZone[line] : nonVolatileSettings.currentZone);
}

// Remember where the active channel row is (zone changes and the rotary move it)
static void rememberActiveChannelRow(int line)
{
	if (zoneUsable())
	{
		slotZone[line] = nonVolatileSettings.currentZone;
		slotIndex[line] = codeplugGetLastUsedChannelInCurrentZone();
	}
}

bool uiDualScreenIsEnabled(void)
{
	return ((opts() & DUALWATCH_DUAL_SCREEN) != 0U);
}

bool uiDualScreenCanDraw(void)
{
	// An active channel row needs the channel screen's data
	if (lineIsChannel(activeLine()) && (channelScreenChannelData.rxFreq == 0U))
	{
		return false;
	}

	return (uiDualScreenIsEnabled() && (trxTransmissionEnabled == false) && (trxIsTransmitting == false) && (uiDataGlobal.displayChannelSettings == false));
}

static void drawRow(int line, int16_t y, bool active)
{
	char l1[SCREEN_LINE_BUFFER_SIZE + 8];
	char l2[SCREEN_LINE_BUFFER_SIZE + 8];
	char l3[SCREEN_LINE_BUFFER_SIZE + 8];
	char name[SCREEN_LINE_BUFFER_SIZE];
	CodeplugChannel_t tmp;
	CodeplugZone_t zoneTmp;
	CodeplugChannel_t *ch = NULL;
	const char *modeShort = "";

	l3[0] = 0;

	if (lineIsChannel(line) == false)
	{
		ch = &settingsVFOChannel[line];
		modeShort = ((ch->chMode == RADIO_MODE_DIGITAL) ? "DMR" : "ANA");
		snprintf(l1, sizeof(l1), "%d.%05d", (int)(ch->rxFreq / 100000), (int)(ch->rxFreq % 100000));
		snprintf(l2, sizeof(l2), "%s", modeShort);
		snprintf(l3, sizeof(l3), "VFO %c", (line ? 'B' : 'A'));
	}
	else
	{
		const CodeplugZone_t *zone = NULL;
		int number = 0; // position in the zone, 1 based (the channel number itself in the All Channels zone)

		if (active)
		{
			ch = &channelScreenChannelData;
			zone = &currentZone;
			number = codeplugGetLastUsedChannelInCurrentZone();
		}
		else if (codeplugZoneGetDataForNumber(zoneOfLine(line), &zoneTmp) && (zoneTmp.NOT_IN_CODEPLUGDATA_numChannelsInZone > 0))
		{
			int16_t idx = validIndexInZone(&zoneTmp, slotIndex[line]);

			codeplugChannelGetDataForIndex(channelNumberForIndex(&zoneTmp, idx), &tmp);
			ch = &tmp;
			zone = &zoneTmp;
			number = idx;
		}

		if (zone != NULL)
		{
			number += (CODEPLUG_ZONE_IS_ALLCHANNELS(*zone) ? 0 : 1);
		}

		if ((ch != NULL) && (ch->rxFreq != 0U) && (zone != NULL))
		{
			codeplugUtilConvertBufToString((char *)ch->name, name, 16);
			snprintf(l1, sizeof(l1), "%s", name);
			// channels show the number in the zone and the mode, not the frequency
			snprintf(l2, sizeof(l2), "C%03d %s", number, ((ch->chMode == RADIO_MODE_DIGITAL) ? "DMR" : "ANA"));

			if (CODEPLUG_ZONE_IS_ALLCHANNELS(*zone))
			{
				snprintf(l3, sizeof(l3), "%s", currentLanguage->all_channels);
			}
			else
			{
				char zoneName[SCREEN_LINE_BUFFER_SIZE];

				codeplugUtilConvertBufToString((char *)zone->name, zoneName, 16);
				snprintf(l3, sizeof(l3), "%s", zoneName);
			}
		}
		else
		{
			snprintf(l1, sizeof(l1), "%s", "--");
			snprintf(l2, sizeof(l2), "%s", "no channel");
		}
	}

	if (active)
	{
		displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
		displayFillRoundRect(DISPLAY_X_POS_MENU_OFFSET, y, (DISPLAY_SIZE_X - (DISPLAY_X_POS_MENU_OFFSET * 2)), ((FONT_SIZE_3_HEIGHT * 2) + FONT_SIZE_2_HEIGHT + 6), 2, true);
	}

	displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
	displayPrintCore(0, (y + 2), l1, FONT_SIZE_3, TEXT_ALIGN_CENTER, active);
	displayPrintCore(0, (y + 2 + FONT_SIZE_3_HEIGHT), l2, FONT_SIZE_3, TEXT_ALIGN_CENTER, active);
	displayPrintCore(0, (y + 2 + (FONT_SIZE_3_HEIGHT * 2)), l3, FONT_SIZE_2, TEXT_ALIGN_CENTER, active);
	displayThemeResetToDefault();
}

void uiDualScreenDraw(void)
{
	drawRow(0, 20, (activeLine() == 0));
	drawRow(1, 70, (activeLine() == 1));
}

void uiDualScreenScreenEntered(bool channelScreen)
{
	if (uiDualScreenIsEnabled() == false)
	{
		return;
	}

	// Whatever way the screen has been entered, the active row follows it
	if (channelScreen)
	{
		int a = activeLine();

		setLineType(a, true);
		rememberActiveChannelRow(a);
	}
	else
	{
		int v = nonVolatileSettings.currentVFONumber;

		setActiveLine(v);
		setLineType(v, false);
	}
}

// Select what the radio has to show / tune to for a row, by running the matching screen
static void goToLine(int line)
{
	if (lineIsChannel(line))
	{
		int16_t zoneNum = zoneOfLine(line);

		if ((zoneNum != nonVolatileSettings.currentZone) || (zoneUsable() == false))
		{
			if (zoneNum != nonVolatileSettings.currentZone)
			{
				// Same things as choosing the zone in the zone list
				settingsSet(nonVolatileSettings.overrideTG, 0);
				settingsSet(nonVolatileSettings.currentZone, zoneNum);
				settingsSet(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE], 0);
			}
			uiChannelInitializeCurrentZone();
		}

		if (zoneUsable() == false)
		{
			// No channel to go to: stay a VFO
			setLineType(line, false);
			goToLine(line);
			return;
		}

		slotZone[line] = nonVolatileSettings.currentZone;
		codeplugSetLastUsedChannelInCurrentZone(validIndexInZone(&currentZone, slotIndex[line]));
		channelScreenChannelData.rxFreq = 0; // tells the channel screen its data is not valid and has to be reloaded
		menuSystemSetCurrentMenu(UI_CHANNEL_MODE);
	}
	else
	{
		settingsSet(nonVolatileSettings.currentVFONumber, (uint8_t)line);
		currentChannelData = &settingsVFOChannel[line];
		menuSystemSetCurrentMenu(UI_VFO_MODE);
	}
}

bool uiDualScreenHandleKey(uiEvent_t *ev, bool channelScreen, bool busy)
{
	if ((uiDualScreenIsEnabled() == false) || busy || uiDataGlobal.Scan.active || ((ev->events & KEY_EVENT) == 0) ||
			BUTTONCHECK_DOWN(ev, BUTTON_SK1) || BUTTONCHECK_DOWN(ev, BUTTON_SK2) || trxTransmissionEnabled || trxIsTransmitting)
	{
		return false;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_DOWN))
	{
		// The other row
		int a = activeLine();

		if (channelScreen && lineIsChannel(a))
		{
			rememberActiveChannelRow(a); // where the rotary / the zone list left it
		}

		setActiveLine(1 - a);
		goToLine(1 - a);
		return true;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		// VFO <-> channel for the active row
		int a = activeLine();

		if (channelScreen && lineIsChannel(a))
		{
			rememberActiveChannelRow(a);
		}

		setLineType(a, (lineIsChannel(a) == false));
		goToLine(a);
		return true;
	}

	return false;
}

#endif // HAS_DUAL_WATCH_OPTIONS
