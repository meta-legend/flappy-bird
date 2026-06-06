// Flappy Bird frame orchestration and gameplay updates
#include "game.h"
#include "net.h"
#include "font.h"
#include "screen_main_menu.h"
#include "particles.h"
#include "render_menus.h"
#include "screen_secondary.h"
#include "screen_tutorial.h"
#include "ui.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace
{
	std::string TrimAsciiWhitespace(std::string text)
	{
		auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
		text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
		text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
		return text;
	}
}

// one frame = prepare state -> read input -> update -> draw
void FlappyGame::RunFrame()
{
	PrepareFrame();
	ProcessInput();
	Update();
	Draw();
}

void FlappyGame::PrepareFrame()
{
	if (ReconcileStartupDisplay(interfaceState.startupDisplay, storage.save)) CaptureWindowState(storage.save);
	// deferred rebake from a customize-screen skin change last frame. doing it here, before BeginTextureMode(screen),
	// keeps the BeginTextureMode inside RefreshTwoPlayerButton from nesting and flashing the screen
	if (interfaceState.twoPlayerButtonNeedsRefresh)
	{
		interfaceState.twoPlayerButtonNeedsRefresh = false;
		RefreshTwoPlayerButton();
	}
	// time warp bends the music with the gameplay. slowFactor is already eased, so the pitch ramps smoothly; clamp it
	// so full slow/speed stays readable
	const bool inRun = interfaceState.current == GameState::PLAYING || interfaceState.current == GameState::VS_PLAYING;
	const float musicPitch = inRun
		? std::clamp(singlePlayer.slowFactor, Constants::Pickups::SlowMusicPitchFactor, Constants::Pickups::SpeedMusicPitchFactor)
		: 1.0f;
	SetMusicPitch(audio.theme, musicPitch);
	UpdateMusicStream(audio.theme);   // keep the streaming music buffer fed each frame

	deltaTime = GetFrameTime();
	// clamp the per-frame delta so a single long hitch (alt-tab, a stalled vblank, the OS scheduler napping) can't
	// teleport the world forward in one step. 1/20s is well above any healthy frame time, so normal play is unaffected;
	// only pathological spikes get capped
	if (deltaTime > 0.05f) deltaTime = 0.05f;
	// menu + versus motion use raw elapsed time; single-player horizontal motion is fixed-step and interpolated in
	// AdvanceSinglePlayerScroll(). frame.scale normalizes "per frame" amounts to a 144 fps baseline
	frame.scale = deltaTime * 144.0f;

	// present transform, also used to map the mouse into virtual coords for the ui
	frame.destination = ComputePresentationRect();
	frame.virtualMouse = WindowToVirtualMouse(frame.destination);

	UpdateCustomizeAnimations(interfaceState.customize, deltaTime);   // decay the customize click-bounce timers in any state

	// guard the active theme/skin indices against OOB before anything reads them
	if (!EnumInRange(storage.save.themeIndex, resources.themes.size())) storage.save.themeIndex = ThemeIndex::CLASSIC;
	if (!EnumInRange(storage.save.skinIndex, resources.skins.size())) storage.save.skinIndex = SkinIndex::YELLOW_BIRD;
	// keep only the active theme's heavy bg/mid textures resident; runs before any backdrop draw (which is in Draw)
	UpdateActiveThemeVisuals();

	// drive the menu fade: dip to black, swap state at the midpoint, and swallow mouse input so nothing is clicked mid-fade
	StateTransitionTracker& transition = interfaceState.transition;
	if (transition.active)
	{
		frame.virtualMouse = { -9999.0f, -9999.0f };
		transition.time += deltaTime / 0.24f;
		if (transition.time >= 0.5f) interfaceState.current = transition.target;
		if (transition.time >= 1.0f) { transition.active = false; transition.time = 0.0f; }
	}

	// lazy pipe-preview textures for the Customize pickers: load the visible cross while the screen is open, free the
	// extras once on leaving so browsing doesn't permanently inflate RAM. runs after the transition swap so it sees the
	// new `current`, and before BeginTextureMode so any texture upload happens outside the screen render target
	if (interfaceState.current == GameState::CUSTOMIZE)
	{
		EnsureCustomizeCrossLoaded();
		customizePipesLoaded = true;
	}
	else if (customizePipesLoaded)
	{
		FreeUnusedPipes();
		customizePipesLoaded = false;
	}

	// tick the toast timer + lifetime-playtime autosave
	if (interfaceState.toast.remainingSeconds > 0.0f) interfaceState.toast.remainingSeconds -= deltaTime;
	FlushScreenshotToasts();
	if (interfaceState.current == GameState::PLAYING || interfaceState.current == GameState::VS_PLAYING)
	{
		storage.save.playtimeSeconds += deltaTime;
		interfaceState.saveTimer += deltaTime;
		if (interfaceState.saveTimer >= 30.0f) { interfaceState.saveTimer = 0.0f; CaptureWindowState(storage.save); WriteSave(storage.save, storage.savePath); }   // autosave every 30s of play
	}
}

void FlappyGame::ProcessInput()
{
	if (IsKeyPressed(KEY_F11))   // F11 toggles borderless fullscreen anywhere
	{
		ToggleBorderlessMode(storage.save);
		interfaceState.settingsDirty = true;
	}
	if (IsKeyPressed(KEY_F1)) singlePlayer.showHitboxes = !singlePlayer.showHitboxes;   // dev: hitbox overlay
	if (IsKeyPressed(KEY_N)) singlePlayer.noClip = !singlePlayer.noClip;                // dev: noclip (ignore ground + pipes)

	ProcessPhotoModeInput();

	// restart key re-launches the current run. single-player only while alive (avoids replaying a finished daily);
	// versus can rematch anytime
	if (IsKeyPressed(storage.save.keyRestart))
	{
		if (interfaceState.current == GameState::VS_PLAYING ||
			((interfaceState.current == GameState::PLAYING || interfaceState.current == GameState::READY) &&
				singlePlayer.alive && !singlePlayer.dailyMode))
		{
			RestartCurrentRun();
		}
	}

	// esc / pause-key toggles pause while playing
	if (interfaceState.current == GameState::PLAYING && (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(storage.save.keyPause)))
		interfaceState.current = GameState::PAUSED;
	else if (interfaceState.current == GameState::PAUSED && !interfaceState.pauseConfirm && (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(storage.save.keyPause) || IsKeyPressed(KEY_ENTER)))
		interfaceState.current = GameState::PLAYING;
	else if (interfaceState.current == GameState::READY && IsKeyPressed(KEY_ESCAPE))
	{
		// go through GoToState() (not a direct assignment) so the change rides the fade transition. otherwise the state
		// flips mid-frame and the MENU input block below (which is NOT in this else-if chain) runs the same frame, sees
		// the same ESC press, and opens the exit-confirm dialog instead of just returning to the menu
		if (interfaceState.sandboxEffect != SandboxEffect::NONE)
		{
			interfaceState.tutorialPage = interfaceState.sandboxReturnPage;
			interfaceState.sandboxEffect = SandboxEffect::NONE;
			singlePlayer.alive = false;
			GoToState(GameState::TUTORIAL);
		}
		else GoToState(GameState::MENU);
	}
	else if ((interfaceState.current == GameState::CREDITS || interfaceState.current == GameState::LEADERBOARD || interfaceState.current == GameState::TROPHIES ||
		interfaceState.current == GameState::CUSTOMIZE || interfaceState.current == GameState::SETTINGS || interfaceState.current == GameState::STATS || interfaceState.current == GameState::ACHIEVEMENTS || interfaceState.current == GameState::VS_MENU || interfaceState.current == GameState::INFO || interfaceState.current == GameState::TUTORIAL) &&
		IsKeyPressed(KEY_ESCAPE) && interfaceState.settings.rebindTarget == RebindTarget::NONE && !interfaceState.transition.active)
	{
		// Settings can be opened from PAUSE / VS_PLAYING, so ESC there should pop back to wherever we came from rather
		// than always slamming to the main menu (matching what the Save & Back button does via settingsReturnTo)
		if (interfaceState.current == GameState::SETTINGS && interfaceState.settingsReturnTo != GameState::MENU)
		{
			const GameState returnTo = interfaceState.settingsReturnTo;
			interfaceState.settingsReturnTo = GameState::MENU;
			GoToState(returnTo);
		}
		else
		{
			GoToState(GameState::MENU);
		}
	}
}

void FlappyGame::Update()
{
	UpdateVolumeAnimation();   // ease the volume bars/audio toward their mute/unmute targets
	Theme& currentTheme = resources.themes[EnumIndex(storage.save.themeIndex)];
	if (interfaceState.current == GameState::MENU)
	{
		MainMenuState& mainMenu = interfaceState.mainMenu;
		// splash title bobbing
		if ((mainMenu.splashOffset == 0 || mainMenu.splashOffset < 5) && mainMenu.splashRising)
			mainMenu.splashOffset += 5 * deltaTime;
		else
		{
			mainMenu.splashRising = false;
			mainMenu.splashOffset -= 5 * deltaTime;
			if (mainMenu.splashOffset < -5) mainMenu.splashRising = true;
		}

		// let the player type their leaderboard name (only while editing); cap at 15 printable ASCII chars
		if (mainMenu.editingName)
		{
			int ch = GetCharPressed();
			while (ch > 0)
			{
				if (storage.save.playerName.size() < 15 && ch >= 32 && ch < 127) storage.save.playerName += (char)ch;
				ch = GetCharPressed();
			}
			if (IsKeyPressed(KEY_BACKSPACE) && !storage.save.playerName.empty()) storage.save.playerName.pop_back();
		}

		if (!mainMenu.exitConfirm && IsKeyPressed(KEY_ENTER))
		{
			// while editing, Enter confirms a non-empty name; otherwise it starts the game (the play button is in the draw pass)
			if (mainMenu.editingName)
			{
				storage.save.playerName = TrimAsciiWhitespace(storage.save.playerName);
				if (!storage.save.playerName.empty())
				{
					mainMenu.editingName = false;
					WriteSave(storage.save, storage.savePath);
				}
				else ShowToast("Name cannot be blank");
			}
			else StartNormal();
		}
		// ESC toggles the exit-confirm dialog (single source of truth, so we never both open AND close it on one frame)
		if (!mainMenu.editingName && IsKeyPressed(KEY_ESCAPE)) mainMenu.exitConfirm = !mainMenu.exitConfirm;
	}
	else if (interfaceState.current == GameState::READY || interfaceState.current == GameState::PLAYING)
	{
		UpdateSinglePlayer(currentTheme, frame.scale);
	}
	else if (interfaceState.current == GameState::VS_PLAYING)
	{
		UpdateVersus(currentTheme, frame.scale);
	}

	UpdateParticles(particles, deltaTime);   // advance + retire particles (feathers fall under gravity)
}

void FlappyGame::Draw()
{
	UiBeginFrame();   // roll the button hover-claim so occluded buttons don't highlight
	Theme& currentTheme = resources.themes[EnumIndex(storage.save.themeIndex)];
	MainMenuState& mainMenu = interfaceState.mainMenu;
	// everything draws into the supersampled offscreen target first, then gets presented to the window at the end
	BeginTextureMode(screen);
	ClearBackground(GREEN);
	// below is drawn in VIRTUAL_* logical coords; the camera zoom scales it up to fill the supersampled target.
	// present then downscales it, anti-aliasing the point-filtered sprites so they stay crisp AND scroll smoothly
	BeginMode2D(Camera2D{ Vector2{ 0.0f, 0.0f }, Vector2{ 0.0f, 0.0f }, 0.0f, (float)RENDER_SUPERSAMPLE });

	// shared menu backdrop: parallax sky + scrolling ground behind every menu / sub-menu, with a translucent scrim so dense text stays readable
	const GameState state = interfaceState.current;
	const bool isMenuState =
		state == GameState::MENU || state == GameState::LEADERBOARD || state == GameState::CREDITS ||
		state == GameState::TROPHIES || state == GameState::CUSTOMIZE || state == GameState::SETTINGS ||
		state == GameState::STATS || state == GameState::ACHIEVEMENTS || state == GameState::INFO ||
		state == GameState::VS_MENU;
	if (isMenuState)
	{
		DaynightShaderHandle dnHandle = MakeDaynightHandle();
		TickAndDrawMenuBackdrop(currentTheme, resources.ui, world.skyScroll, world.midScroll, world.baseScroll, world.nightAmount, BaseTop, frame.scale, &dnHandle);
		if (state != GameState::MENU)
		{
			// behind-content scrim for sub-menus: keeps the title/back chrome visible against the sky while making dense panels readable
			DrawRectangle(0, 0, VIRTUAL_W, VIRTUAL_H, Color{ 8, 14, 34, 150 });
		}
	}

	// each state draws its screen; screens return actions (button clicks) that drive the state transitions below
	switch (state)
	{
	case GameState::MENU:
	{
		MainMenuAction menuAction = DrawMainMenuScreen(storage.save, storage.save.playerName, currentTheme, resources.ui, resources.menuButtons, frame.virtualMouse, mainMenu.editingName, mainMenu.exitConfirm, frame.scale, mainMenu.splashOffset, world.nightAmount, BaseTop, world.skyScroll, world.midScroll, world.baseScroll);
		switch (menuAction)
		{
			case MainMenuAction::OpenSettings:
				// opening Settings from the main menu is always "return to MENU"; resetting explicitly guards against a
				// stale settingsReturnTo left over from a run that bypassed the normal exit (e.g. tutorial-from-pause then Skip)
				interfaceState.settingsReturnTo = GameState::MENU;
				interfaceState.settings.scroll = 0.0f;
				GoToState(GameState::SETTINGS);
				break;
			case MainMenuAction::ConfirmExit:  mainMenu.exitConfirm = true; break;
			case MainMenuAction::Quit:         interfaceState.quitRequested = true; break;
			case MainMenuAction::CancelExit:   mainMenu.exitConfirm = false; break;
			case MainMenuAction::StartNormal:  StartNormal(); break;
			case MainMenuAction::StartDaily:   StartDaily(); break;
			case MainMenuAction::DailyAlreadyDone: ShowToast("Daily done - score " + std::to_string(storage.save.lastDailyScore)); break;
			case MainMenuAction::OpenCustomize: interfaceState.customize.pageScroll = 0.0f; GoToState(GameState::CUSTOMIZE); break;
			case MainMenuAction::OpenVsMenu:   GoToState(GameState::VS_MENU); break;
			case MainMenuAction::StartEndless: StartEndless(); break;
			case MainMenuAction::OpenLeaderboard: interfaceState.leaderboardDaily = false; FetchLeaderboard(); GoToState(GameState::LEADERBOARD); break;
			case MainMenuAction::OpenTrophies:  GoToState(GameState::TROPHIES); break;
			case MainMenuAction::OpenAchievements: GoToState(GameState::ACHIEVEMENTS); break;
			case MainMenuAction::OpenStats:     GoToState(GameState::STATS); break;
			case MainMenuAction::OpenInfo:      GoToState(GameState::INFO); break;
			case MainMenuAction::None: break;
		}
		break;
	}
	case GameState::LEADERBOARD:
		if (DrawLeaderboardScreen(interfaceState.leaderboardDaily, storage.save, frame.virtualMouse)) GoToState(GameState::MENU);
		break;
	case GameState::CREDITS:
		if (DrawCreditsScreen(frame.virtualMouse)) GoToState(GameState::MENU);
		break;
	case GameState::TROPHIES:
		if (DrawTrophiesScreen(storage.save.bestScore, resources.medals, frame.virtualMouse)) GoToState(GameState::MENU);
		break;
	case GameState::CUSTOMIZE:
	{
		CustomizeScreenResult customizeResult = DrawCustomizeScreen(interfaceState.customize, storage.save, resources.skins, resources.themes, resources.pipeTextures, frame.virtualMouse, deltaTime);
		if (customizeResult.birdChoiceChanged) interfaceState.twoPlayerButtonNeedsRefresh = true;   // rebake next frame (see PrepareFrame)
		if (customizeResult.pipeChoiceChanged) ApplySelectedPipes();
		if (customizeResult.backClicked) { WriteSave(storage.save, storage.savePath); GoToState(GameState::MENU); }
		break;
	}
	case GameState::SETTINGS:
		DrawSettingsScreen(frame.virtualMouse);
		break;
	case GameState::STATS:
		if (DrawStatsScreen(storage.save, storage.save.bestScore, frame.virtualMouse)) GoToState(GameState::MENU);
		break;
	case GameState::ACHIEVEMENTS:
		if (DrawAchievementsScreen(storage.save, frame.virtualMouse)) GoToState(GameState::MENU);
		break;
	case GameState::INFO:
		if (DrawInfoScreen(storage.save.playerName, frame.virtualMouse)) GoToState(GameState::MENU);
		break;
	case GameState::TUTORIAL:
	{
		TutorialScreenResult tutorialResult = DrawTutorialScreen(interfaceState.tutorialPage, storage.save, resources.skins, currentTheme, resources.ui, resources.menuButtons, frame.virtualMouse);
		if (tutorialResult.startSandbox) StartSandbox(tutorialResult.sandboxEffect, tutorialResult.sandboxReturnPage);
		if (tutorialResult.finishToMenu) { WriteSave(storage.save, storage.savePath); interfaceState.current = GameState::MENU; }
		break;
	}
	case GameState::VS_MENU:
		DrawText("2-Player", VIRTUAL_W / 2 - MeasureText("2-Player", 50) / 2, 80, 50, DARKBLUE);
		DrawText("P1: Space / W      P2: Up arrow", VIRTUAL_W / 2 - MeasureText("P1: Space / W      P2: Up arrow", 24) / 2, 220, 24, RAYWHITE);
		DrawText("Shared pipes - last bird flying wins", VIRTUAL_W / 2 - MeasureText("Shared pipes - last bird flying wins", 20) / 2, 262, 20, SKYBLUE);
		if (UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 350, 180, 46 }, "Start", frame.virtualMouse)) StartVersus();
		if (UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 412, 180, 42 }, "Back", frame.virtualMouse)) GoToState(GameState::MENU);
		break;
	case GameState::VS_PLAYING:
		DrawVersusScene(currentTheme, frame.virtualMouse);
		break;
	case GameState::READY:
	case GameState::PLAYING:
	case GameState::PAUSED:
	case GameState::PHOTO_MODE:
		DrawSinglePlayerScene(currentTheme, frame.virtualMouse);   // PAUSE/PHOTO draw the frozen scene; the overlay goes on top below
		break;
	}

	if (state == GameState::PAUSED)
	{
		int pauseBest = singlePlayer.dailyMode ? storage.save.bestDailyScore
			: (singlePlayer.endlessMode ? storage.save.bestClassicScore : storage.save.bestScore);
		PauseMenuAction pauseAction = DrawPauseMenu(interfaceState.pauseConfirm, singlePlayer.score,
			pauseBest, !singlePlayer.dailyMode, interfaceState.sandboxEffect, frame.virtualMouse);
		switch (pauseAction)
		{
			case PauseMenuAction::Resume: interfaceState.current = GameState::PLAYING; break;
			case PauseMenuAction::ConfirmQuit:
				// first press arms the confirm prompt; the second (with pauseConfirm set) actually quits to the menu
				if (interfaceState.pauseConfirm) { interfaceState.pauseConfirm = false; ResetGame(); singlePlayer.alive = false; interfaceState.current = GameState::MENU; }
				else interfaceState.pauseConfirm = true;
				break;
			case PauseMenuAction::CancelQuit: interfaceState.pauseConfirm = false; break;
			case PauseMenuAction::RestartNormal: RestartCurrentRun(); break;
			case PauseMenuAction::RestartSandbox: StartSandbox(interfaceState.sandboxEffect, interfaceState.sandboxReturnPage); break;
			case PauseMenuAction::OpenSettings: interfaceState.settingsReturnTo = GameState::PAUSED; interfaceState.settings.scroll = 0.0f; GoToState(GameState::SETTINGS); break;
			case PauseMenuAction::ReturnToTutorial:
				ResetGame(); singlePlayer.alive = false; interfaceState.tutorialPage = interfaceState.sandboxReturnPage; interfaceState.sandboxEffect = SandboxEffect::NONE; interfaceState.current = GameState::TUTORIAL;
				break;
			case PauseMenuAction::None: break;
		}
	}

	DrawRuntimeOverlays(state, storage.save, interfaceState.toast.message, interfaceState.toast.remainingSeconds, interfaceState.transition.active, interfaceState.transition.time);

	EndMode2D();
	EndTextureMode();

	// present the render texture scaled to the window. srcRec spans the full (supersampled) target; the negative height flips y
	Rectangle srcRec = { 0.0f, 0.0f, (float)screen.texture.width, -(float)screen.texture.height };
	// screen shake on death, decaying over time (skipped under reduce-motion); jitter the present rect, not the world
	if (singlePlayer.shakeTimer > 0.0f)
	{
		singlePlayer.shakeTimer -= deltaTime;
		if (!storage.save.reduceMotion)
		{
			float k = singlePlayer.shakeTimer > 0.0f ? singlePlayer.shakeTimer / 0.35f : 0.0f;   // shake strength fades with the timer
			frame.destination.x += GetRandomValue(-12, 12) * k;
			frame.destination.y += GetRandomValue(-12, 12) * k;
		}
	}
	if (state == GameState::PHOTO_MODE) ApplyPhotoModeTransform(frame.destination);
	BeginDrawing();
	ClearBackground(BLACK);
	DrawTexturePro(screen.texture, srcRec, frame.destination, Vector2{ 0.0f, 0.0f }, 0.0f, WHITE);
	if (state == GameState::PHOTO_MODE) DrawPhotoModeToast();
	EndDrawing();

	// persist settings once a slider drag finishes (debounced to the mouse release, not every dragged frame)
	if (interfaceState.settingsDirty && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { SaveSettings(); interfaceState.settingsDirty = false; }
}
