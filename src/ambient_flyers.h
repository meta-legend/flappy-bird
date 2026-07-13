#pragma once
#include "raylib.h"

struct Theme;

// one background flyer (bird or aircraft) drifting across the sky. base art faces LEFT; facingRight flips it
struct AmbientFlyer
{
	float x = 0.0f, y = 0.0f;
	float vx = 0.0f;          // px/s; sign is the travel direction
	float bobPhase = 0.0f;    // drives a gentle vertical bob (birds only; aircraft hold altitude)
	float animTimer = 0.0f;
	int frame = 0;
	int species = 0;          // index into Theme::flyer[species]
	bool facingRight = false; // sprite flipped horizontally when travelling right
	bool active = false;
};

// a small pool of flyers plus the spawn timer. one loose flock (2-3) is aloft at a time, appearing occasionally
struct AmbientFlyerSystem
{
	static constexpr int kMax = 3;
	AmbientFlyer flyers[kMax];
	float spawnCountdown = 5.0f;   // seconds until the next spawn attempt
	int lastThemeIndex = -1;       // clear the flock on a theme switch so species never mixes
};

// advance positions + flap animation, cull off-screen flyers, and occasionally spawn. aircraft spawn one at a time at a
// fixed altitude; birds spawn in loose scattered flocks. clears everything (and no-ops) when the theme has no flyers
// (flyerCount == 0) or reduce-motion is on. call once per frame, in any game state
void UpdateAmbientFlyers(AmbientFlyerSystem& sys, int themeIndex, int flyerCount, bool aircraft, bool reduceMotion, float dt);

// draw the active flyers in the sky. call right after the theme sky backdrop, before the pipes/foreground
void DrawAmbientFlyers(const AmbientFlyerSystem& sys, const Theme& theme);
