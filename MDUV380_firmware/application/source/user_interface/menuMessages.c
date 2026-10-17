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
#include "user_interface/uiWidgets.h"
#include "functions/messaging.h"
#include "functions/trx.h"

#define VIEW_CHARS       18  // characters per line of a message (8 px font, 160 px screen, room for the scroll marks)
#define VIEW_ROWS        4   // lines of a message shown at once
#define COMPOSE_CHARS    17  // characters per line inside the message box
#define COMPOSE_ROWS     4
#define LIST_VISIBLE     3   // cards shown at once
#define CARD_TOP         19
#define CARD_HEIGHT      32
#define PREVIEW_CHARS    18
#define NAME_CHARS       8   // sender name in a card

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
	// Compose is a 3 step wizard: To -> Message -> Send?
	COMPOSE_TO = 0,
	COMPOSE_TEXT,
	COMPOSE_CONFIRM,
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
static bool statusError;
static const char *statusLine1;
static const char *statusLine2;
static bool lastBlink;

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
	cannedIndex = -1; // the first arrow / knob step shows the first quick message
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

// Text is only appended to: the red key deletes the last character. False when there is nothing left to delete.
static bool deleteLast(char *str, int *pos)
{
	int len = strlen(str);

	if (len == 0)
	{
		return false;
	}

	str[len - 1] = 0;
	*pos = (len - 1);
	return true;
}

static int viewTotalRows(const messagingEntry_t *e)
{
	return (((int)strlen(e->text) + VIEW_CHARS - 1) / VIEW_CHARS);
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
		// Blinking cursor: redraw when it changes
		bool blink = (((ticksGetMillis() / 500U) & 1U) == 0U);

		if (blink != lastBlink)
		{
			lastBlink = blink;
			updateScreen(false);
		}
	}

	if (ev->hasEvent || (statusTimeout > 0))
	{
		handleEvent(ev);
	}

	return menuMessagesExitCode;
}

// "20:46" for a message of today, "05/10" for an older one, "--:--" when the clock was not set
static void formatTime(uint32_t t, char *buf, size_t size)
{
	if (t == 0U)
	{
		snprintf(buf, size, "--:--");
		return;
	}

	int32_t offset = ((nonVolatileSettings.timezone & 0x80) ? (((nonVolatileSettings.timezone & 0x7F) - 64) * (15 * 60)) : 0);
	time_t_custom local = (time_t_custom)((int32_t)t + offset);
	time_t_custom now = (time_t_custom)((int32_t)uiDataGlobal.dateTimeSecs + offset);
	struct tm tmv;

	gmtime_r_Custom(&local, &tmv);

	if ((local / 86400) == (now / 86400))
	{
		snprintf(buf, size, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
	}
	else
	{
		snprintf(buf, size, "%02d/%02d", tmv.tm_mday, (tmv.tm_mon + 1));
	}
}

// Name of the other party: the callsign from the DMR ID database when it is known, otherwise the ID / callsign as is
static void peerName(const messagingEntry_t *e, char *buf, size_t size, size_t maxChars)
{
	dmrIdDataStruct_t rec;

	if ((e->transport == MESSAGING_TRANSPORT_DMR) && dmrIDLookup((uint32_t)atoi(e->peer), &rec) && (rec.text[0] != 0))
	{
		size_t n = 0;

		while ((rec.text[n] != 0) && (rec.text[n] != ' ') && (n < maxChars) && (n < (size - 1U)))
		{
			buf[n] = rec.text[n];
			n++;
		}
		buf[n] = 0;
		return;
	}

	snprintf(buf, size, "%.*s", (int)maxChars, e->peer);
}

// One card of the list: item 0 is "New message". Plain: name, a < / > arrow for the direction, the time and the start of
// the text; a blue dot and brighter text mark an unread message
// The focused element is drawn inverted (filled with the text colour, text in the background colour): readable in sunlight
static uint32_t invertedText(const uiWidgetPalette_t *pal)
{
	return (((pal->text & 0xFFU) < 0x80U) ? 0xFFFFFFU : 0x000000U);
}

static void drawCard(int16_t y, bool selected, int item)
{
	const uiWidgetPalette_t *pal = uiWidgetsPalette();
	uint16_t bg = uiWidgetsThemeBackground();

	if (selected)
	{
		uiWidgetsFillRoundRect(2, y, (DISPLAY_SIZE_X - 4), (CARD_HEIGHT - 2), 3, pal->text);
		bg = uiWidgetsColour(pal->text);
	}
	const uint32_t inv = invertedText(pal);

	if (item == 0)
	{
		uiWidgetsText(8, (y + 7), "+", FONT_SIZE_3, (selected ? inv : pal->accent), bg);
		uiWidgetsText(28, (y + 7), "New message", FONT_SIZE_3, (selected ? inv : pal->text), bg);
		return;
	}

	const messagingEntry_t *e = messagingGetEntry(item - 1);

	if (e == NULL)
	{
		return;
	}

	char name[NAME_CHARS + 1];
	char when[16];
	char preview[PREVIEW_CHARS + 1];
	const uint32_t textColour = (selected ? inv : (e->unread ? pal->text : pal->muted));

	peerName(e, name, sizeof(name), NAME_CHARS);
	snprintf(when, sizeof(when), "%s ", (e->outgoing ? ">" : "<"));
	formatTime(e->time, &when[2], (sizeof(when) - 2U));
	snprintf(preview, sizeof(preview), "%.*s", PREVIEW_CHARS, e->text);

	if (e->unread)
	{
		uiWidgetsFillRoundRect(6, (y + 4), 6, 6, 3, (selected ? inv : pal->accent));
	}

	uiWidgetsText(16, (y + 3), name, FONT_SIZE_2, textColour, bg);
	uiWidgetsTextRight((DISPLAY_SIZE_X - 5), (y + 3), when, FONT_SIZE_2, (selected ? inv : pal->muted), bg);
	uiWidgetsText(6, (y + 13), preview, FONT_SIZE_3, textColour, bg);
}

static void drawList(void)
{
	const uiWidgetPalette_t *pal = uiWidgetsPalette();
	const uint16_t bg = uiWidgetsThemeBackground();
	const int total = menuDataGlobal.numItems; // "New message" + the messages
	const uint32_t unread = messagingGetUnreadCount();
	char buf[SCREEN_LINE_BUFFER_SIZE + 6];
	int first = (menuDataGlobal.currentItemIndex - 1);

	if (first > (total - LIST_VISIBLE))
	{
		first = (total - LIST_VISIBLE);
	}
	if (first < 0)
	{
		first = 0;
	}

	if (unread > 0U)
	{
		snprintf(buf, sizeof(buf), "Messages (%u new)", (unsigned int)unread);
	}
	else
	{
		snprintf(buf, sizeof(buf), "%s", "Messages");
	}
	menuDisplayTitle(buf);

	for (int i = 0; (i < LIST_VISIBLE) && ((first + i) < total); i++)
	{
		drawCard((CARD_TOP + (i * CARD_HEIGHT)), ((first + i) == menuDataGlobal.currentItemIndex), (first + i));
	}

	if (total == 1)
	{
		// Nothing received or sent yet
		uiWidgetsTextCentered((CARD_TOP + CARD_HEIGHT + 14), "No messages yet", FONT_SIZE_3, pal->muted, bg);
		uiWidgetsTextCentered((CARD_TOP + CARD_HEIGHT + 34), "Select New message", FONT_SIZE_2, pal->muted, bg);
	}

	if (menuDataGlobal.currentItemIndex > 0)
	{
		snprintf(buf, sizeof(buf), "%d/%d", menuDataGlobal.currentItemIndex, (total - 1));
		uiWidgetsSoftKeys("Select", buf, "Back");
	}
	else
	{
		uiWidgetsSoftKeys("Select", NULL, "Back");
	}
}

static void drawView(void)
{
	const uiWidgetPalette_t *pal = uiWidgetsPalette();
	const uint16_t bg = uiWidgetsThemeBackground();
	const messagingEntry_t *e = messagingGetEntryBySeq(viewSeq, NULL);

	if (e == NULL)
	{
		return;
	}

	char buf[SCREEN_LINE_BUFFER_SIZE + 4];
	char row[VIEW_CHARS + 1];
	char when[8];
	const bool isDmr = (e->transport == MESSAGING_TRANSPORT_DMR);
	const int totalRows = viewTotalRows(e);

	peerName(e, buf, sizeof(buf), 16);
	menuDisplayTitle(buf);

	// One muted line: Received / Sent, the transport and the time
	formatTime(e->time, when, sizeof(when));
	snprintf(row, sizeof(row), "%s %s", (e->outgoing ? "Sent" : "Received"), (isDmr ? "DMR" : "APRS"));
	uiWidgetsText(6, 22, row, FONT_SIZE_2, pal->muted, bg);
	uiWidgetsTextRight((DISPLAY_SIZE_X - 5), 22, when, FONT_SIZE_2, pal->muted, bg);

	// The text, 19 characters per line
	for (int r = 0; r < VIEW_ROWS; r++)
	{
		size_t start = (size_t)(viewScroll + r) * VIEW_CHARS;

		if (start < strlen(e->text))
		{
			snprintf(row, sizeof(row), "%.*s", VIEW_CHARS, &e->text[start]);
			uiWidgetsText(4, (36 + (r * 16)), row, FONT_SIZE_3, pal->text, bg);
		}
	}

	// More text above / below
	if (viewScroll > 0)
	{
		uiWidgetsTextRight((DISPLAY_SIZE_X - 3), 36, "^", FONT_SIZE_2, pal->accent, bg);
	}
	if ((viewScroll + VIEW_ROWS) < totalRows)
	{
		uiWidgetsTextRight((DISPLAY_SIZE_X - 3), 96, "v", FONT_SIZE_2, pal->accent, bg);
	}

	uiWidgetsSoftKeys("Reply", "# Del", "Back");
}

static void drawCompose(void)
{
	const uiWidgetPalette_t *pal = uiWidgetsPalette();
	const uint16_t bg = uiWidgetsThemeBackground();
	const int step = menuDataGlobal.currentItemIndex;
	const bool dmr = composeIsDmr();
	char buf[SCREEN_LINE_BUFFER_SIZE + 8];
	char row[COMPOSE_CHARS + 1];
	const int textLen = (int)strlen(composeText);
	const uint32_t inv = invertedText(pal);
	const uint16_t fbg = uiWidgetsColour(pal->text); // background of the input box

	menuDisplayTitle("New message");
	// DMR ID is digits only, entered with the plain keypad
	keypadAlphaEnable = ((step == COMPOSE_TEXT) || ((step == COMPOSE_TO) && (dmr == false)));

	switch (step)
	{
		case COMPOSE_TO:
			uiWidgetsText(6, 19, (dmr ? "1/3  TO DMR ID" : "1/3  TO CALLSIGN"), FONT_SIZE_2, pal->accent, bg);
			uiWidgetsFillRoundRect(4, 32, 152, 22, 3, pal->text);
			if (composeTo[0] != 0)
			{
				uiWidgetsText(10, 35, composeTo, FONT_SIZE_3, inv, fbg);
			}
			if (lastBlink)
			{
				uiWidgetsFillRoundRect((int16_t)(10 + (toPos * UI_WIDGET_CHAR_WIDTH)), 50, UI_WIDGET_CHAR_WIDTH, 2, 0, inv);
			}
			uiWidgetsTextCentered(64, (dmr ? "Type the number" : "Type the callsign"), FONT_SIZE_2, pal->muted, bg);
			uiWidgetsSoftKeys("Next", NULL, ((composeTo[0] == 0) ? "Back" : "Del"));
			break;

		case COMPOSE_TEXT:
		{
			int cursorLine = (textPos / COMPOSE_CHARS);
			int firstLine = ((cursorLine >= COMPOSE_ROWS) ? (cursorLine - (COMPOSE_ROWS - 1)) : 0);

			snprintf(buf, sizeof(buf), "%d/%d", textLen, (MESSAGING_TEXT_LEN - 1));
			uiWidgetsText(6, 19, "2/3  MESSAGE", FONT_SIZE_2, pal->accent, bg);
			uiWidgetsTextRight((DISPLAY_SIZE_X - 6), 19, buf, FONT_SIZE_2, ((textLen >= (MESSAGING_TEXT_LEN - 8)) ? pal->error : pal->muted), bg);
			uiWidgetsFillRoundRect(4, 28, 152, 68, 3, pal->text);

			for (int r = 0; r < COMPOSE_ROWS; r++)
			{
				int start = ((firstLine + r) * COMPOSE_CHARS);

				if (start < textLen)
				{
					snprintf(row, sizeof(row), "%.*s", COMPOSE_CHARS, &composeText[start]);
					uiWidgetsText(10, (31 + (r * 16)), row, FONT_SIZE_3, inv, fbg);
				}
			}

			if (lastBlink)
			{
				uiWidgetsFillRoundRect((int16_t)(10 + ((textPos % COMPOSE_CHARS) * UI_WIDGET_CHAR_WIDTH)), (int16_t)(31 + ((cursorLine - firstLine) * 16) + 14), UI_WIDGET_CHAR_WIDTH, 2, 0, inv);
			}
			uiWidgetsTextCentered(99, "Arrows: quick msg", FONT_SIZE_2, pal->muted, bg);
			uiWidgetsSoftKeys("Next", NULL, ((textLen == 0) ? "Back" : "Del"));
		}
		break;

		default: // COMPOSE_CONFIRM
			uiWidgetsText(6, 19, "3/3  SEND?", FONT_SIZE_2, pal->accent, bg);
			uiWidgetsText(6, 33, "TO", FONT_SIZE_2, pal->muted, bg);
			uiWidgetsText(30, 30, composeTo, FONT_SIZE_3, pal->text, bg);
			uiWidgetsOutline(4, 50, 152, 52, 3, pal->separator, bg);
			for (int r = 0; r < 3; r++)
			{
				int start = (r * COMPOSE_CHARS);

				if (start < textLen)
				{
					// The last preview row ends with ".." when the message is longer
					if ((r == 2) && (textLen > (3 * COMPOSE_CHARS)))
					{
						snprintf(row, sizeof(row), "%.*s..", (COMPOSE_CHARS - 2), &composeText[start]);
					}
					else
					{
						snprintf(row, sizeof(row), "%.*s", COMPOSE_CHARS, &composeText[start]);
					}
					uiWidgetsText(10, (53 + (r * 16)), row, FONT_SIZE_3, pal->text, bg);
				}
			}
			uiWidgetsSoftKeys("Send", NULL, "Back");
			break;
	}
}

static void drawStatus(void)
{
	const uiWidgetPalette_t *pal = uiWidgetsPalette();
	const uint16_t bg = uiWidgetsThemeBackground();

	menuDisplayTitle("Messages");
	uiWidgetsTextCentered(46, statusLine1, FONT_SIZE_3, (statusError ? pal->error : pal->text), bg);

	if (statusLine2 != NULL)
	{
		uiWidgetsTextCentered(68, statusLine2, FONT_SIZE_2, pal->muted, bg);
	}
}

static void updateScreen(bool isFirstRun)
{
	(void)isFirstRun;

	displayClearBuf();

	switch (state)
	{
		case STATE_LIST:
			drawList();
			break;

		case STATE_VIEW:
			drawView();
			break;

		case STATE_COMPOSE:
			drawCompose();
			break;

		case STATE_STATUS:
			drawStatus();
			break;
	}

	displayThemeResetToDefault();
	displayRender();
}

static void composeSend(void)
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
			statusError = true;
			state = STATE_STATUS;
			statusTimeout = 1500;
		}
	}
	else if (messagingSendAPRS(composeTo, composeText))
	{
		statusLine1 = "Sent";
		statusLine2 = NULL;
		statusError = false;
		state = STATE_STATUS;
		statusTimeout = 1500;
	}
	else
	{
		soundSetMelody(MELODY_ERROR_BEEP);
		statusLine1 = "Cannot send";
		statusLine2 = "check APRS cfg";
		statusError = true;
		state = STATE_STATUS;
		statusTimeout = 1500;
	}
	updateScreen(false);
}

// Multi-tap text entry, appended at the end (preview first, then the committed character)
static void composeTypeChar(uiEvent_t *ev, char *str, int *pos, int max)
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

static void handleCompose(uiEvent_t *ev)
{
	const int step = menuDataGlobal.currentItemIndex;
	char *str = ((step == COMPOSE_TO) ? composeTo : composeText);
	int *pos = ((step == COMPOSE_TO) ? &toPos : &textPos);
	const int max = ((step == COMPOSE_TO) ? (MESSAGING_PEER_LEN - 1) : (MESSAGING_TEXT_LEN - 1));

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		// Red: delete the last character; on an empty field (or on the last step) go one step back
		if ((step == COMPOSE_CONFIRM) || (deleteLast(str, pos) == false))
		{
			if (step == COMPOSE_TO)
			{
				backToList();
			}
			else
			{
				menuDataGlobal.currentItemIndex = (step - 1);
			}
		}
		updateScreen(false);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (step == COMPOSE_CONFIRM)
		{
			composeSend();
		}
		else if (str[0] == 0)
		{
			soundSetMelody(MELODY_ERROR_BEEP);
		}
		else
		{
			menuDataGlobal.currentItemIndex = (step + 1);
			updateScreen(false);
		}
	}
	else if (step == COMPOSE_TEXT)
	{
		// Arrows and knob step through the quick messages (they replace the text)
		int dir = 0;

		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN) || KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
		{
			dir = 1;
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
		{
			dir = -1;
		}

		if (dir != 0)
		{
			cannedIndex = ((dir > 0) ? ((cannedIndex + 1) % MESSAGING_NUM_CANNED) : ((cannedIndex <= 0) ? (MESSAGING_NUM_CANNED - 1) : (cannedIndex - 1)));
			memset(composeText, 0, sizeof(composeText));
			strncpy(composeText, messagingGetCanned(cannedIndex), (sizeof(composeText) - 1U));
			textPos = strlen(composeText);
			updateScreen(false);
		}
		else
		{
			composeTypeChar(ev, str, pos, max);
		}
	}
	else if (step == COMPOSE_TO)
	{
		if (composeIsDmr())
		{
			int keyval = menuGetKeypadKeyValue(ev, true);

			if ((keyval != 99) && (toPos < DMR_ID_MAX_DIGITS))
			{
				composeTo[toPos++] = (char)('0' + keyval);
				composeTo[toPos] = 0;
				updateScreen(false);
			}
		}
		else
		{
			composeTypeChar(ev, str, pos, max);
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
					if ((viewScroll + VIEW_ROWS) < viewTotalRows(e))
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
