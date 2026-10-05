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

#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/messaging.h"
#include "functions/trx.h"

#define LINE_CHARS 16 // characters per row (8px font)

static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);

typedef enum
{
	STATE_LIST = 0,
	STATE_VIEW,
	STATE_COMPOSE,
	STATE_STATUS
} messagesState_t;

enum
{
	COMPOSE_TO = 0,
	COMPOSE_TEXT,
	COMPOSE_CANNED,
	NUM_COMPOSE_ITEMS
};

static menuStatus_t menuMessagesExitCode = MENU_STATUS_SUCCESS;
static messagesState_t state;
static int listItemIndex;     // remembered list position while viewing / composing
static uint32_t viewSeq;      // sequence number of the viewed message (indices shift when a message arrives)
static int viewScroll;        // first visible text row in VIEW
static char composeTo[MESSAGING_PEER_LEN];
static char composeText[MESSAGING_TEXT_LEN];
static int toPos;
static int textPos;
static int cannedIndex;
static int statusTimeout;
static const char *statusLine1;
static const char *statusLine2;

#define DMR_ID_MAX_DIGITS 8

// On a digital channel messages are addressed to a DMR ID (DMR data call), otherwise to an APRS callsign
static bool composeIsDmr(void)
{
	return (trxGetMode() == RADIO_MODE_DIGITAL);
}

static void startCompose(const char *peer)
{
	memset(composeTo, 0, sizeof(composeTo));
	memset(composeText, 0, sizeof(composeText));
	strncpy(composeTo, peer, (sizeof(composeTo) - 1U));

	if (composeIsDmr())
	{
		// Only keep a numeric (DMR ID) peer, drop callsigns
		for (const char *c = composeTo; *c != 0; c++)
		{
			if ((*c < '0') || (*c > '9'))
			{
				composeTo[0] = 0;
				break;
			}
		}
	}
	toPos = strlen(composeTo);
	textPos = 0;
	cannedIndex = 0;
	state = STATE_COMPOSE;
	menuDataGlobal.numItems = NUM_COMPOSE_ITEMS;
	// Start on the To field when it is empty, otherwise straight on the text
	menuDataGlobal.currentItemIndex = ((composeTo[0] == 0) ? COMPOSE_TO : COMPOSE_TEXT);
}

static void backToList(void)
{
	state = STATE_LIST;
	keypadAlphaEnable = false;
	menuDataGlobal.numItems = (int)messagingGetCount() + 1;
	menuDataGlobal.currentItemIndex = ((listItemIndex < menuDataGlobal.numItems) ? listItemIndex : (menuDataGlobal.numItems - 1));
}

// Local versions of moveCursor{Left,Right}InString(): those have a quirk for 16 chars long strings
static void cursorLeft(char *str, int *pos, bool deleteChar)
{
	if (*pos > 0)
	{
		(*pos)--;
		if (deleteChar)
		{
			memmove(&str[*pos], &str[*pos + 1], (strlen(str) - *pos));
		}
	}
}

static void cursorRight(char *str, int *pos, int max, bool insertSpace)
{
	int len = strlen(str);

	if (*pos < len)
	{
		if (insertSpace && (len < max))
		{
			memmove(&str[*pos + 1], &str[*pos], (len - *pos + 1));
			str[*pos] = ' ';
		}
		(*pos)++;
	}
}

static int viewTextRows(void)
{
	// last row is used by the key hints
	return (MENU_END_ITERATION_VALUE - MENU_START_ITERATION_VALUE);
}

static int viewTotalRows(const messagingEntry_t *e)
{
	return (((int)strlen(e->text) + LINE_CHARS - 1) / LINE_CHARS);
}

menuStatus_t menuMessages(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		voicePromptsInit();
		listItemIndex = 0;
		backToList();
		updateScreen(true);
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}

	menuMessagesExitCode = MENU_STATUS_SUCCESS;

	if (state == STATE_COMPOSE)
	{
		int col = ((menuDataGlobal.currentItemIndex == COMPOSE_TO) ? (3 + toPos) : (textPos % LINE_CHARS));

		if ((menuDataGlobal.currentItemIndex == COMPOSE_TO) || (menuDataGlobal.currentItemIndex == COMPOSE_TEXT))
		{
			displayThemeApply(THEME_ITEM_BG, THEME_ITEM_BG_MENU_ITEM_SELECTED);
			menuUpdateCursor(col, false, true);
			displayThemeResetToDefault();
		}
	}

	if (ev->hasEvent || (statusTimeout > 0))
	{
		handleEvent(ev);
	}

	return menuMessagesExitCode;
}

static void viewGetRow(const messagingEntry_t *e, int row, char *buf)
{
	size_t len = strlen(e->text);
	size_t start = ((size_t)row * LINE_CHARS);

	buf[0] = 0;
	if (start < len)
	{
		snprintf(buf, (LINE_CHARS + 1), "%s", &e->text[start]);
	}
}

static void updateScreen(bool isFirstRun)
{
	char buf[SCREEN_LINE_BUFFER_SIZE];

	(void)isFirstRun;

	displayClearBuf();

	switch (state)
	{
		case STATE_LIST:
			menuDisplayTitle("Messages");

			for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
			{
				int mNum = menuGetMenuOffset(menuDataGlobal.numItems, i);

				if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
				{
					continue;
				}
				else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
				{
					break;
				}

				if (mNum == 0)
				{
					snprintf(buf, sizeof(buf), "%s", "[New message]");
				}
				else
				{
					const messagingEntry_t *e = messagingGetEntry(mNum - 1);

					snprintf(buf, sizeof(buf), "%c%c%s %s", (e->unread ? '*' : ' '), (e->outgoing ? '>' : '<'), e->peer, e->text);
				}

				menuDisplayEntry(i, mNum, buf, 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
			}
			break;

		case STATE_VIEW:
			{
				const messagingEntry_t *e = messagingGetEntryBySeq(viewSeq, NULL);

				if (e == NULL)
				{
					break;
				}

				snprintf(buf, sizeof(buf), "%s %s", (e->outgoing ? "To" : "From"), e->peer);
				menuDisplayTitle(buf);

				displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
				for (int row = 0; row < viewTextRows(); row++)
				{
					viewGetRow(e, (viewScroll + row), buf);
					displayPrintCore(DISPLAY_X_POS_MENU_TEXT_OFFSET, (DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT + ((MENU_START_ITERATION_VALUE + row) * MENU_ENTRY_HEIGHT)), buf, FONT_SIZE_3, TEXT_ALIGN_LEFT, false);
				}

				displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
				displayPrintCore(DISPLAY_X_POS_MENU_TEXT_OFFSET, (DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT + (MENU_END_ITERATION_VALUE * MENU_ENTRY_HEIGHT)), "GRN:reply #:del", FONT_SIZE_3, TEXT_ALIGN_LEFT, false);
				displayThemeResetToDefault();
			}
			break;

		case STATE_COMPOSE:
			{
				static const char *const titles[NUM_COMPOSE_ITEMS] = { "To (callsign)", "Message", "Canned message" };

				menuDisplayTitle((menuDataGlobal.currentItemIndex == COMPOSE_TO) ? (composeIsDmr() ? "To (DMR ID)" : titles[COMPOSE_TO]) : titles[menuDataGlobal.currentItemIndex]);
				// DMR ID is digits only, entered with the plain keypad
				keypadAlphaEnable = ((menuDataGlobal.currentItemIndex == COMPOSE_TEXT) || ((menuDataGlobal.currentItemIndex == COMPOSE_TO) && (composeIsDmr() == false)));

				for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
				{
					int mNum = menuGetMenuOffset(NUM_COMPOSE_ITEMS, i);

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
						case COMPOSE_TO:
							snprintf(buf, sizeof(buf), "To:%s", composeTo);
							break;

						case COMPOSE_TEXT:
							// Show the 16 chars window containing the cursor
							snprintf(buf, sizeof(buf), "%s", &composeText[(textPos / LINE_CHARS) * LINE_CHARS]);
							if (buf[0] == 0)
							{
								snprintf(buf, sizeof(buf), "%s", "(empty)");
							}
							break;

						default:
							snprintf(buf, sizeof(buf), "%s", messagingGetCanned(cannedIndex));
							break;
					}

					menuDisplayEntry(i, mNum, buf, 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
				}

				displayThemeApply(THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
				displayPrintCore(DISPLAY_X_POS_MENU_TEXT_OFFSET, (DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT + (MENU_END_ITERATION_VALUE * MENU_ENTRY_HEIGHT)), "GRN:send RED:back", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
				displayThemeResetToDefault();
			}
			break;

		case STATE_STATUS:
			displayPrintCentered(16, statusLine1, FONT_SIZE_3);
			if (statusLine2 != NULL)
			{
				displayPrintCentered(32, statusLine2, FONT_SIZE_3);
			}
			break;
	}

	displayRender();
}

static void handleCompose(uiEvent_t *ev)
{
	const bool sk2 = BUTTONCHECK_DOWN(ev, BUTTON_SK2);
	const int field = menuDataGlobal.currentItemIndex;
	char *str = ((field == COMPOSE_TO) ? composeTo : composeText);
	int *pos = ((field == COMPOSE_TO) ? &toPos : &textPos);
	const int max = ((field == COMPOSE_TO) ? (MESSAGING_PEER_LEN - 1) : (MESSAGING_TEXT_LEN - 1));

	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, NUM_COMPOSE_ITEMS);
		updateScreen(false);
		menuMessagesExitCode |= MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, NUM_COMPOSE_ITEMS);
		updateScreen(false);
		menuMessagesExitCode |= MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
	{
		if (field == COMPOSE_CANNED)
		{
			cannedIndex = ((cannedIndex + 1) % MESSAGING_NUM_CANNED);
		}
		else if ((field == COMPOSE_TO) && composeIsDmr())
		{
			// digits are appended, nothing to move
		}
		else
		{
			cursorRight(str, pos, max, sk2);
		}
		updateScreen(false);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
	{
		if (field == COMPOSE_CANNED)
		{
			cannedIndex = ((cannedIndex + MESSAGING_NUM_CANNED - 1) % MESSAGING_NUM_CANNED);
		}
		else if ((field == COMPOSE_TO) && composeIsDmr())
		{
			// backspace
			if (toPos > 0)
			{
				composeTo[--toPos] = 0;
			}
		}
		else
		{
			cursorLeft(str, pos, sk2);
		}
		updateScreen(false);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (field == COMPOSE_CANNED)
		{
			memset(composeText, 0, sizeof(composeText));
			strncpy(composeText, messagingGetCanned(cannedIndex), (sizeof(composeText) - 1U));
			textPos = strlen(composeText);
			menuDataGlobal.currentItemIndex = COMPOSE_TEXT;
			updateScreen(false);
		}
		else if ((composeTo[0] == 0) || (composeText[0] == 0))
		{
			soundSetMelody(MELODY_ERROR_BEEP);
			menuDataGlobal.currentItemIndex = ((composeTo[0] == 0) ? COMPOSE_TO : COMPOSE_TEXT);
			updateScreen(false);
		}
		else
		{
			keypadAlphaEnable = false;

			if (txInhibitCheckAndWarn())
			{
				return;
			}

			if (composeIsDmr())
			{
				if (messagingSendDMR((uint32_t)atoi(composeTo), composeText))
				{
					// The main loop keys up (TX screen) and the HR-C6000 sends the data call
					backToList();
				}
				else
				{
					soundSetMelody(MELODY_ERROR_BEEP);
					statusLine1 = "Cannot send";
					statusLine2 = "check DMR ID";
					state = STATE_STATUS;
					statusTimeout = 1500;
				}
			}
			else if (messagingSendAPRS(composeTo, composeText))
			{
				statusLine1 = "Sent";
				statusLine2 = NULL;
				state = STATE_STATUS;
				statusTimeout = 1500;
			}
			else
			{
				soundSetMelody(MELODY_ERROR_BEEP);
				statusLine1 = "Cannot send";
				statusLine2 = "check APRS cfg";
				state = STATE_STATUS;
				statusTimeout = 1500;
			}
			updateScreen(false);
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		backToList();
		updateScreen(false);
	}
	else if ((field == COMPOSE_TO) && composeIsDmr())
	{
		int keyval = menuGetKeypadKeyValue(ev, true);

		if ((keyval != 99) && (toPos < DMR_ID_MAX_DIGITS))
		{
			composeTo[toPos++] = (char)('0' + keyval);
			composeTo[toPos] = 0;
			updateScreen(false);
		}
	}
	else if (field != COMPOSE_CANNED)
	{
		if ((ev->keys.event == KEY_MOD_PREVIEW) && (*pos < max))
		{
			str[*pos] = ev->keys.key;
			updateScreen(false);
		}
		else if ((ev->keys.event == KEY_MOD_PRESS) && (*pos < max))
		{
			str[*pos] = ev->keys.key;
			if ((*pos < (int)strlen(str)) && (*pos < (max - 1)))
			{
				(*pos)++;
			}
			updateScreen(false);
		}
	}
}

static void handleEvent(uiEvent_t *ev)
{
	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		updateScreen(false);
		return;
	}

	switch (state)
	{
		case STATE_LIST:
			if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
			{
				menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, menuDataGlobal.numItems);
				updateScreen(false);
				menuMessagesExitCode |= MENU_STATUS_LIST_TYPE;
			}
			else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
			{
				menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, menuDataGlobal.numItems);
				updateScreen(false);
				menuMessagesExitCode |= MENU_STATUS_LIST_TYPE;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				listItemIndex = menuDataGlobal.currentItemIndex;

				if (listItemIndex == 0)
				{
					startCompose("");
				}
				else
				{
					const messagingEntry_t *opened = messagingGetEntry(listItemIndex - 1);

					viewSeq = ((opened != NULL) ? opened->seq : 0U);
					viewScroll = 0;
					messagingMarkRead(listItemIndex - 1);
					state = STATE_VIEW;
				}
				updateScreen(false);
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				menuSystemPopPreviousMenu();
			}
			break;

		case STATE_VIEW:
			{
				uint32_t viewIndex = 0;
				const messagingEntry_t *e = messagingGetEntryBySeq(viewSeq, &viewIndex);

				if (e == NULL)
				{
					backToList();
					updateScreen(false);
					break;
				}

				if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
				{
					if ((viewScroll + viewTextRows()) < viewTotalRows(e))
					{
						viewScroll++;
						updateScreen(false);
					}
				}
				else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
				{
					if (viewScroll > 0)
					{
						viewScroll--;
						updateScreen(false);
					}
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					startCompose(e->peer);
					updateScreen(false);
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
				{
					messagingDelete(viewIndex);
					backToList();
					updateScreen(false);
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					backToList();
					updateScreen(false);
				}
			}
			break;

		case STATE_COMPOSE:
			handleCompose(ev);
			break;

		case STATE_STATUS:
			statusTimeout--;
			if ((statusTimeout <= 0) || KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) || KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				statusTimeout = 0;
				backToList();
				updateScreen(false);
			}
			break;
	}
}

#endif // !PLATFORM_GD77S
