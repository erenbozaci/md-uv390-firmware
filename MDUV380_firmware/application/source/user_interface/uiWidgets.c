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
#include "user_interface/uiWidgets.h"

static const uiWidgetPalette_t paletteLight = { 0x000000U, 0x6B7782U, 0xD6E6F7U, 0x1E88E5U, 0xB7C3CFU, 0x1B8E3EU, 0xC62828U, 0x00802BU, 0xC62828U };
static const uiWidgetPalette_t paletteDark  = { 0xFFFFFFU, 0x93A1AEU, 0x24415FU, 0x4FA3FFU, 0x46535FU, 0x3DE88AU, 0xFF6B6BU, 0x3DE88AU, 0xFF6B6BU };

// Text is cut so that it never runs past the right edge of the screen
static void clipToScreen(int16_t x, const char *text, char *out, size_t outSize)
{
	int maxChars = ((DISPLAY_SIZE_X - ((x < 0) ? 0 : x)) / UI_WIDGET_CHAR_WIDTH);

	if (maxChars > (int)(outSize - 1U))
	{
		maxChars = (int)(outSize - 1U);
	}
	snprintf(out, outSize, "%.*s", ((maxChars > 0) ? maxChars : 0), text);
}

#if defined(HAS_COLOURS)

uint16_t uiWidgetsColour(uint32_t rgb888)
{
	return displayConvertRGB888ToNative(rgb888);
}

uint16_t uiWidgetsThemeBackground(void)
{
	uint16_t fg;
	uint16_t bg;

	displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	displayGetForegroundAndBackgroundColours(&fg, &bg);
	return bg;
}

const uiWidgetPalette_t *uiWidgetsPalette(void)
{
	uint16_t bg = uiWidgetsThemeBackground();

	// Brightness from the green channel (6 bits, same position in RGB565 and BGR565), the colours are stored byte swapped
	return ((((((uint16_t)((bg >> 8) | (bg << 8))) >> 5) & 0x3FU) < 0x20U) ? &paletteDark : &paletteLight);
}

void uiWidgetsText(int16_t x, int16_t y, const char *text, ucFont_t font, uint32_t rgb888, uint16_t bgNative)
{
	char clipped[(DISPLAY_SIZE_X / UI_WIDGET_CHAR_WIDTH) + 1];

	if (x < 0)
	{
		x = 0;
	}

	clipToScreen(x, text, clipped, sizeof(clipped));
	displaySetForegroundAndBackgroundColours(uiWidgetsColour(rgb888), bgNative);
	displayPrintCore(x, y, clipped, font, TEXT_ALIGN_LEFT, false);
}

void uiWidgetsTextCentered(int16_t y, const char *text, ucFont_t font, uint32_t rgb888, uint16_t bgNative)
{
	uiWidgetsText((int16_t)((DISPLAY_SIZE_X - (int)(strlen(text) * UI_WIDGET_CHAR_WIDTH)) / 2), y, text, font, rgb888, bgNative);
}

void uiWidgetsTextRight(int16_t xRight, int16_t y, const char *text, ucFont_t font, uint32_t rgb888, uint16_t bgNative)
{
	uiWidgetsText((int16_t)(xRight - (int)(strlen(text) * UI_WIDGET_CHAR_WIDTH)), y, text, font, rgb888, bgNative);
}

void uiWidgetsFillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint32_t rgb888)
{
	uint16_t c = uiWidgetsColour(rgb888);

	displaySetForegroundAndBackgroundColours(c, c);
	displayFillRoundRect(x, y, w, h, r, true);
}

void uiWidgetsOutline(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint32_t rgb888, uint16_t bgNative)
{
	displaySetForegroundAndBackgroundColours(uiWidgetsColour(rgb888), bgNative);
	displayDrawRoundRect(x, y, w, h, r, true);
}

#else // no colours: plain theme drawing

uint16_t uiWidgetsColour(uint32_t rgb888)
{
	(void)rgb888;
	return 0;
}

uint16_t uiWidgetsThemeBackground(void)
{
	return 0;
}

const uiWidgetPalette_t *uiWidgetsPalette(void)
{
	return &paletteLight;
}

void uiWidgetsText(int16_t x, int16_t y, const char *text, ucFont_t font, uint32_t rgb888, uint16_t bgNative)
{
	char clipped[(DISPLAY_SIZE_X / UI_WIDGET_CHAR_WIDTH) + 1];

	(void)rgb888;
	(void)bgNative;
	clipToScreen(((x < 0) ? 0 : x), text, clipped, sizeof(clipped));
	displayPrintCore(((x < 0) ? 0 : x), y, clipped, font, TEXT_ALIGN_LEFT, false);
}

void uiWidgetsTextCentered(int16_t y, const char *text, ucFont_t font, uint32_t rgb888, uint16_t bgNative)
{
	(void)rgb888;
	(void)bgNative;
	displayPrintCore(0, y, text, font, TEXT_ALIGN_CENTER, false);
}

void uiWidgetsTextRight(int16_t xRight, int16_t y, const char *text, ucFont_t font, uint32_t rgb888, uint16_t bgNative)
{
	uiWidgetsText((int16_t)(xRight - (int)(strlen(text) * UI_WIDGET_CHAR_WIDTH)), y, text, font, rgb888, bgNative);
}

void uiWidgetsFillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint32_t rgb888)
{
	(void)rgb888;
	displayFillRoundRect(x, y, w, h, r, true);
}

void uiWidgetsOutline(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint32_t rgb888, uint16_t bgNative)
{
	(void)rgb888;
	(void)bgNative;
	displayDrawRoundRect(x, y, w, h, r, true);
}

#endif // HAS_COLOURS

void uiWidgetsSoftKeys(const char *left, const char *center, const char *right)
{
	const uiWidgetPalette_t *pal = uiWidgetsPalette();
	const uint16_t bg = uiWidgetsThemeBackground();
	const int16_t y = (DISPLAY_SIZE_Y - FONT_SIZE_2_HEIGHT - 1);

	// A thin separator above the labels
	displaySetForegroundAndBackgroundColours(uiWidgetsColour(pal->separator), bg);
	displayDrawFastHLine(0, (y - 2), DISPLAY_SIZE_X, true);

	if (left != NULL)
	{
		uiWidgetsText(4, y, left, FONT_SIZE_2, pal->text, bg);
	}

	if (right != NULL)
	{
		uiWidgetsTextRight((DISPLAY_SIZE_X - 4), y, right, FONT_SIZE_2, pal->text, bg);
	}

	if (center != NULL)
	{
		uiWidgetsTextCentered(y, center, FONT_SIZE_2, pal->muted, bg);
	}

	displayThemeResetToDefault();
}
