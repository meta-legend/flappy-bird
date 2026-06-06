// fixed resolution, the GameState enum, and the Bird/Pipe/Theme entities
#pragma once
#include "constants.h"
#include "raylib.h"

#include <string>

// fixed internal resolution: everything draws at this size into a render texture, then scales to the window
static constexpr int VIRTUAL_W = 1067;
static constexpr int VIRTUAL_H = 600;

// render target is VIRTUAL_* x this, then bilinear-downscaled to the window; there is cheap anti-aliasing on the scaled result
static constexpr int RENDER_SUPERSAMPLE = 4;

// every screen/mode the game can occupy; drives the top-level state machine
enum class GameState {
	MENU, READY, PLAYING, PAUSED, CREDITS, LEADERBOARD, TROPHIES,
	CUSTOMIZE, SETTINGS, STATS, ACHIEVEMENTS, TUTORIAL,
	VS_MENU, VS_PLAYING, PHOTO_MODE, INFO
};

// per-pipe twist, rolled in on recycle once the score is high enough
enum class PipeVariation { NORMAL, OSCILLATE, WIND, PORTAL };

// collectibles that can spawn inside a pipe gap
enum class PickupType { NONE, SHIELD, TIME_WARP };

// flap frames, ordered so the value indexes BirdSkin::frames[]
enum class BirdFrame
{
	UP_FLAP,
	MID_FLAP,
	DOWN_FLAP,
	COUNT
};

// end-of-run medal tiers, ascending; NONE = -1 keeps "no medal" falsy
enum class MedalRank
{
	NONE = -1,
	BRONZE_MEDAL,
	SILVER_MEDAL,
	GOLD_MEDAL,
	PLATINUM_MEDAL,
	DIAMOND_MEDAL,
	RUBY_MEDAL
};

// tutorial pages in display order; COUNT sizes the page array
enum class TutorialPage
{
	WELCOME,
	GAME_MODES,
	SHIELD_ORB,
	TIME_WARP_ORB,
	WIND_PIPES,
	OSCILLATING_PIPES,
	PHOTO_MODE,
	READY,
	COUNT
};

// which effect the tutorial "Try it!" sandbox is demoing; NONE = -1 means none active
enum class SandboxEffect
{
	NONE = -1,
	SHIELD,
	TIME_WARP,
	WIND,
	OSCILLATING_PIPES
};

// the player entity; one vertical velocity under gravity, no horizontal motion
struct Bird
{
	// x is fixed — the bird holds station and the world scrolls past it
	float x = Constants::Bird::HomeX;
	float y = VIRTUAL_H / 2 - 30;
	// vertical velocity; a hop sets it negative (up), gravity adds positive each frame
	float velocity = 0;
	// draw rotation in degrees, derived from velocity
	float rotation = 0;
	// seconds since last hop; starts huge so the bird is already drooping before first input
	float sinceJump = 999;

	// restore start position/physics; also rewrites speed (the shared scroll speed) back to its base
	void Reset(float& speed);
};

// a full visual theme: day/night backgrounds, pipes, and ground strip
// theme 0 is the classic look; the rest bake at startup from the megacrash art pack (see the bake helpers in main.cpp)
struct Theme
{
	Texture2D bg, bgNight;     // full scene; also the far parallax layer
	Texture2D mid, midNight;   // bottom skyline band; the near parallax layer
	Texture2D pipe, pipe180;   // 88x440 bottom pipe (cap up) and its flipped top counterpart
	Texture2D base;            // horizontally tileable ground strip, drawn at baseTop
	const char* name;
	Vector3 duskTint = { 1.0f, 0.85f, 0.7f }; // tint multiplier at the day/night crossfade peak
	// does this theme draw a near parallax band (mid) over the far bg? Skyline/Sunset/Canyon/Meadow do.
	// the near layer MUST be point-filtered: its hill silhouette has binary alpha, and bilinear bleeds the
	// edge toward transparent-black texels, leaving a dark seam against the far layer — point sampling plus
	// the supersampled target keep it clean
	bool hasMid = true;

	// lazy-load state: the textures above are baked on first use, not at startup
	bool isClassic = false;       // Classic loads raw images; pack themes bake from the art pack
	std::string dayPath, nightPath;        // background sources
	std::string dayNearPath, nightNearPath;// near/mid sources (pack themes with hasMid)
	bool visualsLoaded = false;   // whether bg/bgNight/mid/midNight have been baked yet
	Texture2D dayThumb{}, nightThumb{};    // small previews for the Customize theme picker
};

// one selectable bird; frames are indexed by BirdFrame
struct BirdSkin
{
	Texture2D frames[static_cast<int>(BirdFrame::COUNT)];
	const char* name;
};

// one pipe pair from the recycled scroll pool
struct Pipe
{
	// live position; moves left with the scroll each frame
	float x, y;
	// spawn position, restored by Reset()
	float defaultX, defaultY;
	// twist state, set on recycle; baseY is the unshifted y that OSCILLATE swings around
	PipeVariation variation = PipeVariation::NORMAL;
	float baseY = 0.0f;
	float oscPhase = 0.0f;
	float windDir = 0.0f;   // -1 / +1 sideways push for the WIND variation
	float windFireTime = -1.0f;   // runClock when this pipe first became visible (pausable),
	                              // so the wind sweep sprite plays exactly once
	// pickup riding in this pair's gap
	PickupType pickup = PickupType::NONE;
	bool pickupCollected = false;

	// spawns at (defX, defY) and records them as the Reset() defaults
	Pipe(float defX, float defY);

	// random vertical shift for this pipe's spawn; RNG is seeded once at startup (not here),
	// so pipes recycled in the same second still differ
	int Random();

	// restore the default position; xTrue/yTrue choose which axes reset
	void Reset(bool xTrue, bool yTrue);
};
