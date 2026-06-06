#include "game.h"

#include "gameplay_helpers.h"
#include "net.h"
#include "pak_archive.h"
#include "progression.h"
#include "run_artifacts.h"

#include <ctime>

// construct the game: seed RNG, resolve save/screenshot paths, load all assets + shaders, then load the save and apply it
FlappyGame::FlappyGame(GamePlatform& platform)
	: screen(platform.screen),
	  audio(platform.audio),
	  particleEmitter(particles, storage.save.reduceMotion, storage.save.skinIndex)
{
	SetRandomSeed((unsigned)time(nullptr));
	world.nightSwitchScore = GetRandomValue(Constants::Themes::NightSwitchMin, Constants::Themes::NightSwitchMax);
	storage.saveDirectory = SaveDir();
	storage.savePath = storage.saveDirectory + "/save.bin";
	storage.screenshotDirectory = ScreenshotDir();
	storage.ghostPath = storage.saveDirectory + "/ghost.bin";
	storage.classicGhostPath = storage.saveDirectory + "/classic_ghost.bin";
	storage.dailyGhostPath = storage.saveDirectory + "/daily_ghost.bin";

	resources.ui = LoadUiTextures();
	resources.skins = LoadBirdSkins();
	resources.menuButtons = LoadMenuButtonTextures(resources.skins);
	resources.themes = LoadThemes();
	// pipes load lazily — only the selected gameplay pair up front (ApplySelectedPipes below). the Customize screen
	// pulls in more on demand and frees them on exit
	LoadScoreDigits(resources.scoreBig, resources.scoreSmall);
	LoadMedalTextures(resources.medals);

	// post-process shader for the day/night crossfade. if it fails to load (older GPU, missing file), DrawThemeSky's
	// call sites detect the invalid handle and fall back to the original two-DrawTexture alpha blend
	daynightShader = LoadShaderViaPak("./resources/shaders/daynight.vs", "./resources/shaders/daynight.fs");
	if (IsShaderValid(daynightShader))
	{
		daynightDayTexLoc        = GetShaderLocation(daynightShader, "dayTex");
		daynightNightTexLoc      = GetShaderLocation(daynightShader, "nightTex");
		daynightNightAmountLoc   = GetShaderLocation(daynightShader, "nightAmount");
		daynightDuskTintLoc      = GetShaderLocation(daynightShader, "duskTint");
		daynightDayOffsetLoc     = GetShaderLocation(daynightShader, "dayOffset");
		daynightNightOffsetLoc   = GetShaderLocation(daynightShader, "nightOffset");
		daynightLayerSizeLoc     = GetShaderLocation(daynightShader, "layerSize");
		daynightScreenSizeLoc    = GetShaderLocation(daynightShader, "screenSize");
	}
	// procedural radial gradient for the pickup bloom flashes — generated, so no asset file needed
	{
		Image flash = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK);
		bloomFlashTex = LoadTextureFromImage(flash);
		SetTextureFilter(bloomFlashTex, TEXTURE_FILTER_BILINEAR);
		UnloadImage(flash);
	}

	storage.fileManager.createFolders(storage.saveDirectory);
	const bool hadSave = LoadSave(storage.save, storage.savePath);

	RefreshUnlocks();
	ApplySelectedPipes();
	// bake the starting theme's full visuals up front so the first frame has them; other themes stay as lightweight
	// specs + thumbnails until selected
	UpdateActiveThemeVisuals();
	interfaceState.mainMenu.editingName = storage.save.playerName.empty();   // first launch: prompt for a name
	// saved* are the chosen levels; the live values start at 0 if launched muted, so the bars sit at the bottom
	// (UpdateVolumeAnimation eases them in/out from here)
	interfaceState.savedMusicVolume = storage.save.musicVolume / 100.0f;
	interfaceState.savedSfxVolume = storage.save.sfxVolume / 100.0f;
	interfaceState.musicVolume = storage.save.muted ? 0.0f : interfaceState.savedMusicVolume;
	interfaceState.sfxVolume = storage.save.muted ? 0.0f : interfaceState.savedSfxVolume;
	ApplyStartupDisplayMode(storage.save.resIndex, storage.save.winW, storage.save.winH);
	ApplyFps();
	RefreshTwoPlayerButton();
	if (!hadSave) WriteSave(storage.save, storage.savePath);
	if (!hadSave) interfaceState.current = GameState::TUTORIAL;   // brand-new players land in the tutorial

	particles.reserve(256);
	PlayMusicStream(audio.theme);
	ApplyVolumes();
}

FlappyGame::~FlappyGame()
{
	// final save on exit, then release every GPU handle the game owns
	CaptureWindowState(storage.save);
	WriteSave(storage.save, storage.savePath);
	if (IsShaderValid(daynightShader)) UnloadShader(daynightShader);
	if (IsTextureValid(bloomFlashTex)) UnloadTexture(bloomFlashTex);
	UnloadBirdSkins(resources.skins);
	UnloadThemes(resources.themes);
	UnloadUiTextures(resources.ui);
	UnloadMenuButtonTextures(resources.menuButtons);
	UnloadMedalTextures(resources.medals);
	UnloadScoreDigits(resources.scoreBig, resources.scoreSmall);
	UnloadPipeLibrary(resources.pipeTextures, resources.flippedPipeTextures);
}

bool FlappyGame::ShouldContinue() const
{
	return !WindowShouldClose() && !interfaceState.quitRequested;
}

DaynightShaderHandle FlappyGame::MakeDaynightHandle() const
{
	DaynightShaderHandle handle{};
	if (!IsShaderValid(daynightShader)) return handle;
	handle.shader = daynightShader;
	handle.dayTexLoc = daynightDayTexLoc;
	handle.nightTexLoc = daynightNightTexLoc;
	handle.nightAmountLoc = daynightNightAmountLoc;
	handle.duskTintLoc = daynightDuskTintLoc;
	handle.dayOffsetLoc = daynightDayOffsetLoc;
	handle.nightOffsetLoc = daynightNightOffsetLoc;
	handle.layerSizeLoc = daynightLayerSizeLoc;
	handle.screenSizeLoc = daynightScreenSizeLoc;
	// require the uniforms the shader actually needs; a loc < 0 means the shader compiled but that uniform was optimized out
	handle.valid = handle.nightTexLoc >= 0 && handle.nightAmountLoc >= 0 &&
		handle.dayOffsetLoc >= 0 && handle.nightOffsetLoc >= 0 &&
		handle.layerSizeLoc >= 0 && handle.screenSizeLoc >= 0;
	return handle;
}

void FlappyGame::ApplySelectedPipes()
{
	// the themes copy the selected pipe textures, so that cell must be resident first; EnsurePipeLoaded is a no-op if it already is
	EnsurePipeLoaded(resources.pipeTextures, resources.flippedPipeTextures,
		EnumValue(storage.save.pipeStyleIndex), EnumValue(storage.save.pipeColorIndex));
	ApplyPipeChoice(resources.themes, resources.pipeTextures, resources.flippedPipeTextures,
		storage.save.pipeStyleIndex, storage.save.pipeColorIndex);
}

void FlappyGame::EnsureCustomizeCrossLoaded()
{
	// the Customize strips render the current color across every style (one column) and the current style across every
	// color (one row); load that cross so the thumbnails have textures, while everything else stays unloaded
	const int style = EnumValue(storage.save.pipeStyleIndex);
	const int color = EnumValue(storage.save.pipeColorIndex);
	for (int s = 0; s < (int)Constants::Customization::PipeStyleCount; s++)
		EnsurePipeLoaded(resources.pipeTextures, resources.flippedPipeTextures, s, color);
	for (int c = 0; c < (int)Constants::Customization::PipeColorCount; c++)
		EnsurePipeLoaded(resources.pipeTextures, resources.flippedPipeTextures, style, c);
}

void FlappyGame::FreeUnusedPipes()
{
	// drop every pipe cell except the selected gameplay pair (which the themes reference); caps the RAM cost of browsing the pickers
	UnloadPipesExcept(resources.pipeTextures, resources.flippedPipeTextures,
		EnumValue(storage.save.pipeStyleIndex), EnumValue(storage.save.pipeColorIndex));
}

void FlappyGame::UpdateActiveThemeVisuals()
{
	const int active = EnumValue(storage.save.themeIndex);
	if (active < 0 || active >= (int)resources.themes.size()) return;
	if (active != loadedThemeIndex)
	{
		// switched themes: free the previously-resident one before baking the new
		if (loadedThemeIndex >= 0 && loadedThemeIndex < (int)resources.themes.size())
			UnloadThemeVisuals(resources.themes[loadedThemeIndex]);
		loadedThemeIndex = active;
	}
	LoadThemeVisuals(resources.themes[active]);   // idempotent: a no-op once the active theme is resident
}

void FlappyGame::RefreshTwoPlayerButton()
{
	RebuildTwoPlayerButton(resources.menuButtons, resources.skins,
		storage.save.skinIndex, storage.save.skinIndex2);
}

void FlappyGame::ApplyFps()
{
	ApplyFpsSettings(storage.save.vsync, storage.save.fpsCap);
}

void FlappyGame::RefreshUnlocks()
{
	RecomputeUnlocks(storage.save, storage.save.bestScore, (int)resources.skins.size(), (int)resources.themes.size());
}

void FlappyGame::ApplyVolumes()
{
	// pushes the LIVE slider values straight to the mixer; mute is handled by UpdateVolumeAnimation easing these toward 0,
	// so the audio fades with the bars
	ApplyAudioVolumes(audio, interfaceState.musicVolume, interfaceState.sfxVolume);
}

void FlappyGame::UpdateVolumeAnimation()
{
	// ease the live slider values toward their targets: 0 while muted, the saved (chosen) levels while unmuted.
	// drives both the on-screen bars and the audio
	const float targetMusic = storage.save.muted ? 0.0f : interfaceState.savedMusicVolume;
	const float targetSfx   = storage.save.muted ? 0.0f : interfaceState.savedSfxVolume;
	const float rate = deltaTime * 9.0f > 1.0f ? 1.0f : deltaTime * 9.0f;   // ~0.1s to settle
	bool changed = false;
	auto ease = [&](float& v, float target)
	{
		float d = v - target; if (d < 0) d = -d;
		if (d <= 0.0005f) { if (v != target) { v = target; changed = true; } return; }
		v += (target - v) * rate;
		float d2 = v - target; if (d2 < 0) d2 = -d2;
		if (d2 <= 0.005f) v = target;   // snap once close enough, so it doesn't crawl forever
		changed = true;
	};
	ease(interfaceState.musicVolume, targetMusic);
	ease(interfaceState.sfxVolume, targetSfx);
	if (changed) ApplyVolumes();
}

void FlappyGame::SaveSettings()
{
	// persist the chosen levels (saved*), not the live values — those sit at 0 while muted and would wipe the real volumes
	storage.save.musicVolume = (int)(interfaceState.savedMusicVolume * 100.0f + 0.5f);
	storage.save.sfxVolume = (int)(interfaceState.savedSfxVolume * 100.0f + 0.5f);
	WriteSave(storage.save, storage.savePath);
}

void FlappyGame::ShowToast(const std::string& message)
{
	interfaceState.toast.message = message;
	interfaceState.toast.remainingSeconds = 2.2f;
}

void FlappyGame::UnlockAchievement(Achievement which)
{
	const int bit = static_cast<int>(which);
	if (bit < 0 || static_cast<std::size_t>(bit) >= Constants::Achievements::Count) return;
	if ((storage.save.achMask >> bit) & 1ull) return;   // already earned, nothing to do
	storage.save.achMask |= 1ull << bit;
	ShowToast(std::string("Achievement: ") + Constants::Achievements::Names[bit]);
	WriteSave(storage.save, storage.savePath);   // persist immediately so an unlock can't be lost to a crash
}

void FlappyGame::GoToState(GameState target)
{
	// arm the fade transition; PrepareFrame advances it and swaps to target at the midpoint
	interfaceState.transition.active = true;
	interfaceState.transition.time = 0.0f;
	interfaceState.transition.target = target;
}

void FlappyGame::SaveGhost(int finalScore)
{
	// each mode has its own ghost file, so a Classic run's ghost never bleeds into a Normal run
	const std::string& path = singlePlayer.dailyMode ? storage.dailyGhostPath
		: (singlePlayer.endlessMode ? storage.classicGhostPath : storage.ghostPath);
	SaveGhostRun(path, ghost.recordedFlaps, storage.save, finalScore);
}

void FlappyGame::LoadGhost()
{
	const std::string& path = singlePlayer.dailyMode ? storage.dailyGhostPath
		: (singlePlayer.endlessMode ? storage.classicGhostPath : storage.ghostPath);
	ghost.valid = LoadGhostRun(path, ghost.playbackFlaps, ghost.skin, (int)resources.skins.size());
}

void FlappyGame::ResetGame()
{
	singlePlayer.score = 0;
	world.bird.Reset(world.speed);
	for (int i = 0; i < Constants::Pipes::Count; i++)
	{
		world.topPipes[i].Reset(true, true);
		world.bottomPipes[i].Reset(true, true);
		// randomize each pair's starting height so every run opens differently (same shift used on recycle). daily mode
		// still produces the SAME layout for everyone, because its RNG is seeded from the date, not the clock — so the
		// opening is randomized but shared
		world.topPipes[i].y = world.topPipes[i].defaultY + (float)world.topPipes[i].Random();
		world.bottomPipes[i].y = world.topPipes[i].y + Constants::Pipes::BodyHeight + Constants::Pipes::StartingGap;
		world.topPipes[i].baseY = world.topPipes[i].y;
		world.bottomPipes[i].baseY = world.bottomPipes[i].y;
		singlePlayer.pipeScored[i] = false;
		world.topPipes[i].variation = PipeVariation::NORMAL;
		world.topPipes[i].pickup = PickupType::NONE;
		world.topPipes[i].pickupCollected = false;
	}
	singlePlayer.offset = 0;
	singlePlayer.scoreSubmitted = false;
	singlePlayer.savedScore = -1;
	singlePlayer.crossedBest = false;
	particles.clear();
	world.cameraY = 0.0f;
	world.moonScroll = 0.0f;
	ResetSinglePlayerScrollInterpolation();
	singlePlayer.shieldTime = singlePlayer.slowTime = singlePlayer.graceTime = 0.0f;
	singlePlayer.timeWarpDir = 0;
	singlePlayer.runClock = 0.0f;
	singlePlayer.slowFactor = 1.0f;
	singlePlayer.windForce = 0.0f;
	singlePlayer.windForceSmooth = 0.0f;
	singlePlayer.windTimeLeft = 0.0f;
	world.nightSwitchScore = GetRandomValue(Constants::Themes::NightSwitchMin, Constants::Themes::NightSwitchMax);
	ghost.recordedFlaps.clear();
	ghost.playbackIndex = 0;
	ghost.y = VIRTUAL_H / 2 - 30;
	ghost.velocity = 0.0f;
	ghost.rotation = 0.0f;
	LoadGhost();   // pull in this mode's best-run ghost to race against
	singlePlayer.gameOverFade = 0.0f;
	singlePlayer.deathFlash = 0.0f;
	singlePlayer.panelSlide = 0.0f;
	singlePlayer.panelWoosh = false;
	singlePlayer.deathProcessed = false;
	singlePlayer.newBest = false;
	singlePlayer.medalRank = MedalRank::NONE;
}

void FlappyGame::StartGame()
{
	ResetGame();
	singlePlayer.alive = true;
	singlePlayer.readyTime = 0.0f;
	interfaceState.current = GameState::READY;   // every run begins in the Get Ready pose
	PlaySound(audio.start);
}

void FlappyGame::StartNormal()
{
	interfaceState.sandboxEffect = SandboxEffect::NONE;
	singlePlayer.dailyMode = false;
	singlePlayer.endlessMode = false;
	singlePlayer.lastShieldSpawn = singlePlayer.lastTimeWarpSpawn = singlePlayer.lastWindSpawn = -100.0f;
	SetRandomSeed((unsigned)time(nullptr));   // clock seed = a fresh random layout
	StartGame();
}

// Classic mode: endless, no difficulty ramp and no powerups; scored against bestClassicScore
void FlappyGame::StartEndless()
{
	interfaceState.sandboxEffect = SandboxEffect::NONE;
	singlePlayer.dailyMode = false;
	singlePlayer.endlessMode = true;
	singlePlayer.lastShieldSpawn = singlePlayer.lastTimeWarpSpawn = singlePlayer.lastWindSpawn = -100.0f;
	SetRandomSeed((unsigned)time(nullptr));
	StartGame();
}

void FlappyGame::StartDaily()
{
	interfaceState.sandboxEffect = SandboxEffect::NONE;
	singlePlayer.dailyMode = true;
	singlePlayer.endlessMode = false;
	SetRandomSeed((unsigned)TodayYMD());   // date seed = the same layout for everyone today
	StartGame();
}

void FlappyGame::StartSandbox(SandboxEffect effect, TutorialPage returnPage)
{
	interfaceState.sandboxEffect = effect;
	interfaceState.sandboxReturnPage = returnPage;
	singlePlayer.dailyMode = false;
	singlePlayer.endlessMode = false;
	SetRandomSeed((unsigned)time(nullptr));
	StartGame();
	// pre-seed the demo: put the relevant pickup/variation right where the player will meet it
	if (effect == SandboxEffect::SHIELD) world.topPipes[0].pickup = PickupType::SHIELD;
	else if (effect == SandboxEffect::TIME_WARP) world.topPipes[0].pickup = PickupType::TIME_WARP;
	else if (effect == SandboxEffect::WIND)
	{
		// first gust right away, then the furthest pipe carries the opposite gust, so there's a clear wait (and no overlap)
		// before the second wind burst
		world.topPipes[0].variation = PipeVariation::WIND;
		world.topPipes[0].windDir = 1.0f;
		world.topPipes[Constants::Pipes::Count - 1].variation = PipeVariation::WIND;
		world.topPipes[Constants::Pipes::Count - 1].windDir = -1.0f;
	}
	else if (effect == SandboxEffect::OSCILLATING_PIPES)
	{
		for (Pipe& pipe : world.topPipes) pipe.variation = PipeVariation::OSCILLATE;
	}
}

void FlappyGame::StartVersus()
{
	interfaceState.sandboxEffect = SandboxEffect::NONE;
	singlePlayer.dailyMode = false;
	SetRandomSeed((unsigned)time(nullptr));
	float resetSpeed = Constants::Pipes::ScrollSpeed;
	world.bird.Reset(resetSpeed);
	world.bird.y = VIRTUAL_H / 2 - 70;   // stagger the two birds vertically at the start
	world.bird2.Reset(resetSpeed);
	world.bird2.y = VIRTUAL_H / 2 + 10;
	for (int i = 0; i < Constants::Pipes::Count; i++)
	{
		world.topPipes[i].Reset(true, true);
		world.bottomPipes[i].Reset(true, true);
		// unique opening each match (mirrors the recycle shift); both players fly the same pipes, so it stays fair
		world.topPipes[i].y = world.topPipes[i].defaultY + (float)world.topPipes[i].Random();
		world.bottomPipes[i].y = world.topPipes[i].y + Constants::Pipes::BodyHeight + Constants::Pipes::StartingGap;
		world.topPipes[i].baseY = world.topPipes[i].y;
		world.bottomPipes[i].baseY = world.bottomPipes[i].y;
		versus.scored1[i] = versus.scored2[i] = false;
		versus.pickup1[i] = versus.pickup2[i] = false;
		world.topPipes[i].variation = PipeVariation::NORMAL;
		world.topPipes[i].pickup = PickupType::NONE;
		world.topPipes[i].pickupCollected = false;
	}
	versus.score1 = versus.score2 = 0;
	versus.alive1 = versus.alive2 = true;
	world.speed = Constants::Pipes::ScrollSpeed;
	singlePlayer.shieldTime = versus.shieldTime2 = singlePlayer.slowTime = 0.0f;
	singlePlayer.timeWarpDir = 0;
	singlePlayer.slowFactor = 1.0f;
	world.skyScroll = world.midScroll = 0.0f;
	world.cameraY = 0.0f;
	particles.clear();
	versus.started = false;
	versus.paused = false;
	versus.readyTime = 0.0f;
	versus.runClock = 0.0f;
	versus.windActive = false;
	interfaceState.current = GameState::VS_PLAYING;
	PlaySound(audio.start);
}

void FlappyGame::RestartCurrentRun()
{
	// re-launch the in-progress run in whatever mode it's currently in
	if (interfaceState.current == GameState::VS_PLAYING) { StartVersus(); return; }
	if (interfaceState.sandboxEffect != SandboxEffect::NONE)
	{
		StartSandbox(interfaceState.sandboxEffect, interfaceState.sandboxReturnPage);
		return;
	}
	if (singlePlayer.endlessMode) StartEndless();
	else if (singlePlayer.dailyMode) StartDaily();
	else StartNormal();
}
