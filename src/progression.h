#pragma once

#include "constants.h"
#include "raylib.h"
#include "save.h"
#include "types.h"
#include <vector>

// point every theme's pipe textures at the chosen (style, color) cell, loading that cell on demand
void ApplyPipeChoice(std::vector<Theme>& themes, Texture2D (&pipeTex)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], Texture2D (&pipeTex180)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], PipeStyleIndex pipeStyle, PipeColorIndex pipeColor);
// refresh sd's unlock bitmasks from bestScore (skins/themes/pipes gated behind score thresholds)
void RecomputeUnlocks(SaveData& sd, int bestScore, int skinCount, int themeCount);
