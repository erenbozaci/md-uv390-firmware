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

#ifndef _OPENGD77_UIWIDGETS_H_
#define _OPENGD77_UIWIDGETS_H_

#include "user_interface/uiGlobals.h"
#include "hardware/HX8353E.h"

// Small colour widgets shared by the screens that draw their own layout (messages, ...).
// Everything is drawn on the theme background with fixed palettes, two of them so that it reads on a light
// theme background (the default) as well as on a dark one.

typedef struct
{
	uint32_t text;
	uint32_t muted;
	uint32_t selFill;   // background of the selected row of a list
	uint32_t accent;    // focus colour
	uint32_t separator;
	uint32_t keyGreen;
	uint32_t keyRed;
	uint32_t ok;
	uint32_t error;
} uiWidgetPalette_t;

const uiWidgetPalette_t *uiWidgetsPalette(void);   // light or dark, depending on the theme background
uint16_t uiWidgetsThemeBackground(void);           // native colour of the theme background
uint16_t uiWidgetsColour(uint32_t rgb888);         // native colour
void uiWidgetsText(int16_t x, int16_t y, const char *text, ucFont_t font, uint32_t rgb888, uint16_t bgNative); // x is the left edge
void uiWidgetsTextCentered(int16_t y, const char *text, ucFont_t font, uint32_t rgb888, uint16_t bgNative);
void uiWidgetsTextRight(int16_t xRight, int16_t y, const char *text, ucFont_t font, uint32_t rgb888, uint16_t bgNative);
void uiWidgetsFillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint32_t rgb888);
void uiWidgetsOutline(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint32_t rgb888, uint16_t bgNative);
// Soft key bar at the bottom, like the "Menu" label of the home screen: what the green key does on the left, what the
// red key does on the right, an optional hint in the middle (any of them may be NULL)
void uiWidgetsSoftKeys(const char *left, const char *center, const char *right);

#define UI_WIDGET_CHAR_WIDTH   8 // FONT_SIZE_2 and FONT_SIZE_3

#endif // _OPENGD77_UIWIDGETS_H_
