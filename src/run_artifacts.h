#pragma once

#include "raylib.h"
#include "save.h"
#include <string>
#include <vector>

// persist a ghost replay to ghostPath: the flap timestamps plus the skin and final score for later playback
void SaveGhostRun(const std::string& ghostPath, const std::vector<float>& recordFlaps, const SaveData& sd, int finalScore);
// load a ghost replay; false if missing or corrupt. ghostFlaps/ghostSkin are filled, maxSkinCount clamps a stale skin index
bool LoadGhostRun(const std::string& ghostPath, std::vector<float>& ghostFlaps, SkinIndex& ghostSkin, int maxSkinCount);
// capture the current frame from screen and write it into shotDir; returns the saved file path. name/score feed the caption
std::string SaveFrameScreenshot(RenderTexture2D screen, const std::string& shotDir, const std::string& playerName, int score, int bestScore);
// the disk-writing half of SaveFrameScreenshot: write an already-captured Image into shotDir; returns the saved file path
std::string WriteFrameScreenshotImage(Image& image, const std::string& shotDir, const std::string& playerName, int score, int bestScore);
