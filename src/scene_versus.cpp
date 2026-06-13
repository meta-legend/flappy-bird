#include "game.h"

#include "font.h"
#include "gameplay_helpers.h"
#include "render_game.h"
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <string>

// 2-player versus on shared pipes. P1's shield/time-warp effects reuse the singlePlayer.* fields (shieldTime, slowTime,
// slowFactor, timeWarpDir); P2's shield is versus.shieldTime2. the time warp scales the shared world speed, so it isn't
// duplicated per player

// per-bird gravity + rotation — the same physics single-player uses, run once for each bird
void FlappyGame::StepVersusBird(Bird& targetBird, bool isAlive)
{
	if (!isAlive) return;
	targetBird.velocity += Constants::Bird::Gravity * deltaTime;
	targetBird.y += targetBird.velocity * deltaTime;
	targetBird.sinceJump += deltaTime;
	if (targetBird.sinceJump >= Constants::Bird::Rotation::HoldSeconds)
		targetBird.rotation = std::min(Constants::Bird::Rotation::DownAngle, targetBird.rotation + Constants::Bird::Rotation::Rate * deltaTime);
	if (targetBird.y < 0) { targetBird.y = 0; targetBird.velocity = 0; }   // clamp at the ceiling
}

// collision + scoring + pickups for one bird; the bool*/score args point at that player's slice of VersusState
void FlappyGame::CheckVersusBird(Bird& targetBird, bool& isAlive, int& targetScore,
	bool* scoredPipes, bool* collectedPickups, float& targetShieldTime)
{
	if (!isAlive) return;
	const Vector2 center = { targetBird.x + 30.0f, targetBird.y + 30.0f };
	if (targetBird.y + 30.0f + BirdHalfHeight >= BaseTop)   // hit the ground = instant out (shields don't save you from the floor)
	{
		targetBird.y = BaseTop - 30.0f - BirdHalfHeight;
		isAlive = false;
		ResetTimeWarpEffect();
		PlaySound(audio.hit);
		particleEmitter.Burst(center.x, center.y, ParticleType::Feather, 10);
		return;
	}

	for (int i = 0; i < Constants::Pipes::Count; i++)
	{
		const bool hitTopPipe = CheckCollisionCircleRec(center, BirdRadius, world.topCollision[i]);
		const bool hitBottomPipe = CheckCollisionCircleRec(center, BirdRadius, world.bottomCollision[i]);
		if (hitTopPipe || hitBottomPipe)
		{
			if (targetShieldTime > 0.0f)
			{
				// shield absorbs the hit: bounce off (down off a top pipe, up off a bottom one) and consume the shield
				const bool pushDown = hitTopPipe && !hitBottomPipe;
				targetShieldTime = 0.0f;
				if (pushDown)
				{
					targetBird.velocity = -Constants::Bird::JumpImpulse * Constants::Pickups::TopPipeShieldPushFactor;
				}
				else
				{
					targetBird.velocity = Constants::Bird::JumpImpulse;
					targetBird.rotation = Constants::Bird::Rotation::UpAngle;
					targetBird.sinceJump = 0.0f;
				}
				// nudge the bird clear of the pipe it bounced off, so it doesn't immediately re-collide
				if (pushDown)
				{
					const float minY = world.topCollision[i].y + world.topCollision[i].height + BirdRadius - 30.0f;
					if (targetBird.y < minY) targetBird.y = minY;
				}
				else if (hitBottomPipe)
				{
					const float maxY = world.bottomCollision[i].y - BirdRadius - 30.0f;
					if (targetBird.y > maxY) targetBird.y = maxY;
				}
				PlaySound(audio.hit);
				particleEmitter.Burst(center.x, center.y, ParticleType::Spark, 8);
			}
			else
			{
				isAlive = false;
				ResetTimeWarpEffect();
				PlaySound(audio.hit);
				particleEmitter.Burst(center.x, center.y, ParticleType::Feather, 10);
				return;
			}
		}
		// score when the bird's center crosses the pipe pair's centerline
		if (!scoredPipes[i] &&
			targetBird.x + 30.0f >= world.topPipes[i].x + Constants::Pipes::Width * 0.5f)
		{
			targetScore++;
			scoredPipes[i] = true;
			PlaySound(audio.score);
			const float plusOneX = world.topPipes[i].x + 30.0f;
			const float plusOneY = targetBird.y - 30.0f;
			particleEmitter.BloomFlash(plusOneX, plusOneY,
				Color{ 255, 240, 180, (unsigned char)Constants::Effects::ScoreBloomStrength }, 28.0f);
			particleEmitter.Text(plusOneX, plusOneY, "+1", WHITE);
		}
		if (world.topPipes[i].pickup == PickupType::NONE || world.topPipes[i].pickupCollected || collectedPickups[i]) continue;
		const Vector2 pickupCenter = {
			world.topPipes[i].x + Constants::Pipes::Width * 0.5f,
			(world.topPipes[i].y + Constants::Pipes::BodyHeight + world.bottomPipes[i].y) * 0.5f
		};
		const Rectangle birdBounds = {
			center.x - BirdRadius, center.y - BirdRadius,
			BirdRadius * 2, BirdRadius * 2
		};
		if (CheckCollisionCircleRec(pickupCenter, Constants::Pickups::Radius, birdBounds))
		{
			collectedPickups[i] = true;
			if (world.topPipes[i].pickup == PickupType::SHIELD)
			{
				targetShieldTime += Constants::Pickups::ShieldDuration;
				SetSoundPitch(audio.pickup, Constants::Pickups::ShieldPickupPitch);
				PlaySound(audio.pickup);
				particleEmitter.BloomFlash(center.x, center.y, Color{ 80, 180, 255, 170 }, 64.0f);
			}
			else
			{
				ActivateTimeWarp(pickupCenter, 0.5f);
			}
			// the orb only vanishes once both live birds have grabbed it (or the other is already out), so each player gets one
			const bool bothBirdsAlive = versus.alive1 && versus.alive2;
			if (!bothBirdsAlive || (versus.pickup1[i] && versus.pickup2[i]))
				world.topPipes[i].pickupCollected = true;
		}
	}
}

// shield glow + bubble while shielded, then the bird; a dead bird draws faded with its OUT label
void FlappyGame::DrawVersusBird(Bird& targetBird, SkinIndex skinIndex, bool isAlive,
	Color tint, const char* statusText, float targetShieldTime)
{
	Texture2D texture = resources.skins[EnumIndex(skinIndex)].frames[static_cast<int>(BirdFrame::MID_FLAP)];
	const float width = texture.width * BirdScale;
	const float height = texture.height * BirdScale;
	if (isAlive && targetShieldTime > 0.0f)
	{
		const float centerX = targetBird.x + 30.0f;
		const float centerY = targetBird.y + 30.0f;
		const float pulse = 0.5f + 0.5f * sinf(GetTime() * 6.0f);
		const float glowR = std::max(width, height) * 0.85f + 12.0f + pulse * 5.0f;
		const Color glowTint = Color{ 80, 180, 255, (unsigned char)(85 + pulse * 45) };
		const Rectangle glowSrc = { 0, 0, (float)bloomFlashTex.width, (float)bloomFlashTex.height };
		const Rectangle glowDst = { centerX - glowR, centerY - glowR, glowR * 2.0f, glowR * 2.0f };
		BeginBlendMode(BLEND_ADDITIVE);
		DrawTexturePro(bloomFlashTex, glowSrc, glowDst, Vector2{ 0.0f, 0.0f }, 0.0f, glowTint);
		EndBlendMode();
		const float radius = std::max(width, height) * 0.5f + 2.0f + pulse * 1.5f;
		DrawCircle((int)centerX, (int)centerY, radius, Color{ 120, 200, 255, 70 });
		DrawRing(Vector2{ centerX, centerY }, radius - 2.0f, radius, 0.0f, 360.0f, 48, Fade(SKYBLUE, 0.85f + 0.15f * pulse));
	}
	const Rectangle source = { 0, 0, (float)texture.width, (float)texture.height };
	const Rectangle destination = { targetBird.x + 30.0f, targetBird.y + 30.0f, width, height };
	const Color drawTint = isAlive && targetShieldTime > 0.0f ? Color{ 170, 220, 255, 255 } : tint;   // tint blue while shielded
	DrawTexturePro(texture, source, destination, Vector2{ width / 2, height / 2 },
		targetBird.rotation, isAlive ? drawTint : Fade(tint, 0.5f));
	if (!isAlive)
		DrawText(statusText, (int)(targetBird.x + 30 - MeasureText(statusText, 18) / 2),
			(int)(targetBird.y - 4), 18, RED);
}

void FlappyGame::UpdateVersus(const Theme& currentTheme, float frameScale)
{
	// difficulty tracks the leading score; both birds share one world speed (scaled by the time-warp factor)
	const int leadScore = std::max(versus.score1, versus.score2);
	world.speed = CurrentSpeed(leadScore, true);
	const float slowTarget = singlePlayer.slowTime > 0.0f
		? (singlePlayer.timeWarpDir > 0 ? Constants::Pickups::SpeedFactor : Constants::Pickups::SlowFactor)
		: 1.0f;
	singlePlayer.slowFactor += (slowTarget - singlePlayer.slowFactor) * std::min(1.0f, deltaTime * 3.0f);   // ease the warp factor
	world.speed *= singlePlayer.slowFactor;

	const bool player1Flap = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_W) || IsKeyPressed(storage.save.keyFlapP1);
	const bool player2Flap = IsKeyPressed(KEY_UP) || IsKeyPressed(storage.save.keyFlapP2);
	if (versus.started && (versus.alive1 || versus.alive2) &&
		(IsKeyPressed(storage.save.keyPause) || IsKeyPressed(KEY_ESCAPE)))
		versus.paused = !versus.paused;
	if (!versus.started && IsKeyPressed(KEY_ESCAPE))
	{
		// ESC before the match starts bails straight to the menu (no fade, since nothing's in progress)
		versus.paused = false;
		interfaceState.transition.active = false;
		interfaceState.transition.time = 0.0f;
		interfaceState.current = GameState::MENU;
		return;
	}

	if (versus.paused) return;
	versus.runClock += deltaTime;   // pausable clock for wind + oscillation timing
	if (!versus.started)
	{
		// pre-start "Get Ready" idle: both birds bob (offset by pi so they're out of phase), the world scrolls, and the
		// first flap from either player kicks off the match
		versus.readyTime += deltaTime;
		world.bird.y = (VIRTUAL_H / 2 - 70) + sinf(versus.readyTime * 8.0f) * 8.0f;
		world.bird2.y = (VIRTUAL_H / 2 + 10) + sinf(versus.readyTime * 8.0f + 3.14f) * 8.0f;
		world.baseScroll += world.speed * frameScale;
		if (world.baseScroll >= currentTheme.base.width) world.baseScroll -= currentTheme.base.width;
		world.skyScroll += world.speed * frameScale * 0.2f;
		world.midScroll += world.speed * frameScale * 0.4f;
		if (player1Flap || player2Flap)
		{
			versus.started = true;
			if (player1Flap) { world.bird.velocity = Constants::Bird::JumpImpulse; world.bird.rotation = Constants::Bird::Rotation::UpAngle; world.bird.sinceJump = 0; particleEmitter.Burst(world.bird.x + 14.0f, world.bird.y + 36.0f, ParticleType::Puff, 4); }
			if (player2Flap) { world.bird2.velocity = Constants::Bird::JumpImpulse; world.bird2.rotation = Constants::Bird::Rotation::UpAngle; world.bird2.sinceJump = 0; particleEmitter.Burst(world.bird2.x + 14.0f, world.bird2.y + 36.0f, ParticleType::Puff, 4); }
			resources.flapBoost = 0.18f;
			PlaySound(audio.jump);
		}
		return;
	}

	if (versus.alive1 && player1Flap)
	{
		world.bird.velocity = Constants::Bird::JumpImpulse; world.bird.rotation = Constants::Bird::Rotation::UpAngle; world.bird.sinceJump = 0;
		particleEmitter.Burst(world.bird.x + 14.0f, world.bird.y + 36.0f, ParticleType::Puff, 4);   // tail puffs, same as single-player
		resources.flapBoost = 0.18f;                                                                  // briefly speed up the flap animation
		PlaySound(audio.jump);
	}
	if (versus.alive2 && player2Flap)
	{
		world.bird2.velocity = Constants::Bird::JumpImpulse; world.bird2.rotation = Constants::Bird::Rotation::UpAngle; world.bird2.sinceJump = 0;
		particleEmitter.Burst(world.bird2.x + 14.0f, world.bird2.y + 36.0f, ParticleType::Puff, 4);
		resources.flapBoost = 0.18f;
		PlaySound(audio.jump);
	}

	StepVersusBird(world.bird, versus.alive1);
	StepVersusBird(world.bird2, versus.alive2);

	if (versus.alive1 || versus.alive2)
	{
		// recycle any pipe scrolled fully off the left edge to just past the current rightmost one, rerolling its layout/extras
		float maxX = world.topPipes[0].x;
		for (int i = 1; i < Constants::Pipes::Count; i++) maxX = std::max(maxX, world.topPipes[i].x);
		for (int i = 0; i < Constants::Pipes::Count; i++)
		{
			if (world.topPipes[i].x + Constants::Pipes::Width >= 0) continue;
			const float nextX = maxX + Constants::Pipes::Spacing;
			maxX = nextX;
			world.topPipes[i].x = nextX;
			world.bottomPipes[i].x = nextX;
			const int pipeOffset = world.topPipes[i].Random();
			world.topPipes[i].y = world.topPipes[i].defaultY + pipeOffset;
			world.bottomPipes[i].y = world.topPipes[i].y + Constants::Pipes::BodyHeight + CurrentGap(leadScore, true);
			world.topPipes[i].baseY = world.topPipes[i].y;
			world.bottomPipes[i].baseY = world.bottomPipes[i].y;
			versus.scored1[i] = versus.scored2[i] = false;
			versus.pickup1[i] = versus.pickup2[i] = false;
			world.topPipes[i].windFireTime = -1.0f;
			RollPipeExtras(i, leadScore);
		}
		for (int i = 0; i < Constants::Pipes::Count; i++)
		{
			world.topPipes[i].x -= world.speed * frameScale;
			world.bottomPipes[i].x -= world.speed * frameScale;
		}
		world.baseScroll += world.speed * frameScale;
		if (world.baseScroll >= currentTheme.base.width) world.baseScroll -= currentTheme.base.width;
		world.skyScroll += world.speed * frameScale * 0.2f;
		world.midScroll += world.speed * frameScale * 0.4f;

		// oscillating pipes swing around their baseY on the pausable run clock
		const float clock = versus.runClock;
		for (int i = 0; i < Constants::Pipes::Count; i++)
		{
			if (world.topPipes[i].variation != PipeVariation::OSCILLATE) continue;
			const float displacement = sinf(clock * Constants::Pipes::OscillationPeriod + world.topPipes[i].oscPhase) * Constants::Pipes::OscillationAmplitude;
			world.topPipes[i].y = world.topPipes[i].baseY + displacement;
			world.bottomPipes[i].y = world.bottomPipes[i].baseY + displacement;
		}

		// sum the active wind directions; the gust window starts 0.6s after a wind pipe fires and lasts to 3.6s
		float windForce = 0.0f;
		for (int i = 0; i < Constants::Pipes::Count; i++)
		{
			const Pipe& pipe = world.topPipes[i];
			if (pipe.variation == PipeVariation::WIND && pipe.x + Constants::Pipes::Width > 0 &&
				pipe.x + Constants::Pipes::Width < VIRTUAL_W && pipe.windFireTime >= 0.0f)
			{
				const float elapsed = versus.runClock - pipe.windFireTime;
				if (elapsed >= 0.6f && elapsed < 3.6f) windForce += pipe.windDir;
			}
		}
		// gust sfx once, the moment the wind starts pushing (0 -> non-zero)
		if (!versus.windActive && windForce != 0.0f) PlaySound(audio.windBurst);
		versus.windActive = (windForce != 0.0f);
		if (windForce != 0.0f)
		{
			if (versus.alive1) world.bird.x = std::clamp(world.bird.x + windForce * Constants::Pipes::WindStrength * deltaTime, 60.0f, 380.0f);
			if (versus.alive2) world.bird2.x = std::clamp(world.bird2.x + windForce * Constants::Pipes::WindStrength * deltaTime, 60.0f, 380.0f);
		}
	}

	// rebuild collision rects from the live pipe positions (the top rect is only the visible cap height)
	for (int i = 0; i < Constants::Pipes::Count; i++)
	{
		world.topCollision[i] = Rectangle{ world.topPipes[i].x, world.topPipes[i].y + Constants::Pipes::BodyHeight - currentTheme.pipe.height, Constants::Pipes::Width, (float)currentTheme.pipe.height };
		world.bottomCollision[i] = Rectangle{ world.bottomPipes[i].x, world.bottomPipes[i].y, Constants::Pipes::Width, Constants::Pipes::BodyHeight };
	}

	// tick effect timers: P1's shield + the shared time warp live in singlePlayer.*, P2's shield in versus.shieldTime2
	if (singlePlayer.shieldTime > 0.0f) singlePlayer.shieldTime -= deltaTime;
	if (versus.shieldTime2 > 0.0f) versus.shieldTime2 -= deltaTime;
	if (singlePlayer.slowTime > 0.0f)
	{
		singlePlayer.slowTime -= deltaTime;
		if (singlePlayer.slowTime <= 0.0f) singlePlayer.timeWarpDir = 0;
	}
	CheckVersusBird(world.bird, versus.alive1, versus.score1, versus.scored1, versus.pickup1, singlePlayer.shieldTime);
	CheckVersusBird(world.bird2, versus.alive2, versus.score2, versus.scored2, versus.pickup2, versus.shieldTime2);
	// retire an orb once both players have taken it, or one took it and the other is already out
	for (int i = 0; i < Constants::Pipes::Count; i++)
	{
		if (world.topPipes[i].pickup == PickupType::NONE || world.topPipes[i].pickupCollected) continue;
		if ((versus.pickup1[i] && versus.pickup2[i]) ||
			(versus.pickup1[i] && !versus.alive2) ||
			(versus.pickup2[i] && !versus.alive1))
		{
			world.topPipes[i].pickupCollected = true;
		}
	}
	if (!versus.alive1 && !versus.alive2 && IsKeyPressed(KEY_ENTER)) StartVersus();   // both out: Enter rematches
}

void FlappyGame::DrawVersusScene(const Theme& currentTheme, Vector2 virtualMouse)
{

	// night fade driven by the leading score (same crossfade as single-player)
	int vsLeadScore = (versus.score1 > versus.score2) ? versus.score1 : versus.score2;
	if (vsLeadScore >= world.nightSwitchScore)
	{
		world.nightPhase = !world.nightPhase;
		world.nightSwitchScore += GetRandomValue(Constants::Themes::NightSwitchMin, Constants::Themes::NightSwitchMax);
	}
	float nightTarget = world.nightPhase ? 1.0f : 0.0f;
	float fadeStep = deltaTime / 0.8f;
	if (world.nightAmount < nightTarget) { world.nightAmount += fadeStep; if (world.nightAmount > nightTarget) world.nightAmount = nightTarget; }
	else if (world.nightAmount > nightTarget) { world.nightAmount -= fadeStep; if (world.nightAmount < nightTarget) world.nightAmount = nightTarget; }

	DaynightShaderHandle dnHandle = MakeDaynightHandle();
	// versus advances skyScroll unwrapped, so it doubles as the moon scroll (no jump on bg wrap)
	DrawThemeSky(currentTheme, resources.ui, world.skyScroll, world.midScroll, world.nightAmount, world.skyScroll, 0.0f, &dnHandle);
	int topPipeOff = 266 - currentTheme.pipe.height;
	for (int i = 0; i < Constants::Pipes::Count; i++)
	{
		DrawTexture(currentTheme.pipe, world.topPipes[i].x, world.topPipes[i].y + topPipeOff, WHITE);
		DrawTexture(currentTheme.pipe180, world.bottomPipes[i].x, world.bottomPipes[i].y, WHITE);
		if (world.topPipes[i].variation == PipeVariation::WIND
			&& world.topPipes[i].x + 88 > 0
			&& world.topPipes[i].x + 88 < VIRTUAL_W)
		{
			// run clock (frozen while paused), not wall-clock, so the chevron sweep doesn't keep advancing during a pause
			if (world.topPipes[i].windFireTime < 0.0f) world.topPipes[i].windFireTime = versus.runClock;
			float elapsed = versus.runClock - world.topPipes[i].windFireTime;
			float dir = world.topPipes[i].windDir;
			const float sweepDur = dir > 0.0f
				? Constants::Pipes::WindBurstRightSweepSeconds
				: Constants::Pipes::WindBurstLeftSweepSeconds;
			if (elapsed >= 0.0f && elapsed < sweepDur)
			{
				int wIdx = ((int)(versus.runClock * 10.0f)) % 3;
				Texture2D wt = resources.ui.windFrames[wIdx];
				float gapMid = (world.topPipes[i].y + 266.0f + world.bottomPipes[i].y) * 0.5f;
				float wY = gapMid - wt.height * 0.5f;
				float spriteW = wt.width * 2.0f;
				float t = elapsed / sweepDur;
				float startX = dir > 0 ? -spriteW : (float)VIRTUAL_W;
				float endX   = dir > 0 ? (float)VIRTUAL_W : -spriteW;
				float spriteX = startX + (endX - startX) * t;
				Rectangle wsrc = { 0, 0, dir > 0 ? (float)wt.width : -(float)wt.width, (float)wt.height };
				Rectangle wdst = { spriteX, wY, spriteW, (float)wt.height * 2 };
				DrawTexturePro(wt, wsrc, wdst, Vector2{ 0, 0 }, 0.0f, Fade(WHITE, 0.9f));
			}
		}
	}
	DrawThemeGround(currentTheme, world.baseScroll, BaseTop);

	// uncollected orbs, bobbing in their gaps
	for (int i = 0; i < Constants::Pipes::Count; i++)
	{
		if (world.topPipes[i].pickup == PickupType::NONE) continue;
		if (world.topPipes[i].pickupCollected) continue;
		Texture2D ot = (world.topPipes[i].pickup == PickupType::SHIELD) ? resources.ui.orbShield : resources.ui.orbSlowMo;
		float s = 2.4f;
		float gapMid = (world.topPipes[i].y + 266.0f + world.bottomPipes[i].y) * 0.5f;
		float bob = sinf((float)GetTime() * 3.0f + world.topPipes[i].x * 0.01f) * 3.0f;
		Rectangle src = { 0, 0, (float)ot.width, (float)ot.height };
		Rectangle dst = { world.topPipes[i].x + 44, gapMid + bob, ot.width * s, ot.height * s };
		DrawTexturePro(ot, src, dst, Vector2{ ot.width * s / 2, ot.height * s / 2 }, 0.0f, WHITE);
	}

	DrawVersusBird(world.bird, storage.save.skinIndex, versus.alive1, Color{ 195, 220, 255, 255 }, "OUT", singlePlayer.shieldTime);
	DrawVersusBird(world.bird2, storage.save.skinIndex2, versus.alive2, Color{ 255, 195, 200, 255 }, "OUT", versus.shieldTime2);

	// particles (death feathers, time-warp arrows, score "+1" + bloom). versus has no camera offset, so positions are
	// used directly. mirrors the single-player particle pass
	for (auto& p : particles)
	{
		float a = p.life / p.maxLife;
		if (p.kind == ParticleType::Text)
		{
			float pfs = 18.0f;
			float pw = MeasureTextEx(gPixelFont, p.text, pfs, pfs / 16.0f).x;
			DrawTextEx(gPixelFont, p.text, Vector2{ p.pos.x - pw / 2, p.pos.y }, pfs, pfs / 16.0f, Fade(p.color, a));
		}
		else if (p.kind == ParticleType::BloomFlash)
		{
			float t = 1.0f - a;
			float radius = p.size * (1.0f + 3.0f * t);
			float alphaNorm = 0.6f * (a * a);
			Color tint = { p.color.r, p.color.g, p.color.b, (unsigned char)(alphaNorm * p.color.a) };
			Rectangle src = { 0, 0, (float)bloomFlashTex.width, (float)bloomFlashTex.height };
			Rectangle dst = { p.pos.x - radius, p.pos.y - radius, radius * 2.0f, radius * 2.0f };
			BeginBlendMode(BLEND_ADDITIVE);
			DrawTexturePro(bloomFlashTex, src, dst, Vector2{ 0.0f, 0.0f }, 0.0f, tint);
			EndBlendMode();
		}
		else if (p.kind == ParticleType::DownArrow || p.kind == ParticleType::UpArrow)
		{
			float s = p.size;
			Color c = Fade(p.color, a);
			const bool up = p.kind == ParticleType::UpArrow;
			float tipY = up ? p.pos.y - s : p.pos.y + s;
			float tailY = up ? p.pos.y + s : p.pos.y - s;
			float headBaseY = up ? tipY + s * 0.7f : tipY - s * 0.7f;
			DrawLineEx(Vector2{ p.pos.x, tailY }, Vector2{ p.pos.x, tipY }, 2.5f, c);
			DrawLineEx(Vector2{ p.pos.x - s * 0.6f, headBaseY }, Vector2{ p.pos.x, tipY }, 2.5f, c);
			DrawLineEx(Vector2{ p.pos.x + s * 0.6f, headBaseY }, Vector2{ p.pos.x, tipY }, 2.5f, c);
		}
		else
		{
			DrawCircle((int)p.pos.x, (int)p.pos.y, p.size * a, Fade(p.color, a));
		}
	}

	// HUD: P1 score top-left, P2 top-right, effect readouts under each, shared time-warp centered
	const Color hudTextColor = world.nightAmount >= 0.5f ? RAYWHITE : BLACK;
	const char* p1ScoreTxt = TextFormat("P1  %d", versus.score1);
	DrawText(p1ScoreTxt, 30, 24, 30, hudTextColor);
	std::string p2ScoreTxt = TextFormat("P2  %d", versus.score2);
	DrawText(p2ScoreTxt.c_str(), VIRTUAL_W - 30 - MeasureText(p2ScoreTxt.c_str(), 30), 24, 30, hudTextColor);
	int hy1 = 60, hy2 = 60;
	if (singlePlayer.shieldTime > 0.0f) { DrawText(TextFormat("Shield %.0f", singlePlayer.shieldTime), 30, hy1, 18, hudTextColor); hy1 += 22; }
	if (versus.shieldTime2 > 0.0f)
	{
		std::string shield2Txt = TextFormat("Shield %.0f", versus.shieldTime2);
		DrawText(shield2Txt.c_str(), VIRTUAL_W - 30 - MeasureText(shield2Txt.c_str(), 18), hy2, 18, hudTextColor);
		hy2 += 22;
	}
	if (singlePlayer.slowTime > 0.0f)
	{
		const bool speeding = singlePlayer.timeWarpDir > 0;
		const char* slowMsg = TextFormat(speeding ? "Time Warp + %.0f" : "Time Warp - %.0f", singlePlayer.slowTime);
		DrawText(slowMsg, VIRTUAL_W / 2 - MeasureText(slowMsg, 18) / 2, 60, 18, hudTextColor);
	}

	// overlays: win banner (both out), Get Ready (pre-start), or the running-status line
	if (!versus.alive1 && !versus.alive2)
	{
		DrawRectangle(0, 0, VIRTUAL_W, VIRTUAL_H, Color{ 0, 0, 0, 150 });
		const char* w = versus.score1 > versus.score2 ? "P1 WINS!" : (versus.score2 > versus.score1 ? "P2 WINS!" : "DRAW!");
		DrawText(w, VIRTUAL_W / 2 - MeasureText(w, 60) / 2, 200, 60, RAYWHITE);
		if (UiButton(Rectangle{ VIRTUAL_W / 2 - 175, 320, 165, 46 }, "Rematch", virtualMouse)) StartVersus();
		if (UiButton(Rectangle{ VIRTUAL_W / 2 + 10, 320, 165, 46 }, "Menu", virtualMouse))
		{
			versus.paused = false;
			GoToState(GameState::MENU);
		}
	}
	else if (!versus.started)
	{
		DrawText("Get Ready!", VIRTUAL_W / 2 - MeasureText("Get Ready!", 40) / 2, 150, 40, RAYWHITE);
		DrawText("Either player flap to start", VIRTUAL_W / 2 - MeasureText("Either player flap to start", 22) / 2, 200, 22, Fade(RAYWHITE, 0.85f));
	}
	else
	{
		DrawText("first to crash loses", VIRTUAL_W / 2 - MeasureText("first to crash loses", 18) / 2, 70, 18, Fade(hudTextColor, 0.75f));
	}

	if (versus.paused)
	{
		DrawRectangle(0, 0, VIRTUAL_W, VIRTUAL_H, Fade(BLACK, 0.55f));
		DrawText("PAUSED", VIRTUAL_W / 2 - MeasureText("PAUSED", 50) / 2, 150, 50, RAYWHITE);
		std::string vsPauseScoreTxt = TextFormat("P1 %d   |   P2 %d", versus.score1, versus.score2);
		DrawText(vsPauseScoreTxt.c_str(), VIRTUAL_W / 2 - MeasureText(vsPauseScoreTxt.c_str(), 24) / 2, 215, 24, SKYBLUE);
		float pbx = VIRTUAL_W / 2 - 110, pbw = 220;
		if (UiButton(Rectangle{ pbx, 260, pbw, 40 }, "Resume", virtualMouse)) versus.paused = false;
		if (UiButton(Rectangle{ pbx, 308, pbw, 40 }, "Restart Match", virtualMouse)) StartVersus();
		if (UiButton(Rectangle{ pbx, 356, pbw, 40 }, "Settings", virtualMouse))
		{
			interfaceState.settingsReturnTo = GameState::VS_PLAYING;
			interfaceState.settings.scroll = 0.0f;
			GoToState(GameState::SETTINGS);
		}
		if (UiButton(Rectangle{ pbx, 404, pbw, 40 }, "Quit to Menu", virtualMouse))
		{
			versus.paused = false;
			GoToState(GameState::MENU);
		}
	}
}
