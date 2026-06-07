// immediate-mode UI helpers; each widget draws itself and returns true the frame it's clicked
#pragma once
#include "raylib.h"

#include "font.h"
#include "types.h"

// scissor given in virtual coords — scales up to the supersampled render target so the clip lands correctly
inline void BeginScaledScissor(int x, int y, int w, int h)
{
	BeginScissorMode(x * RENDER_SUPERSAMPLE, y * RENDER_SUPERSAMPLE,
		w * RENDER_SUPERSAMPLE, h * RENDER_SUPERSAMPLE);
}

// horizontal slider in bar; edits value in place while dragged and returns true on change. m is the virtual-space mouse
bool UiSlider(Rectangle bar, float& value, Vector2 m);

// call once per frame before drawing any widgets, to reset their per-frame interaction state
void UiBeginFrame();

// labelled button filling r; m is the virtual-space mouse
bool UiButton(Rectangle r, const char* label, Vector2 m);

// button drawn as a texture, centered at (cx, cy) and scaled to size
bool UiTextureButton(Texture2D tex, Vector2 m, float cx, float cy, float size);

// smaller icon button centered at (cx, cy)
bool UiIconButton(Texture2D tex, Vector2 m, float cx, float cy);

// open url in the system default browser
void OpenUrl(const char* url);
