#pragma once

#include "raylib.h"
#include "types.h"

#include <vector>

struct BirdSkin;
struct MenuButtonTextures;
struct SaveData;
struct Theme;
struct UiTextures;

// what the tutorial wants next: launch a "Try it!" sandbox (which effect, and which page to return to after),
// or finish out to the main menu
struct TutorialScreenResult
{
	bool startSandbox = false;
	SandboxEffect sandboxEffect = SandboxEffect::NONE;
	TutorialPage sandboxReturnPage = TutorialPage::WELCOME;
	bool finishToMenu = false;
};

// draw the current tutorial page and handle its nav; tutorialPage is read + written so the arrows can flip pages
TutorialScreenResult DrawTutorialScreen(
	TutorialPage& tutorialPage,
	const SaveData& sd,
	const std::vector<BirdSkin>& skins,
	const Theme& curTheme,
	const UiTextures& uiTextures,
	const MenuButtonTextures& menuButtons,
	Vector2 vmouse);
