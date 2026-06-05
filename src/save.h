// versioned binary save (save.bin): everything persisted between runs — identity, best scores,
// customise selection, unlocks, settings, lifetime stats, achievements, daily-challenge tracking
#pragma once
#include "raylib.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

// the four index enums below are serialized into save.bin by their integer value, so existing
// entries must keep their numbers — only ever append new ones at the end
enum class ThemeIndex : std::int32_t
{
	CLASSIC,
	SKYLINE,
	SUNSET,
	CANYON,
	MEADOW,
	COUNT
};

enum class SkinIndex : std::int32_t
{
	YELLOW_BIRD,
	ORANGE_BIRD,
	BLUE_BIRD,
	GREEN_BIRD,
	RED_BIRD,
	PURPLE_BIRD,
	PINK_BIRD,
	RAINBOW_BIRD,
	COUNT
};

enum class PipeStyleIndex : std::int32_t
{
	CLASSIC,
	STYLE1,
	STYLE2,
	STYLE3,
	STYLE4,
	STYLE5,
	COUNT
};

enum class PipeColorIndex : std::int32_t
{
	YELLOW_PIPE,
	ORANGE_PIPE,
	BLUE_PIPE,
	GREEN_PIPE,
	RED_PIPE,
	PURPLE_PIPE,
	PINK_PIPE,
	RAINBOW_PIPE,
	COUNT
};

// display mode; values match the legacy save.bin format, hence the gap up to 5 for BORDERLESS
enum class ResIndex : std::int32_t
{
	WINDOWED = 0,
	BORDERLESS = 5
};

// pin a sample of the serialized values, so reordering the enums above can't silently corrupt every save.bin
static_assert(static_cast<std::int32_t>(ThemeIndex::CLASSIC) == 0);
static_assert(static_cast<std::int32_t>(SkinIndex::ORANGE_BIRD) == 1);
static_assert(static_cast<std::int32_t>(PipeStyleIndex::STYLE5) == 5);
static_assert(static_cast<std::int32_t>(PipeColorIndex::GREEN_PIPE) == 3);
static_assert(static_cast<std::int32_t>(ResIndex::BORDERLESS) == 5);

// scoped-enum -> its underlying integer (for serialization and array indexing)
template <typename Enum>
constexpr std::underlying_type_t<Enum> EnumValue(Enum value) noexcept
{
	static_assert(std::is_enum_v<Enum>);
	return static_cast<std::underlying_type_t<Enum>>(value);
}

// scoped-enum -> a size_t suitable for indexing
template <typename Enum>
constexpr std::size_t EnumIndex(Enum value) noexcept
{
	return static_cast<std::size_t>(EnumValue(value));
}

// is value a valid [0, count) index for its enum? guards against an out-of-range id read from an old save
template <typename Enum>
constexpr bool EnumInRange(Enum value, std::size_t count) noexcept
{
	const auto raw = EnumValue(value);
	return raw >= 0 && static_cast<std::size_t>(raw) < count;
}

struct SaveData
{
	// version of save.bin we LOADED from; WriteSave always stamps the latest format.
	// 0 means "never loaded" — after a successful LoadSave it equals the on-disk file version
	int version = 0;
	// identity
	std::string playerName;
	int bestScore = 0;
	int bestClassicScore = 0;   // separate high score for Classic (the no-ramp, no-powerups mode)
	int bestDailyScore = 0;     // separate high score for Daily Challenge runs
	// customize
	ThemeIndex themeIndex = ThemeIndex::CLASSIC;
	SkinIndex skinIndex = SkinIndex::YELLOW_BIRD;
	SkinIndex skinIndex2 = SkinIndex::ORANGE_BIRD;   // player two's bird in 2-player
	PipeStyleIndex pipeStyleIndex = PipeStyleIndex::CLASSIC;
	PipeColorIndex pipeColorIndex = PipeColorIndex::GREEN_PIPE;
	unsigned long long unlockedSkins = 3;   // bitmask, one bit per SkinIndex (defaults: yellow + orange)
	unsigned long long unlockedThemes = 1;  // bitmask, one bit per ThemeIndex (defaults: classic only)
	// audio (0..100)
	int musicVolume = 30;
	int sfxVolume = 100;
	bool muted = false;
	// display
	ResIndex resIndex = ResIndex::BORDERLESS;   // new players start in borderless fullscreen
	int winW = 800;         // last unmaximized windowed client width
	int winH = 600;         // last unmaximized windowed client height
	bool winMaximized = false; // OS Maximize-button state (only meaningful in WINDOWED mode)
	bool vsync = true;
	bool showFps = false;     // when on, the FPS counter draws top-left during a live run (PLAYING / PAUSED / VS_PLAYING)
	int  fpsCap  = 0;         // custom frame cap when vsync is off; 0 = unlimited; ignored while vsync is on
	// controls (raylib key codes)
	int keyFlapP1 = KEY_SPACE;
	int keyFlapP2 = KEY_UP;
	int keyPause = KEY_P;
	int keyPhotoMode = KEY_T;
	int keyRestart = KEY_R;
	// gameplay toggles
	bool showGhost = true;
	int ghostOpacity = 15;
	bool reduceMotion = false;
	bool largerHud = false;
	// lifetime stats
	unsigned long long totalFlaps = 0;
	unsigned long long totalDeaths = 0;
	unsigned long long totalPipes = 0;
	unsigned long long totalGames = 0;
	double playtimeSeconds = 0;
	unsigned long long pipesPerSkin[2] = { 0, 0 };   // [0] solo / player one, [1] player two
	unsigned long long deathsPerSkin[2] = { 0, 0 };
	// achievements
	unsigned long long achMask = 0;   // bitmask, one bit per achievement
	// daily challenge
	int lastDailyDate = 0;   // YYYYMMDD of the last daily played (gates one run per day)
	int lastDailyScore = 0;
	int dailyCount = 0;
};

// load save.bin; false if the file is missing OR the checksum is bad (caller treats both as "no save" and keeps defaults)
bool LoadSave(SaveData& sd, const std::string& path);

// write save.bin; atomically rewrites the file with a fresh checksum, stamped to the latest format version
void WriteSave(const SaveData& sd, const std::string& path);
