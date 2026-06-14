#pragma once

#include "raylib.h"

#include <string>

struct MenuButtonTextures;
struct SaveData;
struct Theme;
struct UiTextures;

// which main-menu button fired this frame; None means nothing was pressed (DailyAlreadyDone = daily run already used today)
enum class MainMenuAction
{
	None,
	OpenSettings,
	ConfirmExit,
	Quit,
	CancelExit,
	StartNormal,
	StartDaily,
	DailyAlreadyDone,
	OpenCustomize,
	OpenVsMenu,
	StartEndless,
	OpenLeaderboard,
	OpenTrophies,
	OpenAchievements,
	OpenStats,
	OpenInfo
};

// draw the main menu for one frame and return the button pressed; editingName focuses the name field,
// exitConfirm shows the quit prompt, splashScreenShakeY is the title's juice offset, and the *Scroll
// refs are advanced in place to drive the parallax backdrop
MainMenuAction DrawMainMenuScreen(
	const SaveData& sd,
	const std::string& playerName,
	const Theme& curTheme,
	const UiTextures& uiTextures,
	const MenuButtonTextures& menuButtons,
	Vector2 vmouse,
	bool editingName,
	bool exitConfirm,
	float frameScale,
	float splashScreenShakeY,
	float nightAmount,
	float baseTop,
	float& skyScroll,
	float& midScroll,
	float& baseScroll);
