#include "game.h"

#include "font.h"
#include "render_game.h"
#include "ui.h"

#include <algorithm>
#include <cmath>

// per-frame single-player render: backdrop, pipes + their overlays, the bird, particles, the HUD, and the game-over panel
void FlappyGame::DrawSinglePlayerScene(const Theme& currentTheme, Vector2 virtualMouse)
{

	// when the score crosses the next night-switch threshold, flip the phase and schedule the following one
	if (singlePlayer.score >= world.nightSwitchScore)
	{
		world.nightPhase = !world.nightPhase;
		world.nightSwitchScore += GetRandomValue(Constants::Themes::NightSwitchMin, Constants::Themes::NightSwitchMax);
	}
	// ease nightAmount toward the target (0 day / 1 night) over ~0.8s
	float nightTarget = world.nightPhase ? 1.0f : 0.0f;
	float fadeStep = deltaTime / 0.8f;
	if (world.nightAmount < nightTarget) { world.nightAmount += fadeStep; if (world.nightAmount > nightTarget) world.nightAmount = nightTarget; }
	else if (world.nightAmount > nightTarget) { world.nightAmount -= fadeStep; if (world.nightAmount < nightTarget) world.nightAmount = nightTarget; }

	float cy = world.cameraY;   // photo-mode vertical pan; everything subtracts it
	Color pipeTint = WHITE;
	const Color hudTextColor = world.nightAmount >= 0.5f ? RAYWHITE : BLACK;   // light text on night, dark on day

	DaynightShaderHandle dnHandle = MakeDaynightHandle();
	DrawThemeSky(currentTheme, resources.ui, singlePlayer.scroll.renderedSkyScroll,
		singlePlayer.scroll.renderedMidScroll, world.nightAmount, world.moonScroll, cy, &dnHandle);

	// --- pipes, plus per-pipe wind / portal / pickup overlays ---
	int topPipeOff = 266 - currentTheme.pipe.height;   // a shorter pipe texture is pushed up so its cap aligns to the 266 body
	for (int i = 0; i < Constants::Pipes::Count; i++)
	{
		const float pipeX = singlePlayer.scroll.renderedPipeX[i];   // interpolated render X (not the live sim X)
		DrawTextureV(currentTheme.pipe, Vector2{ pipeX, world.topPipes[i].y + topPipeOff - cy }, pipeTint);
		DrawTextureV(currentTheme.pipe180, Vector2{ pipeX, world.bottomPipes[i].y - cy }, pipeTint);

		float gapMid = (world.topPipes[i].y + 266.0f + world.bottomPipes[i].y) * 0.5f - cy;   // screen-space center of the gap
		if (world.topPipes[i].variation == PipeVariation::WIND
			&& pipeX + Constants::Pipes::Width > 0
			&& pipeX + Constants::Pipes::Width < VIRTUAL_W)
		{
			// use the run clock (frozen while paused), not wall-clock GetTime(), so the chevron sweep + countdown
			// don't keep advancing during a pause
			if (world.topPipes[i].windFireTime < 0.0f) world.topPipes[i].windFireTime = singlePlayer.runClock;
			float elapsed = singlePlayer.runClock - world.topPipes[i].windFireTime;
			float dir = world.topPipes[i].windDir;
			const float sweepDur = dir > 0.0f
				? Constants::Pipes::WindBurstRightSweepSeconds
				: Constants::Pipes::WindBurstLeftSweepSeconds;
			if (elapsed >= 0.0f && elapsed < sweepDur)
			{
				int wIdx = ((int)(singlePlayer.runClock * 10.0f)) % 3;   // cycle the 3 chevron frames ~10/s
				Texture2D wt = resources.ui.windFrames[wIdx];
				float wY = gapMid - wt.height * 0.5f;
				float spriteW = wt.width * 2.0f;
				float t = elapsed / sweepDur;   // 0..1 sweep progress
				float startX = dir > 0 ? -spriteW : (float)VIRTUAL_W;
				float endX   = dir > 0 ? (float)VIRTUAL_W : -spriteW;
				float spriteX = startX + (endX - startX) * t;
				Rectangle wsrc = { 0, 0, dir > 0 ? (float)wt.width : -(float)wt.width, (float)wt.height };   // flip the sprite for leftward wind
				Rectangle wdst = { spriteX, wY, spriteW, (float)wt.height * 2 };
				DrawTexturePro(wt, wsrc, wdst, Vector2{ 0, 0 }, 0.0f, Fade(WHITE, 0.9f));
			}
		}

		if (world.topPipes[i].variation == PipeVariation::PORTAL && !world.topPipes[i].pickupCollected)
		{
			float ang = GetTime() * 90.0f;   // spin the portal orb continuously until collected
			float s = 2.0f;
			Rectangle src = { 0, 0, (float)resources.ui.orbPortal.width, (float)resources.ui.orbPortal.height };
			Rectangle dst = { pipeX + 44, gapMid, resources.ui.orbPortal.width * s, resources.ui.orbPortal.height * s };
			DrawTexturePro(resources.ui.orbPortal, src, dst, Vector2{ resources.ui.orbPortal.width * s / 2, resources.ui.orbPortal.height * s / 2 }, ang, WHITE);
		}

		if (world.topPipes[i].pickup != PickupType::NONE && !world.topPipes[i].pickupCollected)
		{
			Texture2D ot = (world.topPipes[i].pickup == PickupType::SHIELD) ? resources.ui.orbShield : resources.ui.orbSlowMo;
			float s = 2.4f;
			float bob = sinf((float)GetTime() * 3.0f + pipeX * 0.01f) * 3.0f;   // gentle bob, phase-offset per pipe so they don't sync
			DrawTextureEx(ot, Vector2{ pipeX + 44 - ot.width * s / 2, gapMid - ot.height * s / 2 + bob }, 0.0f, s, WHITE);
		}
	}

	DrawThemeGround(currentTheme, singlePlayer.scroll.renderedBaseScroll, BaseTop, cy);

	// --- bird (and its flap animation) ---
	if (singlePlayer.alive && (interfaceState.current == GameState::PLAYING || interfaceState.current == GameState::READY))
	{
		if (resources.flapBoost > 0.0f) resources.flapBoost -= deltaTime;
		float frameDur = (resources.flapBoost > 0.0f) ? 0.04f : 0.09f;   // flapBoost briefly quickens the wing cycle after a hop
		resources.birdAnimationTimer += deltaTime;
		if (resources.birdAnimationTimer >= frameDur) { resources.birdAnimationTimer = 0.0f; resources.birdFrameStep = (resources.birdFrameStep + 1) % 4; }
	}
	Texture2D birdTex = resources.skins[EnumIndex(storage.save.skinIndex)].frames[static_cast<int>(resources.birdFrameOrder[resources.birdFrameStep])];
	float bw = birdTex.width * BirdScale, bh = birdTex.height * BirdScale;
	Rectangle birdSrc = { 0, 0, (float)birdTex.width, (float)birdTex.height };
	// ghost replay: draw the recorded best run faintly behind the bird (normal mode only, when enabled)
	if (interfaceState.current == GameState::PLAYING && singlePlayer.alive && ghost.valid && storage.save.showGhost && interfaceState.sandboxEffect == SandboxEffect::NONE)
	{
		Texture2D gt = resources.skins[EnumIndex(ghost.skin)].frames[static_cast<int>(resources.birdFrameOrder[resources.birdFrameStep])];
		Rectangle gsrc = { 0, 0, (float)gt.width, (float)gt.height };
		Rectangle gdst = { Constants::Bird::HomeX + 30.0f, ghost.y + 30.0f - cy, gt.width * BirdScale, gt.height * BirdScale };
		DrawTexturePro(gt, gsrc, gdst, Vector2{ gt.width * BirdScale / 2, gt.height * BirdScale / 2 }, ghost.rotation,
			Fade(WHITE, storage.save.ghostOpacity / 100.0f));
	}
	Rectangle birdDst = { (float)(int)(world.bird.x + 30.0f), (float)(int)(world.bird.y + 30.0f - cy), bw, bh };
	Vector2 birdOrigin = { bw / 2.0f, bh / 2.0f };
	if (singlePlayer.shieldTime > 0.0f)
	{
		// active shield: pulsing additive glow behind the bird, plus a translucent bubble + ring
		float bcx = world.bird.x + 30.0f;
		float bcy = world.bird.y + 30.0f - cy;
		float pulse = 0.5f + 0.5f * sinf(GetTime() * 6.0f);
		float glowR = std::max(bw, bh) * 0.85f + 12.0f + pulse * 5.0f;
		Color glowTint = Color{ 80, 180, 255, (unsigned char)(85 + pulse * 45) };
		Rectangle glowSrc = { 0, 0, (float)bloomFlashTex.width, (float)bloomFlashTex.height };
		Rectangle glowDst = { bcx - glowR, bcy - glowR, glowR * 2.0f, glowR * 2.0f };
		BeginBlendMode(BLEND_ADDITIVE);
		DrawTexturePro(bloomFlashTex, glowSrc, glowDst, Vector2{ 0.0f, 0.0f }, 0.0f, glowTint);
		EndBlendMode();
		float bubbleR = std::max(bw, bh) * 0.5f + 2.0f + pulse * 1.5f;
		DrawCircle((int)bcx, (int)bcy, bubbleR, Color{ 120, 200, 255, 70 });
		DrawRing(Vector2{ bcx, bcy }, bubbleR - 2.0f, bubbleR, 0.0f, 360.0f, 48, Fade(SKYBLUE, 0.85f + 0.15f * pulse));
	}
	Color birdTint = (singlePlayer.shieldTime > 0.0f) ? Color{ 170, 220, 255, 255 } : WHITE;   // tint the bird blue while shielded
	DrawTexturePro(birdTex, birdSrc, birdDst, birdOrigin, world.bird.rotation, birdTint);

	// --- particles (each ParticleType draws differently) ---
	for (auto& p : particles)
	{
		float a = p.life / p.maxLife;   // remaining-life fraction, used for the fade
		if (p.kind == ParticleType::Text)
		{
			float pfs = 18.0f;
			float pw = MeasureTextEx(gPixelFont, p.text, pfs, pfs / 16.0f).x;
			DrawTextEx(gPixelFont, p.text, Vector2{ p.pos.x - pw / 2, p.pos.y - cy }, pfs, pfs / 16.0f, Fade(p.color, a));
		}
		else if (p.kind == ParticleType::BloomFlash)
		{
			// additive radial gradient that starts tight + bright and expands + fades; t goes 0 at spawn to 1 at end of life
			float t = 1.0f - a;
			float scale = (1.0f + 3.0f * t);
			float radius = p.size * scale;
			float alphaNorm = 0.6f * (a * a);   // quadratic falloff
			Color tint = { p.color.r, p.color.g, p.color.b, (unsigned char)(alphaNorm * p.color.a) };
			Rectangle src = { 0, 0, (float)bloomFlashTex.width, (float)bloomFlashTex.height };
			Rectangle dst = { p.pos.x - radius, p.pos.y - cy - radius, radius * 2.0f, radius * 2.0f };
			BeginBlendMode(BLEND_ADDITIVE);
			DrawTexturePro(bloomFlashTex, src, dst, Vector2{ 0.0f, 0.0f }, 0.0f, tint);
			EndBlendMode();
		}
		else if (p.kind == ParticleType::DownArrow || p.kind == ParticleType::UpArrow)
		{
			// drawn as a full arrow: down = time slowed, up = time sped up
			float s = p.size;
			float px = p.pos.x, py = p.pos.y - cy;
			Color c = Fade(p.color, a);
			const bool up = p.kind == ParticleType::UpArrow;
			float tipY = up ? py - s : py + s;
			float tailY = up ? py + s : py - s;
			float headBaseY = up ? tipY + s * 0.7f : tipY - s * 0.7f;
			DrawLineEx(Vector2{ px, tailY }, Vector2{ px, tipY }, 2.5f, c);
			DrawLineEx(Vector2{ px - s * 0.6f, headBaseY }, Vector2{ px, tipY }, 2.5f, c);
			DrawLineEx(Vector2{ px + s * 0.6f, headBaseY }, Vector2{ px, tipY }, 2.5f, c);
		}
		else
		{
			DrawCircle((int)p.pos.x, (int)(p.pos.y - cy), p.size * a, Fade(p.color, a));   // puffs/sparks/feathers shrink as they fade
		}
	}

	// --- debug overlays ---
	if (singlePlayer.showHitboxes)
	{
		DrawCircleLines((int)(world.bird.x + 30.0f), (int)(world.bird.y + 30.0f - cy), BirdRadius, RED);
		for (int i = 0; i < Constants::Pipes::Count; i++)
		{
			DrawRectangleLinesEx(Rectangle{ singlePlayer.scroll.renderedPipeX[i], world.topCollision[i].y - cy, world.topCollision[i].width, world.topCollision[i].height }, 2, GREEN);
			DrawRectangleLinesEx(Rectangle{ singlePlayer.scroll.renderedPipeX[i], world.bottomCollision[i].y - cy, world.bottomCollision[i].width, world.bottomCollision[i].height }, 2, GREEN);
		}
		DrawLineEx(Vector2{ 0, BaseTop - cy }, Vector2{ (float)VIRTUAL_W, BaseTop - cy }, 2, ORANGE);   // ground line
	}
	// lift out of the fill-width bottom crop (maximized) so the indicator stays on screen
	if (singlePlayer.noClip) DrawText("NOCLIP", 12, VIRTUAL_H - 30 - (int)FillModeBottomCrop(), 20, RED);

	if (interfaceState.current == GameState::READY)
	{
		// Get Ready banner + the control/skin preview
		float grScale = 4.0f;
		DrawTextureEx(resources.ui.getReady, Vector2{ VIRTUAL_W / 2 - resources.ui.getReady.width * grScale / 2, 120 }, 0.0f, grScale, WHITE);
		float pvScale = 3.4f;
		DrawTextureEx(resources.ui.preview, Vector2{ VIRTUAL_W / 2 - resources.ui.preview.width * pvScale / 2 + 45, 250 }, 0.0f, pvScale, WHITE);
	}

	// --- in-run HUD: big score + active-effect readouts ---
	if (singlePlayer.alive && interfaceState.current == GameState::PLAYING)
	{
		float bigScale = storage.save.largerHud ? 7.0f : 6.0f;
		float bigCY = 30 + resources.scoreBig[0].height * bigScale * 0.5f;
		DrawSpriteNumber(resources.scoreBig, singlePlayer.score, VIRTUAL_W / 2.0f, bigCY, bigScale);
		int hy = 50;   // stacked effect readouts run down the top-left
		if (singlePlayer.shieldTime > 0.0f) { DrawText(TextFormat("Shield %.0f", singlePlayer.shieldTime), 20, hy, 22, hudTextColor); hy += 26; }
		if (singlePlayer.slowTime > 0.0f)
		{
			const bool speeding = singlePlayer.timeWarpDir > 0;
			DrawText(TextFormat(speeding ? "Time Warp + %.0f" : "Time Warp - %.0f", singlePlayer.slowTime),
				20, hy, 22, hudTextColor);
			hy += 26;
		}
		if (singlePlayer.windForce != 0.0f) { DrawText(TextFormat(singlePlayer.windForce > 0 ? "Wind -> %.0f" : "Wind <- %.0f", singlePlayer.windTimeLeft), 20, hy, 22, hudTextColor); hy += 26; }
	}

	// --- game over: banner fades in, score panel slides up, then the button row appears ---
	if (!singlePlayer.alive)
	{
		float goScale = 4.5f;
		float goW = resources.ui.gameOver.width * goScale;
		DrawTextureEx(resources.ui.gameOver, Vector2{ VIRTUAL_W / 2 - goW / 2, 110 }, 0.0f, goScale, Fade(WHITE, singlePlayer.gameOverFade));

		float S = 4.0f;
		float pw = resources.ui.scorePanel.width * S;
		float px = VIRTUAL_W / 2 - pw / 2;
		float py = VIRTUAL_H - (VIRTUAL_H - 225.0f) * singlePlayer.panelSlide;   // panelSlide 0..1 drives the slide-up
		if (singlePlayer.panelSlide > 0.0f)
		{
			DrawTextureEx(resources.ui.scorePanel, Vector2{ px, py }, 0.0f, S, WHITE);
			if (singlePlayer.medalRank != MedalRank::NONE)
			{
				Texture2D md = resources.medals[EnumIndex(singlePlayer.medalRank)];
				float mScale = 4.0f;
				DrawTextureEx(md, Vector2{ px + 24 * S - md.width * mScale / 2, py + 33 * S - md.height * mScale / 2 }, 0.0f, mScale, WHITE);
			}

			float smallScale = 3.0f;
			float valRight = px + 104 * S;
			int currentScore = std::max(0, singlePlayer.savedScore);
			// each mode keeps its own best, so the panel shows the high score for the mode just played
			int bestForMode = singlePlayer.dailyMode ? storage.save.bestDailyScore
				: (singlePlayer.endlessMode ? storage.save.bestClassicScore : storage.save.bestScore);
			float curRowCY = py + 16 * S + resources.scoreSmall[0].height * smallScale * 0.5f;
			float bestRowCY = py + 37 * S + resources.scoreSmall[0].height * smallScale * 0.5f;
			DrawSpriteNumberRight(resources.scoreSmall, currentScore, valRight, curRowCY, smallScale);
			DrawSpriteNumberRight(resources.scoreSmall, bestForMode, valRight, bestRowCY, smallScale);
			if (singlePlayer.newBest) DrawTextureEx(resources.ui.newBadge, Vector2{ px + 56 * S, py + 30 * S }, 0.0f, 2.5f, WHITE);
		}

		if (singlePlayer.panelSlide >= 1.0f)
		{
			// buttons only after the panel has fully arrived; lift the row out of the fill-width bottom crop (maximized)
			const float goBtnY = 510.0f - FillModeBottomCrop();
			if (interfaceState.sandboxEffect != SandboxEffect::NONE)
			{
				if (UiButton(Rectangle{ VIRTUAL_W / 2 - 230, goBtnY, 195, 44 }, "Try Again", virtualMouse))
					StartSandbox(interfaceState.sandboxEffect, interfaceState.sandboxReturnPage);
				if (UiButton(Rectangle{ VIRTUAL_W / 2 - 25, goBtnY, 270, 44 }, "Back to Tutorial", virtualMouse))
				{
					singlePlayer.alive = false;
					interfaceState.tutorialPage = interfaceState.sandboxReturnPage;
					interfaceState.sandboxEffect = SandboxEffect::NONE;
					interfaceState.current = GameState::TUTORIAL;
				}
			}
			else
			{
				// daily runs are one-per-day, so they get no "Try Again" — only "Menu"
				if (!singlePlayer.dailyMode &&
					UiButton(Rectangle{ VIRTUAL_W / 2 - 175, goBtnY, 165, 44 }, "Try Again", virtualMouse))
					RestartCurrentRun();
				const float menuX = singlePlayer.dailyMode ? VIRTUAL_W / 2 - 82.5f : VIRTUAL_W / 2 + 10.0f;   // center Menu when it's the only button
				if (UiButton(Rectangle{ menuX, goBtnY, 165, 44 }, "Menu", virtualMouse))
				{
					singlePlayer.alive = false;
					interfaceState.current = GameState::MENU;
				}
			}
		}
	}

	if (singlePlayer.deathFlash > 0.0f) DrawRectangle(0, 0, VIRTUAL_W, VIRTUAL_H, Fade(WHITE, singlePlayer.deathFlash));   // white flash on the death frame
}
