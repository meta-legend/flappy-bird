#include "game.h"

#include "gameplay_helpers.h"
#include "net.h"

#include <algorithm>
#include <cmath>

namespace
{
	// wrap a scroll offset into [0, width) for tiling
	float WrapScroll(float value, float width)
	{
		if (width <= 0.0f) return value;
		value = std::fmod(value, width);
		return value < 0.0f ? value + width : value;
	}

	// interpolate between two wrapped offsets, taking the forward (positive) distance so a wrap doesn't lerp backwards
	float InterpolateWrappedScroll(float previous, float current, float width, float alpha)
	{
		if (width <= 0.0f) return current;
		float distance = current - previous;
		if (distance < 0.0f) distance += width;
		return WrapScroll(previous + distance * alpha, width);
	}
}

// the fixed-step scroll + interpolation machinery: the world moves in fixed 240 Hz steps (see AdvanceSinglePlayerScroll),
// and the renderer reads the interpolated previous->current positions so motion stays smooth at any frame rate

void FlappyGame::ResetSinglePlayerScrollInterpolation()
{
	// snap both the previous and rendered snapshots to the live positions (no interpolation across a reset)
	auto& scroll = singlePlayer.scroll;
	scroll.accumulator = 0.0;
	for (int i = 0; i < Constants::Pipes::Count; i++)
	{
		scroll.previousPipeX[i] = world.topPipes[i].x;
		scroll.renderedPipeX[i] = world.topPipes[i].x;
	}
	scroll.previousBaseScroll = scroll.renderedBaseScroll = world.baseScroll;
	scroll.previousSkyScroll = scroll.renderedSkyScroll = world.skyScroll;
	scroll.previousMidScroll = scroll.renderedMidScroll = world.midScroll;
}

void FlappyGame::SynchronizeSinglePlayerPipe(int pipeIndex)
{
	// after teleporting a pipe (recycle/portal), snap its snapshot to the new X so it doesn't lerp across the screen
	if (pipeIndex < 0 || pipeIndex >= Constants::Pipes::Count) return;
	singlePlayer.scroll.previousPipeX[pipeIndex] = world.topPipes[pipeIndex].x;
	singlePlayer.scroll.renderedPipeX[pipeIndex] = world.topPipes[pipeIndex].x;
}

void FlappyGame::ActivateTimeWarp(Vector2 center, float durationScale)
{
	// randomly slow or speed time; pitch the pickup sound + spawn the matching up/down arrows to telegraph which
	singlePlayer.timeWarpDir = GetRandomValue(0, 1) ? 1 : -1;
	const bool speeding = singlePlayer.timeWarpDir > 0;
	singlePlayer.slowTime = (speeding
		? Constants::Pickups::SpeedUpDuration
		: Constants::Pickups::SlowMotionDuration) * durationScale;
	SetSoundPitch(audio.pickup, speeding
		? Constants::Pickups::SpeedPickupPitch
		: Constants::Pickups::SlowPickupPitch);
	PlaySound(audio.pickup);
	particleEmitter.Burst(center.x, center.y, speeding ? ParticleType::UpArrow : ParticleType::DownArrow, 3);
}

void FlappyGame::ResetTimeWarpEffect()
{
	// clear the warp and restore normal speed + music/pickup pitch
	singlePlayer.slowTime = 0.0f;
	singlePlayer.timeWarpDir = 0;
	singlePlayer.slowFactor = 1.0f;
	SetMusicPitch(audio.theme, 1.0f);
	SetSoundPitch(audio.pickup, 1.0f);
}

void FlappyGame::MaybeCelebratePersonalBest(float x, float y)
{
	if (singlePlayer.crossedBest || interfaceState.sandboxEffect != SandboxEffect::NONE)
		return;

	const int bestForMode = singlePlayer.dailyMode ? storage.save.bestDailyScore
		: (singlePlayer.endlessMode ? storage.save.bestClassicScore : storage.save.bestScore);
	// only celebrate against a real prior best — don't fire at score 1 on a fresh save
	if (bestForMode <= 0 || singlePlayer.score <= bestForMode) return;

	singlePlayer.crossedBest = true;   // once per run
	particleEmitter.Burst(x, y, ParticleType::Spark, 20);
	particleEmitter.BloomFlash(x, y, Color{ 255, 170, 55, 150 }, 52.0f);
	particleEmitter.Text(x, y - 30.0f, "BEST", Color{ 255, 220, 80, 255 });
}

void FlappyGame::AdvanceSinglePlayerScroll(const Theme& currentTheme, float effectiveSpeed,
	bool movePipes, float parallaxScale)
{
	auto& scroll = singlePlayer.scroll;
	const double fixedStep = Constants::Motion::FixedScrollStep;   // 1/240 s
	scroll.accumulator += deltaTime;

	// run as many whole fixed steps as fit in the accumulated time; snapshot the "previous" positions before each
	while (scroll.accumulator >= fixedStep)
	{
		for (int i = 0; i < Constants::Pipes::Count; i++)
			scroll.previousPipeX[i] = world.topPipes[i].x;
		scroll.previousBaseScroll = world.baseScroll;
		scroll.previousSkyScroll = world.skyScroll;
		scroll.previousMidScroll = world.midScroll;

		const float distance = effectiveSpeed * 144.0f * static_cast<float>(fixedStep);   // px moved this fixed step
		if (movePipes)
		{
			for (int i = 0; i < Constants::Pipes::Count; i++)
			{
				world.topPipes[i].x -= distance;
				world.bottomPipes[i].x = world.topPipes[i].x;
			}
		}
		world.baseScroll = WrapScroll(world.baseScroll + distance, static_cast<float>(currentTheme.base.width));
		world.skyScroll = WrapScroll(world.skyScroll + distance * 0.2f * parallaxScale,
			static_cast<float>(currentTheme.bg.width));
		// the moon rides the same parallax rate but wraps at its own full-screen travel period, so it crosses smoothly
		// instead of jumping when skyScroll wraps the (smaller) bg tile
		world.moonScroll = WrapScroll(world.moonScroll + distance * 0.2f * parallaxScale,
			static_cast<float>(VIRTUAL_W + resources.ui.moon.width));
		if (currentTheme.hasMid)
			world.midScroll = WrapScroll(world.midScroll + distance * 0.4f * parallaxScale,
				static_cast<float>(currentTheme.mid.width));

		scroll.accumulator -= fixedStep;
	}

	// interpolate the render positions between the last two fixed steps by the leftover-accumulator fraction
	const float alpha = static_cast<float>(scroll.accumulator / fixedStep);
	for (int i = 0; i < Constants::Pipes::Count; i++)
	{
		scroll.renderedPipeX[i] = movePipes
			? scroll.previousPipeX[i] + (world.topPipes[i].x - scroll.previousPipeX[i]) * alpha
			: world.topPipes[i].x;
	}
	scroll.renderedBaseScroll = InterpolateWrappedScroll(scroll.previousBaseScroll, world.baseScroll,
		static_cast<float>(currentTheme.base.width), alpha);
	scroll.renderedSkyScroll = InterpolateWrappedScroll(scroll.previousSkyScroll, world.skyScroll,
		static_cast<float>(currentTheme.bg.width), alpha);
	scroll.renderedMidScroll = currentTheme.hasMid
		? InterpolateWrappedScroll(scroll.previousMidScroll, world.midScroll,
			static_cast<float>(currentTheme.mid.width), alpha)
		: world.midScroll;
}

void FlappyGame::UpdateSinglePlayer(const Theme& currentTheme, float frameScale)
{
	(void)frameScale;   // single-player uses fixed-step motion, not the per-frame scale
	if (interfaceState.current == GameState::READY)
	{
		// gentle hover bob — same idea as the menu splash cycle, just quicker
		singlePlayer.readyTime += deltaTime;
		world.bird.y = (VIRTUAL_H / 2 - 30) + sinf(singlePlayer.readyTime * 8.0f) * 8.0f;
		world.bird.rotation = 0.0f;

		// the ground + parallax keep scrolling while the player gets ready (pipes held still)
		AdvanceSinglePlayerScroll(currentTheme, world.speed, false, 1.0f);

		// the first hop launches the run and sets the pipes moving in from the right
		if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsKeyPressed(storage.save.keyFlapP1) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
		{
			const bool realRun = interfaceState.sandboxEffect == SandboxEffect::NONE;   // sandbox runs don't touch lifetime stats
			world.bird.velocity = Constants::Bird::JumpImpulse;
			world.bird.rotation = Constants::Bird::Rotation::UpAngle;
			world.bird.sinceJump = 0;
			resources.flapBoost = 0.18f;
			PlaySound(audio.jump);
			particleEmitter.Burst(world.bird.x + 14.0f, world.bird.y + 36.0f, ParticleType::Puff, 4);
			if (realRun)
			{
				storage.save.totalFlaps++;
				storage.save.totalGames++;
			}
			ghost.recordedFlaps.push_back(0.0f);   // record the launch flap at t=0
			interfaceState.current = GameState::PLAYING;
		}
	}
	else if (interfaceState.current == GameState::PLAYING)
	{
		// scoring is per-pipe (handled in the pipe loop below); on death do the high-score save, leaderboard submit, and restart handling
		if (!singlePlayer.alive)
		{
			// run once at the moment of death: die sfx, work out the medal + new best, then save/submit the score
			if (!singlePlayer.deathProcessed)
			{
				singlePlayer.deathProcessed = true;
				PlaySound(audio.die);
				ResetTimeWarpEffect();
				int finalScore = singlePlayer.savedScore < 0 ? 0 : singlePlayer.savedScore;
				const bool realRun = interfaceState.sandboxEffect == SandboxEffect::NONE;
				const bool normalRun = realRun && !singlePlayer.dailyMode && !singlePlayer.endlessMode;
				int& bestRef = singlePlayer.dailyMode ? storage.save.bestDailyScore
					: (singlePlayer.endlessMode ? storage.save.bestClassicScore : storage.save.bestScore);
				singlePlayer.newBest = realRun && finalScore > bestRef;
				singlePlayer.medalRank = MedalRank::NONE;
				for (std::size_t m = 0; normalRun && m < Constants::Medals::Count; m++)
				{
					if (finalScore >= Constants::Medals::Thresholds[m]) singlePlayer.medalRank = static_cast<MedalRank>(m);   // highest threshold reached wins
				}
				if (realRun)
				{
					if (singlePlayer.newBest)
					{
						bestRef = finalScore;
						SaveGhost(finalScore);   // the new best run becomes the ghost to race next time
					}
					// lifetime death stats (per-skin uses bit 0 of the skin index as a cheap 2-bucket key)
					storage.save.totalDeaths++;
					storage.save.deathsPerSkin[EnumValue(storage.save.skinIndex) & 1]++;
					// daily challenge: lock out further attempts today
					if (singlePlayer.dailyMode)
					{
						storage.save.lastDailyDate = TodayYMD();
						storage.save.lastDailyScore = finalScore;
						storage.save.dailyCount++;
					}
					RefreshUnlocks();
					// achievement checks against the freshly-updated stats
					if (storage.save.totalPipes >= 1)            UnlockAchievement(Achievement::FirstFlight);
					if (storage.save.totalPipes >= 100)          UnlockAchievement(Achievement::Centurion);
					if (storage.save.totalPipes >= 1000)         UnlockAchievement(Achievement::Marathon);
					if (storage.save.totalDeaths >= 100)         UnlockAchievement(Achievement::Persistent);
					if (storage.save.totalFlaps >= 10000)        UnlockAchievement(Achievement::FlapMaster);
					if (finalScore >= 50)                        UnlockAchievement(world.nightAmount > 0.5f ? Achievement::NightOwl : Achievement::Daylight);
					if (finalScore >= 30 && !storage.save.showGhost) UnlockAchievement(Achievement::Untouchable);
					if (storage.save.bestScore >= 50)            UnlockAchievement(Achievement::TrophyCabinet);
					if (storage.save.dailyCount >= 7)            UnlockAchievement(Achievement::DailyDevotee);
					if (finalScore >= 20 && singlePlayer.runClock < 30.0f) UnlockAchievement(Achievement::Speedrunner);
					if (storage.save.deathsPerSkin[0] >= 10 && storage.save.deathsPerSkin[1] >= 10) UnlockAchievement(Achievement::StyleSwitch);
					WriteSave(storage.save, storage.savePath);
					if (!singlePlayer.scoreSubmitted && singlePlayer.savedScore >= 0)
					{
						try { SubmitScore(storage.save.playerName, finalScore); }
						catch (...) {}
						singlePlayer.scoreSubmitted = true;
					}
				}
			}

			// death animation sequence: white flash decays -> gameover fades in -> score panel slides up
			if (singlePlayer.deathFlash > 0.0f)
				singlePlayer.deathFlash -= deltaTime / Constants::Animation::DeathFlashDuration;
			else if (singlePlayer.gameOverFade < 1.0f)
			{
				singlePlayer.gameOverFade += deltaTime / Constants::Animation::GameOverFadeDuration;
				if (singlePlayer.gameOverFade > 1.0f) singlePlayer.gameOverFade = 1.0f;
			}
			else if (singlePlayer.panelSlide < 1.0f)
			{
				if (!singlePlayer.panelWoosh) { PlaySound(audio.start); singlePlayer.panelWoosh = true; }   // swoosh as the panel flies in
				singlePlayer.panelSlide += deltaTime / Constants::Animation::PanelSlideDuration;
				if (singlePlayer.panelSlide > 1.0f) singlePlayer.panelSlide = 1.0f;
			}

			// after the panel lands, Enter mirrors Try Again for replayable modes
			if (singlePlayer.panelSlide >= 1.0f && IsKeyPressed(KEY_ENTER) && !singlePlayer.dailyMode)
				RestartCurrentRun();
		}

		// run clock + active power-up timers
		if (singlePlayer.alive)
		{
			singlePlayer.runClock += deltaTime;
			if (singlePlayer.shieldTime > 0.0f) singlePlayer.shieldTime -= deltaTime;
			if (singlePlayer.slowTime > 0.0f)
			{
				singlePlayer.slowTime -= deltaTime;
				if (singlePlayer.slowTime <= 0.0f) singlePlayer.timeWarpDir = 0;
			}
		}
		// world speed ramps with the score (flat in daily/classic); the time warp bends it
		world.speed = singlePlayer.alive
			? CurrentSpeed(singlePlayer.score, !singlePlayer.dailyMode && !singlePlayer.endlessMode)
			: Constants::Pipes::ScrollSpeed;
		float slowTarget = singlePlayer.slowTime > 0.0f
				? (singlePlayer.timeWarpDir > 0 ? Constants::Pickups::SpeedFactor : Constants::Pickups::SlowFactor)
				: 1.0f;
		singlePlayer.slowFactor += (slowTarget - singlePlayer.slowFactor) * std::min(1.0f, deltaTime * 3.0f);   // ~0.3-0.5s ease in/out
		float effSpeed = world.speed * singlePlayer.slowFactor;

		// each tap gives an upward hop; gravity does the rest
		bool flap1 = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsKeyPressed(storage.save.keyFlapP1) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
		if (singlePlayer.alive && flap1)
		{
			const bool realRun = interfaceState.sandboxEffect == SandboxEffect::NONE;
			world.bird.velocity = Constants::Bird::JumpImpulse;
			world.bird.rotation = Constants::Bird::Rotation::UpAngle;
			world.bird.sinceJump = 0;
			resources.flapBoost = 0.18f;   // flap faster for a moment after each hop
			PlaySound(audio.jump);
			particleEmitter.Burst(world.bird.x + 14.0f, world.bird.y + 36.0f, ParticleType::Puff, 4);   // tail puffs
			if (realRun) storage.save.totalFlaps++;
			ghost.recordedFlaps.push_back(singlePlayer.runClock);   // log this flap's timestamp for the ghost
		}
		world.bird.velocity += Constants::Bird::Gravity * deltaTime;
		world.bird.y += world.bird.velocity * deltaTime;

		// advance the ghost replay on the same physics, flapping at the recorded times
		if (singlePlayer.alive && ghost.valid && storage.save.showGhost)
		{
			while (ghost.playbackIndex < ghost.playbackFlaps.size() && singlePlayer.runClock >= ghost.playbackFlaps[ghost.playbackIndex])
			{
				ghost.velocity = Constants::Bird::JumpImpulse; ghost.rotation = Constants::Bird::Rotation::UpAngle; ghost.playbackIndex++;
			}
			ghost.velocity += Constants::Bird::Gravity * deltaTime;
			ghost.y += ghost.velocity * deltaTime;
			// same ceiling clamp the live bird gets. without it, a run recorded by spamming flaps against the ceiling
			// (where the bird is pinned at y=0 but the flap *times* still get recorded) replays uncapped and the ghost
			// flies off the top of the screen
			if (ghost.y < 0.0f) { ghost.y = 0.0f; ghost.velocity = 0.0f; }
			if (ghost.rotation < Constants::Bird::Rotation::DownAngle) ghost.rotation += Constants::Bird::Rotation::Rate * 0.5 * deltaTime;
		}

		// wind variation nudges the bird sideways while a wind pipe is on screen; otherwise it eases back to its home
		// column. the first ~0.6s after a wind pipe spawns is a warmup so the player sees the burst coming before the
		// force actually kicks in
		const bool windWasActive = singlePlayer.windForce != 0.0f;
		float windForce = 0.0f;
		float windTimeLeft = 0.0f;
		const float kWindWarmup = 0.6f;
		const float kWindDuration = 4.8f;   // active-force window after the warmup (capped by on-screen time)
		for (int i = 0; i < Constants::Pipes::Count; i++)
		{
			if (world.topPipes[i].variation == PipeVariation::WIND
				&& world.topPipes[i].x + 88 > 0
				&& world.topPipes[i].x + 88 < VIRTUAL_W
				&& world.topPipes[i].windFireTime >= 0.0f)
			{
				float elapsed = singlePlayer.runClock - world.topPipes[i].windFireTime;
				if (elapsed >= kWindWarmup && elapsed < kWindDuration)
				{
					windForce += world.topPipes[i].windDir;
					float remaining = kWindDuration - elapsed;
					if (remaining > windTimeLeft) windTimeLeft = remaining;
				}
			}
		}
		// gust sfx once, the moment the wind actually starts pushing (force 0 -> non-zero after the warmup)
		if (!windWasActive && windForce != 0.0f) PlaySound(audio.windBurst);
		singlePlayer.windForce = windForce;
		singlePlayer.windTimeLeft = windTimeLeft;
		// ease the APPLIED force toward the raw target so the gust ramps in/out gradually instead of snapping to full
		// strength (and back) the instant a wind pipe enters/leaves the screen
		singlePlayer.windForceSmooth += (windForce - singlePlayer.windForceSmooth) * std::min(1.0f, deltaTime * 2.5f);
		if (windForce == 0.0f && std::fabs(singlePlayer.windForceSmooth) < 0.02f) singlePlayer.windForceSmooth = 0.0f;
		world.bird.x += singlePlayer.windForceSmooth * Constants::Pipes::WindStrength * deltaTime;
		// drift back to the home column once there's no active gust (the eased force is still decaying, so the two blend into a smooth return)
		if (windForce == 0.0f)
			world.bird.x += (Constants::Bird::HomeX - world.bird.x) * std::min(1.0f, deltaTime * 2.0f);
		if (world.bird.x < Constants::Bird::WindMinX) world.bird.x = Constants::Bird::WindMinX;
		if (world.bird.x > Constants::Bird::WindMaxX) world.bird.x = Constants::Bird::WindMaxX;

		// hold the up tilt briefly, then rotate down toward a nose dive
		world.bird.sinceJump += deltaTime;
		if (world.bird.sinceJump >= Constants::Bird::Rotation::HoldSeconds)
		{
			world.bird.rotation += Constants::Bird::Rotation::Rate * deltaTime;
			if (world.bird.rotation > Constants::Bird::Rotation::DownAngle) world.bird.rotation = Constants::Bird::Rotation::DownAngle;
		}

		if (singlePlayer.graceTime > 0.0f) singlePlayer.graceTime -= deltaTime;   // i-frames after a shield/portal save

		// keep the bird below the ceiling
		if (world.bird.y < 0) { world.bird.y = 0; world.bird.velocity = 0; }
		// the bird dies when its visual bottom reaches the brown ground line (the base texture's top sits exactly at BaseTop), then rests there
		if (world.bird.y + 30.0f + BirdHalfHeight >= BaseTop && singlePlayer.alive && !singlePlayer.noClip)
		{
			if (singlePlayer.shieldTime > 0.0f)
			{
				// shield consumed; reset rotation + sinceJump too, so the bird pops up in flap-pose instead of holding the nose-down dive
				singlePlayer.shieldTime = 0.0f; singlePlayer.graceTime = Constants::Pickups::ShieldGrace;
				world.bird.velocity = Constants::Bird::JumpImpulse;
				world.bird.rotation = Constants::Bird::Rotation::UpAngle;
				world.bird.sinceJump = 0.0f;
				PlaySound(audio.hit);
				particleEmitter.Burst(world.bird.x + 30.0f, world.bird.y + 30.0f, ParticleType::Spark, 8);
			}
			else if (singlePlayer.graceTime > 0.0f)
			{
				// still inside the i-frame window from a shield/portal save — bounce instead of die
				world.bird.velocity = Constants::Bird::JumpImpulse;
				if (world.bird.y + 30.0f + BirdHalfHeight > BaseTop) world.bird.y = BaseTop - 30.0f - BirdHalfHeight;
			}
			else
			{
				world.bird.y = BaseTop - 30.0f - BirdHalfHeight;
				PlaySound(audio.hit);
				singlePlayer.alive = false;
				ResetTimeWarpEffect();
				singlePlayer.shakeTimer = Constants::Animation::DeathShakeDuration;
				singlePlayer.deathFlash = 1.0f;
				particleEmitter.Burst(world.bird.x + 30.0f, world.bird.y + 30.0f, ParticleType::Feather, 12);
			}
		}

		// recycle a pipe only once it's fully off the left edge: move it to the right of the current rightmost pipe with a fresh random gap height
		float maxX = world.topPipes[0].x;
		for (int i = 1; i < Constants::Pipes::Count; i++) if (world.topPipes[i].x > maxX) maxX = world.topPipes[i].x;
		for (int i = 0; i < Constants::Pipes::Count; i++)
		{
			if (world.topPipes[i].x + 88 < 0)
			{
				float nx = maxX + Constants::Pipes::Spacing;
				maxX = nx;
				world.topPipes[i].x = nx; world.bottomPipes[i].x = nx;
				SynchronizeSinglePlayerPipe(i);   // snap the interpolation snapshot to the teleported X
				// shift the top pipe by a fresh random amount, then place the bottom a dynamic gap below it (gap narrows as the score climbs)
				singlePlayer.offset = world.topPipes[i].Random();
				world.topPipes[i].y = world.topPipes[i].defaultY + singlePlayer.offset;
				float gap = CurrentGap(singlePlayer.score, !singlePlayer.dailyMode && !singlePlayer.endlessMode);
				world.bottomPipes[i].y = world.topPipes[i].y + 266.0f + gap;
				world.topPipes[i].baseY = world.topPipes[i].y;
				world.bottomPipes[i].baseY = world.bottomPipes[i].y;
				singlePlayer.pipeScored[i] = false;
				RollPipeExtras(i, singlePlayer.score);   // roll a variation + pickup for this pair
				world.topPipes[i].windFireTime = -1.0f;
			}
		}

		// oscillating pairs swing their whole gap around the spawn position
		for (int i = 0; i < Constants::Pipes::Count; i++)
			if (world.topPipes[i].variation == PipeVariation::OSCILLATE)
			{
				float d = sinf(singlePlayer.runClock * Constants::Pipes::OscillationPeriod + world.topPipes[i].oscPhase) * Constants::Pipes::OscillationAmplitude;
				world.topPipes[i].y = world.topPipes[i].baseY + d;
				world.bottomPipes[i].y = world.bottomPipes[i].baseY + d;
			}

		// rebuild collision shapes (pipes are rects, the bird is a circle); the top rect is only the visible cap height
		Vector2 birdCenter = { world.bird.x + 30.0f, world.bird.y + 30.0f };
		for (int i = 0; i < Constants::Pipes::Count; i++)
		{
			world.topCollision[i] = Rectangle{ world.topPipes[i].x, world.topPipes[i].y + 266 - currentTheme.pipe.height, 88, (float)currentTheme.pipe.height };
			world.bottomCollision[i] = Rectangle{ world.bottomPipes[i].x, world.bottomPipes[i].y, 88, 266 };
		}

		// pipe collisions (a live shield absorbs one hit instead of ending the run; grace window + noclip skip the check)
		if (singlePlayer.alive && singlePlayer.graceTime <= 0.0f && !singlePlayer.noClip)
			for (int i = 0; i < Constants::Pipes::Count; i++)
			{
				const bool hitTopPipe = CheckCollisionCircleRec(birdCenter, BirdRadius, world.topCollision[i]);
				const bool hitBottomPipe = CheckCollisionCircleRec(birdCenter, BirdRadius, world.bottomCollision[i]);
				if (hitTopPipe || hitBottomPipe)
				{
					if (singlePlayer.shieldTime > 0.0f)
					{
						// shield bounce: down off a top pipe, up off a bottom one, then nudge clear so it doesn't re-collide
						const bool pushDown = hitTopPipe && !hitBottomPipe;
						singlePlayer.shieldTime = 0.0f; singlePlayer.graceTime = Constants::Pickups::ShieldGrace;
						if (pushDown)
						{
							world.bird.velocity = -Constants::Bird::JumpImpulse * Constants::Pickups::TopPipeShieldPushFactor;
						}
						else
						{
							world.bird.velocity = Constants::Bird::JumpImpulse;
							world.bird.rotation = Constants::Bird::Rotation::UpAngle;
							world.bird.sinceJump = 0.0f;
						}
						if (pushDown)
						{
							const float minY = world.topCollision[i].y + world.topCollision[i].height + BirdRadius - 30.0f;
							if (world.bird.y < minY) world.bird.y = minY;
						}
						else if (hitBottomPipe)
						{
							const float maxY = world.bottomCollision[i].y - BirdRadius - 30.0f;
							if (world.bird.y > maxY) world.bird.y = maxY;
						}
						PlaySound(audio.hit);
						particleEmitter.Burst(birdCenter.x, birdCenter.y, ParticleType::Spark, 8);
					}
					else
					{
						PlaySound(audio.hit);
						singlePlayer.alive = false;
						ResetTimeWarpEffect();
						singlePlayer.shakeTimer = Constants::Animation::DeathShakeDuration;
						singlePlayer.deathFlash = 1.0f;   // white flash, then the gameover + panel sequence
						particleEmitter.Burst(birdCenter.x, birdCenter.y, ParticleType::Spark, 8);
						particleEmitter.Burst(birdCenter.x, birdCenter.y, ParticleType::Feather, 12);
					}
					break;
				}
			}

		// pickup collection: a circle in the center of a pair's gap
		if (singlePlayer.alive)
			for (int i = 0; i < Constants::Pipes::Count; i++)
				if (world.topPipes[i].pickup != PickupType::NONE && !world.topPipes[i].pickupCollected)
				{
					Vector2 pc = { world.topPipes[i].x + 44.0f, (world.topPipes[i].y + 266.0f + world.bottomPipes[i].y) * 0.5f };
					if (CheckCollisionCircleRec(pc, Constants::Pickups::Radius, Rectangle{ birdCenter.x - BirdRadius, birdCenter.y - BirdRadius, BirdRadius * 2, BirdRadius * 2 }))
					{
						world.topPipes[i].pickupCollected = true;
						if (world.topPipes[i].pickup == PickupType::SHIELD)
						{
							singlePlayer.shieldTime += Constants::Pickups::ShieldDuration;
							SetSoundPitch(audio.pickup, Constants::Pickups::ShieldPickupPitch);
							PlaySound(audio.pickup);
							particleEmitter.BloomFlash(birdCenter.x, birdCenter.y, Color{ 80, 180, 255, (unsigned char)Constants::Effects::ShieldBloomStrength }, 64.0f);
						}
						else
						{
							ActivateTimeWarp(pc);
						}
					}
				}

		// portal pairs warp the bird to the next pipe's gap when entered
		if (singlePlayer.alive)
			for (int i = 0; i < Constants::Pipes::Count; i++)
				if (!singlePlayer.noClip && world.topPipes[i].variation == PipeVariation::PORTAL && !world.topPipes[i].pickupCollected &&
					birdCenter.x > world.topPipes[i].x && birdCenter.x < world.topPipes[i].x + Constants::Pipes::Width)
				{
					int nxt = -1; float best = 1e9f;
					for (int j = 0; j < Constants::Pipes::Count; j++)
						if (world.topPipes[j].x > world.topPipes[i].x && world.topPipes[j].x < best) { best = world.topPipes[j].x; nxt = j; }
					if (nxt >= 0)
					{
						// warp: snap the *next* pipe to where the current pipe was so the bird lands inside an actual gap,
						// push the current (now spent) pipe behind, count it scored, and grace-window the collision so the
						// warp itself can't kill you on the same frame
						world.topPipes[i].pickupCollected = true;
						float warpX = world.topPipes[i].x;
						world.topPipes[i].x = -300.0f; world.bottomPipes[i].x = -300.0f; singlePlayer.pipeScored[i] = true;
						world.topPipes[nxt].x = warpX; world.bottomPipes[nxt].x = warpX;
						SynchronizeSinglePlayerPipe(i);
						SynchronizeSinglePlayerPipe(nxt);
						world.bird.y = (world.topPipes[nxt].y + Constants::Pipes::BodyHeight + world.bottomPipes[nxt].y) * 0.5f - 30.0f;
						world.bird.velocity = 0.0f;
						singlePlayer.graceTime = Constants::Pickups::ShieldGrace;
						particleEmitter.Burst(birdCenter.x, birdCenter.y, ParticleType::Spark, 12);
					}
				}

		// run the fixed-step scroll (moving the pipes), then award a point for each pair the bird passes
		if (singlePlayer.alive)
		{
			float parScale = storage.save.reduceMotion ? 0.5f : 1.0f;   // reduce-motion dampens the parallax
			AdvanceSinglePlayerScroll(currentTheme, effSpeed, true, parScale);
			for (int i = 0; i < Constants::Pipes::Count; i++)
			{
				// award the point when the bird's center crosses the pair's vertical centerline (bird exactly between the
				// two pipes), not after it has fully cleared the far edge
				if (!singlePlayer.pipeScored[i] &&
					world.bird.x + 30.0f >= world.topPipes[i].x + Constants::Pipes::Width * 0.5f)
				{
					singlePlayer.score++;
					if (interfaceState.sandboxEffect == SandboxEffect::NONE)
					{
						storage.save.totalPipes++;
						storage.save.pipesPerSkin[EnumValue(storage.save.skinIndex) & 1]++;
					}
					singlePlayer.pipeScored[i] = true;
					PlaySound(audio.score);
					const float plusOneX = world.topPipes[i].x + 30.0f;
					const float plusOneY = world.bird.y - 30.0f;
					// cream-yellow bloom centered on the "+1"; low alpha keeps it a quiet halo, not a flashbang
					particleEmitter.BloomFlash(plusOneX, plusOneY,
						Color{ 255, 240, 180, (unsigned char)Constants::Effects::ScoreBloomStrength }, 28.0f);
					particleEmitter.Text(plusOneX, plusOneY, "+1", WHITE);
					MaybeCelebratePersonalBest(plusOneX, plusOneY);
				}
			}
		}

		// camera follow is disabled (felt off); cameraY stays 0 so the world draws straight and parallax is unaffected

		// in a tutorial sandbox, cap the run at a few points and pop back to the matching tutorial page so the player can move on
		int sandboxCap = interfaceState.sandboxEffect == SandboxEffect::TIME_WARP ? 6
			: interfaceState.sandboxEffect == SandboxEffect::WIND ? 7 : 6;
		if (interfaceState.sandboxEffect != SandboxEffect::NONE && singlePlayer.score >= sandboxCap && singlePlayer.alive)
		{
			singlePlayer.alive = false;
			interfaceState.tutorialPage = interfaceState.sandboxReturnPage;
			interfaceState.sandboxEffect = SandboxEffect::NONE;
			interfaceState.current = GameState::TUTORIAL;
		}
		if (!singlePlayer.alive) world.speed = Constants::Pipes::ScrollSpeed;
		if (singlePlayer.alive) singlePlayer.savedScore = singlePlayer.score;   // keep the last live score for the death panel
	}
}
