/*
 * Options -> 5 Tone (this fork): PTT ID, standard, own ID, selective call.
 *
 * Use of this source code or binary releases for commercial purposes is strictly forbidden
 * (see the licence of the project).
 */

#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/fiveTone.h"
#include "functions/sound.h"
#include "functions/trx.h"

enum
{
	FT_OPT_PTTID = 0,
	FT_OPT_STANDARD,
	FT_OPT_MYID,
	FT_OPT_CALL,
	NUM_FT_OPTIONS
};

static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);

static menuStatus_t menuFiveToneExitCode = MENU_STATUS_SUCCESS;
static char callCode[FIVETONE_DIGITS + 1]; // RAM only, kept while the radio is on (last called code)
static bool callError;

static const char *const optionNames[NUM_FT_OPTIONS] = { "PTT ID", "Standard", "My ID", "Call" };
static const char *const pttIdNames[FIVETONE_NUM_PTTIDS] = { "Off", "Start", "End", "Both" };

menuStatus_t menuFiveTone(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = NUM_FT_OPTIONS;
		menuDataGlobal.currentItemIndex = 0;
		callError = false;
		voicePromptsInit();

		updateScreen(true);
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}

	menuFiveToneExitCode = MENU_STATUS_SUCCESS;

	if (ev->hasEvent)
	{
		handleEvent(ev);
	}

	return menuFiveToneExitCode;
}

static const char *optionHelp(int option)
{
	switch (option)
	{
		case FT_OPT_PTTID:
			return "Send My ID on PTT";

		case FT_OPT_STANDARD:
			return "Tone set and speed";

		case FT_OPT_MYID:
			return "Keys 0-9: type ID";

		default:
			if (callError)
			{
				return "FM channel only";
			}
			return ((callCode[0] == 0) ? "Keys 0-9: code" : "Green: call");
	}
}

static void optionValue(int option, char *buf, size_t size)
{
	switch (option)
	{
		case FT_OPT_PTTID:
			snprintf(buf, size, "%s", pttIdNames[fiveToneGetPttId()]);
			break;

		case FT_OPT_STANDARD:
			snprintf(buf, size, "%s", fiveToneStandardName(fiveToneGetStandard()));
			break;

		case FT_OPT_MYID:
			fiveToneGetId(buf);
			break;

		default:
			snprintf(buf, size, "%s", ((callCode[0] == 0) ? "-" : callCode));
			break;
	}
}

// Same layout as the DW options: plain list, value in the options colour, help on the last row
static void updateScreen(bool isFirstRun)
{
	char buf[SCREEN_LINE_BUFFER_SIZE];
	char value[SCREEN_LINE_BUFFER_SIZE];
	const int rows = (MENU_END_ITERATION_VALUE - MENU_START_ITERATION_VALUE + 1);
	const int visible = (rows - 1);
	int first = (menuDataGlobal.currentItemIndex - (visible - 1));

	(void)isFirstRun;

	if (first > (NUM_FT_OPTIONS - visible))
	{
		first = (NUM_FT_OPTIONS - visible);
	}
	if (first < 0)
	{
		first = 0;
	}

	displayClearBuf();
	menuDisplayTitle("5 Tone");

	for (int r = 0; (r < visible) && ((first + r) < NUM_FT_OPTIONS); r++)
	{
		const int item = (first + r);

		optionValue(item, value, sizeof(value));
		snprintf(buf, sizeof(buf), "%s:%s", optionNames[item], value);
		menuDisplayEntry((MENU_START_ITERATION_VALUE + r), item, buf, (strlen(optionNames[item]) + 1), THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	displayPrintCore(0, (DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT + ((MENU_START_ITERATION_VALUE + visible) * MENU_ENTRY_HEIGHT) + 4),
			optionHelp(menuDataGlobal.currentItemIndex), FONT_SIZE_2, TEXT_ALIGN_CENTER, false);
	displayThemeResetToDefault();

	displayRender();
}

static void changeOption(bool up)
{
	switch (menuDataGlobal.currentItemIndex)
	{
		case FT_OPT_PTTID:
			fiveToneSetPttId((fiveTonePttId_t)(up ? ((fiveToneGetPttId() + 1) % FIVETONE_NUM_PTTIDS) : ((fiveToneGetPttId() + FIVETONE_NUM_PTTIDS - 1) % FIVETONE_NUM_PTTIDS)));
			break;

		case FT_OPT_STANDARD:
			fiveToneSetStandard((fiveToneStandard_t)(up ? ((fiveToneGetStandard() + 1) % FIVETONE_NUM_STDS) : ((fiveToneGetStandard() + FIVETONE_NUM_STDS - 1) % FIVETONE_NUM_STDS)));
			break;

		default:
			break;
	}
}

static void handleEvent(uiEvent_t *ev)
{
	const int item = menuDataGlobal.currentItemIndex;

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		updateScreen(false);
		return;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, NUM_FT_OPTIONS);
		callError = false;
		updateScreen(false);
		menuFiveToneExitCode |= MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, NUM_FT_OPTIONS);
		callError = false;
		updateScreen(false);
		menuFiveToneExitCode |= MENU_STATUS_LIST_TYPE;
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
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (item != FT_OPT_CALL)
		{
			menuSystemPopPreviousMenu();
		}
		else if ((callCode[0] == 0) || (fiveToneCallStart(callCode) == false))
		{
			callError = (callCode[0] != 0);
			soundSetMelody(MELODY_ERROR_BEEP);
			updateScreen(false);
		}
		// else: the main loop keys up, the TX screen sends the code and comes back here
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		// Red deletes the last digit of the call code, otherwise leaves
		size_t len = strlen(callCode);

		if ((item == FT_OPT_CALL) && (len > 0))
		{
			callCode[len - 1] = 0;
			callError = false;
			updateScreen(false);
		}
		else
		{
			menuSystemPopPreviousMenu();
		}
	}
	else if ((item == FT_OPT_MYID) || (item == FT_OPT_CALL))
	{
		int keyval = menuGetKeypadKeyValue(ev, true);

		if ((keyval >= 0) && (keyval <= 9))
		{
			if (item == FT_OPT_MYID)
			{
				// The digits shift in from the right
				char id[FIVETONE_DIGITS + 1];

				fiveToneGetId(id);
				memmove(&id[0], &id[1], (FIVETONE_DIGITS - 1));
				id[FIVETONE_DIGITS - 1] = (char)('0' + keyval);
				fiveToneSetId(id);
			}
			else
			{
				size_t len = strlen(callCode);

				if (len < FIVETONE_DIGITS)
				{
					callCode[len] = (char)('0' + keyval);
					callCode[len + 1] = 0;
				}
				callError = false;
			}
			updateScreen(false);
		}
	}
}
