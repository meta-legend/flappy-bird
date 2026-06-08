// flappy bird game loop. shared helpers live in types.h / ui.h / net.h

// gui subsystem (no console) is set by cmake not a pragma so the build is portable
#include "raylib.h"
#include "networkml.h"
#include "types.h"
#include "ui.h"
#include "net.h"
#include <string>
#include <sstream>
#include <iostream>
#include <mutex>
using namespace std;

// function for debuging
void Debug(string debugName, float debugValue)
{
	cout << "[DEBUG] " + debugName + ": " << debugValue << endl;
}

int main()
{
	// run from the exe's own folder so the ./assets paths resolve no matter where
	// the game is launched from
	ChangeDirectory(GetApplicationDirectory());

	// enable vsync and make it resizeable
	SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);

	// window initiation
	InitWindow(VIRTUAL_W, VIRTUAL_H, "Flappy Bird");

	// don't let esc close the window, esc is used to pause
	SetExitKey(KEY_NULL);

	// render target the whole game draws into, then scaled to the window so
	// fullscreen and any window size keeps the fixed 800x600 layout
	RenderTexture2D screen = LoadRenderTexture(VIRTUAL_W, VIRTUAL_H);
	SetTextureFilter(screen.texture, TEXTURE_FILTER_BILINEAR);
	// loading and setting the window icon
	const char* windowIcon = "./assets/images/Flappy_Bird_icon.png";
	SetWindowIcon(LoadImage(windowIcon));
	// int audio device
	InitAudioDevice();
	
	// bird initiation
	Bird bird;

	// bird collision box
	Rectangle birdRect = Rectangle{bird.x, bird.y, 60, 61};

	// pipes and their collision boxes initiation. start them well to the right of
	// the bird (x=200) so there is a runway before the first pipe, spaced 250 apart
	// pipe pairs in arrays so the logic is loops instead of duplicated per pipe.
	// 4 pipes spaced PIPE_SPACING apart, starting right of the bird (x=200) for a runway
	constexpr int PIPE_COUNT = 4;
	constexpr float PIPE_SPACING = 280.0f;
	Pipe topPipes[PIPE_COUNT] = { Pipe(500, -30), Pipe(780, -10), Pipe(1060, -50), Pipe(1340, -30) };
	Pipe botPipes[PIPE_COUNT] = { Pipe(500, 380), Pipe(780, 400), Pipe(1060, 360), Pipe(1340, 380) };
	Rectangle topRects[PIPE_COUNT] = {};
	Rectangle botRects[PIPE_COUNT] = {};
	bool pipeScored[PIPE_COUNT] = { false, false, false, false };

	/*other variables*/
	// speed of scrolling
	float speed = 1;
	// score (one point per pipe pair passed, like the original)
	int score = 0;
	// score pos to change when player dies
	float scorePos = 30.0f;
	// offset for pipes to be subtracted by to give a infinite spawning illusion
	int offset = 0;
	// score width to place at center of screen
	int scoreWidth = 0;
	// used for sounds management
	int playSound = 0;

	// alive bool
	bool alive = false;

	// current screen (menu / playing / paused)
	GameState state = MENU;

	// volume settings (0..1), loaded from disk below
	float musicVolume = 0.3f;
	float sfxVolume = 1.0f;
	bool settingsDirty = false;
	// set by the menu quit button to exit the game
	bool quitGame = false;

	// splash screen shake var
	float splashScreenShakeY = 0;
	// splash up cycle bool
	bool splashScreenUpCycle = true;
	// loading the textures
	Texture2D splashScreenTexture = LoadTexture("./assets/images/flappybirdsplash.png");
	Texture2D birdTexture = LoadTexture("./assets/images/bird.png");
	Texture2D pipeTexture = LoadTexture("./assets/images/pipe.png");
	Texture2D pipeTexture180 = LoadTexture("./assets/images/pipe180.png");
	Texture2D gameOverTexture = LoadTexture("./assets/images/gameover.png");
	Texture2D backgroundTexture = LoadTexture("./assets/images/background.png");

	// loading the audio 
	Sound hit = LoadSound("./assets/sounds/sfx_hit.wav");
	Sound die = LoadSound("./assets/sounds/sfx_die.wav");
	Sound restart = LoadSound("./assets/sounds/sfx_point.wav");
	Sound start = LoadSound("./assets/sounds/sfx_swooshing.wav");
	Sound jump = LoadSound("./assets/sounds/jump.mp3");
	Sound scoreSfx = LoadSound("./assets/sounds/score.mp3");
	// stream the long track instead of loadsound, loadsound decodes the whole
	// file into ram (~125mb here) while a music stream decodes on the fly
	Music theme = LoadMusicStream("./assets/sounds/theme.mp3");
	
	// stringstream to convert score to string
	stringstream ss;
	string stringScore = "";

	// instantiate my file manager
	ML::File fileManager = ML::File();

	// keep save data in a per-user folder, not next to the exe
	string saveDir = SaveDir();
	fileManager.createFolders(saveDir);
	string scorePath = saveDir + "/score.txt";
	string playerPath = saveDir + "/player.txt";

	// networkml v2.0.0 readfile errors on a missing file so check exists() first
	// and seed the high score file if its not there
	if (!fileManager.exists(scorePath))
	{
		fileManager.createFile(scorePath, "0");
	}

	// string saved score
	string savedScore = "";

	// highScore
	string highScore = "";

	// player name for the online leaderboard, remembered between runs
	string playerName = fileManager.exists(playerPath) ? fileManager.readFile(playerPath) : string("");
	// readfile appends a trailing newline so strip trailing whitespace
	while (!playerName.empty() && (playerName.back() == '\n' || playerName.back() == '\r' || playerName.back() == ' '))
		playerName.pop_back();

	// only ask for a name on the first run, reuse the saved one afterwards
	bool editingName = playerName.empty();

	// makes each death submit the score only once
	bool scoreSubmitted = false;

	// load saved volume settings if present
	string settingsPath = saveDir + "/settings.txt";
	if (fileManager.exists(settingsPath))
	{
		std::istringstream sin(fileManager.readFile(settingsPath));
		float mv, sv;
		if (sin >> mv >> sv)
		{
			if (mv >= 0.0f && mv <= 1.0f) musicVolume = mv;
			if (sv >= 0.0f && sv <= 1.0f) sfxVolume = sv;
		}
	}

	// apply current volumes to the music and every sound effect
	auto applyVolumes = [&]()
	{
		SetMusicVolume(theme, musicVolume);
		SetSoundVolume(hit, sfxVolume);
		SetSoundVolume(die, sfxVolume);
		SetSoundVolume(restart, sfxVolume);
		SetSoundVolume(start, sfxVolume);
	};
	// write volume settings to disk
	auto saveSettings = [&]()
	{
		fileManager.createFile(settingsPath, std::to_string(musicVolume) + "\n" + std::to_string(sfxVolume));
	};
	// reset the bird, pipes, score and speed for a fresh run
	auto resetGame = [&]()
	{
		score = 0;
		bird.Reset(speed);
		for (int i = 0; i < PIPE_COUNT; i++) { topPipes[i].Reset(true, true); botPipes[i].Reset(true, true); pipeScored[i] = false; }
		offset = 0; playSound = 0; scoreSubmitted = false;
		ss.str(""); stringScore = "";
	};
	// start a new run from the menu
	auto startGame = [&]()
	{
		fileManager.createFile(playerPath, playerName);
		resetGame();
		alive = true;
		state = PLAYING;
		PlaySound(start);
	};

	// play theme song, music loops automatically
	PlayMusicStream(theme);
	applyVolumes();

	// game loop
	while (!WindowShouldClose() && !quitGame)
	{
		// keep the streaming music buffer fed each frame
		UpdateMusicStream(theme);

		// frame rate independent movement, tuned to 144 fps
		float frameScale = GetFrameTime() * 144.0f;

		// present transform, also used to map the mouse into virtual coords for ui
		float scale = (float)GetScreenWidth() / VIRTUAL_W;
		float scaleY = (float)GetScreenHeight() / VIRTUAL_H;
		if (scaleY < scale) scale = scaleY;
		Rectangle dstRec = {
			(GetScreenWidth() - VIRTUAL_W * scale) * 0.5f,
			(GetScreenHeight() - VIRTUAL_H * scale) * 0.5f,
			VIRTUAL_W * scale, VIRTUAL_H * scale
		};
		Vector2 vmouse = {
			(GetMousePosition().x - dstRec.x) / scale,
			(GetMousePosition().y - dstRec.y) / scale
		};

		// f11 toggles borderless fullscreen
		if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();

		// esc / p toggles pause while playing
		if (state == PLAYING && (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)))
			state = PAUSED;
		else if (state == PAUSED && (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)))
			state = PLAYING;
		else if ((state == CREDITS || state == LEADERBOARD || state == CONTROLS) && IsKeyPressed(KEY_ESCAPE))
			state = MENU;

		// ---------------- update ----------------
		if (state == MENU)
		{
			// splash bobbing animation
			if ((splashScreenShakeY == 0 || splashScreenShakeY < 5) && splashScreenUpCycle)
				splashScreenShakeY += 5 * GetFrameTime();
			else
			{
				splashScreenUpCycle = false;
				splashScreenShakeY -= 5 * GetFrameTime();
				if (splashScreenShakeY < -5) splashScreenUpCycle = true;
			}

			// let the player type their leaderboard name (only while editing)
			if (editingName)
			{
				int ch = GetCharPressed();
				while (ch > 0)
				{
					if (playerName.size() < 15 && ch >= 32 && ch < 127) playerName += (char)ch;
					ch = GetCharPressed();
				}
				if (IsKeyPressed(KEY_BACKSPACE) && !playerName.empty()) playerName.pop_back();
			}

			// enter starts the game (the play button is handled in the draw pass)
			if (IsKeyPressed(KEY_ENTER))
			{
				// while editing, enter confirms a non-empty name; otherwise it starts the game
				if (editingName) { if (!playerName.empty()) { fileManager.createFile(playerPath, playerName); editingName = false; } }
				else startGame();
			}
		}
		else if (state == PLAYING)
		{
			// scoring is per-pipe now (handled in the pipe loop below); on death do
			// the high score save, leaderboard submit and restart handling
			if (!alive)
			{
				// plays death sound once
				playSound++;
				scorePos = 360;
				if (playSound == 1) { PlaySound(die); playSound++; }

				if (!savedScore.empty() && stoi(savedScore) > stoi(fileManager.readFile(scorePath)))
				{
					fileManager.deleteFile(scorePath);
					fileManager.createFile(scorePath, savedScore);
				}

				// submit this run's score to the leaderboard once
				if (!scoreSubmitted && !savedScore.empty())
				{
					try { SubmitScore(playerName, stoi(savedScore)); }
					catch (...) {}
					scoreSubmitted = true;
				}

				// space restarts the run
				if (IsKeyPressed(KEY_ENTER)) { resetGame(); alive = true; PlaySound(start); }
			}

			// each tap gives an upward hop, gravity does the rest
			if (alive && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
			{
				bird.velocity = JUMP_IMPULSE;
				bird.rotation = BIRD_UP_ANGLE;
				bird.sinceJump = 0;
				PlaySound(jump);
			}
			bird.velocity += GRAVITY * GetFrameTime();
			bird.y += bird.velocity * GetFrameTime();

			// hold the up tilt briefly, then rotate down toward a nose dive
			bird.sinceJump += GetFrameTime();
			if (bird.sinceJump >= BIRD_ROT_HOLD)
			{
				bird.rotation += BIRD_ROT_RATE * GetFrameTime();
				if (bird.rotation > BIRD_DOWN_ANGLE) bird.rotation = BIRD_DOWN_ANGLE;
			}

			// reset position if the bird goes offscreen
			if ((bird.y < 0 || bird.y > VIRTUAL_H + 30) && alive) bird.Reset(speed);

			// recycle a pipe only once it is fully off the left edge: move it to the
			// right of the current rightmost pipe with a fresh random gap height
			float maxX = topPipes[0].x;
			for (int i = 1; i < PIPE_COUNT; i++) if (topPipes[i].x > maxX) maxX = topPipes[i].x;
			for (int i = 0; i < PIPE_COUNT; i++)
			{
				if (topPipes[i].x + 88 < 0)
				{
					float nx = maxX + PIPE_SPACING;
					maxX = nx;
					topPipes[i].x = nx; botPipes[i].x = nx;
					offset = topPipes[i].Random();
					topPipes[i].y += offset; botPipes[i].y += offset;
					pipeScored[i] = false;
				}
				// snap the gap back to default if the random offset drifts too far
				if (topPipes[i].y < -80 || topPipes[i].y > 60) { topPipes[i].Reset(false, true); botPipes[i].Reset(false, true); }
			}

			// update collision boxes
			birdRect = Rectangle{ bird.x, bird.y, 60, 61 };
			for (int i = 0; i < PIPE_COUNT; i++)
			{
				topRects[i] = Rectangle{ topPipes[i].x, topPipes[i].y + 266 - pipeTexture180.height, 88, (float)pipeTexture180.height };
				botRects[i] = Rectangle{ botPipes[i].x, botPipes[i].y, 88, 266 };
			}

			// collisions
			if (alive)
				for (int i = 0; i < PIPE_COUNT; i++)
					if (CheckCollisionRecs(birdRect, topRects[i]) || CheckCollisionRecs(birdRect, botRects[i]))
					{
						PlaySound(hit);
						alive = false;
						break;
					}

			// animate pipes and award a point for each pair the bird passes
			if (alive)
			{
				for (int i = 0; i < PIPE_COUNT; i++)
				{
					topPipes[i].x -= speed * frameScale;
					botPipes[i].x -= speed * frameScale;
					if (!pipeScored[i] && topPipes[i].x + 88 < bird.x)
					{
						score++;
						pipeScored[i] = true;
						PlaySound(scoreSfx);
					}
				}
			}

			// build the score string for display, frozen at the value reached on death
			ss.str(""); ss.clear(); ss << score;
			stringScore = ss.str();
			if (alive) savedScore = stringScore;
			scoreWidth = MeasureText(stringScore.c_str(), 60);

			// increasing speed as time passes
			if (speed < 8) speed += 0.2 * GetFrameTime();
		}

		// keep the high score string current
		highScore = "High Score: " + fileManager.readFile(scorePath);

		// ---------------- draw ----------------
		BeginTextureMode(screen);
		ClearBackground(GREEN);

		if (state == MENU)
		{
			DrawTexture(splashScreenTexture, 25, 25 + splashScreenShakeY, WHITE);

			if (editingName)
			{
				DrawText("Enter your name:", VIRTUAL_W / 2 - MeasureText("Enter your name:", 22) / 2, 278, 22, DARKBLUE);
				string nameDisplay = playerName + (((int)(GetTime() * 2) % 2) ? "_" : " ");
				DrawText(nameDisplay.c_str(), VIRTUAL_W / 2 - MeasureText(nameDisplay.c_str(), 32) / 2, 302, 32, RAYWHITE);
				DrawText("press ENTER to confirm", VIRTUAL_W / 2 - MeasureText("press ENTER to confirm", 16) / 2, 342, 16, BLUE);
			}
			else
			{
				string greet = "Playing as: " + playerName;
				DrawText(greet.c_str(), VIRTUAL_W / 2 - MeasureText(greet.c_str(), 26) / 2, 285, 26, RAYWHITE);
				if (UiButton(Rectangle{ VIRTUAL_W / 2 - 70, 320, 140, 30 }, "Change", vmouse)) editingName = true;
			}

			DrawText("Music", 232, 372, 20, DARKBLUE);
			if (UiSlider(Rectangle{ 320, 376, 240, 14 }, musicVolume, vmouse)) { applyVolumes(); settingsDirty = true; }
			DrawText("SFX", 252, 405, 20, DARKBLUE);
			if (UiSlider(Rectangle{ 320, 409, 240, 14 }, sfxVolume, vmouse)) { applyVolumes(); settingsDirty = true; }

			if (UiButton(Rectangle{ VIRTUAL_W / 2 - 175, 442, 165, 38 }, "Play", vmouse)) startGame();
			if (UiButton(Rectangle{ VIRTUAL_W / 2 + 10, 442, 165, 38 }, "Leaderboard", vmouse)) { state = LEADERBOARD; FetchLeaderboard(); }
			if (UiButton(Rectangle{ VIRTUAL_W / 2 - 175, 484, 165, 38 }, "Controls", vmouse)) state = CONTROLS;
			if (UiButton(Rectangle{ VIRTUAL_W / 2 + 10, 484, 165, 38 }, "Credits", vmouse)) state = CREDITS;
			if (UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 526, 180, 38 }, "Quit", vmouse)) quitGame = true;
		}
		else if (state == LEADERBOARD)
		{
			DrawText("Leaderboard", VIRTUAL_W / 2 - MeasureText("Leaderboard", 50) / 2, 55, 50, DARKBLUE);
			{
				std::lock_guard<std::mutex> lk(g_lb.mtx);
				if (g_lb.status == 1)
					DrawText("Loading...", VIRTUAL_W / 2 - MeasureText("Loading...", 28) / 2, 280, 28, RAYWHITE);
				else if (g_lb.status == 3)
					DrawText("Couldn't load leaderboard", VIRTUAL_W / 2 - MeasureText("Couldn't load leaderboard", 24) / 2, 280, 24, RED);
				else if (g_lb.entries.empty())
					DrawText("No scores yet!", VIRTUAL_W / 2 - MeasureText("No scores yet!", 26) / 2, 280, 26, RAYWHITE);
				else
				{
					int y = 140;
					int rank = 1;
					for (auto& e : g_lb.entries)
					{
						string line = std::to_string(rank) + ":  " + e.first;
						DrawText(line.c_str(), 210, y, 26, RAYWHITE);
						string sc = std::to_string(e.second);
						DrawText(sc.c_str(), 590 - MeasureText(sc.c_str(), 26), y, 26, SKYBLUE);
						y += 38;
						if (++rank > 10) break;
					}
				}
			}
			if (UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 540, 180, 40 }, "Back", vmouse)) state = MENU;
		}
		else if (state == CREDITS)
		{
			DrawText("Credits", VIRTUAL_W / 2 - MeasureText("Credits", 60) / 2, 90, 60, DARKBLUE);
			DrawText("Made by: meta_legend", VIRTUAL_W / 2 - MeasureText("Made by: meta_legend", 30) / 2, 220, 30, RAYWHITE);
			DrawText("Theme music by HeatleyBros", VIRTUAL_W / 2 - MeasureText("Theme music by HeatleyBros", 26) / 2, 290, 26, RAYWHITE);

			// clickable link to the music channel
			const char* url = "youtube.com/@HeatleyBros";
			int uw = MeasureText(url, 20);
			Rectangle urlRect = { VIRTUAL_W / 2 - uw / 2.0f, 332, (float)uw, 24 };
			bool urlHover = CheckCollisionPointRec(vmouse, urlRect);
			DrawText(url, (int)urlRect.x, (int)urlRect.y, 20, urlHover ? SKYBLUE : BLUE);
			if (urlHover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
				OpenUrl("https://www.youtube.com/channel/UCsLlqLIE-TqDq3lh5kU2PeA");

			if (UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 470, 180, 42 }, "Back", vmouse)) state = MENU;
			DrawText("or press ESC", VIRTUAL_W / 2 - MeasureText("or press ESC", 16) / 2, 524, 16, BLUE);
		}
		else if (state == CONTROLS)
		{
			DrawText("Controls", VIRTUAL_W / 2 - MeasureText("Controls", 50) / 2, 70, 50, DARKBLUE);
			int y = 175;
			int lx = 220;
			int vx = 380;
			DrawText("Flap:", lx, y, 26, RAYWHITE);       DrawText("Space / W / Up / Click", vx, y + 3, 22, SKYBLUE); y += 52;
			DrawText("Start:", lx, y, 26, RAYWHITE);      DrawText("Enter", vx, y + 3, 22, SKYBLUE); y += 52;
			DrawText("Restart:", lx, y, 26, RAYWHITE);    DrawText("Enter (after a crash)", vx, y + 3, 22, SKYBLUE); y += 52;
			DrawText("Pause:", lx, y, 26, RAYWHITE);      DrawText("Esc / P", vx, y + 3, 22, SKYBLUE); y += 52;
			DrawText("Fullscreen:", lx, y, 26, RAYWHITE); DrawText("F11", vx, y + 3, 22, SKYBLUE); y += 52;
			if (UiButton(Rectangle{ VIRTUAL_W / 2 - 90, 520, 180, 40 }, "Back", vmouse)) state = MENU;
		}
		else
		{
			// game scene (shown while playing and behind the pause overlay)
			DrawTexture(backgroundTexture, 0, 0, WHITE);
			int topPipeOff = 266 - pipeTexture180.height;
			for (int i = 0; i < PIPE_COUNT; i++)
			{
				DrawTexture(pipeTexture180, topPipes[i].x, topPipes[i].y + topPipeOff, WHITE);
				DrawTexture(pipeTexture, botPipes[i].x, botPipes[i].y, WHITE);
			}

			Rectangle birdSrc = { 0, 0, (float)birdTexture.width, (float)birdTexture.height };
			Rectangle birdDst = { bird.x + birdTexture.width / 2.0f, bird.y + birdTexture.height / 2.0f, (float)birdTexture.width, (float)birdTexture.height };
			Vector2 birdOrigin = { birdTexture.width / 2.0f, birdTexture.height / 2.0f };
			DrawTexturePro(birdTexture, birdSrc, birdDst, birdOrigin, bird.rotation, WHITE);

			if (!alive)
			{
				// game over image scaled to fit (it is wider than the screen), with the prompt below
				float goScale = 640.0f / gameOverTexture.width;
				float goW = gameOverTexture.width * goScale;
				DrawTextureEx(gameOverTexture, Vector2{ VIRTUAL_W / 2 - goW / 2, 50 }, 0.0f, goScale, WHITE);
				DrawText("Press ENTER to play again", VIRTUAL_W / 2 - MeasureText("Press ENTER to play again", 28) / 2, 555, 28, BLUE);
			}

			if (alive)
				DrawText(stringScore.c_str(), VIRTUAL_W / 2 - scoreWidth / 2, 30, 60, DARKBLUE);
			else
				DrawText(stringScore.c_str(), VIRTUAL_W / 2 - scoreWidth / 2, scorePos, 60, DARKBLUE);

			DrawText(highScore.c_str(), 620, 15, 20, DARKBLUE);
		}

		if (state == PAUSED)
		{
			DrawRectangle(0, 0, VIRTUAL_W, VIRTUAL_H, Color{ 0, 0, 0, 160 });
			DrawText("PAUSED", VIRTUAL_W / 2 - MeasureText("PAUSED", 60) / 2, 110, 60, RAYWHITE);

			DrawText("Music", 232, 226, 20, RAYWHITE);
			if (UiSlider(Rectangle{ 320, 230, 240, 14 }, musicVolume, vmouse)) { applyVolumes(); settingsDirty = true; }
			DrawText("SFX", 252, 261, 20, RAYWHITE);
			if (UiSlider(Rectangle{ 320, 265, 240, 14 }, sfxVolume, vmouse)) { applyVolumes(); settingsDirty = true; }

			if (UiButton(Rectangle{ VIRTUAL_W / 2 - 110, 310, 220, 42 }, "Resume", vmouse)) state = PLAYING;
			if (UiButton(Rectangle{ VIRTUAL_W / 2 - 110, 362, 220, 42 }, "Restart", vmouse)) { resetGame(); alive = true; state = PLAYING; PlaySound(start); }
			if (UiButton(Rectangle{ VIRTUAL_W / 2 - 110, 414, 220, 42 }, "Quit to Menu", vmouse)) { resetGame(); alive = false; state = MENU; }
		}

		EndTextureMode();

		// present the render texture scaled to the window (letterboxed, centered)
		Rectangle srcRec = { 0.0f, 0.0f, (float)VIRTUAL_W, -(float)VIRTUAL_H }; // negative height flips y
		BeginDrawing();
		ClearBackground(BLACK);
		DrawTexturePro(screen.texture, srcRec, dstRec, Vector2{ 0.0f, 0.0f }, 0.0f, WHITE);
		EndDrawing();

		// persist volume settings once a slider drag finishes
		if (settingsDirty && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { saveSettings(); settingsDirty = false; }
	}

	// unload textures
	UnloadTexture(birdTexture);
	UnloadTexture(pipeTexture);
	UnloadTexture(pipeTexture180);
	UnloadTexture(gameOverTexture);
	UnloadTexture(backgroundTexture);

	// unload audio
	UnloadSound(hit);
	UnloadSound(die);
	UnloadSound(restart);
	UnloadSound(start);
	UnloadSound(jump);
	UnloadSound(scoreSfx);
	UnloadMusicStream(theme);
	UnloadRenderTexture(screen);

	// close audio device
	CloseAudioDevice();

	// closing window if user presses x on window or presses esc key
	CloseWindow();
	return 0;
}