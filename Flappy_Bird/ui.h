// tiny immediate-mode ui widgets and a url opener
#pragma once
#include "raylib.h"

// immediate-mode slider drawn in virtual coords, returns true if the value
// changed this frame (m is the mouse already mapped into virtual space)
bool UiSlider(Rectangle bar, float& value, Vector2 m);

// immediate-mode button, returns true on click
bool UiButton(Rectangle r, const char* label, Vector2 m);

// open a url in the default browser (no console flash on windows)
void OpenUrl(const char* url);
