#include "screen_customize.h"

#include "constants.h"
#include "progression.h"
#include "render_menus.h"
#include "save.h"
#include "system_display.h"
#include "types.h"
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <string>

void ApplyPipeChoice(std::vector<Theme>& themes, Texture2D (&pipeTex)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], Texture2D (&pipeTex180)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], PipeStyleIndex pipeStyle, PipeColorIndex pipeColor)
{
	// clamp out-of-range indices to the defaults, then point every theme's pipe pair at the chosen [style][color] cell
	const std::size_t style = EnumInRange(pipeStyle, EnumIndex(PipeStyleIndex::COUNT)) ? EnumIndex(pipeStyle) : EnumIndex(PipeStyleIndex::CLASSIC);
	const std::size_t color = EnumInRange(pipeColor, EnumIndex(PipeColorIndex::COUNT)) ? EnumIndex(pipeColor) : EnumIndex(PipeColorIndex::GREEN_PIPE);
	for (Theme& theme : themes)
	{
		theme.pipe = pipeTex[style][color];
		theme.pipe180 = pipeTex180[style][color];
	}
}

void RecomputeUnlocks(SaveData& save, int bestScore, int skinCount, int themeCount)
{
	// skins unlock at score thresholds; rainbow (index 7) is the prestige unlock — the hardest bird to earn
	static constexpr int skinRequirements[8] = { 0, 0, 20, 30, 40, 50, 60, 100 };
	save.unlockedSkins = 0ull;
	for (int i = 0; i < skinCount && i < 8; i++)
	{
		if (bestScore >= skinRequirements[i]) save.unlockedSkins |= 1ull << i;
	}

	// themes unlock at the medal thresholds: Classic free, then Bronze 10 / Silver 20 / Gold 30 / Platinum 40 (bit 0 always set)
	save.unlockedThemes = 1ull;
	if (bestScore >= Constants::Medals::Thresholds[0]) save.unlockedThemes |= 2ull;   // Skyline @ Bronze
	if (bestScore >= Constants::Medals::Thresholds[1]) save.unlockedThemes |= 4ull;   // Sunset  @ Silver
	if (bestScore >= Constants::Medals::Thresholds[2]) save.unlockedThemes |= 8ull;   // Canyon  @ Gold
	if (bestScore >= Constants::Medals::Thresholds[3]) save.unlockedThemes |= 16ull;  // Meadow  @ Platinum

	// if a saved selection is out of range or no longer unlocked (e.g. best score dropped), fall back to a safe default
	if (!EnumInRange(save.skinIndex, skinCount) || !((save.unlockedSkins >> EnumValue(save.skinIndex)) & 1ull))
		save.skinIndex = SkinIndex::YELLOW_BIRD;
	if (!EnumInRange(save.skinIndex2, skinCount) || !((save.unlockedSkins >> EnumValue(save.skinIndex2)) & 1ull))
		save.skinIndex2 = SkinIndex::YELLOW_BIRD;
	if (!EnumInRange(save.themeIndex, themeCount) || !((save.unlockedThemes >> EnumValue(save.themeIndex)) & 1ull))
		save.themeIndex = ThemeIndex::CLASSIC;
	// the two players can't share a bird — bump P2 to the other default on a collision
	if (save.skinIndex == save.skinIndex2)
		save.skinIndex2 = save.skinIndex == SkinIndex::ORANGE_BIRD ? SkinIndex::YELLOW_BIRD : SkinIndex::ORANGE_BIRD;
	if (!EnumInRange(save.pipeStyleIndex, EnumIndex(PipeStyleIndex::COUNT)) ||
		bestScore < Constants::Customization::PipeStyleRequirements[EnumIndex(save.pipeStyleIndex)])
		save.pipeStyleIndex = PipeStyleIndex::CLASSIC;
	if (!EnumInRange(save.pipeColorIndex, EnumIndex(PipeColorIndex::COUNT)) ||
		bestScore < Constants::Customization::PipeColorRequirements[EnumIndex(save.pipeColorIndex)])
		save.pipeColorIndex = PipeColorIndex::GREEN_PIPE;
	// Versus pipe cosmetic drops back off if its win requirement isn't met (e.g. a save edited down)
	if (save.versusPipes && save.versusWins < Constants::Customization::VersusPipesRequirement)
		save.versusPipes = false;
}

void UpdateCustomizeAnimations(CustomizeScreenState& state, float dt)
{
	// decay each click-bounce timer toward 0 (one per bird tile, one per theme tile)
	for (int ci = 0; ci < 8; ci++)
	{
		if (state.birdClickAnim[ci] > 0.0f)
		{
			state.birdClickAnim[ci] -= dt * 4.0f;
			if (state.birdClickAnim[ci] < 0.0f) state.birdClickAnim[ci] = 0.0f;
		}
	}

	for (int ci = 0; ci < 5; ci++)
	{
		if (state.themeClickAnim[ci] > 0.0f)
		{
			state.themeClickAnim[ci] -= dt * 4.0f;
			if (state.themeClickAnim[ci] < 0.0f) state.themeClickAnim[ci] = 0.0f;
		}
	}
}

// the Customize screen: a vertically-scrolling page that holds three horizontally-scrolling strips (bird / pipe
// style / pipe color) plus a theme grid; edits sd in place and reports what changed via the result
CustomizeScreenResult DrawCustomizeScreen(
	CustomizeScreenState& state,
	SaveData& sd,
	std::vector<BirdSkin>& skins,
	std::vector<Theme>& themes,
	Texture2D (&pipeTex)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount],
	Vector2 vmouse,
	float dt)
{
	CustomizeScreenResult result;

	// dev toggle: F1 renders the screen as a brand-new player (only the starting unlocks) so unlock-gated UI can be
	// tested without touching the real save. it's a pure display override — the save is untouched, and because the
	// click handlers all gate on the same display flags, locked tiles stay unclickable in this mode too
	static bool devNewPlayerView = false;
	if (IsKeyPressed(KEY_F1)) devNewPlayerView = !devNewPlayerView;
	const unsigned long long dispUnlockedSkins  = devNewPlayerView ? 3ull : sd.unlockedSkins;   // yellow + orange are the starting birds
	const unsigned long long dispUnlockedThemes = devNewPlayerView ? 1ull : sd.unlockedThemes;  // classic is the only starting theme
	const int dispBestScore = devNewPlayerView ? 0 : sd.bestScore;                              // pipe style/color gate on best score
	const int dispVersusWins = devNewPlayerView ? 0 : sd.versusWins;                            // Versus cosmetic gates on 2P wins

	// a hovered locked tile writes its unlock requirement here; the tooltip box is drawn last, above every strip + band
	std::string lockTip;

	DrawText("Customize", VIRTUAL_W / 2 - MeasureText("Customize", 50) / 2, 14, 46, DARKBLUE);
	const float kCustViewTop = 70.0f;
	const float kCustViewBot = 520.0f;
	const float kCustViewH = kCustViewBot - kCustViewTop;
	const float kCustContentH = 726.0f;
	float custMaxScroll = (kCustContentH > kCustViewH) ? (kCustContentH - kCustViewH) : 0.0f;
	float custWheel = GetMouseWheelMove();

	// center the 800-wide content block on the (possibly wider, 16:9) canvas; ix is 0 at the classic 800 width,
	// so reverting VIRTUAL_W to 800 restores the original layout exactly
	const float ix = (VIRTUAL_W - 800) / 2.0f;

	// if the cursor is over a horizontal strip (Bird / Pipe Style / Pipe Color), let that strip own the wheel
	// exclusively; otherwise the page scrolls vertically while the strip scrolls horizontally, which feels wrong.
	// these strip Y positions mirror the layout below
	bool wheelOwnedByStrip = false;
	{
		constexpr float stripH = 88.0f;
		constexpr float stripWidth = 660.0f;
		const float stripContentY[3] = { 122.0f, 276.0f, 432.0f };
		const float stripContentWidth[3] = {
			(float)skins.size() * 106.0f - 10.0f,
			6.0f * 94.0f - 8.0f,
			7.0f * 106.0f - 10.0f
		};
		if (vmouse.x >= 90.0f + ix && vmouse.x < 90.0f + ix + 660.0f)
		{
			for (int si = 0; si < 3; si++)
			{
				float screenY = stripContentY[si] - state.pageScroll;
				if (stripContentWidth[si] > stripWidth && vmouse.y >= screenY && vmouse.y < screenY + stripH)
				{
					wheelOwnedByStrip = true;
					break;
				}
			}
		}
	}

	if (!wheelOwnedByStrip) state.pageScroll -= custWheel * 34.0f;
	if (IsKeyDown(KEY_DOWN)) state.pageScroll += 400.0f * dt;
	if (IsKeyDown(KEY_UP))   state.pageScroll -= 400.0f * dt;
	if (state.pageScroll < 0.0f) state.pageScroll = 0.0f;
	if (state.pageScroll > custMaxScroll) state.pageScroll = custMaxScroll;
	float ofs = -state.pageScroll;   // every row adds ofs to scroll as one block

	DrawText("Bird Color", (int)(90 + ix), (int)(82 + ofs), 22, DARKBLUE);
	{
		// P1 / P2 tabs choose which player's bird the strip below edits
		float btnW = 86.0f, btnH = 28.0f, gap = 18.0f;
		float btnLX = VIRTUAL_W / 2 - (btnW * 2.0f + gap) / 2.0f;
		float btnRX = btnLX + btnW + gap;
		if (UiButton(Rectangle{ btnLX, 80 + ofs, btnW, btnH }, "P1", vmouse)) state.playerSlot = PlayerSlot::PLAYER_ONE;
		if (UiButton(Rectangle{ btnRX, 80 + ofs, btnW, btnH }, "P2", vmouse)) state.playerSlot = PlayerSlot::PLAYER_TWO;
		DrawRectangleLinesEx(Rectangle{ state.playerSlot == PlayerSlot::PLAYER_ONE ? btnLX : btnRX, 80 + ofs, btnW, btnH }, 3, GOLD);
	}

	// --- bird strip: horizontally scrolling tiles, clipped to the strip rect ---
	{
		static const int skinReqV[8] = { 0, 0, 20, 30, 40, 50, 60, 100 };
		const float slotW = 96.0f, slotH = 88.0f, gap = 10.0f;
		const Rectangle strip = { 90 + ix, 122 + ofs, 660, slotH };
		float contentW = (float)skins.size() * (slotW + gap) - gap;
		float maxScroll = contentW > strip.width ? contentW - strip.width : 0.0f;
		if (CheckCollisionPointRec(vmouse, strip)) state.birdScroll -= custWheel * 60.0f;
		if (IsKeyDown(KEY_RIGHT)) state.birdScroll += 400.0f * dt;
		if (IsKeyDown(KEY_LEFT))  state.birdScroll -= 400.0f * dt;
		if (state.birdScroll < 0.0f) state.birdScroll = 0.0f;
		if (state.birdScroll > maxScroll) state.birdScroll = maxScroll;
		BeginScaledScissor((int)strip.x, (int)strip.y, (int)strip.width, (int)strip.height);
		for (int i = 0; i < (int)skins.size(); i++)
		{
			float bx = strip.x + i * (slotW + gap) - state.birdScroll;
			float by = strip.y;
			if (bx + slotW < strip.x || bx > strip.x + strip.width) continue;   // skip tiles fully outside the strip
			bool unlocked = (dispUnlockedSkins >> i) & 1ull;
			SkinIndex& activeSkin = (state.playerSlot == PlayerSlot::PLAYER_ONE) ? sd.skinIndex : sd.skinIndex2;
			SkinIndex& otherSkin = (state.playerSlot == PlayerSlot::PLAYER_ONE) ? sd.skinIndex2 : sd.skinIndex;
			bool sel = activeSkin == static_cast<SkinIndex>(i);
			float bAnim = (i < 8) ? state.birdClickAnim[i] : 0.0f;
			float bScale = 1.0f + 0.08f * sinf(bAnim * 3.14159f);   // click-bounce pop
			Rectangle sRect = { bx, by, (slotW - 4) * bScale, slotH * bScale };
			DrawRectangleRec(sRect, Color{ 30, 45, 80, 255 });
			if (sel) DrawRectangleLinesEx(sRect, 3 + (bAnim > 0.0f ? 2 : 0), GOLD);
			Texture2D bt = skins[i].frames[static_cast<int>(BirdFrame::MID_FLAP)];
			float s = 2.6f * bScale;
			float cx = bx + (slotW - 4) * 0.5f;
			DrawTextureEx(bt, Vector2{ cx - bt.width * s / 2, by + 34 - bt.height * s / 2 }, 0, s, unlocked ? WHITE : Color{ 35, 35, 35, 255 });
			// name always shows (grayed while locked); the unlock requirement moves to a hover tooltip
			DrawText(skins[i].name, (int)(cx - MeasureText(skins[i].name, 13) / 2), (int)(by + 66), 13, unlocked ? RAYWHITE : GRAY);
			if (!unlocked && CheckCollisionPointRec(vmouse, strip) && CheckCollisionPointRec(vmouse, Rectangle{ bx, by, slotW - 4, slotH }))
			{
				int reqIndex = i < 8 ? i : 7;
				lockTip = "Reach " + std::to_string(skinReqV[reqIndex]) + " in Normal mode";
			}
			if (unlocked && CheckCollisionPointRec(vmouse, Rectangle{ bx, by, slotW - 4, slotH }) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
			{
				const SkinIndex chosenSkin = static_cast<SkinIndex>(i);
				if (activeSkin != chosenSkin)
				{
					// picking the other player's current bird swaps them, so the two always stay distinct
					const SkinIndex previousSkin = activeSkin;
					activeSkin = chosenSkin;
					if (otherSkin == chosenSkin) otherSkin = previousSkin;
					result.birdChoiceChanged = true;
				}
				if (i < 8) state.birdClickAnim[i] = 1.0f;
			}
		}
		EndScissorMode();
		DrawStripScrollbar(strip, contentW, state.birdScroll, CustomizeStrip::BIRD_COLOR, vmouse, state.dragWhich, state.dragGrab);
	}

	// --- pipe style strip: each tile previews the style in the currently-selected color ---
	DrawText("Pipe Style", (int)(90 + ix), (int)(250 + ofs), 22, DARKBLUE);
	{
		const float slotW = 86.0f, slotH = 88.0f, gap = 8.0f;
		const Rectangle strip = { 90 + ix, 276 + ofs, 660, slotH };
		float contentW = 6 * (slotW + gap) - gap;
		float maxScroll = contentW > strip.width ? contentW - strip.width : 0.0f;
		if (CheckCollisionPointRec(vmouse, strip)) state.pipeStyleScroll -= custWheel * 60.0f;
		if (state.pipeStyleScroll < 0.0f) state.pipeStyleScroll = 0.0f;
		if (state.pipeStyleScroll > maxScroll) state.pipeStyleScroll = maxScroll;
		BeginScaledScissor((int)strip.x, (int)strip.y, (int)strip.width, (int)strip.height);
		for (int s = 0; s < 6; s++)
		{
			float bx = strip.x + s * (slotW + gap) - state.pipeStyleScroll;
			float by = strip.y;
			if (bx + slotW < strip.x || bx > strip.x + strip.width) continue;
			const bool unlocked = dispBestScore >= Constants::Customization::PipeStyleRequirements[s];
			bool sel = sd.pipeStyleIndex == static_cast<PipeStyleIndex>(s);
			Rectangle sRect = { bx, by, slotW - 2, slotH };
			DrawRectangleRec(sRect, Color{ 30, 45, 80, 255 });
			if (sel) DrawRectangleLinesEx(sRect, 3, GOLD);
			Texture2D pv = pipeTex[s][EnumIndex(sd.pipeColorIndex)];   // preview in the chosen color
			float pvScale = 0.11f;
			float pvW = pv.width * pvScale, pvH = pv.height * pvScale;
			float cx = bx + (slotW - 2) / 2.0f;
			DrawTextureEx(pv, Vector2{ cx - pvW / 2.0f, by + 4 }, 0, pvScale, unlocked ? WHITE : Color{ 35, 35, 35, 255 });
			// label always shows (grayed while locked); the unlock requirement moves to a hover tooltip
			DrawText(Constants::Customization::PipeStyleLabels[s], (int)(cx - MeasureText(Constants::Customization::PipeStyleLabels[s], 12) / 2), (int)(by + slotH - 18), 12, unlocked ? RAYWHITE : GRAY);
			if (!unlocked && CheckCollisionPointRec(vmouse, strip) && CheckCollisionPointRec(vmouse, Rectangle{ bx, by, slotW - 2, slotH }))
				lockTip = "Reach " + std::to_string(Constants::Customization::PipeStyleRequirements[s]) + " in Normal mode";
			if (unlocked && CheckCollisionPointRec(vmouse, Rectangle{ bx, by, slotW - 2, slotH }) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
			{
				sd.pipeStyleIndex = static_cast<PipeStyleIndex>(s);
				result.pipeChoiceChanged = true;
			}
		}
		EndScissorMode();
		DrawStripScrollbar(strip, contentW, state.pipeStyleScroll, CustomizeStrip::PIPE_STYLE, vmouse, state.dragWhich, state.dragGrab);
	}

	// --- pipe color strip: each tile previews the chosen style in that color (cap end cropped) ---
	DrawText("Pipe Color", (int)(90 + ix), (int)(406 + ofs), 22, DARKBLUE);
	{
		const float slotW = 96.0f, slotH = 88.0f, gap = 10.0f;
		const Rectangle strip = { 90 + ix, 432 + ofs, 660, slotH };
		// +1 slot for the special "Versus" two-tone tile appended after the real colours
		float contentW = ((int)Constants::Customization::PipeColorCount + 1) * (slotW + gap) - gap;
		float maxScroll = contentW > strip.width ? contentW - strip.width : 0.0f;
		if (CheckCollisionPointRec(vmouse, strip)) state.pipeColorScroll -= custWheel * 60.0f;
		if (state.pipeColorScroll < 0.0f) state.pipeColorScroll = 0.0f;
		if (state.pipeColorScroll > maxScroll) state.pipeColorScroll = maxScroll;
		BeginScaledScissor((int)strip.x, (int)strip.y, (int)strip.width, (int)strip.height);
		for (int c = 0; c < (int)Constants::Customization::PipeColorCount; c++)
		{
			float bx = strip.x + c * (slotW + gap) - state.pipeColorScroll;
			float by = strip.y;
			if (bx + slotW < strip.x || bx > strip.x + strip.width) continue;
			const bool unlocked = dispBestScore >= Constants::Customization::PipeColorRequirements[c];
			bool sel = sd.pipeColorIndex == static_cast<PipeColorIndex>(c) && !sd.versusPipes;   // Versus selected = no solid colour is the active pick
			Rectangle sRect = { bx, by, slotW - 4, slotH };
			DrawRectangleRec(sRect, Color{ 30, 45, 80, 255 });
			if (sel) DrawRectangleLinesEx(sRect, 3, GOLD);
			Texture2D pv = pipeTex[EnumIndex(sd.pipeStyleIndex)][c];
			Rectangle src = { 0, (float)(pv.height - 44), (float)pv.width, 44 };   // show just the cap end
			float pvScale = 0.5f;
			float pvW = pv.width * pvScale, pvH = 44.0f * pvScale;
			float cx = bx + (slotW - 4) / 2.0f;
			DrawTexturePro(pv, src, Rectangle{ cx - pvW / 2.0f, by + 10, pvW, pvH }, Vector2{ 0, 0 }, 0.0f, unlocked ? WHITE : Color{ 35, 35, 35, 255 });
			// label always shows (grayed while locked); the unlock requirement moves to a hover tooltip
			DrawText(Constants::Customization::PipeColorLabels[c], (int)(cx - MeasureText(Constants::Customization::PipeColorLabels[c], 12) / 2), (int)(by + slotH - 20), 12, unlocked ? RAYWHITE : GRAY);
			if (!unlocked && CheckCollisionPointRec(vmouse, strip) && CheckCollisionPointRec(vmouse, Rectangle{ bx, by, slotW - 4, slotH }))
				lockTip = "Reach " + std::to_string(Constants::Customization::PipeColorRequirements[c]) + " in Normal mode";
			if (unlocked && CheckCollisionPointRec(vmouse, Rectangle{ bx, by, slotW - 4, slotH }) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
			{
				sd.pipeColorIndex = static_cast<PipeColorIndex>(c);
				sd.versusPipes = false;   // picking a solid colour turns Versus off (single-select, like every other colour)
				result.pipeChoiceChanged = true;
			}
		}

		// --- special "Versus" tile: a two-tone colour (ceiling = P1 bird, floor = P2) that applies in every mode.
		// unlocked by 2P wins; selected/deselected like any other colour ---
		{
			const int vc = (int)Constants::Customization::PipeColorCount;   // one slot past the real colours
			float bx = strip.x + vc * (slotW + gap) - state.pipeColorScroll;
			float by = strip.y;
			if (bx + slotW >= strip.x && bx <= strip.x + strip.width)
			{
				const bool unlocked = dispVersusWins >= Constants::Customization::VersusPipesRequirement;
				const bool sel = sd.versusPipes;
				Rectangle sRect = { bx, by, slotW - 4, slotH };
				DrawRectangleRec(sRect, Color{ 30, 45, 80, 255 });
				if (sel) DrawRectangleLinesEx(sRect, 3, GOLD);
				// split preview: upper cap slice in P1's colour, lower in P2's (skin index doubles as pipe-color index)
				const int style = EnumIndex(sd.pipeStyleIndex);
				Texture2D pvTop = pipeTex[style][EnumValue(sd.skinIndex)];
				Texture2D pvBot = pipeTex[style][EnumValue(sd.skinIndex2)];
				const Color tint = unlocked ? WHITE : Color{ 35, 35, 35, 255 };
				float cx = bx + (slotW - 4) / 2.0f;
				if (pvTop.id != 0)
				{
					Rectangle src = { 0, (float)(pvTop.height - 44), (float)pvTop.width, 22 };
					float pw = pvTop.width * 0.5f;
					DrawTexturePro(pvTop, src, Rectangle{ cx - pw / 2.0f, by + 10, pw, 11.0f }, Vector2{ 0, 0 }, 0.0f, tint);
				}
				if (pvBot.id != 0)
				{
					Rectangle src = { 0, (float)(pvBot.height - 22), (float)pvBot.width, 22 };
					float pw = pvBot.width * 0.5f;
					DrawTexturePro(pvBot, src, Rectangle{ cx - pw / 2.0f, by + 21, pw, 11.0f }, Vector2{ 0, 0 }, 0.0f, tint);
				}
				DrawText("Versus", (int)(cx - MeasureText("Versus", 12) / 2), (int)(by + slotH - 20), 12, unlocked ? RAYWHITE : GRAY);
				if (CheckCollisionPointRec(vmouse, strip) && CheckCollisionPointRec(vmouse, Rectangle{ bx, by, slotW - 4, slotH }))
				{
					const std::string howItWorks = "Top pipe = P1 bird, bottom = P2";
					lockTip = unlocked ? howItWorks
						: ("Win " + std::to_string(Constants::Customization::VersusPipesRequirement) + " versus matches\n" + howItWorks);
				}
				if (unlocked && CheckCollisionPointRec(vmouse, Rectangle{ bx, by, slotW - 4, slotH }) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !sd.versusPipes)
				{
					sd.versusPipes = true;
					result.pipeChoiceChanged = true;   // re-bake the themes' pipe pair as the two-tone
				}
			}
		}
		EndScissorMode();
		DrawStripScrollbar(strip, contentW, state.pipeColorScroll, CustomizeStrip::PIPE_COLOR, vmouse, state.dragWhich, state.dragGrab);
	}

	// --- theme grid: each tile is a day-over-night split preview ---
	DrawText("Theme", (int)(90 + ix), (int)(564 + ofs), 22, DARKBLUE);
	// parallel to the 5 themes: Classic is free, the rest gate on medal tiers (see RecomputeUnlocks thresholds)
	const char* themeReq[5] = { "Free", "Bronze Medal", "Silver Medal", "Gold Medal", "Platinum Medal" };
	for (int i = 0; i < (int)themes.size(); i++)
	{
		float tx = 90.0f + ix + i * 118.0f, ty = 592.0f + ofs;
		bool unlocked = (dispUnlockedThemes >> i) & 1ull;
		bool sel = sd.themeIndex == static_cast<ThemeIndex>(i);
		float tAnim = (i < 5) ? state.themeClickAnim[i] : 0.0f;
		float tScale = 1.0f + 0.08f * sinf(tAnim * 3.14159f);
		float tExp = 100.0f * (tScale - 1.0f) * 0.5f;   // expand around the tile center as it pops
		Rectangle tRect = { tx - tExp, ty - tExp, 100.0f * tScale, 100.0f * tScale };
		Color tint = unlocked ? WHITE : Color{ 35, 35, 35, 255 };
		// use the small resident thumbnails, not the full bg textures (only the active theme's are loaded under lazy loading)
		Rectangle src = { 0, 0, (float)themes[i].dayThumb.width, (float)themes[i].dayThumb.height };
		Rectangle srcN = { 0, 0, (float)themes[i].nightThumb.width, (float)themes[i].nightThumb.height };
		DrawTexturePro(themes[i].dayThumb,   src,  Rectangle{ tRect.x, tRect.y, tRect.width, tRect.height * 0.5f }, Vector2{ 0, 0 }, 0, tint);
		DrawTexturePro(themes[i].nightThumb, srcN, Rectangle{ tRect.x, tRect.y + tRect.height * 0.5f, tRect.width, tRect.height * 0.5f }, Vector2{ 0, 0 }, 0, tint);
		DrawLineEx(Vector2{ tRect.x, tRect.y + tRect.height * 0.5f }, Vector2{ tRect.x + tRect.width, tRect.y + tRect.height * 0.5f }, 1, Fade(RAYWHITE, 0.5f));   // day/night divider
		DrawRectangleLinesEx(tRect, sel ? 4 + (tAnim > 0.0f ? 2 : 0) : 1, sel ? GOLD : RAYWHITE);
		DrawText(themes[i].name, (int)(tx + 50 - MeasureText(themes[i].name, 14) / 2), (int)(ty + 102), 14, unlocked ? RAYWHITE : GRAY);
		// requirement moved to a hover tooltip (was a persistent label in the tile center)
		if (!unlocked && CheckCollisionPointRec(vmouse, Rectangle{ tx, ty, 100, 100 }))
			lockTip = std::string("Earn the ") + themeReq[i < 5 ? i : 4];
		if (unlocked && CheckCollisionPointRec(vmouse, Rectangle{ tx, ty, 100, 100 }) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
		{
			sd.themeIndex = static_cast<ThemeIndex>(i);
			if (i < 5) state.themeClickAnim[i] = 1.0f;
		}
	}

	// dim bands above/below the scroll viewport, so the title and Back button stay readable over the menu sky
	DrawRectangle(0, 0, VIRTUAL_W, (int)kCustViewTop, Color{ 8, 14, 34, 170 });
	DrawText("Customize", VIRTUAL_W / 2 - MeasureText("Customize", 50) / 2, 14, 46, DARKBLUE);   // redraw title over the top band
	DrawRectangle(0, (int)kCustViewBot, VIRTUAL_W, VIRTUAL_H - (int)kCustViewBot, Color{ 8, 14, 34, 170 });

	// page scrollbar (same drag/click-track behavior as the Settings one)
	if (custMaxScroll > 0.0f)
	{
		const float trackX = 765.0f + ix, trackW = 14.0f;
		Rectangle track = { trackX, kCustViewTop, trackW, kCustViewH };
		DrawRectangleRec(track, Color{ 40, 40, 60, 200 });
		float handleH = kCustViewH * (kCustViewH / kCustContentH);
		if (handleH < 24.0f) handleH = 24.0f;
		float handleY = kCustViewTop + (state.pageScroll / custMaxScroll) * (kCustViewH - handleH);
		Rectangle handle = { trackX, handleY, trackW, handleH };
		if (!state.pageDragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
		{
			if (CheckCollisionPointRec(vmouse, handle))
			{
				state.pageDragging = true;
				state.pageDragGrab = vmouse.y - handleY;
			}
			else if (CheckCollisionPointRec(vmouse, track))
			{
				state.pageDragging = true;
				state.pageDragGrab = handleH * 0.5f;
				float t = (vmouse.y - state.pageDragGrab - kCustViewTop) / (kCustViewH - handleH);
				if (t < 0) t = 0;
				if (t > 1) t = 1;
				state.pageScroll = t * custMaxScroll;
			}
		}
		if (state.pageDragging)
		{
			float t = (vmouse.y - state.pageDragGrab - kCustViewTop) / (kCustViewH - handleH);
			if (t < 0) t = 0;
			if (t > 1) t = 1;
			state.pageScroll = t * custMaxScroll;
			if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) state.pageDragging = false;
		}
		bool handleHover = CheckCollisionPointRec(vmouse, handle);
		DrawRectangleRec(handle, state.pageDragging ? GOLD : (handleHover ? Color{ 220, 220, 220, 255 } : RAYWHITE));
	}
	else
	{
		state.pageDragging = false;
	}

	// lift the Back button out of the fill-width bottom crop (maximized)
	result.backClicked = UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 555.0f - FillModeBottomCrop(), 180, 36 }, "Back", vmouse);

	// tile tooltip: drawn last so it floats above every strip, dim band, and the Back button. anchored just above the
	// cursor and clamped to the screen so it never runs off an edge. supports up to two lines split on '\n'
	if (!lockTip.empty())
	{
		const int fs = 16;
		const float lineH = fs + 4.0f;
		const auto nl = lockTip.find('\n');
		const std::string line1 = (nl == std::string::npos) ? lockTip : lockTip.substr(0, nl);
		const std::string line2 = (nl == std::string::npos) ? std::string() : lockTip.substr(nl + 1);
		const int wdt1 = MeasureText(line1.c_str(), fs);
		const int wdt2 = line2.empty() ? 0 : MeasureText(line2.c_str(), fs);
		const float boxW = (float)std::max(wdt1, wdt2) + 24.0f;
		const float boxH = (line2.empty() ? lineH : lineH * 2.0f) + 12.0f;
		float bxp = vmouse.x - boxW * 0.5f;
		float byp = vmouse.y - boxH - 14.0f;
		if (bxp < 4.0f) bxp = 4.0f;
		if (bxp + boxW > VIRTUAL_W - 4.0f) bxp = VIRTUAL_W - 4.0f - boxW;
		if (byp < 4.0f) byp = vmouse.y + 18.0f;   // no room above the cursor: flip below it
		DrawRectangleRec(Rectangle{ bxp, byp, boxW, boxH }, Color{ 12, 18, 34, 240 });
		DrawRectangleLinesEx(Rectangle{ bxp, byp, boxW, boxH }, 2, Color{ 90, 120, 180, 255 });
		DrawText(line1.c_str(), (int)(bxp + 12), (int)(byp + 8), fs, RAYWHITE);
		if (!line2.empty()) DrawText(line2.c_str(), (int)(bxp + 12), (int)(byp + 8 + lineH), fs, Color{ 180, 200, 230, 255 });
	}

	// dev-view badge so it's obvious the screen is faking a new-player unlock state (F1 to toggle)
	if (devNewPlayerView)
		DrawText("DEV: new-player view (F1)", 10, VIRTUAL_H - 24, 16, Color{ 255, 120, 120, 255 });

	return result;
}
