#pragma once

#include "raylib.h"

struct Theme;
struct UiTextures;

// cached uniform locations for the day/night crossfade shader; pass a handle with valid == true to take the
// shader path, while nullptr or valid == false falls back to the plain two-DrawTexture alpha blend
struct DaynightShaderHandle
{
	Shader shader;
	int dayTexLoc = -1;
	int nightTexLoc = -1;
	int nightAmountLoc = -1;
	int duskTintLoc = -1;
	int dayOffsetLoc = -1;
	int nightOffsetLoc = -1;
	int layerSizeLoc = -1;
	int screenSizeLoc = -1;
	bool valid = false;
};

// draw value from a 0-9 digit atlas centered on (cx, cy); returns the total width drawn, for layout
float DrawSpriteNumber(Texture2D* set, int value, float cx, float cy, float scale, Color tint = WHITE, float spacing = 2.0f);
// same, but right-aligned so the last digit ends at rightX
void DrawSpriteNumberRight(Texture2D* set, int value, float rightX, float cy, float scale, Color tint = WHITE, float spacing = 2.0f);
// draw far bg + near mid parallax + moon, crossfading day->night by nightAmount (0..1); the *Scroll args are
// parallax offsets, cameraY pans for photo mode, and dn selects the shader path when non-null
void DrawThemeSky(const Theme& theme, const UiTextures& uiTextures, float skyScroll, float midScroll, float nightAmount, float moonScroll, float cameraY = 0.0f, const DaynightShaderHandle* dn = nullptr);
// draw the horizontally tiling ground strip at baseTop, scrolled by baseScroll
void DrawThemeGround(const Theme& theme, float baseScroll, float baseTop, float cameraY = 0.0f);
