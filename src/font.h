#pragma once
#include "raylib.h"

// the one pixel font every screen draws with; loaded once at startup
extern Font gPixelFont;

void LoadGameFont();
void UnloadGameFont();

// raylib's int "size" is treated as a line height here; the real glyph size is size * PX_SCALE
constexpr float PX_SCALE = 0.6f;
constexpr float PX_SPACING = 1.0f;

// our pixel-font versions of DrawText / MeasureText. named with a Px suffix so they don't redeclare raylib's
// same-signature globals (which trips Clang/GCC's "static decl follows non-static" rule even though MSVC tolerates it),
// then a preprocessor remap below transparently routes call sites that write DrawText / MeasureText through these.
// y is the line top; the (size - fs) * 0.5 term vertically centers the smaller glyph in that line height
inline void DrawTextPx(const char* t, int x, int y, int size, Color c)
{
	float fs = size * PX_SCALE;
	DrawTextEx(gPixelFont, t, Vector2{ (float)x, y + (size - fs) * 0.5f }, fs, PX_SPACING, c);
}

inline int MeasureTextPx(const char* t, int size)
{
	float fs = size * PX_SCALE;
	return (int)MeasureTextEx(gPixelFont, t, fs, PX_SPACING).x;
}

// route every DrawText / MeasureText call in this codebase to the gPixelFont versions; raylib's originals are still
// callable through DrawTextEx / MeasureTextEx if anything ever needs them
#undef DrawText
#define DrawText DrawTextPx
#undef MeasureText
#define MeasureText MeasureTextPx
