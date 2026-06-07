#include "ui.h"

// raylib's OpenURL shells out via system(), which flashes a console window on a GUI app — so on Windows call
// ShellExecute directly (forward-declared to avoid including windows.h, which clashes with raylib)
#ifdef _WIN32
extern "C" __declspec(dllimport) void* __stdcall ShellExecuteA(void*, const char*, const char*, const char*, const char*, int);
void OpenUrl(const char* url) { ShellExecuteA(nullptr, "open", url, nullptr, nullptr, 1); }
#else
void OpenUrl(const char* url) { OpenURL(url); }
#endif

bool UiSlider(Rectangle bar, float& value, Vector2 m)
{
	bool changed = false;
	Rectangle hitArea = { bar.x - 6, bar.y - 10, bar.width + 12, bar.height + 20 };   // padded grab area, easier to catch
	if (CheckCollisionPointRec(m, hitArea) && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
	{
		float t = (m.x - bar.x) / bar.width;
		if (t < 0) t = 0;
		if (t > 1) t = 1;
		if (t != value) { value = t; changed = true; }
	}
	DrawRectangleRec(bar, Color{ 40, 40, 60, 255 });
	DrawRectangle((int)bar.x, (int)bar.y, (int)(bar.width * value), (int)bar.height, SKYBLUE);
	DrawRectangleLinesEx(bar, 1, RAYWHITE);
	DrawCircle((int)(bar.x + bar.width * value), (int)(bar.y + bar.height / 2), 9, RAYWHITE);   // knob
	return changed;
}

// press-claim so overlapping buttons don't both fire on one click: buttons draw back-to-front, so the LAST
// (topmost) button under the cursor on the press frame claims it, and only the claimant activates on release
static Rectangle gUiPressClaim = { 0, 0, 0, 0 };
static bool gUiHasClaim = false;
// topmost hovered button, resolved one frame late (last evaluated wins), so an occluded button skips its highlight
static Rectangle gUiHoverPrev = { 0, 0, 0, 0 };
static Rectangle gUiHoverCur = { 0, 0, 0, 0 };
static bool gUiHoverPrevValid = false;
static bool gUiHoverCurValid = false;
static bool UiSameRect(Rectangle a, Rectangle b)
{
	return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

void UiBeginFrame()
{
	// roll this frame's hover result into "prev" so next frame can tell which button was actually on top
	gUiHoverPrev = gUiHoverCur;
	gUiHoverPrevValid = gUiHoverCurValid;
	gUiHoverCurValid = false;
}

bool UiButton(Rectangle r, const char* label, Vector2 m)
{
	bool hover = CheckCollisionPointRec(m, r);
	if (hover) { gUiHoverCur = r; gUiHoverCurValid = true; }   // last (topmost) hovered wins
	if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { gUiPressClaim = r; gUiHasClaim = true; }
	bool claimed = gUiHasClaim && UiSameRect(gUiPressClaim, r);
	bool fired = hover && claimed && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
	// drop the claim once the mouse is fully idle, but keep it through the release frame so fired can read it
	if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) gUiHasClaim = false;
	bool down = hover && claimed && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
	// only the topmost hovered button highlights; occluded ones underneath stay un-highlighted
	bool showHover = hover && (!gUiHoverPrevValid || UiSameRect(gUiHoverPrev, r));

	// drop shadow sits below-right; when held, the face presses down into it
	DrawRectangleRec(Rectangle{ r.x + 2, r.y + 3, r.width, r.height }, Color{ 0, 0, 0, 70 });
	Rectangle face = down ? Rectangle{ r.x + 1, r.y + 2, r.width, r.height } : r;

	Color fill = down ? Color{ 70, 95, 165, 255 } : (showHover ? Color{ 92, 124, 204, 255 } : Color{ 50, 70, 130, 255 });
	DrawRectangleRec(face, fill);
	if (!down) DrawRectangleRec(Rectangle{ face.x, face.y, face.width, 3 }, Color{ 255, 255, 255, 45 });   // top highlight = raised look
	DrawRectangleLinesEx(face, 2, RAYWHITE);

	// render the label through the shared pixel-font math (size * PX_SCALE), so it matches every other DrawText
	int fs = 22;
	float actualFs = fs * PX_SCALE;
	float lw = MeasureTextEx(gPixelFont, label, actualFs, PX_SPACING).x;
	DrawTextEx(gPixelFont, label, Vector2{ face.x + face.width / 2 - lw / 2, face.y + face.height / 2 - actualFs / 2 }, actualFs, PX_SPACING, RAYWHITE);
	return fired;   // activate on release, so the press-in is visible before it fires
}

bool UiTextureButton(Texture2D tex, Vector2 m, float cx, float cy, float size)
{
	Rectangle r = { cx - size / 2.0f, cy - size / 2.0f, size, size };
	bool hov = CheckCollisionPointRec(m, r);
	bool dn = hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT);

	// RenderTexture content is Y-flipped, so negate the source height to draw it right-side up
	Rectangle src = { 0, 0, (float)tex.width, -(float)tex.height };
	Rectangle shadow = { r.x + 3, r.y + 4, r.width, r.height };
	DrawTexturePro(tex, src, shadow, Vector2{ 0, 0 }, 0.0f, Fade(BLACK, 0.35f));
	Rectangle dst = dn ? Rectangle{ r.x + 1, r.y + 2, r.width, r.height } : r;
	DrawTexturePro(tex, src, dst, Vector2{ 0, 0 }, 0.0f, hov ? WHITE : Fade(WHITE, 0.95f));
	return hov && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

bool UiIconButton(Texture2D tex, Vector2 m, float cx, float cy)
{
	float r = 56.0f;
	Rectangle hit = { cx - r, cy - r, r * 2.0f, r * 2.0f };
	bool hov = CheckCollisionPointRec(m, hit);
	bool dn = hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
	float off = dn ? 3.0f : 0.0f;   // shove the whole icon down-right while held

	DrawCircle((int)(cx + 3), (int)(cy + 4), r, Fade(BLACK, 0.35f));
	DrawCircle((int)(cx + off), (int)(cy + off), r, dn ? Color{ 70, 95, 165, 255 } : (hov ? Color{ 92, 124, 204, 255 } : Color{ 50, 70, 130, 255 }));
	// DrawRing, not DrawCircleLines: a 1px GL line doesn't scale with the supersample camera (its width is fixed in
	// device pixels), so it downscales to a faint sliver — a ring is real geometry that scales to a clean ~2px border
	DrawRing(Vector2{ cx + off, cy + off }, r - 2.0f, r, 0.0f, 360.0f, 64, RAYWHITE);
	float ts = 2.0f;
	DrawTextureEx(tex, Vector2{ cx + off - tex.width * ts / 2.0f, cy + off - tex.height * ts / 2.0f }, 0.0f, ts, WHITE);
	return hov && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}
