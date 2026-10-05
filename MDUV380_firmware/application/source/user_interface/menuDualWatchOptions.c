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
#include "user_interface/uiLocalisation.h"

#if defined(HAS_DUAL_WATCH_OPTIONS)

enum
{
	DW_OPT_DUAL_SCREEN = 0,
	DW_OPT_AUTOSTART,
	DW_OPT_SIDE_B,
	DW_OPT_HOME,
	DW_OPT_SPEED,
	DW_OPT_ON_RX,
	NUM_DW_OPTIONS
};

static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);

static menuStatus_t menuDualWatchExitCode = MENU_STATUS_SUCCESS;

static const char *const homeNames[4] = { "Current", "VFO A", "VFO B", "Channel" };
static const char *const speedNames[DUALWATCH_NUM_SPEEDS] = { "Default", "90ms", "200ms", "400ms" };

static uint16_t options(void)
{
	return (uint16_t)nonVolatileSettings.dualWatchOptions;
}

static void setOptions(uint16_t value)
{
	settingsSet(nonVolatileSettings.dualWatchOptions, (int16_t)value);
}

menuStatus_t menuDualWatchOptions(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = NUM_DW_OPTIONS;
		menuDataGlobal.currentItemIndex = 0;

		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->dual_watch);
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		promptsPlayNotAfterTx();

		updateScreen(true);
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}

	menuDualWatchExitCode = MENU_STATUS_SUCCESS;

	if (ev->hasEvent)
	{
		handleEvent(ev);
	}

	return menuDualWatchExitCode;
}

static void updateScreen(bool isFirstRun)
{
	char buf[SCREEN_LINE_BUFFER_SIZE];

	(void)isFirstRun;

	displayClearBuf();
	menuDisplayTitle(currentLanguage->dual_watch);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = menuGetMenuOffset(NUM_DW_OPTIONS, i);

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		switch (mNum)
		{
			case DW_OPT_DUAL_SCREEN:
				snprintf(buf, sizeof(buf), "Dual scr:%s", ((options() & DUALWATCH_DUAL_SCREEN) ? currentLanguage->on : currentLanguage->off));
				break;

			case DW_OPT_AUTOSTART:
				snprintf(buf, sizeof(buf), "Auto:%s", ((options() & DUALWATCH_AUTOSTART) ? currentLanguage->on : currentLanguage->off));
				break;

			case DW_OPT_SIDE_B:
				snprintf(buf, sizeof(buf), "Side B:%s", ((options() & DUALWATCH_CHANNEL_B) ? "Channel" : "VFO B"));
				break;

			case DW_OPT_HOME:
				snprintf(buf, sizeof(buf), "Home:%s", homeNames[(options() & DUALWATCH_HOME_MASK) >> DUALWATCH_HOME_SHIFT]);
				break;

			case DW_OPT_SPEED:
				snprintf(buf, sizeof(buf), "Speed:%s", speedNames[(options() & DUALWATCH_SPEED_MASK) >> DUALWATCH_SPEED_SHIFT]);
				break;

			default:
				snprintf(buf, sizeof(buf), "On RX:%s", ((options() & DUALWATCH_STAY) ? "Stay" : "Switch"));
				break;
		}

		menuDisplayEntry(i, mNum, buf, 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
	}

	displayRender();
}

// Step a value of 'count' states, wrapping around
static uint16_t cycle(uint16_t value, uint16_t count, bool up)
{
	return (up ? ((value + 1U) % count) : ((value + count - 1U) % count));
}

static void changeOption(bool up)
{
	uint16_t o = options();

	switch (menuDataGlobal.currentItemIndex)
	{
		case DW_OPT_DUAL_SCREEN:
			o ^= DUALWATCH_DUAL_SCREEN;
			break;

		case DW_OPT_AUTOSTART:
			o ^= DUALWATCH_AUTOSTART;
			break;

		case DW_OPT_SIDE_B:
			o ^= DUALWATCH_CHANNEL_B;
			break;

		case DW_OPT_HOME:
			o = ((o & ~DUALWATCH_HOME_MASK) | (cycle(((o & DUALWATCH_HOME_MASK) >> DUALWATCH_HOME_SHIFT), 4U, up) << DUALWATCH_HOME_SHIFT));
			break;

		case DW_OPT_SPEED:
			o = ((o & ~DUALWATCH_SPEED_MASK) | (cycle(((o & DUALWATCH_SPEED_MASK) >> DUALWATCH_SPEED_SHIFT), DUALWATCH_NUM_SPEEDS, up) << DUALWATCH_SPEED_SHIFT));
			break;

		default:
			o ^= DUALWATCH_STAY;
			break;
	}

	setOptions(o);
}

static void handleEvent(uiEvent_t *ev)
{
	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		updateScreen(false);
		return;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, NUM_DW_OPTIONS);
		updateScreen(false);
		menuDualWatchExitCode |= MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, NUM_DW_OPTIONS);
		updateScreen(false);
		menuDualWatchExitCode |= MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_RIGHT))
	{
		changeOption(true);
		updateScreen(false);
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_LEFT))
	{
		changeOption(false);
		updateScreen(false);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED) || KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		menuSystemPopPreviousMenu();
	}
}

#endif // HAS_DUAL_WATCH_OPTIONS
