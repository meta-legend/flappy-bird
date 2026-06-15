#pragma once

#include "raylib.h"

// which key the Settings screen is currently waiting to rebind; NONE == -1 means not capturing a key
enum class RebindTarget
{
	NONE = -1,
	PLAYER_ONE_FLAP,
	PLAYER_TWO_FLAP,
	PAUSE,
	PHOTO_MODE,
	RESTART
};

// persistent Settings UI state: the in-progress rebind plus the list's scroll/drag position
struct SettingsScreenState
{
	RebindTarget rebindTarget = RebindTarget::NONE;
	float scroll = 0.0f;
	bool dragging = false;
	float dragGrab = 0.0f;
};
