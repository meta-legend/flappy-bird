// gui subsystem (no console) is set by cmake not a pragma so the build is portable

// include for iostream for debuging
#include <iostream>
// include ctime for random numbers
#include <ctime>
// include string for scoring and debuging
#include <string>
// include sstream for int to string conversion and vice versa
#include <sstream>
// include fstream for high score
#include <fstream>
// include thread so leaderboard submissions never block the game loop
#include <thread>
// include cstdlib for std::getenv (configurable leaderboard endpoint)
#include <cstdlib>
// include raylib (graphics, input, audio, etc.) 
#include "raylib.h"
// include networkml for file management and the online leaderboard requests
#include "networkml.h"
// include random for the per request nonce
#include <random>
// include our tiny hmac-sha256 for signing leaderboard submissions
#include "hmac_sha256.h"

// this makes its easier to debug
using namespace std;

// shared secret used to sign leaderboard submissions, injected at build time by
// cmake (-DLEADERBOARD_SECRET=...), falls back to a dev value for local testing
#ifndef LEADERBOARD_SECRET
#define LEADERBOARD_SECRET "dev-secret-change-me"
#endif

// the game's fixed internal resolution; everything is drawn at this size into a
// render texture, then scaled to the actual window (for fullscreen support)
static constexpr int VIRTUAL_W = 800;
static constexpr int VIRTUAL_H = 600;

// bird physics: gravity pulls down constantly, each tap sets an upward velocity
static constexpr float GRAVITY = 1500.0f;       // px per second^2 (downward)
static constexpr float JUMP_IMPULSE = -450.0f;  // px per second (upward burst on tap)

// the online leaderboard endpoint (netlify function backed by netlify blobs)
// override with the FLAPPY_LEADERBOARD_URL env var to point at a local backend
// (e.g. http://localhost:8899/api/leaderboard) for testing
// portable env var read, msvc deprecates std::getenv (C4996, and /sdl makes it
// an error) so use _dupenv_s there and plain getenv everywhere else
static string GetEnvVar(const char* name)
{
#ifdef _MSC_VER
	char* buf = nullptr;
	size_t sz = 0;
	if (_dupenv_s(&buf, &sz, name) == 0 && buf)
	{
		string value(buf);
		free(buf);
		return value;
	}
	return "";
#else
	const char* env = std::getenv(name);
	return env ? string(env) : string("");
#endif
}

// per-user folder for save data (high score, player name) instead of next to
// the exe (which may be read-only)
//   windows: %LOCALAPPDATA%\FlappyBird
//   macos:   ~/Library/Application Support/FlappyBird
//   linux:   $XDG_DATA_HOME or ~/.local/share/FlappyBird
static string SaveDir()
{
#ifdef _WIN32
	string base = GetEnvVar("LOCALAPPDATA");
	if (base.empty()) base = ".";
	return base + "\\FlappyBird";
#elif defined(__APPLE__)
	return GetEnvVar("HOME") + "/Library/Application Support/FlappyBird";
#else
	string base = GetEnvVar("XDG_DATA_HOME");
	if (base.empty()) base = GetEnvVar("HOME") + "/.local/share";
	return base + "/FlappyBird";
#endif
}

static string LeaderboardUrl()
{
	string env = GetEnvVar("FLAPPY_LEADERBOARD_URL");
	if (!env.empty()) return env;
	return "https://personalwebsiteclonetest.netlify.app/api/leaderboard";
}

// minimal json string escaping for the player name
static string JsonEscape(const string& s)
{
	string out;
	for (char c : s)
	{
		switch (c)
		{
		case '"':  out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n";  break;
		case '\r': out += "\\r";  break;
		case '\t': out += "\\t";  break;
		default:
			if ((unsigned char)c < 0x20) continue; // drop other control chars
			out += c;
		}
	}
	return out;
}

// post a score on a detached thread so a slow or offline network never freezes
// the game, failures are silently ignored
static void SubmitScore(const string& name, int score)
{
	string player = name.empty() ? string("Anonymous") : name;
	std::thread([player, score]() {
		try
		{
			string ts = std::to_string((long long)time(nullptr));
			string scoreStr = std::to_string(score);

			// random hex nonce so each request is unique (replay protection)
			string nonce;
			std::random_device rd;
			static const char* hexd = "0123456789abcdef";
			for (int i = 0; i < 24; ++i) nonce += hexd[rd() & 0xf];

			// sign name + score + nonce + timestamp, server recomputes and checks
			string canonical = player + "\n" + scoreStr + "\n" + nonce + "\n" + ts;
			string sig = hmacsha256::hmacHex(LEADERBOARD_SECRET, canonical);

			ML::Requests req;
			string body = "{\"name\":\"" + JsonEscape(player) +
				"\",\"score\":" + scoreStr + "}";
			req.post(LeaderboardUrl(), body, {
				"Content-Type: application/json",
				"X-Timestamp: " + ts,
				"X-Nonce: " + nonce,
				"X-Signature: " + sig
			}, 5);
		}
		catch (...) {}
	}).detach();
}

// the flappy bird obj
struct Bird
{
	// the default x and y for the flappy bird
	float x = 200;
	float y = VIRTUAL_H / 2 - 30;
	// vertical velocity for the hop/gravity physics
	float velocity = 0;

	// reset the x, y, velocity and speed to the defaults
	void Reset(float& speed)
	{
		x = 200;
		y = VIRTUAL_H / 2 - 30;
		velocity = 0;
		speed = 1;
	}
};

// the pipe obj
struct Pipe
{
	// the x and y of the pipe
	float x, y;
	// the default x and y of the pipe
	float defaultX, defaultY;

	// pipe contructor
	Pipe(float defX, float defY)
	{
		x = defX;
		defaultX = defX;
		y = defY;
		defaultY = defY;
	}

	// returns random y offset
	int Random()
	{
		SetRandomSeed(time(0));
		return GetRandomValue(-35, 35);
	}

	// resets pipe positions to default positions
	void Reset(bool xTrue, bool yTrue)
	{
		if (xTrue)
		{
			x = defaultX;
		}

		if (yTrue)
		{
			y = defaultY;
		}
	}
};

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

	// enable vsync before initwindow via config flags, setting it after left the
	// window blank on some drivers
	SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
	// window initiation
	InitWindow(VIRTUAL_W, VIRTUAL_H, "Flappy Bird");

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

	// pipes and their collision boxes initiation
	Pipe pipe1TOP = Pipe(100, -30);
	Rectangle pipe1TOPRect = Rectangle{ pipe1TOP.x, pipe1TOP.y, 88, 266 };
	Pipe pipe1BOTTOM = Pipe(100, 380);
	Rectangle pipe1BOTTOMRect = Rectangle{ pipe1BOTTOM.x, pipe1BOTTOM.y, 88, 266 };
	Pipe pipe2TOP = Pipe(350, -10);
	Rectangle pipe2TOPRect = Rectangle{ pipe2TOP.x, pipe2TOP.y, 88, 266 };
	Pipe pipe2BOTTOM = Pipe(350, 400);
	Rectangle pipe2BOTTOMRect = Rectangle{ pipe2BOTTOM.x, pipe2BOTTOM.y, 88, 266 };
	Pipe pipe3TOP = Pipe(600, -50);
	Rectangle pipe3TOPRect = Rectangle{ pipe3TOP.x, pipe3TOP.y, 88, 266 };
	Pipe pipe3BOTTOM = Pipe(600, 360);
	Rectangle pipe3BOTTOMRect = Rectangle{ pipe3BOTTOM.x, pipe3BOTTOM.y, 88, 266 };

	/*other variables*/
	// speed of scrolling
	float speed = 1;
	// score 
	float score = 0;
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

	// splash screen bool
	bool splashScreen = true;
	// splash screen shake var
	float splashScreenShakeY = 0;
	// splash up cycle bool
	bool splashScreenUpCycle = true;
	// loading the textures
	Texture2D splashScreenTexture = LoadTexture("./assets/images/flappybirdsplash.png");
	Texture2D birdTexture = LoadTexture("./assets/images/bird.png");
	Texture2D pipeTexture = LoadTexture("./assets/images/pipe.png");
	Texture2D pipeTexture180 = LoadTexture("./assets/images/pipe180.png");

	// loading the audio 
	Sound hit = LoadSound("./assets/sounds/sfx_hit.wav");
	Sound die = LoadSound("./assets/sounds/sfx_die.wav");
	Sound restart = LoadSound("./assets/sounds/sfx_point.wav");
	Sound start = LoadSound("./assets/sounds/sfx_swooshing.wav");
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

	// makes each death submit the score only once
	bool scoreSubmitted = false;

	// play theme song, music loops automatically
	PlayMusicStream(theme);
	// keep the theme quieter than the sfx
	SetMusicVolume(theme, 0.3f);

	// game loop
	while (!WindowShouldClose())
	{
		// keep the streaming music buffer fed each frame
		UpdateMusicStream(theme);

		// frame rate independent movement, per frame speeds below are tuned to
		// 144 fps so scale them by how long this frame took, same speed on any
		// refresh rate (change the 144.0f to make the game slower or faster)
		float frameScale = GetFrameTime() * 144.0f;

		// f11 toggles borderless fullscreen
		if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();

		if (splashScreen) {
			if ((splashScreenShakeY == 0 || splashScreenShakeY < 5) && splashScreenUpCycle) {
				splashScreenShakeY += 5 * GetFrameTime();
			}
			else {
				splashScreenUpCycle = false;
				if (!splashScreenUpCycle) {
					splashScreenShakeY -= 5 * GetFrameTime();
					if (splashScreenShakeY < -5) {
						splashScreenUpCycle = true;
					}
				}
			}
			// let the player type their leaderboard name on the splash screen
			int ch = GetCharPressed();
			while (ch > 0) {
				if (playerName.size() < 15 && ch >= 32 && ch < 127)
					playerName += (char)ch;
				ch = GetCharPressed();
			}
			if (IsKeyPressed(KEY_BACKSPACE) && !playerName.empty())
				playerName.pop_back();

			// enter starts the game, space is reserved for flapping/typing
			if (IsKeyPressed(KEY_ENTER)) {
				fileManager.createFile(playerPath, playerName);
				splashScreen = false;
				alive = true;
			}
		}

		// scoring
		if (alive)
		{
			score += 0.2 * frameScale;
		}
		else if (!splashScreen)
		{
			// plays death sound
			playSound++;
			scorePos = VIRTUAL_H / 2;
			if (playSound == 1)
			{
				PlaySound(die);
				playSound++;
			}

			if (stoi(savedScore) > stoi(fileManager.readFile(scorePath)))
			{
				fileManager.deleteFile(scorePath);
				fileManager.createFile(scorePath, savedScore);
			}

			// submit this run's score to the online leaderboard (once per death)
			if (!scoreSubmitted && !savedScore.empty())
			{
				try { SubmitScore(playerName, stoi(savedScore)); }
				catch (...) {}
				scoreSubmitted = true;
			}

			// reset the game if space clicked
			if (IsKeyDown(KEY_SPACE))
			{
				scoreSubmitted = false;
				score = 0;
				bird.Reset(speed);
				pipe1TOP.Reset(true, true);
				pipe1BOTTOM.Reset(true, true);
				pipe2TOP.Reset(true, true);
				pipe2BOTTOM.Reset(true, true);
				pipe2TOP.Reset(true, true);
				pipe2BOTTOM.Reset(true, true);
				pipe3TOP.Reset(true, true);
				pipe3BOTTOM.Reset(true, true);
				offset = 0;
				alive = true;
				playSound = 0;
				PlaySound(start);
			}
		}
		// flappy bird behavior
		// each tap gives an upward hop, gravity does the rest (works alive or not
		// so the bird keeps falling on the death screen)
		if (alive && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
		{
			bird.velocity = JUMP_IMPULSE;
		}
		bird.velocity += GRAVITY * GetFrameTime();
		bird.y += bird.velocity * GetFrameTime();

		// checking if the bird goes offscreen and if its true reset its position
		if ((bird.y < 0 || bird.y > VIRTUAL_H + 30) && alive)
		{
			bird.Reset(speed);
		}

		// pipe infinite spawning behavior
		if (pipe1TOP.x - 88 < 0)
		{
			pipe1TOP.x = pipe3TOP.x + 250;
			pipe1BOTTOM.x = pipe3TOP.x + 250;
			offset = pipe1TOP.Random();
			pipe1TOP.y += offset;
			pipe1BOTTOM.y += offset;
		}

		if (pipe2TOP.x - 88 < 0)
		{
			pipe2TOP.x = pipe1TOP.x + 250;
			pipe2BOTTOM.x = pipe1TOP.x + 250;
			offset = pipe2TOP.Random();
			pipe2TOP.y += offset;
			pipe2BOTTOM.y += offset;
		}

		if (pipe3TOP.x - 88 < 0)
		{
			pipe3TOP.x = pipe2TOP.x + 250;
			pipe3BOTTOM.x = pipe2TOP.x + 250;
			offset = pipe3TOP.Random();
			pipe3TOP.y += offset;
			pipe3BOTTOM.y += offset;
		}

		// pipe reset position behavior
		if (pipe1TOP.y < -80 || pipe1TOP.y > 60)
		{
			pipe1TOP.Reset(false, true);
			pipe1BOTTOM.Reset(false, true);
		}

		if (pipe2TOP.y < -80 || pipe2TOP.y > 60)
		{
			pipe2TOP.Reset(false, true);
			pipe2BOTTOM.Reset(false, true);
		}

		if (pipe3TOP.y < -80 || pipe3TOP.y > 60)
		{
			pipe3TOP.Reset(false, true);
			pipe3BOTTOM.Reset(false, true);
		}

		//store score in stringstream and convert
		if (stringScore == "")
		{
			ss << (int)(score + 0.5);
			stringScore = ss.str();
			savedScore = stringScore;
			scoreWidth = MeasureText(stringScore.c_str(), 60);
		}

		// updating bird rect collision box
		birdRect = Rectangle{ bird.x, bird.y, 60, 61 };

		// updating pipe rect collision boxes
		pipe1TOPRect = Rectangle{ pipe1TOP.x, pipe1TOP.y + 266 - pipeTexture180.height, 88, (float)pipeTexture180.height };
		pipe1BOTTOMRect = Rectangle{ pipe1BOTTOM.x, pipe1BOTTOM.y, 88, 266 };
		pipe2TOPRect = Rectangle{ pipe2TOP.x, pipe2TOP.y + 266 - pipeTexture180.height, 88, (float)pipeTexture180.height };
		pipe2BOTTOMRect = Rectangle{ pipe2BOTTOM.x, pipe2BOTTOM.y, 88, 266 };
		pipe3TOPRect = Rectangle{ pipe3TOP.x, pipe3TOP.y + 266 - pipeTexture180.height, 88, (float)pipeTexture180.height };
		pipe3BOTTOMRect = Rectangle{ pipe3BOTTOM.x, pipe3BOTTOM.y, 88, 266 };

		// checking collisions
		if (CheckCollisionRecs(birdRect, pipe1TOPRect) && alive)
		{
			PlaySound(hit);
			alive = false;
		} else if (CheckCollisionRecs(birdRect, pipe1BOTTOMRect) && alive)
		{
			PlaySound(hit);
			alive = false;
		} else if (CheckCollisionRecs(birdRect, pipe2TOPRect) && alive)
		{
			PlaySound(hit);
			alive = false;
		} else if (CheckCollisionRecs(birdRect, pipe2BOTTOMRect) && alive)
		{
			PlaySound(hit);
			alive = false;
		} else if (CheckCollisionRecs(birdRect, pipe3TOPRect) && alive)
		{
			PlaySound(hit);
			alive = false;
		} else if (CheckCollisionRecs(birdRect, pipe3BOTTOMRect) && alive)
		{
			PlaySound(hit);
			alive = false;
		}

		// set high score
		highScore = "High Score: " + fileManager.readFile(scorePath);

		// draw the whole game into the fixed-size render texture
		BeginTextureMode(screen);
		ClearBackground(GREEN);
		if (splashScreen) {
			DrawTexture(splashScreenTexture, 25, 150 + splashScreenShakeY, WHITE);

				// name entry prompt for the online leaderboard
				DrawText("Enter your name:", VIRTUAL_W / 2 - MeasureText("Enter your name:", 25) / 2, 400, 25, DARKBLUE);
				// blinking caret while typing
				string nameDisplay = playerName + (((int)(GetTime() * 2) % 2) ? "_" : " ");
				DrawText(nameDisplay.c_str(), VIRTUAL_W / 2 - MeasureText(nameDisplay.c_str(), 40) / 2, 430, 40, RAYWHITE);
				DrawText("Press ENTER to start", VIRTUAL_W / 2 - MeasureText("Press ENTER to start", 30) / 2, 490, 30, RED);
		}
		else {
			// top pipes are drawn from the top left, so anchor their bottom edge at
			// y+266 (the original opening) and let extra image height extend up offscreen
			int topPipeOff = 266 - pipeTexture180.height;
			DrawTexture(pipeTexture180, pipe1TOP.x, pipe1TOP.y + topPipeOff, WHITE);
			DrawTexture(pipeTexture, pipe1BOTTOM.x, pipe1BOTTOM.y, WHITE);
			DrawTexture(pipeTexture180, pipe2TOP.x, pipe2TOP.y + topPipeOff, WHITE);
			DrawTexture(pipeTexture, pipe2BOTTOM.x, pipe2BOTTOM.y, WHITE);
			DrawTexture(pipeTexture180, pipe3TOP.x, pipe3TOP.y + topPipeOff, WHITE);
			DrawTexture(pipeTexture, pipe3BOTTOM.x, pipe3BOTTOM.y, WHITE);
			// tilt the bird with its velocity: nose up on a hop, rotating down as it falls
			float birdAngle = bird.velocity * 0.08f;
			if (birdAngle < -25.0f) birdAngle = -25.0f;
			if (birdAngle > 90.0f) birdAngle = 90.0f;
			Rectangle birdSrc = { 0, 0, (float)birdTexture.width, (float)birdTexture.height };
			Rectangle birdDst = { bird.x + birdTexture.width / 2.0f, bird.y + birdTexture.height / 2.0f, (float)birdTexture.width, (float)birdTexture.height };
			Vector2 birdOrigin = { birdTexture.width / 2.0f, birdTexture.height / 2.0f };
			DrawTexturePro(birdTexture, birdSrc, birdDst, birdOrigin, birdAngle, WHITE);
			if (!alive)
			{
				DrawText("Press Space To Start!", VIRTUAL_W / 2 - MeasureText("Press Space To Start!", 35) / 2, 50, 35, RED);
			}

			if (alive)
			{
				DrawText(stringScore.c_str(), VIRTUAL_W / 2 - scoreWidth / 2, 30, 60, DARKBLUE);
			}
			else
			{
				DrawText(stringScore.c_str(), VIRTUAL_W / 2 - scoreWidth / 2, scorePos, 60, DARKBLUE);
			}

			DrawText("Made By: meta_legend!", VIRTUAL_W / 2 - MeasureText("Credit: Pranab Shukla!", 50) / 2, 525, 50, DARKBLUE);

			DrawText(highScore.c_str(), 620, 15, 20, DARKBLUE);
		}
		EndTextureMode();

		// present the render texture scaled to the window (letterboxed, centered)
		float scaleW = (float)GetScreenWidth() / VIRTUAL_W;
		float scaleH = (float)GetScreenHeight() / VIRTUAL_H;
		float scale = scaleW < scaleH ? scaleW : scaleH;
		Rectangle srcRec = { 0.0f, 0.0f, (float)VIRTUAL_W, -(float)VIRTUAL_H }; // negative height flips y
		Rectangle dstRec = {
			(GetScreenWidth() - VIRTUAL_W * scale) * 0.5f,
			(GetScreenHeight() - VIRTUAL_H * scale) * 0.5f,
			VIRTUAL_W * scale, VIRTUAL_H * scale
		};
		BeginDrawing();
		ClearBackground(BLACK);
		DrawTexturePro(screen.texture, srcRec, dstRec, { 0.0f, 0.0f }, 0.0f, WHITE);
		EndDrawing();

		// reset score
		ss.str("");
		stringScore = "";

		// animating pipes 
		if (alive)
		{
			pipe1TOP.x -= speed * frameScale;
			pipe1BOTTOM.x -= speed * frameScale;
			pipe2TOP.x -= speed * frameScale;
			pipe2BOTTOM.x -= speed * frameScale;
			pipe3TOP.x -= speed * frameScale;
			pipe3BOTTOM.x -= speed * frameScale;
		}

		//increasing speed as time passes
		if (speed < 8)
		{
			speed += 0.4 * GetFrameTime();
		}
	}

	// unload textures
	UnloadTexture(birdTexture);
	UnloadTexture(pipeTexture);
	UnloadTexture(pipeTexture180);

	// unload audio
	UnloadSound(hit);
	UnloadSound(die);
	UnloadSound(restart);
	UnloadSound(start);
	UnloadMusicStream(theme);
	UnloadRenderTexture(screen);

	// close audio device
	CloseAudioDevice();

	// closing window if user presses x on window or presses esc key
	CloseWindow();
	return 0;
}