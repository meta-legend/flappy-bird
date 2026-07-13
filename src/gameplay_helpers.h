#pragma once
#include "constants.h"
#include "raylib.h"
#include "types.h"
#include <string>

// uniform random float in [a, b]
float Randf(float a, float b);
// printable name for a keybind (raylib key code or the MOUSE_LEFT_BIND sentinel), for the settings rebind display
const char* KeyName(int k);
// was the action bound to `bind` triggered this frame? handles the MOUSE_LEFT_BIND sentinel plus normal key codes
bool BindPressed(int bind);
// today's UTC date packed as YYYYMMDD; used to seed and gate the daily challenge (rolls over at UTC midnight)
int TodayYMD();
// vertical pipe gap at this score; ramp == false holds the easy StartingGap, ramp == true tightens it to MinimumGap by score 30
float CurrentGap(int score, bool ramp);
// world scroll speed at this score; ramp == false holds the base speed, ramp == true accelerates up to score 50
float CurrentSpeed(int score, bool ramp);
