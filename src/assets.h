#pragma once
#include "constants.h"
#include "raylib.h"
#include "save.h"
#include "types.h"
#include <vector>

// pre-rendered main-menu buttons; the bird/preview art is composited once here, not per frame
struct MenuButtonTextures
{
	Texture2D rainbowBird;
	RenderTexture2D customize;
	RenderTexture2D twoPlayer;
	RenderTexture2D classic;
	RenderTexture2D daily;
};

// shared UI sprites, loaded once and reused on every screen
struct UiTextures
{
	Texture2D splashScreen;
	Texture2D getReady;
	Texture2D preview;
	Texture2D gameOver;
	Texture2D settingsIcon;
	Texture2D playButton;
	Texture2D exitIcon;
	Texture2D iconLeaderboard;
	Texture2D iconTrophy;
	Texture2D iconAchievements;
	Texture2D iconStats;
	Texture2D iconInfo;
	Texture2D orbShield;
	Texture2D orbSlowMo;
	Texture2D orbPortal;
	Texture2D moon;
	Texture2D windFrames[3];
	Texture2D scorePanel;
	Texture2D newBadge;
};

// extract the near parallax band (bottom skyline strip) from a full background image
Texture2D MakeMidBand(const Image& full);
// bake a scrollable background / foreground from an art-pack sheet at path
Image BakePackBackground(const char* path);
Image BakePackForeground(const char* path);
// build theme 0 (the original Flappy look) from the loose classic images
Theme MakeClassicTheme();
// build a pack theme; hasMid toggles the near layer, the *NearPath args supply it (pass nullptr when hasMid is false)
Theme BakePackTheme(const char* name, const char* dayPath, const char* nightPath,
	const char* basePath, bool hasMid = true,
	const char* dayNearPath = nullptr, const char* nightNearPath = nullptr);
// bake the active theme's full bg/bgNight/mid/midNight on demand; idempotent, so it's safe to call every frame.
// only the active theme stays resident — the Customize picker reads the always-loaded thumbnails instead
void LoadThemeVisuals(Theme& theme);
// free what LoadThemeVisuals baked; the thumbnails survive
void UnloadThemeVisuals(Theme& theme);

std::vector<BirdSkin> LoadBirdSkins();
std::vector<Theme> LoadThemes();
UiTextures LoadUiTextures();
MenuButtonTextures LoadMenuButtonTextures(const std::vector<BirdSkin>& skins);
// re-composite just the 2-player preview button after either player changes skin
void RebuildTwoPlayerButton(MenuButtonTextures& buttons, const std::vector<BirdSkin>& skins,
	SkinIndex playerOneSkin, SkinIndex playerTwoSkin);

// pipe art is a [style][color] grid; the 180 array holds the flipped top-pipe of each cell.
// EnsurePipeLoaded bakes one cell on demand (no-op if cached), UnloadPipesExcept frees all but the kept cell,
// so gameplay never holds all 84 textures resident at once
void EnsurePipeLoaded(Texture2D (&pipeTex)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], Texture2D (&pipeTex180)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], int style, int color);
void UnloadPipesExcept(Texture2D (&pipeTex)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], Texture2D (&pipeTex180)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], int keepStyle, int keepColor);
// digit atlases: scoreBig is the in-run number, scoreSmall is for panels/leaderboards
void LoadScoreDigits(Texture2D (&scoreBig)[10], Texture2D (&scoreSmall)[10]);
void LoadMedalTextures(Texture2D (&medalTextures)[Constants::Medals::Count]);

// teardown counterparts for the loaders above
void UnloadBirdSkins(std::vector<BirdSkin>& skins);
void UnloadThemes(std::vector<Theme>& themes);
void UnloadUiTextures(UiTextures& textures);
void UnloadMenuButtonTextures(MenuButtonTextures& buttons);
void UnloadPipeLibrary(Texture2D (&pipeTex)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], Texture2D (&pipeTex180)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount]);
void UnloadScoreDigits(Texture2D (&scoreBig)[10], Texture2D (&scoreSmall)[10]);
void UnloadMedalTextures(Texture2D (&medalTextures)[Constants::Medals::Count]);
