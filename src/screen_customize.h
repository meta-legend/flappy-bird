#pragma once

#include "constants.h"
#include "raylib.h"

#include <vector>

struct BirdSkin;
struct SaveData;
struct Theme;

// which player's loadout the Customize screen is editing (2-player has two)
enum class PlayerSlot
{
	PLAYER_ONE,
	PLAYER_TWO
};

// the horizontal pick-rows on the Customize screen; NONE == -1 marks "no row grabbed"
enum class CustomizeStrip
{
	NONE = -1,
	BIRD_COLOR,
	PIPE_STYLE,
	PIPE_COLOR
};

// persistent Customize UI state kept across frames: per-row scroll offsets, the active drag, and
// per-item click-bounce timers (one slot per bird, one per theme)
struct CustomizeScreenState
{
	PlayerSlot playerSlot = PlayerSlot::PLAYER_ONE;
	float birdScroll = 0.0f;
	float pipeStyleScroll = 0.0f;
	float pipeColorScroll = 0.0f;
	float pageScroll = 0.0f;
	bool pageDragging = false;
	float pageDragGrab = 0.0f;
	CustomizeStrip dragWhich = CustomizeStrip::NONE;
	float dragGrab = 0.0f;
	float birdClickAnim[8] = { 0 };
	float themeClickAnim[5] = { 0 };
};

// what the user changed this frame, so the caller can persist the save and apply the new bird/pipe
struct CustomizeScreenResult
{
	bool backClicked = false;
	bool birdChoiceChanged = false;
	bool pipeChoiceChanged = false;
};

// advance the click-bounce timers in state by dt
void UpdateCustomizeAnimations(CustomizeScreenState& state, float dt);
// draw + handle the Customize screen for one frame; edits sd directly and reports what changed via the result
CustomizeScreenResult DrawCustomizeScreen(
	CustomizeScreenState& state,
	SaveData& sd,
	std::vector<BirdSkin>& skins,
	std::vector<Theme>& themes,
	Texture2D (&pipeTex)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount],
	Vector2 vmouse,
	float dt);
