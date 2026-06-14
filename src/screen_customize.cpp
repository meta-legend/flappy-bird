#include "screen_customize.h"

#include "constants.h"
#include "progression.h"
#include "render_menus.h"
#include "save.h"
#include "system_display.h"
#include "types.h"
#include "ui.h"

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

	// themes unlock at 0 / 5 / 10 / 20 / 30 (bit 0 is always set)
	save.unlockedThemes = 1ull;
	if (bestScore >= 5) save.unlockedThemes |= 2ull;
	if (bestScore >= 10) save.unlockedThemes |= 4ull;
	if (bestScore >= 20) save.unlockedThemes |= 8ull;
	if (bestScore >= 30) save.unlockedThemes |= 16ull;

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
			bool unlocked = (sd.unlockedSkins >> i) & 1ull;
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
			if (unlocked) DrawText(skins[i].name, (int)(cx - MeasureText(skins[i].name, 13) / 2), (int)(by + 66), 13, RAYWHITE);
			else
			{
				int reqIndex = i < 8 ? i : 7;
				std::string rq = "Normal " + std::to_string(skinReqV[reqIndex]);   // locked: show the unlock requirement
				DrawText(rq.c_str(), (int)(cx - MeasureText(rq.c_str(), 12) / 2), (int)(by + 66), 12, LIGHTGRAY);
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
			const bool unlocked = sd.bestScore >= Constants::Customization::PipeStyleRequirements[s];
			bool sel = sd.pipeStyleIndex == static_cast<PipeStyleIndex>(s);
			Rectangle sRect = { bx, by, slotW - 2, slotH };
			DrawRectangleRec(sRect, Color{ 30, 45, 80, 255 });
			if (sel) DrawRectangleLinesEx(sRect, 3, GOLD);
			Texture2D pv = pipeTex[s][EnumIndex(sd.pipeColorIndex)];   // preview in the chosen color
			float pvScale = 0.11f;
			float pvW = pv.width * pvScale, pvH = pv.height * pvScale;
			float cx = bx + (slotW - 2) / 2.0f;
			DrawTextureEx(pv, Vector2{ cx - pvW / 2.0f, by + 4 }, 0, pvScale, unlocked ? WHITE : Color{ 35, 35, 35, 255 });
			const std::string styleLabel = unlocked ? Constants::Customization::PipeStyleLabels[s]
				: "Normal " + std::to_string(Constants::Customization::PipeStyleRequirements[s]);
			DrawText(styleLabel.c_str(), (int)(cx - MeasureText(styleLabel.c_str(), 12) / 2), (int)(by + slotH - 18), 12, unlocked ? RAYWHITE : LIGHTGRAY);
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
		float contentW = (int)Constants::Customization::PipeColorCount * (slotW + gap) - gap;
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
			const bool unlocked = sd.bestScore >= Constants::Customization::PipeColorRequirements[c];
			bool sel = sd.pipeColorIndex == static_cast<PipeColorIndex>(c);
			Rectangle sRect = { bx, by, slotW - 4, slotH };
			DrawRectangleRec(sRect, Color{ 30, 45, 80, 255 });
			if (sel) DrawRectangleLinesEx(sRect, 3, GOLD);
			Texture2D pv = pipeTex[EnumIndex(sd.pipeStyleIndex)][c];
			Rectangle src = { 0, (float)(pv.height - 44), (float)pv.width, 44 };   // show just the cap end
			float pvScale = 0.5f;
			float pvW = pv.width * pvScale, pvH = 44.0f * pvScale;
			float cx = bx + (slotW - 4) / 2.0f;
			DrawTexturePro(pv, src, Rectangle{ cx - pvW / 2.0f, by + 10, pvW, pvH }, Vector2{ 0, 0 }, 0.0f, unlocked ? WHITE : Color{ 35, 35, 35, 255 });
			const std::string colorLabel = unlocked ? Constants::Customization::PipeColorLabels[c]
				: "Normal " + std::to_string(Constants::Customization::PipeColorRequirements[c]);
			DrawText(colorLabel.c_str(), (int)(cx - MeasureText(colorLabel.c_str(), 12) / 2), (int)(by + slotH - 20), 12, unlocked ? RAYWHITE : LIGHTGRAY);
			if (unlocked && CheckCollisionPointRec(vmouse, Rectangle{ bx, by, slotW - 4, slotH }) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
			{
				sd.pipeColorIndex = static_cast<PipeColorIndex>(c);
				result.pipeChoiceChanged = true;
			}
		}
		EndScissorMode();
		DrawStripScrollbar(strip, contentW, state.pipeColorScroll, CustomizeStrip::PIPE_COLOR, vmouse, state.dragWhich, state.dragGrab);
	}

	// --- theme grid: each tile is a day-over-night split preview ---
	DrawText("Theme", (int)(90 + ix), (int)(564 + ofs), 22, DARKBLUE);
	const char* themeReq[6] = { "Free", "Normal 5", "Bronze Medal", "Silver Medal", "Gold Medal", "Platinum Medal" };
	for (int i = 0; i < (int)themes.size(); i++)
	{
		float tx = 90.0f + ix + i * 118.0f, ty = 592.0f + ofs;
		bool unlocked = (sd.unlockedThemes >> i) & 1ull;
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
		if (!unlocked)
		{
			const char* req = themeReq[i < 6 ? i : 5];
			DrawText(req, (int)(tx + 50 - MeasureText(req, 14) / 2), (int)(ty + 42), 14, LIGHTGRAY);
		}
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
	return result;
}
