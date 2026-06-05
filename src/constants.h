#pragma once

#include "save.h"

#include <cstddef>
#include <type_traits>

// achievement ids; each indexes the parallel Names/Descriptions arrays in Constants::Achievements
enum class Achievement : int
{
	FirstFlight = 0,
	Centurion,
	Marathon,
	Persistent,
	FlapMaster,
	NightOwl,
	Daylight,
	Untouchable,
	TrophyCabinet,
	DailyDevotee,
	Speedrunner,
	StyleSwitch,
	COUNT
};

namespace Constants
{
	namespace Bird
	{
		inline constexpr float Gravity = 1500.0f;       // downward acceleration, px/s^2
		inline constexpr float JumpImpulse = -450.0f;   // velocity set on a flap, px/s (negative = up)
		inline constexpr float HitboxRadius = 19.0f;    // collision-circle radius, px
		inline constexpr float HomeX = 340.0f;          // fixed bird x; the world scrolls past this
		inline constexpr float WindMinX = 220.0f;       // wind clamps the bird to [WindMinX, WindMaxX] around HomeX
		inline constexpr float WindMaxX = 460.0f;

		namespace Rotation
		{
			inline constexpr float UpAngle = -20.0f;    // nose-up angle right after a flap, degrees
			inline constexpr float DownAngle = 90.0f;   // straight-down angle at terminal fall, degrees
			inline constexpr float HoldSeconds = 0.40f; // hold UpAngle this long after a flap before rotating down
			inline constexpr float Rate = 200.0f;       // rotate-toward-DownAngle speed, degrees/second
		}
	}

	namespace Pipes
	{
		inline constexpr float ScrollSpeed = 1.5f;   // base world speed, px per fixed scroll step (see Motion)
		inline constexpr float BodyHeight = 266.0f;  // visible pipe-body height, px
		inline constexpr float Width = 88.0f;        // pipe width, px
		inline constexpr int Count = 5;              // pipes kept in the recycled pool (enough to span the screen)
		inline constexpr float Spacing = 255.0f;     // horizontal distance between consecutive pipe pairs, px
		inline constexpr float StartingGap = 144.0f; // vertical gap at score 0, px
		inline constexpr float MinimumGap = 110.0f;  // gap the difficulty ramp tightens toward, px
		inline constexpr int VariationStartScore = 20;   // score at which pipe twists (wind/oscillate/portal) begin rolling in
		inline constexpr float WindStrength = 200.0f;    // sideways push from a WIND pipe, px/s
		inline constexpr float WindBurstRightSweepSeconds = 3.6f;  // duration of the >>> wind sweep sprite
		inline constexpr float WindBurstLeftSweepSeconds = 2.8f;   // duration of the <<< wind sweep sprite
		inline constexpr float OscillationAmplitude = 40.0f; // OSCILLATE vertical swing from the spawn y, px
		inline constexpr float OscillationPeriod = 2.5f;     // seconds for one full up-and-down cycle
	}

	namespace Pickups
	{
		inline constexpr float ShieldDuration = 8.0f;       // seconds a shield lasts
		inline constexpr float SlowMotionDuration = 4.0f;   // slow half of the time orb, seconds
		inline constexpr float SpeedUpDuration = 3.0f;      // speed half, seconds (shorter because it is harder)
		inline constexpr float SlowFactor = 0.5f;           // world-speed multiplier while slowed
		inline constexpr float SpeedFactor = 1.5f;          // world-speed multiplier while sped up
		inline constexpr float SlowMusicPitchFactor = 0.7f; // music pitch while slowed
		inline constexpr float SpeedMusicPitchFactor = 1.2f;// music pitch while sped up
		inline constexpr float ShieldPickupPitch = 1.25f;   // collect-sound pitch for a shield orb
		inline constexpr float SlowPickupPitch = 0.9f;      // collect-sound pitch for the slow time orb
		inline constexpr float SpeedPickupPitch = 1.55f;    // collect-sound pitch for the fast time orb
		inline constexpr float TopPipeShieldPushFactor = 0.25f; // downward shove when a shield eats a top-pipe hit
		inline constexpr float ShieldGrace = 0.3f;          // seconds of invulnerability after a shield absorbs a hit
		inline constexpr int StartScore = 15;               // pickups begin spawning at this score
		inline constexpr int ChancePercent = 15;            // percent chance an eligible pipe carries a pickup
		inline constexpr float Radius = 14.0f;              // pickup collision radius, px
	}

	namespace Themes
	{
		// each run flips day -> night once, at a random score within this range
		inline constexpr int NightSwitchMin = 10;
		inline constexpr int NightSwitchMax = 15;
	}

	namespace Animation
	{
		inline constexpr float DeathShakeDuration = 0.35f;   // screen-shake length on death, seconds
		inline constexpr float DeathFlashDuration = 0.15f;   // white flash on death, seconds
		inline constexpr float GameOverFadeDuration = 0.4f;  // fade into the game-over panel, seconds
		inline constexpr float PanelSlideDuration = 0.4f;    // score-panel slide-in, seconds
	}

	namespace PhotoMode
	{
		inline constexpr float ActivationHoldSeconds = 0.40f; // hold the photo-mode key this long to enter it
		inline constexpr float MinimumZoom = 0.75f;
		inline constexpr float MaximumZoom = 2.25f;
		inline constexpr float KeyboardZoomSpeed = 1.00f;     // zoom units/second from the keyboard
		inline constexpr float MouseWheelZoomStep = 0.15f;    // zoom units per wheel notch
		inline constexpr float KeyboardPanSpeed = 200.0f;     // px/second from the keyboard
		inline constexpr float PanLimitViewportFraction = 0.50f; // how far the camera may pan past the viewport edge
	}

	namespace Motion
	{
		// horizontal world motion runs on a fixed 240 Hz step, decoupled from the render frame rate, so the
		// world scrolls the same distance per second at any fps (no faster world on faster machines)
		inline constexpr double FixedScrollStep = 1.0 / 240.0;
	}

	namespace Effects
	{
		// additive alpha for the glow around each +1 score popup
		inline constexpr int ScoreBloomStrength = 110;
		// alpha for the blue bloom when a shield orb is collected (lower = subtler)
		inline constexpr int ShieldBloomStrength = 90;
	}

	namespace Achievements
	{
		inline constexpr std::size_t Count = static_cast<std::size_t>(Achievement::COUNT); // total achievements
		// Names + Descriptions are parallel to the Achievement enum; index with EnumIndex(Achievement::X)
		inline constexpr const char* Names[Count] = {
			"First Flight", "Centurion", "Marathon", "Persistent", "Flap Master",
			"Night Owl", "Daylight", "Untouchable", "Trophy Cabinet", "Daily Devotee",
			"Speedrunner", "Style Switch"
		};
		inline constexpr const char* Descriptions[Count] = {
			"Pass your first pipe", "Pass 100 pipes total", "Pass 1,000 pipes total",
			"Crash 100 times", "Flap 10,000 times", "Reach 50 at night",
			"Reach 50 in daylight", "Reach 30 with ghost off", "Earn every medal tier",
			"Finish 7 daily challenges", "Reach 20 in under 30s", "Use both birds 10x each"
		};
	}

	namespace Customization
	{
		inline constexpr std::size_t PipeStyleCount = EnumIndex(PipeStyleIndex::COUNT); // total pipe styles
		inline constexpr std::size_t PipeColorCount = EnumIndex(PipeColorIndex::COUNT); // total pipe colors
		// the arrays below are parallel to PipeStyleIndex / PipeColorIndex; *Folders/*Names are on-disk asset
		// folder names, *Labels are the strings shown in the UI
		inline constexpr const char* PipeStyleFolders[PipeStyleCount] = { "classic", "style1", "style2", "style3", "style4", "style5" };
		inline constexpr const char* PipeStyleLabels[PipeStyleCount] = { "Classic", "Style 1", "Style 2", "Style 3", "Style 4", "Style 5" };
		inline constexpr const char* PipeColorNames[PipeColorCount] = { "yellow", "orange", "blue", "green", "red", "purple", "pink", "rainbow" };
		inline constexpr const char* PipeColorLabels[PipeColorCount] = { "Yellow", "Orange", "Blue", "Green", "Red", "Purple", "Pink", "Rainbow" };
		inline constexpr int PipeStyleRequirements[PipeStyleCount] = { 0, 10, 20, 30, 40, 50 }; // best normal mode score needed to unlock each style
		inline constexpr int PipeColorRequirements[PipeColorCount] = { 0, 10, 20, 0, 30, 40, 50, 100 }; // best normal mode score per color; green is 0 because classic+green is the default pipe
	}

	namespace Medals
	{
		inline constexpr std::size_t Count = 6;
		inline constexpr int Thresholds[Count] = { 10, 20, 30, 40, 50, 75 }; // best normal mode score for each medal tier, bronze -> ruby
	}
}
