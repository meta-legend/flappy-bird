#include "gameplay_helpers.h"
#include "game.h"
#include <algorithm>
#include <cstdio>
#include <ctime>

void Bird::Reset(float& speed)
{
	x = Constants::Bird::HomeX;
	y = VIRTUAL_H / 2 - 30;
	velocity = 0;
	rotation = 0;
	sinceJump = 999;
	speed = Constants::Pipes::ScrollSpeed;   // also rewinds the caller's shared scroll speed
}

Pipe::Pipe(float defX, float defY)
	: x(defX), y(defY), defaultX(defX), defaultY(defY)
{
}

// vertical jitter added to a pipe's spawn y on recycle (negative raises the gap, positive lowers it)
int Pipe::Random()
{
	return GetRandomValue(-100, 50);
}

void Pipe::Reset(bool resetX, bool resetY)
{
	if (resetX) x = defaultX;
	if (resetY) y = defaultY;
}

float Randf(float a, float b)
{
	return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f);
}

const char* KeyName(int k)
{
	switch (k)
	{
		case KEY_SPACE: return "Space";
		case KEY_UP: return "Up";
		case KEY_DOWN: return "Down";
		case KEY_LEFT: return "Left";
		case KEY_RIGHT: return "Right";
		case KEY_W: return "W";
		case KEY_A: return "A";
		case KEY_S: return "S";
		case KEY_D: return "D";
		case KEY_P: return "P";
		case KEY_ENTER: return "Enter";
		case KEY_LEFT_SHIFT: return "LShift";
		case KEY_RIGHT_SHIFT: return "RShift";
		case KEY_LEFT_CONTROL: return "LCtrl";
		case KEY_TAB: return "Tab";
	}
	// fall back for keys not special-cased above: F1-F12, then any printable ASCII char, else "?"
	static char buf[8];
	if (k >= KEY_F1 && k <= KEY_F12)
	{
		snprintf(buf, sizeof(buf), "F%d", k - KEY_F1 + 1);
		return buf;
	}
	if (k >= 32 && k < 127) { buf[0] = (char)k; buf[1] = 0; return buf; }
	return "?";
}

// today's UTC date packed as YYYYMMDD (uses gmtime, so the daily challenge rolls over at UTC midnight everywhere)
int TodayYMD()
{
	time_t t = time(nullptr);
	struct tm g;
#ifdef _WIN32
	gmtime_s(&g, &t);
#else
	gmtime_r(&t, &g);
#endif
	return (g.tm_year + 1900) * 10000 + (g.tm_mon + 1) * 100 + g.tm_mday;
}

float CurrentGap(int score, bool ramp)
{
	if (!ramp) return Constants::Pipes::StartingGap;
	float t = std::min((float)score / 30.0f, 1.0f);   // lerp from StartingGap to MinimumGap over the first 30 points
	return Constants::Pipes::StartingGap * (1.0f - t) + Constants::Pipes::MinimumGap * t;
}

float CurrentSpeed(int score, bool ramp)
{
	if (!ramp) return Constants::Pipes::ScrollSpeed;
	int s = score < 50 ? score : 50;   // speed bonus caps out at score 50
	return Constants::Pipes::ScrollSpeed + s * 0.006f;
}

// on a recycled pipe, clear its twist + pickup and maybe roll new ones (skipped entirely in daily/endless)
void FlappyGame::RollPipeExtras(int i, int score)
{
	Pipe& pipe = world.topPipes[i];
	pipe.variation = PipeVariation::NORMAL;
	pipe.windDir = 0.0f;
	pipe.oscPhase = Randf(0.0f, 6.2832f);   // random start phase so oscillating pipes aren't all in lockstep
	pipe.pickup = PickupType::NONE;
	pipe.pickupCollected = false;
	if (singlePlayer.dailyMode || singlePlayer.endlessMode) return;   // those modes stay plain, no twists or pickups

	// tutorial sandbox forces only the effect being demoed, nothing random
	if (interfaceState.sandboxEffect != SandboxEffect::NONE)
	{
		if (interfaceState.sandboxEffect == SandboxEffect::OSCILLATING_PIPES) pipe.variation = PipeVariation::OSCILLATE;
		return;
	}

	if (score >= Constants::Pipes::VariationStartScore)
	{
		// keep at most one twisted pipe on screen: scan the other currently-visible pipes first
		bool anotherVariation = false;
		bool anotherWind = false;
		for (int j = 0; j < Constants::Pipes::Count; j++)
		{
			if (j == i) continue;
			const Pipe& other = world.topPipes[j];
			const bool visible = other.x + Constants::Pipes::Width > 0 && other.x < VIRTUAL_W;
			anotherVariation |= visible && other.variation != PipeVariation::NORMAL;
			anotherWind |= visible && other.variation == PipeVariation::WIND;
		}

		if (!anotherVariation)
		{
			const float now = (float)GetTime();
			const int roll = GetRandomValue(0, 99);
			if (roll < 25) pipe.variation = PipeVariation::OSCILLATE;                              // 25% oscillate
			else if (roll < 35 && !anotherWind && now - singlePlayer.lastWindSpawn > 8.0f)        // 10% wind, min 8s apart
			{
				pipe.variation = PipeVariation::WIND;
				pipe.windDir = GetRandomValue(0, 1) ? 1.0f : -1.0f;
				singlePlayer.lastWindSpawn = now;
			}
		}
	}

	// pickups only ride plain pipes, past StartScore, at ChancePercent odds
	if (score < Constants::Pickups::StartScore || pipe.variation != PipeVariation::NORMAL ||
		GetRandomValue(0, 99) >= Constants::Pickups::ChancePercent)
		return;

	// per-type 7s cooldown so the same orb can't spawn back-to-back; pick whichever is ready (random when both are)
	const float now = (float)GetTime();
	const bool shieldReady = now - singlePlayer.lastShieldSpawn > 7.0f;
	const bool timeWarpReady = now - singlePlayer.lastTimeWarpSpawn > 7.0f;
	if (shieldReady && timeWarpReady) pipe.pickup = GetRandomValue(0, 1) ? PickupType::SHIELD : PickupType::TIME_WARP;
	else if (shieldReady) pipe.pickup = PickupType::SHIELD;
	else if (timeWarpReady) pipe.pickup = PickupType::TIME_WARP;

	if (pipe.pickup == PickupType::SHIELD) singlePlayer.lastShieldSpawn = now;
	else if (pipe.pickup == PickupType::TIME_WARP) singlePlayer.lastTimeWarpSpawn = now;
}
