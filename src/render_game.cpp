#include "render_game.h"

#include "assets.h"
#include "types.h"

#include <cmath>
#include <string>

float DrawSpriteNumber(Texture2D* set, int value, float cx, float cy, float scale, Color tint, float spacing)
{
	if (value < 0) value = 0;
	std::string s = std::to_string(value);
	// set[] is indexed by digit, so ch - '0' picks each glyph; first pass measures total width so we can center
	float totalW = 0.0f;
	for (char ch : s) totalW += set[ch - '0'].width * scale + spacing;
	if (!s.empty()) totalW -= spacing;   // no trailing gap after the last digit

	float x = cx - totalW / 2.0f;
	for (char ch : s)
	{
		Texture2D& t = set[ch - '0'];
		float w = t.width * scale;
		float h = t.height * scale;
		DrawTextureEx(t, Vector2{ x, cy - h / 2.0f }, 0.0f, scale, tint);
		x += w + spacing;
	}
	return totalW;
}

void DrawSpriteNumberRight(Texture2D* set, int value, float rightX, float cy, float scale, Color tint, float spacing)
{
	if (value < 0) value = 0;
	std::string s = std::to_string(value);
	float w = 0.0f;
	for (char ch : s) w += set[ch - '0'].width * scale + spacing;
	if (!s.empty()) w -= spacing;
	// DrawSpriteNumber centers on its x, so pass a center shifted left by half-width to make the number end at rightX
	DrawSpriteNumber(set, value, rightX - w / 2.0f, cy, scale, tint, spacing);
}

// day/night shader path: draw a tiled layer (day + night) in a single pass. tiling lives inside the shader via
// dayOffset / nightOffset + layerSize, so this draws ONE screen-wide quad no matter how many tiles wide the layer is
static void DrawDaynightLayer(const DaynightShaderHandle& dn,
	Texture2D dayTex, Texture2D nightTex,
	float scroll, int dstY, int dstHeight, float nightAmount, Vector3 duskTint)
{
	BeginShaderMode(dn.shader);
	// BeginShaderMode binds the active draw's texture into texture0, so we use that as the day layer (drawn below)
	// and bind the night texture separately into texture1
	SetShaderValueTexture(dn.shader, dn.nightTexLoc, nightTex);
	float na[1] = { nightAmount };
	SetShaderValue(dn.shader, dn.nightAmountLoc, na, SHADER_UNIFORM_FLOAT);
	float duskValue[3] = { duskTint.x, duskTint.y, duskTint.z };
	SetShaderValue(dn.shader, dn.duskTintLoc, duskValue, SHADER_UNIFORM_VEC3);
	// scroll is the cumulative offset; the shader takes it mod layerSize.x to wrap the tiling
	float dayOff[2] = { scroll, 0.0f };
	float nightOff[2] = { scroll, 0.0f };
	SetShaderValue(dn.shader, dn.dayOffsetLoc, dayOff, SHADER_UNIFORM_VEC2);
	SetShaderValue(dn.shader, dn.nightOffsetLoc, nightOff, SHADER_UNIFORM_VEC2);
	float layerSize[2] = { (float)dayTex.width, (float)dayTex.height };
	SetShaderValue(dn.shader, dn.layerSizeLoc, layerSize, SHADER_UNIFORM_VEC2);
	float screenSize[2] = { (float)VIRTUAL_W, (float)VIRTUAL_H };
	SetShaderValue(dn.shader, dn.screenSizeLoc, screenSize, SHADER_UNIFORM_VEC2);

	// one screen-wide quad — the shader does the tiling. the day texture is texture0 (raylib's default bind) and its
	// uv is stretched to 0..1 across the screen so the shader can derive per-layer UVs from fragTexCoord
	Rectangle src = { 0, 0, (float)dayTex.width, (float)dayTex.height };
	Rectangle dst = { 0, (float)dstY, (float)VIRTUAL_W, (float)dstHeight };
	DrawTexturePro(dayTex, src, dst, Vector2{ 0, 0 }, 0.0f, WHITE);
	EndShaderMode();
}

void DrawThemeSky(const Theme& theme, const UiTextures& uiTextures, float skyScroll, float midScroll, float nightAmount, float moonScroll, float cameraY, const DaynightShaderHandle* dn)
{
	// the shader only earns its keep mid-crossfade (linear-space blend + dusk tint). at steady day (nightAmount == 0)
	// or steady night (== 1, dusk weight also 0) the plain tiled DrawTexture path is pixel-identical and far cheaper —
	// no per-frame uniform uploads or extra fullscreen passes. most playtime is steady, so the shader stays off the hot path
	const bool useShader = dn && dn->valid && nightAmount > 0.0f && nightAmount < 1.0f;

	const float skyW = (float)theme.bg.width;
	// sub-pixel tile origin via fmod (not integer %): the bg textures are bilinear, so float positions scroll smoothly
	// instead of snapping to whole texels
	float sx = -std::fmod(skyScroll, skyW);
	if (sx > 0.0f) sx -= skyW;

	const float skyY = -cameraY * 0.3f;   // sky parallaxes at 0.3x the photo-mode camera pan
	if (useShader)
	{
		DrawDaynightLayer(*dn, theme.bg, theme.bgNight, skyScroll, (int)skyY, theme.bg.height, nightAmount, theme.duskTint);
	}
	else
	{
		// tile across the screen; overlay the night texture at nightAmount alpha whenever it isn't pure day
		for (float gx = sx; gx < VIRTUAL_W; gx += skyW)
		{
			DrawTextureV(theme.bg, Vector2{ gx, skyY }, WHITE);
			if (nightAmount > 0.0f) DrawTextureV(theme.bgNight, Vector2{ gx, skyY }, Fade(WHITE, nightAmount));
		}
	}

	if (nightAmount > 0.0f)
	{
		// wrap the moon over [screen width + moon width] so it loops seamlessly; offset is its position within that span
		int travel = VIRTUAL_W + uiTextures.moon.width;
		int offset = ((int)moonScroll) % travel;
		if (offset < 0) offset += travel;
		int moonX = VIRTUAL_W - 180 - offset;
		while (moonX < -uiTextures.moon.width) moonX += travel;
		int moonY = (int)(50 - cameraY * 0.3f);
		DrawTexture(uiTextures.moon, moonX, moonY, Fade(WHITE, nightAmount));
	}

	if (theme.hasMid)
	{
		const float midW = (float)theme.mid.width;
		const float midY = VIRTUAL_H - theme.mid.height - cameraY * 0.3f;
		if (useShader)
		{
			DrawDaynightLayer(*dn, theme.mid, theme.midNight, midScroll, (int)midY, theme.mid.height, nightAmount, theme.duskTint);
		}
		else
		{
			float mx = -std::fmod(midScroll, midW);
			if (mx > 0.0f) mx -= midW;
			for (float gx = mx; gx < VIRTUAL_W; gx += midW)
			{
				DrawTextureV(theme.mid, Vector2{ gx, midY }, WHITE);
				if (nightAmount > 0.0f) DrawTextureV(theme.midNight, Vector2{ gx, midY }, Fade(WHITE, nightAmount));
			}
		}
	}
}

void DrawThemeGround(const Theme& theme, float baseScroll, float baseTop, float cameraY)
{
	// tile the ground strip across the screen; baseTop - cameraY moves it 1:1 with the camera (foreground, no parallax)
	const float bx = -std::fmod(baseScroll, static_cast<float>(theme.base.width));
	for (float gx = bx; gx < VIRTUAL_W; gx += theme.base.width)
	{
		DrawTextureV(theme.base, Vector2{ gx, baseTop - cameraY }, WHITE);
	}
}
