#include "screen_tutorial.h"

#include "assets.h"
#include "gameplay_helpers.h"
#include "save.h"
#include "system_display.h"
#include "types.h"
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// draw one tutorial page (its visual + body text + nav row); requests (start a sandbox / finish) come back in the result
TutorialScreenResult DrawTutorialScreen(
	TutorialPage& tutorialPage,
	const SaveData& sd,
	const std::vector<BirdSkin>& skins,
	const Theme& curTheme,
	const UiTextures& uiTextures,
	const MenuButtonTextures& menuButtons,
	Vector2 vmouse)
{
	TutorialScreenResult result;

	ClearBackground(Color{ 120, 200, 230, 255 });
	constexpr int TUTORIAL_PAGES = static_cast<int>(TutorialPage::COUNT);
	if (!EnumInRange(tutorialPage, TUTORIAL_PAGES)) tutorialPage = TutorialPage::WELCOME;
	// per-page title + body, indexed by TutorialPage
	const char* titles[TUTORIAL_PAGES] = {
		"Welcome!", "Game Modes", "Shield Orb", "Time Warp Orb",
		"Wind Pipes", "Oscillating Pipes",
		"Photo Mode",
		"Ready!"
	};
	const char* lines[TUTORIAL_PAGES] = {
		"Tap SPACE, W, Up, or click to flap. The bird falls under gravity, keep tapping to stay airborne.",
		"Play: standard ramping difficulty with powerups and pipe variations. Daily: shared seed, everyone plays the same layout today. Classic: relaxed, fixed gap and speed, no powerups. 2-Player: local versus on shared pipes.",
		"A blue orb in the gap (past score 15). Touching it gives you a shield. Your next crash bounces you off instead of dying.",
		"An hourglass that can manipulate time (past score 15). It randomly either slows time or speeds it up.",
		"After score 20, some pipes carry a gust (>>> or <<<) that pushes you sideways while the pipe is on screen.",
		"After score 20, some pipe pairs drift up and down. The gap stays the same size, just time your flap.",
		"Tap your Photo Mode key to save and copy a screenshot. Hold it while playing to enter Photo Mode, then drag or use arrows to pan, scroll or use +/- to zoom, tap the key to capture, and ESC to resume.",
		"That covers it. You can replay this from Settings any time. Tap below to start playing."
	};
	const std::size_t pageIndex = EnumIndex(tutorialPage);
	DrawText(titles[pageIndex], VIRTUAL_W / 2 - MeasureText(titles[pageIndex], 46) / 2, 40, 46, DARKBLUE);

	float py = 130;
	float pcx = VIRTUAL_W / 2.0f;
	// page-specific visual: the live bird, the mode-icon row, or an animated orb/pipe demo
	switch (tutorialPage)
	{
	case TutorialPage::WELCOME:
	{
		Texture2D bt = skins[EnumIndex(sd.skinIndex)].frames[static_cast<int>(BirdFrame::MID_FLAP)];
		DrawTextureEx(bt, Vector2{ pcx - bt.width * 4.0f / 2, py }, 0, 4.0f, WHITE);
		break;
	}
	case TutorialPage::GAME_MODES:
	{
		// a row of the four mode buttons (Play / Daily / Classic / 2-Player), laid out from x0 rightward
		const float iconSize = 72.0f;
		const float playSlotW = 110.0f;
		const float rowGap = 30.0f;
		const float rowW = playSlotW + 3.0f * iconSize + 3.0f * rowGap;
		float x0 = pcx - rowW / 2.0f;
		float rowY = py + 20;
		{
			int ps = std::max(1, (int)(iconSize / uiTextures.playButton.height));
			float w = uiTextures.playButton.width * (float)ps, h = uiTextures.playButton.height * (float)ps;
			DrawTextureEx(uiTextures.playButton, Vector2{ x0 + playSlotW / 2 - w / 2, rowY + iconSize / 2 - h / 2 }, 0, (float)ps, WHITE);
			DrawText("Play", (int)(x0 + playSlotW / 2 - MeasureText("Play", 18) / 2), (int)(rowY + iconSize + 6), 18, DARKBLUE);
		}
		{
			// Daily tile is drawn procedurally as a little calendar showing today's day-of-month
			float dx = x0 + playSlotW + rowGap;
			Rectangle r = { dx, rowY, iconSize, iconSize };
			DrawRectangleRec(r, Color{ 240, 235, 220, 255 });
			DrawRectangleRec(Rectangle{ r.x, r.y, r.width, 16 }, Color{ 180, 50, 60, 255 });
			DrawRectangleRec(Rectangle{ r.x, r.y + r.height - 14, r.width, 14 }, Color{ 15, 50, 55, 255 });
			int dayNum = TodayYMD() % 100;
			std::string ds = (dayNum < 10 ? "0" : "") + std::to_string(dayNum);
			int fs = 26;
			DrawText(ds.c_str(), (int)(r.x + r.width / 2 - MeasureText(ds.c_str(), fs) / 2), (int)(r.y + 22), fs, Color{ 50, 50, 50, 255 });
			DrawRectangleLinesEx(r, 2, RAYWHITE);
			DrawText("Daily", (int)(dx + iconSize / 2 - MeasureText("Daily", 18) / 2), (int)(rowY + iconSize + 6), 18, DARKBLUE);
		}
		{
			float dx = x0 + playSlotW + rowGap + iconSize + rowGap;
			Rectangle src = { 0, 0, (float)menuButtons.classic.texture.width, -(float)menuButtons.classic.texture.height };   // RenderTexture is Y-flipped
			Rectangle dst = { dx, rowY, iconSize, iconSize };
			DrawTexturePro(menuButtons.classic.texture, src, dst, Vector2{ 0, 0 }, 0.0f, WHITE);
			DrawText("Classic", (int)(dx + iconSize / 2 - MeasureText("Classic", 18) / 2), (int)(rowY + iconSize + 6), 18, DARKBLUE);
		}
		{
			float dx = x0 + playSlotW + rowGap + 2.0f * (iconSize + rowGap);
			Rectangle src = { 0, 0, (float)menuButtons.twoPlayer.texture.width, -(float)menuButtons.twoPlayer.texture.height };
			Rectangle dst = { dx, rowY, iconSize, iconSize };
			DrawTexturePro(menuButtons.twoPlayer.texture, src, dst, Vector2{ 0, 0 }, 0.0f, WHITE);
			DrawText("2-Player", (int)(dx + iconSize / 2 - MeasureText("2-Player", 18) / 2), (int)(rowY + iconSize + 6), 18, DARKBLUE);
		}
		break;
	}
	case TutorialPage::SHIELD_ORB:
		DrawTextureEx(uiTextures.orbShield, Vector2{ pcx - uiTextures.orbShield.width * 3.4f / 2, py }, 0, 3.4f, WHITE);
		break;
	case TutorialPage::TIME_WARP_ORB:
		DrawTextureEx(uiTextures.orbSlowMo, Vector2{ pcx - uiTextures.orbSlowMo.width * 3.4f / 2, py }, 0, 3.4f, WHITE);
		break;
	case TutorialPage::WIND_PIPES:
	{
		int wi = ((int)(GetTime() * 8.0f)) % 3;   // animate the chevron frames
		Texture2D wt = uiTextures.windFrames[wi];
		float ws = 3.0f;
		DrawTextureEx(wt, Vector2{ pcx - wt.width * ws / 2.0f, py + 60 }, 0, ws, WHITE);
		break;
	}
	case TutorialPage::OSCILLATING_PIPES:
	{
		// a live pair of pipes bobbing up and down; the scale/offsets shift to fit borderless vs maximized layouts
		float osc = sinf((float)GetTime() * 2.5f) * 14.0f;
		const bool fillCrop = FillModeBottomCrop() > 0.0f;
		const bool borderless = sd.resIndex == ResIndex::BORDERLESS;
		float ps = fillCrop ? 0.44f : (borderless ? 0.52f : 0.38f);
		float srcH = 120.0f;
		float pw = curTheme.pipe.width * ps;
		float ph = srcH * ps;
		float gapY = py + (fillCrop ? 62.0f : (borderless ? 86.0f : 70.0f)) + osc;
		float gapH = 70.0f;
		Rectangle srcTop = { 0, (float)(curTheme.pipe.height - (int)srcH), (float)curTheme.pipe.width, srcH };   // crop the cap end of the texture
		Rectangle dstTop = { pcx - pw / 2.0f, gapY - gapH / 2 - ph, pw, ph };
		DrawTexturePro(curTheme.pipe, srcTop, dstTop, Vector2{ 0, 0 }, 0.0f, WHITE);
		Rectangle srcBot = { 0, 0, (float)curTheme.pipe180.width, srcH };
		Rectangle dstBot = { pcx - pw / 2.0f, gapY + gapH / 2, pw, ph };
		DrawTexturePro(curTheme.pipe180, srcBot, dstBot, Vector2{ 0, 0 }, 0.0f, WHITE);
		break;
	}
	case TutorialPage::PHOTO_MODE:
	{
		// draw a faux keycap showing the configured photo-mode key
		Rectangle key = { pcx - 74.0f, py + 28.0f, 148.0f, 76.0f };
		DrawRectangleRec(key, Color{ 54, 75, 135, 255 });
		DrawRectangleLinesEx(key, 3.0f, RAYWHITE);
		const char* photoKey = KeyName(sd.keyPhotoMode);
		DrawText(photoKey, (int)(pcx - MeasureText(photoKey, 38) / 2), (int)(key.y + 18), 38, RAYWHITE);
		DrawText("Tap: Capture   Hold: Photo Mode", (int)(pcx - MeasureText("Tap: Capture   Hold: Photo Mode", 16) / 2), (int)(key.y + 92), 16, DARKBLUE);
		break;
	}
	case TutorialPage::READY:
	{
		Texture2D bt = skins[EnumIndex(sd.skinIndex)].frames[static_cast<int>(BirdFrame::MID_FLAP)];
		DrawTextureEx(bt, Vector2{ pcx - bt.width * 4.0f / 2, py }, 0, 4.0f, WHITE);
		break;
	}
	}

	const bool hasTryButton = tutorialPage == TutorialPage::SHIELD_ORB
		|| tutorialPage == TutorialPage::TIME_WARP_ORB
		|| tutorialPage == TutorialPage::WIND_PIPES
		|| tutorialPage == TutorialPage::OSCILLATING_PIPES;
	// in fill-width (maximized) mode the Welcome page and the "Try it!" pages lift their body text with the bottom row
	const int bodyLift = (int)((tutorialPage == TutorialPage::WELCOME || hasTryButton) ? FillModeBottomCrop() : 0.0f);
	if (tutorialPage == TutorialPage::GAME_MODES)
	{
		// the modes page lists each mode on its own line — clearer than one run-on paragraph
		const char* modes[4] = {
			"Play:  ramping difficulty, powerups & pipe twists",
			"Daily:  one shared layout for everyone, each day",
			"Classic:  relaxed: fixed gap & speed, no powerups",
			"2-Player:  local versus on the same pipes",
		};
		const int fs = 20;
		int maxw = 0;
		for (const char* mline : modes) maxw = std::max(maxw, MeasureText(mline, fs));
		const int lx = VIRTUAL_W / 2 - maxw / 2;   // left-align the block, centered as a whole
		int ly = 312;   // sit it in the gap between the mode icons above and the page dots below
		for (const char* mline : modes) { DrawText(mline, lx, ly, fs, DARKBLUE); ly += 34; }
	}
	else
	{
		const int bodyFs = 18;
		// the wrapped body lines + their centered x depend only on the (static) page text, so wrap once per page and
		// cache it instead of re-wrapping every frame
		static std::vector<std::pair<std::string, int>> rowCache[TUTORIAL_PAGES];
		static bool rowCached[TUTORIAL_PAGES] = {};
		if (!rowCached[pageIndex])
		{
			std::string buf(lines[pageIndex]);
			std::string cur;
			size_t i = 0;
			auto commit = [&](const std::string& s) {
				rowCache[pageIndex].push_back({ s, VIRTUAL_W / 2 - MeasureText(s.c_str(), bodyFs) / 2 });
			};
			// greedy word wrap: keep appending words until the line would exceed 680px, then break to a new line
			while (i < buf.size())
			{
				size_t sp = buf.find(' ', i);
				std::string w = (sp == std::string::npos) ? buf.substr(i) : buf.substr(i, sp - i);
				std::string trial = cur.empty() ? w : (cur + " " + w);
				if (MeasureText(trial.c_str(), bodyFs) > 680 && !cur.empty()) { commit(cur); cur = w; }
				else cur = trial;
				if (sp == std::string::npos) break;
				i = sp + 1;
			}
			if (!cur.empty()) commit(cur);
			rowCached[pageIndex] = true;
		}
		int by = 320 - bodyLift;
		for (auto& r : rowCache[pageIndex])
		{
			DrawText(r.first.c_str(), r.second, by, bodyFs, DARKBLUE);
			by += 24;
		}
	}

	// display recommendation, shown on the Welcome page where setup advice belongs; lifts with the rest of the bottom cluster
	if (tutorialPage == TutorialPage::WELCOME)
	{
		const int tipLift = (int)FillModeBottomCrop();
		const char* t1 = "Tip: Borderless fullscreen (press F11) is recommended.";
		const char* t2 = "Maximizing a normal window crops the bottom slightly.";
		DrawText(t1, VIRTUAL_W / 2 - MeasureText(t1, 16) / 2, 430 - tipLift, 16, MAROON);
		DrawText(t2, VIRTUAL_W / 2 - MeasureText(t2, 16) / 2, 452 - tipLift, 16, MAROON);
	}


	// page dots: lift them out of the bottom crop on every page so they stay clear of the lifted nav buttons
	const float dotY = 484.0f - FillModeBottomCrop();
	for (int dpi = 0; dpi < TUTORIAL_PAGES; dpi++)
	{
		float dx = VIRTUAL_W / 2.0f - (TUTORIAL_PAGES * 16) / 2.0f + dpi * 16 + 5;
		DrawCircle((int)dx, (int)dotY, 4, dpi == static_cast<int>(pageIndex) ? DARKBLUE : Fade(DARKBLUE, 0.3f));
	}

	// the "Try it!" pages launch a sandbox demo of their effect, remembering this page to return to
	const bool tryButtonPressed = hasTryButton && UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 420.0f - FillModeBottomCrop(), 180, 38 }, "Try it!", vmouse);
	const bool tryKeyPressed = hasTryButton && IsKeyPressed(KEY_ENTER);
	if (tryButtonPressed || tryKeyPressed)
	{
		result.startSandbox = true;
		switch (tutorialPage)
		{
		case TutorialPage::SHIELD_ORB: result.sandboxEffect = SandboxEffect::SHIELD; break;
		case TutorialPage::TIME_WARP_ORB: result.sandboxEffect = SandboxEffect::TIME_WARP; break;
		case TutorialPage::WIND_PIPES: result.sandboxEffect = SandboxEffect::WIND; break;
		case TutorialPage::OSCILLATING_PIPES: result.sandboxEffect = SandboxEffect::OSCILLATING_PIPES; break;
		default: break;
		}
		result.sandboxReturnPage = tutorialPage;
	}

	const bool first = tutorialPage == TutorialPage::WELCOME;
	const bool last = tutorialPage == TutorialPage::READY;
	// lift the nav row out of the fill-width bottom crop (maximized windowed)
	const float navY = 516.0f - FillModeBottomCrop();
	if (!first && UiButton(Rectangle{ VIRTUAL_W / 2 - 200, navY, 130, 40 }, "Back", vmouse))
		tutorialPage = static_cast<TutorialPage>(EnumValue(tutorialPage) - 1);
	const char* nxt = last ? "Start!" : "Next";
	if (UiButton(Rectangle{ VIRTUAL_W / 2 + 70, navY, 130, 40 }, nxt, vmouse))
	{
		if (last) result.finishToMenu = true;
		else tutorialPage = static_cast<TutorialPage>(EnumValue(tutorialPage) + 1);
	}
	if (first && UiButton(Rectangle{ VIRTUAL_W / 2 - 65, navY, 130, 40 }, "Skip", vmouse)) result.finishToMenu = true;

	// Left / Right arrow keys also flip between pages
	if (IsKeyPressed(KEY_LEFT) && tutorialPage != TutorialPage::WELCOME)
		tutorialPage = static_cast<TutorialPage>(EnumValue(tutorialPage) - 1);
	else if (IsKeyPressed(KEY_RIGHT) && tutorialPage != TutorialPage::READY)
		tutorialPage = static_cast<TutorialPage>(EnumValue(tutorialPage) + 1);
	// hint sits in the gap between the nav buttons and the bottom edge, tracking the crop so it stays there in both modes
	const char* navHint = "Use the Left / Right arrow keys to navigate";
	DrawText(navHint, VIRTUAL_W / 2 - MeasureText(navHint, 14) / 2, (int)(VIRTUAL_H - 26.0f - FillModeBottomCrop()), 14, DARKGRAY);

	return result;
}
