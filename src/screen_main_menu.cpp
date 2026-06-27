#include "screen_main_menu.h"
#include "assets.h"
#include "gameplay_helpers.h"
#include "render_game.h"
#include "save.h"
#include "system_display.h"
#include "types.h"
#include "ui.h"

#include <algorithm>
#include <string>

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
	float& baseScroll)
{
	// the parallax backdrop is now drawn by game.cpp before this runs (shared across every menu), so these
	// backdrop params are unused here — kept in the signature for call-site compatibility
	(void)frameScale; (void)skyScroll; (void)midScroll; (void)baseScroll;
	(void)nightAmount; (void)baseTop;

	// while the exit dialog is up, park the cursor far off-screen so none of the menu buttons behind it can be
	// hovered/clicked; it's restored to the real position for the dialog's own buttons below
	Vector2 menuMouseSaved = vmouse;
	if (exitConfirm) vmouse = Vector2{ -9999.0f, -9999.0f };

	// accumulate the clicked action instead of returning the instant a button is hit. returning early skipped every
	// button drawn AFTER the click, so a click that stays on the menu (e.g. the daily tile when today's run is done)
	// blanked the customize/2-player/classic buttons + the bottom icon row for one frame — a visible flicker
	MainMenuAction action = MainMenuAction::None;
	auto setAction = [&](MainMenuAction a) { if (action == MainMenuAction::None) action = a; };

	// top-right gear: hand-drawn (shadow + press-down offset) rather than UiIconButton, to sit flush in the corner
	float gearS = 1.0f;
	Rectangle gearRect = { VIRTUAL_W - uiTextures.settingsIcon.width * gearS - 16, 16, uiTextures.settingsIcon.width * gearS, uiTextures.settingsIcon.height * gearS };
	bool gearHover = CheckCollisionPointRec(vmouse, gearRect);
	DrawTextureEx(uiTextures.settingsIcon, Vector2{ gearRect.x + 4, gearRect.y + 5 }, 0.0f, gearS, Fade(BLACK, 0.45f));
	bool gearDown = gearHover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
	Vector2 gp = gearDown ? Vector2{ gearRect.x + 2, gearRect.y + 3 } : Vector2{ gearRect.x, gearRect.y };
	DrawTextureEx(uiTextures.settingsIcon, gp, 0.0f, gearS, gearHover ? WHITE : Fade(WHITE, 0.9f));
	if (gearHover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) setAction(MainMenuAction::OpenSettings);

	// top-left exit, same hand-drawn treatment as the gear
	float exitS = 1.0f;
	Rectangle exitRect = { 16, 16, uiTextures.exitIcon.width * exitS, uiTextures.exitIcon.height * exitS };
	bool exitHover = CheckCollisionPointRec(vmouse, exitRect);
	DrawTextureEx(uiTextures.exitIcon, Vector2{ exitRect.x + 4, exitRect.y + 5 }, 0.0f, exitS, Fade(BLACK, 0.45f));
	bool exitDown = exitHover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
	Vector2 ep = exitDown ? Vector2{ exitRect.x + 2, exitRect.y + 3 } : Vector2{ exitRect.x, exitRect.y };
	DrawTextureEx(uiTextures.exitIcon, ep, 0.0f, exitS, exitHover ? WHITE : Fade(WHITE, 0.9f));
	if (exitHover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) setAction(MainMenuAction::ConfirmExit);

	float splashScale = 0.78f;
	DrawTextureEx(uiTextures.splashScreen,   // splashScreenShakeY is the title's idle-bob/juice offset
		Vector2{ (float)(int)(VIRTUAL_W / 2 - uiTextures.splashScreen.width * splashScale / 2),
				 (float)(int)(36 + splashScreenShakeY) },
		0.0f, splashScale, WHITE);

	if (editingName)
	{
		DrawText("Enter your name:", VIRTUAL_W / 2 - MeasureText("Enter your name:", 22) / 2, 250, 22, DARKBLUE);
		// blinking caret: toggle a "_" / " " suffix twice a second
		std::string nameDisplay = playerName + (((int)(GetTime() * 2) % 2) ? "_" : " ");
		DrawText(nameDisplay.c_str(), VIRTUAL_W / 2 - MeasureText(nameDisplay.c_str(), 32) / 2, 276, 32, DARKBLUE);
		DrawText("press ENTER to confirm", VIRTUAL_W / 2 - MeasureText("press ENTER to confirm", 16) / 2, 316, 16, BLUE);
	}
	else
	{
		const float trioCY = 300.0f;
		const float playH = 135.0f;
		const float sideH = 116.0f;
		const float smallH = 100.0f;

		// pick an integer scale for the play button so the pixel art stays crisp (no fractional sampling)
		int pbSi = std::max(1, (int)((playH + uiTextures.playButton.height / 2.0f) / uiTextures.playButton.height));
		float pbS = (float)pbSi;
		float playW = uiTextures.playButton.width * pbS;
		float pbHa = uiTextures.playButton.height * pbS;
		Rectangle playRect = { VIRTUAL_W / 2 - playW / 2, trioCY - pbHa / 2, playW, pbHa };
		bool playHover = CheckCollisionPointRec(vmouse, playRect);
		bool playDown = playHover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
		DrawTextureEx(uiTextures.playButton, Vector2{ playRect.x + 3, playRect.y + 4 }, 0.0f, pbS, Fade(BLACK, 0.35f));
		Vector2 pp = playDown ? Vector2{ playRect.x + 1, playRect.y + 2 } : Vector2{ playRect.x, playRect.y };
		DrawTextureEx(uiTextures.playButton, pp, 0.0f, pbS, playHover ? WHITE : Fade(WHITE, 0.95f));
		if (playHover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) setAction(MainMenuAction::StartNormal);

		// lay the row out from Play outward: customize/2-player flank Play, daily/classic sit further out
		const float sideGap = 85.0f;                          // distance from Play's edge to the side button's center
		const float outerGap = 20.0f;                          // gap between side button edge and outer button edge
		float halfPlay = playW * 0.5f;
		float customizeCX = VIRTUAL_W * 0.5f - halfPlay - sideGap;
		float twoPlayerCX = VIRTUAL_W * 0.5f + halfPlay + sideGap;
		float dailyCX = customizeCX - (sideH + smallH) * 0.5f - outerGap;
		float endlessCX = twoPlayerCX + (sideH + smallH) * 0.5f + outerGap;

		// daily tile reports DailyAlreadyDone instead of StartDaily when today's run was already played
		if (UiTextureButton(menuButtons.daily.texture, vmouse, dailyCX, trioCY, smallH))
			setAction(sd.lastDailyDate == TodayYMD() ? MainMenuAction::DailyAlreadyDone : MainMenuAction::StartDaily);
		if (UiTextureButton(menuButtons.customize.texture, vmouse, customizeCX, trioCY, sideH)) setAction(MainMenuAction::OpenCustomize);
		if (UiTextureButton(menuButtons.twoPlayer.texture, vmouse, twoPlayerCX, trioCY, sideH)) setAction(MainMenuAction::OpenVsMenu);
		if (UiTextureButton(menuButtons.classic.texture, vmouse, endlessCX, trioCY, smallH)) setAction(MainMenuAction::StartEndless);

		// in fill-width mode the bottom is cropped, so lift the icon row by that amount to clear the window edge (0 otherwise)
		float iconY = VIRTUAL_H - 84.0f - FillModeBottomCrop();
		float iconStep = 133.0f, iconX0 = VIRTUAL_W / 2 - iconStep * 2.0f;   // five icons centered, 2 steps left of center
		if (UiIconButton(uiTextures.iconLeaderboard, vmouse, iconX0 + iconStep * 0, iconY)) setAction(MainMenuAction::OpenLeaderboard);
		if (UiIconButton(uiTextures.iconTrophy, vmouse, iconX0 + iconStep * 1, iconY)) setAction(MainMenuAction::OpenTrophies);
		if (UiIconButton(uiTextures.iconAchievements, vmouse, iconX0 + iconStep * 2, iconY)) setAction(MainMenuAction::OpenAchievements);
		if (UiIconButton(uiTextures.iconStats, vmouse, iconX0 + iconStep * 3, iconY)) setAction(MainMenuAction::OpenStats);
		if (UiIconButton(uiTextures.iconInfo, vmouse, iconX0 + iconStep * 4, iconY)) setAction(MainMenuAction::OpenInfo);
	}

	// exit confirm dialog draws OUTSIDE the editingName/else split so clicking the X icon while editing the name still
	// pops the dialog (it used to be nested in the else and was invisible during name entry, leaving the player stuck)
	if (exitConfirm)
	{
		vmouse = menuMouseSaved;   // give the dialog buttons the real cursor back
		DrawRectangle(0, 0, VIRTUAL_W, VIRTUAL_H, Fade(BLACK, 0.55f));
		// dw widened from 380 to 500 so "Are you sure you want to exit?" fits with comfortable side padding
		float dw = 500, dh = 180, dx = VIRTUAL_W / 2 - dw / 2, dy = VIRTUAL_H / 2 - dh / 2;
		Rectangle dlg = { dx, dy, dw, dh };
		DrawRectangleRec(dlg, Color{ 25, 35, 60, 255 });
		DrawRectangleLinesEx(dlg, 3, RAYWHITE);
		const char* q = "Are you sure you want to exit?";
		DrawText(q, VIRTUAL_W / 2 - MeasureText(q, 22) / 2, (int)dy + 38, 22, RAYWHITE);
		if (UiButton(Rectangle{ dx + 50, dy + 100, 160, 44 }, "Yes, exit", vmouse)) setAction(MainMenuAction::Quit);
		if (UiButton(Rectangle{ dx + dw - 210, dy + 100, 160, 44 }, "Cancel", vmouse)) setAction(MainMenuAction::CancelExit);
	}

	return action;
}
