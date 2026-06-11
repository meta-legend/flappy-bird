#pragma once

#include "raylib.h"
#include "types.h"

#include <string>

struct Theme;
struct UiTextures;
struct SaveData;
struct DaynightShaderHandle;
enum class CustomizeStrip;

// what the pause menu returned this frame; None means no button was pressed
enum class PauseMenuAction
{
	None,
	Resume,
	ConfirmQuit,
	CancelQuit,
	RestartNormal,
	RestartSandbox,
	OpenSettings,
	ReturnToTutorial
};

// scrollbar for a horizontal Customize strip; scrollRef is read + written, while dragWhich/dragGrab carry an
// in-progress drag across frames (stripId tags this strip so only the grabbed one scrolls)
void DrawStripScrollbar(Rectangle strip, float contentW, float& scrollRef, CustomizeStrip stripId,
	Vector2 vmouse, CustomizeStrip& dragWhich, float& dragGrab);

// advance the parallax scroll values and draw the themed sky + ground used as the shared backdrop behind every
// menu; centralised here so each screen only deals with its own content
void TickAndDrawMenuBackdrop(const Theme& theme, const UiTextures& uiTextures,
	float& skyScroll, float& midScroll, float& baseScroll,
	float nightAmount, float baseTop, float frameScale,
	const DaynightShaderHandle* dn = nullptr);
// draw the pause overlay and return the chosen action; confirmQuit shows the quit-confirm prompt, allowRestart
// enables the restart buttons, sandboxEffect tags which restart applies (normal run vs tutorial sandbox demo)
PauseMenuAction DrawPauseMenu(bool confirmQuit, int score, int bestScore,
	bool allowRestart,
	SandboxEffect sandboxEffect, Vector2 virtualMouse);
// draw the on-top overlays for the current state: the toast message (fading via toastTime) and screen transitions
void DrawRuntimeOverlays(GameState state, const SaveData& save,
	const std::string& toastMessage, float toastTime,
	bool transitioning, float transitionTime);
