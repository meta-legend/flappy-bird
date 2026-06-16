#pragma once

#include "constants.h"
#include "raylib.h"

#include <string>

struct SaveData;

// each Draw*Screen renders one sub-screen and returns true the frame the user asks to go back; vmouse is the virtual-space mouse

// lbDaily toggles the daily vs all-time board; the screen flips it when its tab is clicked
bool DrawLeaderboardScreen(bool& lbDaily, const SaveData& sd, Vector2 vmouse);
bool DrawCreditsScreen(Vector2 vmouse);
// highlights the medal tier earned at bestScore
bool DrawTrophiesScreen(int bestScore, Texture2D (&medalTextures)[Constants::Medals::Count], Vector2 vmouse);
bool DrawStatsScreen(const SaveData& sd, int bestScore, Vector2 vmouse);
bool DrawAchievementsScreen(const SaveData& sd, Vector2 vmouse);
bool DrawInfoScreen(const std::string& playerName, Vector2 vmouse);
