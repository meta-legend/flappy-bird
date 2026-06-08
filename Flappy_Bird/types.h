// core game constants, the game state enum, and the Bird/Pipe entities
#pragma once
#include "raylib.h"
#include <ctime>

// the game's fixed internal resolution; everything is drawn at this size into a
// render texture, then scaled to the actual window (for fullscreen support)
static constexpr int VIRTUAL_W = 800;
static constexpr int VIRTUAL_H = 600;

// bird physics
static constexpr float GRAVITY = 1500.0f;       // px per second^2 (downward)
static constexpr float JUMP_IMPULSE = -450.0f;  // px per second (upward burst on tap)

// bird rotation
static constexpr float BIRD_UP_ANGLE = -20.0f;  // nose up tilt right after a hop (degrees)
static constexpr float BIRD_DOWN_ANGLE = 90.0f; // max nose down while falling
static constexpr float BIRD_ROT_HOLD = 0.40f;   // seconds to hold the up tilt before diving
static constexpr float BIRD_ROT_RATE = 200.0f;  // degrees per second rotating down after the hold

// the screens the game can be in
enum GameState { MENU, PLAYING, PAUSED, CREDITS, LEADERBOARD, CONTROLS };

// the flappy bird obj
struct Bird
{
	// the default x and y for the flappy bird
	float x = 200;
	float y = VIRTUAL_H / 2 - 30;
	// vertical velocity for the hop/gravity physics
	float velocity = 0;
	// current draw rotation (degrees) and time since the last hop
	float rotation = 0;
	float sinceJump = 999;

	// reset the x, y, velocity, rotation and speed to the defaults
	void Reset(float& speed)
	{
		x = 200;
		y = VIRTUAL_H / 2 - 30;
		velocity = 0;
		rotation = 0;
		sinceJump = 999;
		speed = 1;
	}
};

// the pipe obj
struct Pipe
{
	// the x and y of the pipe
	float x, y;
	// the default x and y of the pipe
	float defaultX, defaultY;

	// pipe contructor
	Pipe(float defX, float defY)
	{
		x = defX;
		defaultX = defX;
		y = defY;
		defaultY = defY;
	}

	// returns random y offset
	int Random()
	{
		SetRandomSeed((unsigned)time(0));
		return GetRandomValue(-35, 35);
	}

	// resets pipe positions to default positions
	void Reset(bool xTrue, bool yTrue)
	{
		if (xTrue)
		{
			x = defaultX;
		}

		if (yTrue)
		{
			y = defaultY;
		}
	}
};
