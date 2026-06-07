#pragma once
#include "raylib.h"

// the one pixel font every screen draws with; loaded once at startup
extern Font gPixelFont;

void LoadGameFont();
void UnloadGameFont();

// raylib's int "size" is treated as a line height here; the real glyph size is size * PX_SCALE
constexpr float PX_SCALE = 0.6f;
constexpr float PX_SPACING = 1.0f;

// deliberately shadows raylib's DrawText so every call in the codebase routes through gPixelFont.
// y is the line top; the (size - fs) * 0.5 term vertically centers the smaller glyph in that line height
static inline void DrawText(const char* t, int x, int y, int size, Color c)
{
	float fs = size * PX_SCALE;
	DrawTextEx(gPixelFont, t, Vector2{ (float)x, y + (size - fs) * 0.5f }, fs, PX_SPACING, c);
}

// shadows raylib's MeasureText with the same PX_SCALE/PX_SPACING as DrawText, so layout math stays consistent
static inline int MeasureText(const char* t, int size)
{
	float fs = size * PX_SCALE;
	return (int)MeasureTextEx(gPixelFont, t, fs, PX_SPACING).x;
}
