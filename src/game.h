#pragma once

#include "assets.h"
#include "system_audio.h"
#include "constants.h"
#include "screen_customize.h"
#include "system_display.h"
#include "networkml.h"
#include "particles.h"
#include "render_game.h"
#include "save.h"
#include "screen_settings.h"
#include "types.h"

#include "raylib.h"

#include <cstddef>
#include <string>
#include <vector>

// references to the externally-owned window target + audio; the game borrows these, it never owns them
struct GamePlatform
{
	RenderTexture2D& screen;
	GameAudio& audio;
};

// every loaded texture/atlas, plus the bird flap-animation cursor
struct GameResources
{
	UiTextures ui{};
	std::vector<BirdSkin> skins;
	MenuButtonTextures menuButtons{};
	std::vector<Theme> themes;
	Texture2D pipeTextures[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount] = {};
	Texture2D flippedPipeTextures[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount] = {};
	Texture2D scoreBig[10] = {};
	Texture2D scoreSmall[10] = {};
	Texture2D medals[Constants::Medals::Count] = {};
	// flap cycle ping-pongs up -> mid -> down -> mid, so MID appears twice and the wings ease through the extremes
	const BirdFrame birdFrameOrder[4] = {
		BirdFrame::UP_FLAP, BirdFrame::MID_FLAP, BirdFrame::DOWN_FLAP, BirdFrame::MID_FLAP
	};
	int birdFrameStep = 0;          // index into birdFrameOrder
	float birdAnimationTimer = 0.0f;
	float flapBoost = 0.0f;         // briefly speeds the flap cycle right after a hop
};

// the world both single-player and versus scroll through: the birds, the recycled pipe pool, and parallax accumulators
struct GameWorldState
{
	Bird bird;
	Bird bird2;
	// initial X is VIRTUAL_W-relative so the first pipe starts just off the right edge (a hard-coded 850
	// used to peek into view on the wider 16:9 canvas during Get Ready); they scroll in once the run starts.
	// the +50/+305/+560/... gaps are 255 apart to match Constants::Pipes::Spacing, so the opening pipes are
	// spaced the same as recycled ones
	Pipe topPipes[Constants::Pipes::Count] = {
		Pipe(VIRTUAL_W + 50, -30), Pipe(VIRTUAL_W + 305, -10), Pipe(VIRTUAL_W + 560, -50), Pipe(VIRTUAL_W + 815, -30), Pipe(VIRTUAL_W + 1070, -20)
	};
	Pipe bottomPipes[Constants::Pipes::Count] = {
		Pipe(VIRTUAL_W + 50, 380), Pipe(VIRTUAL_W + 305, 400), Pipe(VIRTUAL_W + 560, 360), Pipe(VIRTUAL_W + 815, 380), Pipe(VIRTUAL_W + 1070, 390)
	};
	Rectangle topCollision[Constants::Pipes::Count] = {};
	Rectangle bottomCollision[Constants::Pipes::Count] = {};
	float speed = Constants::Pipes::ScrollSpeed;
	float baseScroll = 0.0f;
	float nightAmount = 0.0f;        // day -> night crossfade, 0..1
	bool nightPhase = false;
	int nightSwitchScore = 0;        // score this run flips to night (rolled from NightSwitchMin/Max)
	float skyScroll = 0.0f;
	float midScroll = 0.0f;
	// the moon needs its own accumulator: skyScroll wraps at the (small) bg tile width for tiling, which
	// doesn't match the moon's full-screen travel, so sharing it made the moon jump on every bg wrap
	float moonScroll = 0.0f;
	float cameraY = 0.0f;
};

// everything specific to a single-player run, reset by ResetGame
struct SinglePlayerState
{
	bool alive = false;
	int score = 0;
	bool pipeScored[Constants::Pipes::Count] = {};
	float shakeTimer = 0.0f;
	float gameOverFade = 0.0f;
	float deathFlash = 0.0f;
	float panelSlide = 0.0f;
	bool panelWoosh = false;
	bool deathProcessed = false;
	bool newBest = false;
	MedalRank medalRank = MedalRank::NONE;
	int offset = 0;
	float readyTime = 0.0f;
	bool dailyMode = false;
	bool endlessMode = false;
	float lastShieldSpawn = -100.0f;
	float lastTimeWarpSpawn = -100.0f;
	float lastWindSpawn = -100.0f;
	float windForce = 0.0f;        // raw target (0 or +/-N) — drives the HUD arrow + gust-sfx edge
	float windForceSmooth = 0.0f;  // eased applied force so the gust ramps in/out gradually
	float windTimeLeft = 0.0f;
	float slowFactor = 1.0f;       // eased world-speed multiplier (<1 slowed, >1 sped up)
	float shieldTime = 0.0f;
	float slowTime = 0.0f;         // time-orb effect timer (covers both slow and speed)
	int   timeWarpDir = 0;         // -1 = slowing, +1 = speeding, 0 = none
	bool  crossedBest = false;     // has the personal-best celebration fired this run yet
	float graceTime = 0.0f;
	float runClock = 0.0f;
	// score frozen at death for the post-run panel + leaderboard submission;
	// -1 means "no run completed yet" (cleaner than an empty string + stoi on every check)
	int savedScore = -1;
	bool scoreSubmitted = false;
	bool showHitboxes = false;
	bool noClip = false;

	// snapshot of the previous + interpolated positions, so the 240 Hz fixed scroll step renders smoothly
	// at any frame rate (render reads renderedPipeX between fixed steps, not the live position)
	struct ScrollInterpolation
	{
		double accumulator = 0.0;
		float previousPipeX[Constants::Pipes::Count] = {};
		float renderedPipeX[Constants::Pipes::Count] = {};
		float previousBaseScroll = 0.0f;
		float previousSkyScroll = 0.0f;
		float previousMidScroll = 0.0f;
		float renderedBaseScroll = 0.0f;
		float renderedSkyScroll = 0.0f;
		float renderedMidScroll = 0.0f;
	} scroll;
};

// everything specific to a 2-player versus run
struct VersusState
{
	bool alive1 = false;
	bool alive2 = false;
	bool started = false;
	bool paused = false;
	float readyTime = 0.0f;
	float runClock = 0.0f;   // advances only while unpaused; drives wind + oscillation timing
	bool windActive = false; // tracks the wind 0 -> non-zero edge so the gust sfx fires once
	int score1 = 0;
	int score2 = 0;
	bool scored1[Constants::Pipes::Count] = {};
	bool scored2[Constants::Pipes::Count] = {};
	bool pickup1[Constants::Pipes::Count] = {};
	bool pickup2[Constants::Pipes::Count] = {};
	float shieldTime2 = 0.0f;
};

// main-menu-only UI: the splash bob, the exit-confirm dialog, and the name-entry overlay; all irrelevant once you leave MENU
struct MainMenuState
{
	float splashOffset = 0.0f;
	bool splashRising = true;
	bool editingName = false;
	bool exitConfirm = false;
};

// photo-mode camera transform plus the key-hold timer that opens it
struct PhotoModeState
{
	Vector2 offset = { 0, 0 };
	float zoom = 1.0f;
	float screenshotHoldSeconds = 0.0f;
	bool suppressActivationUntilRelease = false;
	GameState returnState = GameState::PLAYING;   // state to restore on exit (PLAYING or READY)
};

// a transient toast popup ("Achievement unlocked!", "Screenshot saved"); shows until remainingSeconds hits zero
struct ToastState
{
	std::string message;
	float remainingSeconds = 0.0f;
};

// fade between top-level menu states (MENU -> SETTINGS etc.)
struct StateTransitionTracker
{
	bool active = false;
	float time = 0.0f;
	GameState target = GameState::MENU;
};

// all transient UI state: the live state-machine cursor plus each screen's own sub-state
struct InterfaceState
{
	GameState current = GameState::MENU;
	GameState settingsReturnTo = GameState::MENU;
	bool quitRequested = false;
	bool settingsDirty = false;
	bool pauseConfirm = false;
	float saveTimer = 0.0f;
	// the 2-player button caches a render texture of the two chosen skins. picking a different bird needs a
	// rebake, but BeginTextureMode can't nest inside the screen-texture BeginTextureMode that wraps the draw
	// pass — doing it inline flashes the screen for a frame. so we set this flag and honor it from the next PrepareFrame
	bool twoPlayerButtonNeedsRefresh = false;

	StateTransitionTracker transition;
	MainMenuState mainMenu;
	PhotoModeState photoMode;
	ToastState toast;

	SettingsScreenState settings;
	CustomizeScreenState customize;

	// LIVE slider values: what the sliders draw and what's applied to the mixer. on mute they animate to 0,
	// on unmute they animate back to saved*Volume (the chosen levels). see UpdateVolumeAnimation()
	float musicVolume = 0.3f;
	float sfxVolume = 1.0f;
	float savedMusicVolume = 0.3f;   // level to restore to on unmute
	float savedSfxVolume = 1.0f;

	TutorialPage tutorialPage = TutorialPage::WELCOME;
	SandboxEffect sandboxEffect = SandboxEffect::NONE;
	TutorialPage sandboxReturnPage = TutorialPage::WELCOME;
	bool leaderboardDaily = false;

	StartupDisplayTracker startupDisplay;
};

// ghost bird state: recordedFlaps captures the current run, playbackFlaps replays a saved best alongside the player
struct GhostReplayState
{
	std::vector<float> recordedFlaps;
	std::vector<float> playbackFlaps;
	std::size_t playbackIndex = 0;
	float y = 0.0f;
	float velocity = 0.0f;
	float rotation = 0.0f;
	SkinIndex skin = SkinIndex::YELLOW_BIRD;
	bool valid = false;
};

// on-disk paths and the loaded save; one ghost file per mode (normal / classic / daily)
struct StorageState
{
	ML::File fileManager;
	std::string saveDirectory;
	std::string savePath;
	std::string screenshotDirectory;
	std::string ghostPath;
	std::string classicGhostPath;
	std::string dailyGhostPath;
	SaveData save;
};

// recomputed each frame from the window size: the present scale, the letterbox/fill rect, and the mouse mapped to virtual space
struct FrameState
{
	float scale = 0.0f;
	Rectangle destination = {};
	Vector2 virtualMouse = {};
};

// owns all game state and drives one frame at a time from main()
class FlappyGame
{
public:
	explicit FlappyGame(GamePlatform& platform);
	~FlappyGame();

	FlappyGame(const FlappyGame&) = delete;
	FlappyGame& operator=(const FlappyGame&) = delete;

	// false once the window is closing or quit was requested; main()'s loop condition
	bool ShouldContinue() const;
	// advance + render exactly one frame
	void RunFrame();

private:
	// push the saved pipe (style, color) into the themes and the gameplay pair
	void ApplySelectedPipes();
	// lazy pipe loading: EnsureCustomizeCrossLoaded bakes the row + column the Customize strips show for the
	// current selection; FreeUnusedPipes drops everything but the gameplay pair when leaving Customize
	void EnsureCustomizeCrossLoaded();
	void FreeUnusedPipes();
	// keep only the active theme's full bg/mid resident: bake it if needed, free the previously-active one.
	// idempotent and safe every frame — the active theme is always ensured loaded; only freeing the old one is conditional
	void UpdateActiveThemeVisuals();
	// rebake the 2-player preview button (deferred via twoPlayerButtonNeedsRefresh)
	void RefreshTwoPlayerButton();
	// push vsync + fps cap from the save into the window
	void ApplyFps();
	// recompute unlock masks from the best score (after a run or on load)
	void RefreshUnlocks();
	// apply the live slider volumes to the mixer
	void ApplyVolumes();
	// ease the live volumes toward their target on mute/unmute
	void UpdateVolumeAnimation();
	// write the save file to disk
	void SaveSettings();
	// queue a toast popup for its default duration
	void ShowToast(const std::string& message);
	// set the achievement bit if not already earned, and toast it
	void UnlockAchievement(Achievement which);
	// begin a fade transition toward target (see StateTransitionTracker)
	void GoToState(GameState target);
	// record / load the best-run ghost for the current mode (picks the normal / classic / daily path)
	void SaveGhost(int finalScore);
	void LoadGhost();
	// queue a screenshot of the current frame; the write happens off-thread and a toast follows
	void TakeScreenshot();
	// drain finished screenshot writes into toasts
	void FlushScreenshotToasts();
	void ProcessPhotoModeInput();
	// fold the photo-mode pan/zoom into the present destination rect
	void ApplyPhotoModeTransform(Rectangle& destination) const;
	void DrawPhotoModeToast() const;
	// clear per-run state back to defaults (does not pick a mode — the Start* calls do that)
	void ResetGame();
	void StartGame();
	void StartNormal();
	// start Classic mode: endless, no difficulty ramp and no powerups; scored against bestClassicScore
	void StartEndless();
	void StartDaily();
	// launch a tutorial "Try it!" sandbox demoing one effect; returnPage is the tutorial page to come back to
	void StartSandbox(SandboxEffect effect, TutorialPage returnPage);
	void StartVersus();
	// restart the run in progress, keeping its mode (normal / endless / daily / sandbox / versus); bound to the restart key
	void RestartCurrentRun();
	// on a recycled pipe, maybe attach a variation and/or pickup, gated by score
	void RollPipeExtras(int pipeIndex, int score);
	// trigger a time orb at center; randomly slows or speeds the world, durationScale stretches/shrinks the effect
	void ActivateTimeWarp(Vector2 center, float durationScale = 1.0f);
	// clear any active time warp back to normal speed + music pitch
	void ResetTimeWarpEffect();
	// fire the personal-best celebration once, the frame the score first passes a real prior best (> 0)
	void MaybeCelebratePersonalBest(float x, float y);
	// the fixed-step scroll interpolation helpers (see SinglePlayerState::ScrollInterpolation):
	void ResetSinglePlayerScrollInterpolation();
	// snap one pipe's interpolation snapshot to its live X (after a recycle/teleport, so it doesn't lerp across the screen)
	void SynchronizeSinglePlayerPipe(int pipeIndex);
	// run the 240 Hz fixed scroll steps for this frame, moving pipes + parallax and updating the interpolation snapshot
	void AdvanceSinglePlayerScroll(const Theme& currentTheme, float effectiveSpeed,
		bool movePipes, float parallaxScale);
	void DrawSinglePlayerScene(const Theme& currentTheme, Vector2 virtualMouse);
	void UpdateSinglePlayer(const Theme& currentTheme, float frameScale);
	void UpdateVersus(const Theme& currentTheme, float frameScale);
	void DrawVersusScene(const Theme& currentTheme, Vector2 virtualMouse);
	// the versus per-bird helpers run once per player; the bool*/score args point at that player's slice of VersusState
	void StepVersusBird(Bird& targetBird, bool isAlive);
	void CheckVersusBird(Bird& targetBird, bool& isAlive, int& targetScore,
		bool* scoredPipes, bool* collectedPickups, float& targetShieldTime);
	void DrawVersusBird(Bird& targetBird, SkinIndex skinIndex, bool isAlive,
		Color tint, const char* statusText, float targetShieldTime);
	void DrawSettingsScreen(Vector2 virtualMouse);
	// the per-frame pipeline, called in this order by RunFrame: PrepareFrame -> ProcessInput -> Update -> Draw
	void PrepareFrame();
	void ProcessInput();
	void Update();
	void Draw();
	// build a populated handle from the loaded daynight shader; valid == false if it didn't load (old GPU / missing file)
	DaynightShaderHandle MakeDaynightHandle() const;

	// --- post-process shaders + procedural sprites ------------------------
	// daynightShader collapses the sequential day+night DrawTextures into one linear-space crossfade + dusk tint;
	// the *Loc ints are its cached uniform locations. bloomFlashTex is a small radial gradient built at startup,
	// drawn with BLEND_ADDITIVE for the score/shield bloom flash
	Shader daynightShader{};
	int daynightDayTexLoc = -1;
	int daynightNightTexLoc = -1;
	int daynightNightAmountLoc = -1;
	int daynightDuskTintLoc = -1;
	int daynightDayOffsetLoc = -1;
	int daynightNightOffsetLoc = -1;
	int daynightLayerSizeLoc = -1;
	int daynightScreenSizeLoc = -1;
	Texture2D bloomFlashTex{};
	// true while Customize has its extra pipe-preview textures loaded; drives lazy load on enter / free on exit (see PrepareFrame)
	bool customizePipesLoaded = false;
	// which theme currently has its full bg/mid resident (-1 = none); only one at a time (see UpdateActiveThemeVisuals)
	int loadedThemeIndex = -1;

	RenderTexture2D& screen;
	GameAudio& audio;
	GameResources resources;
	GameWorldState world;
	SinglePlayerState singlePlayer;
	VersusState versus;
	InterfaceState interfaceState;
	GhostReplayState ghost;
	StorageState storage;
	FrameState frame;
	float deltaTime = 0.0f;
	std::vector<Particle> particles;
	ParticleEmitter particleEmitter;

	static constexpr float BirdScale = 3.4f;                          // sprite draw scale
	static constexpr float BirdRadius = Constants::Bird::HitboxRadius;
	static constexpr float BirdHalfHeight = 12.0f * BirdScale / 2.0f; // 12 = source sprite half-height in px, scaled up
	static constexpr float BaseTop = (float)VIRTUAL_H - 80.0f;        // y of the ground strip's top edge
};
