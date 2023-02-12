#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

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
// include raylib because that's the library that I'm using
#include "raylib.h"
// include networkml because im using networkml for file management
#include "networkml.h"

// this makes its easier to debug 
using namespace std;

// the flappy bird obj
struct Bird
{
	// the default x and y for the flappy bird
	float x = 200;
	float y = GetScreenHeight() / 2 - 30;
	
	// reset the x, y  and speed to the defaults
	void Reset(float& speed)
	{
		x = 200;
		y = GetScreenHeight() / 2 - 30;
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
	// window initiation 
	InitWindow(800, 600, "Flappy Bird");
	// enables vsync for the window
	SetWindowState(FLAG_VSYNC_HINT);
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
	Sound theme = LoadSound("./assets/sounds/theme.wav");
	
	// stringstream to convert score to string
	stringstream ss;
	string stringScore = "";

	// instantiate my file manager
	ML::File fileManager = ML::File();

	if (fileManager.readFile("score.txt") == "")
	{
		fileManager.createFile("score.txt", "0");
	}

	// string saved score 
	string savedScore = "";

	// highScore 
	string highScore = "";

	// play theme song
	PlaySound(theme);

	// game loop
	while (!WindowShouldClose())
	{
		// consistencely play the theme
		if (!IsSoundPlaying(theme)) {
			PlaySound(theme);
		}

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
			if (IsKeyDown(KEY_SPACE)) {
				splashScreen = false;
				alive = true;
			}
		}

		// scoring
		if (alive)
		{
			score += 0.2;
		}
		else if (!splashScreen)
		{
			// plays death sound
			playSound++;
			scorePos = GetScreenHeight() / 2;
			if (playSound == 1)
			{
				PlaySound(die);
				playSound++;
			}

			if (stoi(savedScore) > stoi(fileManager.readFile("score.txt")))
			{
				fileManager.deleteFile("score.txt");
				fileManager.createFile("score.txt", savedScore);
			}
			
			// reset the game if space clicked
			if (IsKeyDown(KEY_SPACE))
			{
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
		if ((IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_UP) || IsKeyDown(KEY_W) || IsMouseButtonDown(MOUSE_BUTTON_LEFT)) && alive)
		{
			bird.y -= 240 * GetFrameTime();
		}
		else if (alive)
		{
			bird.y += 200 * GetFrameTime();
		}
		else
		{
			bird.y += 400 * GetFrameTime();
		}

		// checking if the bird goes offscreen and if its true reset its position
		if ((bird.y < 0 || bird.y > GetScreenHeight() + 30) && alive)
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
		pipe1TOPRect = Rectangle{ pipe1TOP.x, pipe1TOP.y, 88, 266 };
		pipe1BOTTOMRect = Rectangle{ pipe1BOTTOM.x, pipe1BOTTOM.y, 88, 266 };
		pipe2TOPRect = Rectangle{ pipe2TOP.x, pipe2TOP.y, 88, 266 };
		pipe2BOTTOMRect = Rectangle{ pipe2BOTTOM.x, pipe2BOTTOM.y, 88, 266 };
		pipe3TOPRect = Rectangle{ pipe3TOP.x, pipe3TOP.y, 88, 266 };
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
		highScore = "High Score: " + fileManager.readFile("score.txt");

		// begin drawing the bird and all of the pipes
		BeginDrawing();
		ClearBackground(GREEN);
		if (splashScreen) {
			DrawTexture(splashScreenTexture, 25, 150 + splashScreenShakeY, WHITE);
		}
		else {
			DrawTexture(pipeTexture180, pipe1TOP.x, pipe1TOP.y, WHITE);
			DrawTexture(pipeTexture, pipe1BOTTOM.x, pipe1BOTTOM.y, WHITE);
			DrawTexture(pipeTexture180, pipe2TOP.x, pipe2TOP.y, WHITE);
			DrawTexture(pipeTexture, pipe2BOTTOM.x, pipe2BOTTOM.y, WHITE);
			DrawTexture(pipeTexture180, pipe3TOP.x, pipe3TOP.y, WHITE);
			DrawTexture(pipeTexture, pipe3BOTTOM.x, pipe3BOTTOM.y, WHITE);
			DrawTexture(birdTexture, bird.x, bird.y, WHITE);
			if (!alive)
			{
				DrawText("Press Space To Start!", GetScreenWidth() / 2 - MeasureText("Press Space To Start!", 35) / 2, 50, 35, RED);
			}

			if (alive)
			{
				DrawText(stringScore.c_str(), GetScreenWidth() / 2 - scoreWidth / 2, 30, 60, DARKBLUE);
			}
			else
			{
				DrawText(stringScore.c_str(), GetScreenWidth() / 2 - scoreWidth / 2, scorePos, 60, DARKBLUE);
			}

			DrawText("Made By: meta_legend!", GetScreenWidth() / 2 - MeasureText("Credit: Pranab Shukla!", 50) / 2, 525, 50, DARKBLUE);

			DrawText(highScore.c_str(), 620, 15, 20, DARKBLUE);
		}
		EndDrawing();

		// reset score
		ss.str("");
		stringScore = "";

		// animating pipes 
		if (alive)
		{
			pipe1TOP.x -= speed;
			pipe1BOTTOM.x -= speed;
			pipe2TOP.x -= speed;
			pipe2BOTTOM.x -= speed;
			pipe3TOP.x -= speed;
			pipe3BOTTOM.x -= speed;
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

	// close audio device
	CloseAudioDevice();

	// closing window if user presses x on window or presses esc key
	CloseWindow();
	return 0;
}