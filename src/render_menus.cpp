#include "render_menus.h"

#include "assets.h"
#include "screen_customize.h"
#include "font.h"
#include "gameplay_helpers.h"
#include "render_game.h"
#include "save.h"
#include "system_display.h"
#include "types.h"
#include "ui.h"

#include <cmath>

void TickAndDrawMenuBackdrop(const Theme& theme, const UiTextures& uiTextures,
	float& skyScroll, float& midScroll, float& baseScroll,
	float nightAmount, float baseTop, float frameScale,
	const DaynightShaderHandle* dn)
{
	// parallax: sky drifts slowest, ground fastest (1:1 with frameScale); wrap baseScroll at the tile width
	skyScroll  += 1.0f * frameScale * 0.2f;
	midScroll  += 1.0f * frameScale * 0.4f;
	baseScroll += 1.0f * frameScale;
	if (baseScroll >= theme.base.width) baseScroll -= theme.base.width;

	// menus never wrap skyScroll, so it doubles as the moon scroll (the moonScroll arg) here
	DrawThemeSky(theme, uiTextures, skyScroll, midScroll, nightAmount, skyScroll, 0.0f, dn);
	DrawThemeGround(theme, baseScroll, baseTop);
}

void DrawStripScrollbar(Rectangle strip, float contentW, float& scrollRef, CustomizeStrip stripId,
	Vector2 vmouse, CustomizeStrip& dragWhich, float& dragGrab)
{
	float maxScroll = contentW > strip.width ? contentW - strip.width : 0.0f;
	if (maxScroll <= 0.0f)
	{
		// content fits, so no bar; release the drag if this strip happened to hold it
		if (dragWhich == stripId) dragWhich = CustomizeStrip::NONE;
		return;
	}

	float barY = strip.y + strip.height + 6.0f;
	float barH = 12.0f;
	Rectangle track = { strip.x, barY, strip.width, barH };
	DrawRectangleRec(track, Color{ 40, 40, 60, 200 });
	// thumb width is proportional to the visible fraction; thumbX maps the scroll position onto the free track space
	float thumbW = strip.width * (strip.width / contentW);
	float thumbX = strip.x + (strip.width - thumbW) * (scrollRef / maxScroll);
	Rectangle thumb = { thumbX, barY, thumbW, barH };

	if (dragWhich == CustomizeStrip::NONE && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
	{
		if (CheckCollisionPointRec(vmouse, thumb))
		{
			// grab the thumb: remember where along the thumb we grabbed, so it doesn't snap under the cursor
			dragWhich = stripId;
			dragGrab = vmouse.x - thumbX;
		}
		else if (CheckCollisionPointRec(vmouse, track))
		{
			// click on bare track: center the thumb under the cursor and start dragging
			dragWhich = stripId;
			dragGrab = thumbW * 0.5f;
			float t = (vmouse.x - dragGrab - strip.x) / (strip.width - thumbW);
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			scrollRef = t * maxScroll;
		}
	}
	if (dragWhich == stripId)
	{
		// active drag: cursor x minus the grab offset, as a 0..1 fraction of the free track, drives the scroll
		float t = (vmouse.x - dragGrab - strip.x) / (strip.width - thumbW);
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		scrollRef = t * maxScroll;
		if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) dragWhich = CustomizeStrip::NONE;
	}
	bool thumbHover = CheckCollisionPointRec(vmouse, thumb);
	DrawRectangleRec(thumb, dragWhich == stripId ? GOLD : (thumbHover ? Color{ 220, 220, 220, 255 } : RAYWHITE));
}

PauseMenuAction DrawPauseMenu(bool confirmQuit, int score, int bestScore,
	bool allowRestart,
	SandboxEffect sandboxEffect, Vector2 virtualMouse)
{
	DrawRectangle(0, 0, VIRTUAL_W, VIRTUAL_H, Fade(BLACK, 0.55f));   // dim the frozen game behind the panel
	constexpr float panelWidth = 380.0f;
	constexpr float panelHeight = 360.0f;
	const float panelX = VIRTUAL_W / 2.0f - panelWidth / 2.0f;
	constexpr float panelY = 120.0f;
	DrawRectangleRec(Rectangle{ panelX, panelY, panelWidth, panelHeight }, Color{ 25, 35, 60, 255 });
	DrawRectangleLinesEx(Rectangle{ panelX, panelY, panelWidth, panelHeight }, 3, RAYWHITE);
	DrawText("PAUSED", VIRTUAL_W / 2 - MeasureText("PAUSED", 50) / 2, (int)panelY + 24, 50, RAYWHITE);

	const char* scoreText = TextFormat("Score: %d", score);
	DrawText(scoreText, VIRTUAL_W / 2 - MeasureText(scoreText, 24) / 2, (int)panelY + 90, 24, SKYBLUE);
	const char* bestText = TextFormat("Best: %d", bestScore);
	DrawText(bestText, VIRTUAL_W / 2 - MeasureText(bestText, 24) / 2, (int)panelY + 122, 24, GOLD);

	if (confirmQuit)
	{
		// second-stage prompt: the caller flips confirmQuit on after the "Main Menu" button below returns ConfirmQuit
		DrawText("Quit this run?", VIRTUAL_W / 2 - MeasureText("Quit this run?", 24) / 2, (int)panelY + 170, 24, RAYWHITE);
		if (UiButton(Rectangle{ VIRTUAL_W / 2.0f - 150, panelY + 210, 140, 40 }, "Yes, quit", virtualMouse))
			return PauseMenuAction::ConfirmQuit;
		if (UiButton(Rectangle{ VIRTUAL_W / 2.0f + 10, panelY + 210, 140, 40 }, "No", virtualMouse))
			return PauseMenuAction::CancelQuit;
		return PauseMenuAction::None;
	}

	// the sandbox layout has an extra button + longer labels, so it needs a wider button column
	const float buttonWidth = sandboxEffect != SandboxEffect::NONE ? 280.0f : 220.0f;
	const float buttonX = VIRTUAL_W / 2.0f - buttonWidth / 2.0f;
	if (UiButton(Rectangle{ buttonX, panelY + 160, buttonWidth, 36 }, "Resume", virtualMouse))
		return PauseMenuAction::Resume;
	if (sandboxEffect != SandboxEffect::NONE)
	{
		// Settings deliberately omitted in sandbox mode: rebinding keys mid-tutorial-demo would let the player escape
		// the controlled lesson and adds no value (the sandbox isn't a real run). Restart + Return are enough
		if (UiButton(Rectangle{ buttonX, panelY + 210, buttonWidth, 36 }, "Restart Sandbox", virtualMouse)) return PauseMenuAction::RestartSandbox;
		if (UiButton(Rectangle{ buttonX, panelY + 260, buttonWidth, 36 }, "Return to Tutorial", virtualMouse)) return PauseMenuAction::ReturnToTutorial;
	}
	else
	{
		float y = panelY + 200;
		if (allowRestart)
		{
			if (UiButton(Rectangle{ buttonX, y, buttonWidth, 36 }, "Restart", virtualMouse)) return PauseMenuAction::RestartNormal;
			y += 40;
		}
		if (UiButton(Rectangle{ buttonX, y, buttonWidth, 36 }, "Settings", virtualMouse)) return PauseMenuAction::OpenSettings;
		y += 40;
		// "Main Menu" returns ConfirmQuit; the caller re-enters this menu with confirmQuit = true for the prompt above
		if (UiButton(Rectangle{ buttonX, y, buttonWidth, 36 }, "Main Menu", virtualMouse)) return PauseMenuAction::ConfirmQuit;
	}
	return PauseMenuAction::None;
}

void DrawRuntimeOverlays(GameState state, const SaveData& save,
	const std::string& toastMessage, float toastTime,
	bool transitioning, float transitionTime)
{
	if (state == GameState::PHOTO_MODE)
	{
		// controls hint along the top; KeyName resolves the configurable capture key for display
		const std::string hint = std::string("PHOTO MODE - drag/arrows pan, wheel/+/- zoom, ") +
			KeyName(save.keyPhotoMode) + " capture, ESC exit";
		DrawText(hint.c_str(), VIRTUAL_W / 2 - MeasureText(hint.c_str(), 18) / 2, 20, 18, RAYWHITE);
	}

	if (toastTime > 0.0f && state != GameState::PHOTO_MODE)
	{
		const int width = MeasureText(toastMessage.c_str(), 22);
		// lift the toast out of the fill-width bottom crop (maximized windowed), the same shift the bottom UI rows use
		const int ty = (int)(548.0f - FillModeBottomCrop());
		// near-opaque panel: the toast can appear over any screen (menus are screenshottable), so it must fully cover
		// the text behind it rather than letting a label like "Gameplay" bleed through and read as garbled
		DrawRectangle(VIRTUAL_W / 2 - width / 2 - 14, ty, width + 28, 34, Color{ 12, 18, 34, 240 });
		DrawRectangleLines(VIRTUAL_W / 2 - width / 2 - 14, ty, width + 28, 34, Color{ 90, 120, 180, 255 });
		DrawText(toastMessage.c_str(), VIRTUAL_W / 2 - width / 2, ty + 6, 22, RAYWHITE);
	}

	if (transitioning)
	{
		// triangle fade: alpha ramps 0 -> 1 at the midpoint (transitionTime 0.5) -> 0, blacking out and back
		const float alpha = 1.0f - std::fabs(transitionTime - 0.5f) * 2.0f;
		DrawRectangle(0, 0, VIRTUAL_W, VIRTUAL_H, Fade(BLACK, alpha));
	}

	if (save.showFps && (state == GameState::PLAYING || state == GameState::PAUSED || state == GameState::VS_PLAYING))
	{
		DrawText(TextFormat("FPS %d", GetFPS()), 8, 8, 18, Color{ 255, 240, 130, 255 });
	}
}
