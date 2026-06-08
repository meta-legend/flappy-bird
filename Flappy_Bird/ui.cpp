#include "ui.h"

// raylib's OpenURL shells out via system() which flashes a console window on a
// gui app, so call ShellExecute directly on windows (forward declared to avoid
// including windows.h, which clashes with raylib)
#ifdef _WIN32
extern "C" __declspec(dllimport) void* __stdcall ShellExecuteA(void*, const char*, const char*, const char*, const char*, int);
void OpenUrl(const char* url) { ShellExecuteA(nullptr, "open", url, nullptr, nullptr, 1); }
#else
void OpenUrl(const char* url) { OpenURL(url); }
#endif

bool UiSlider(Rectangle bar, float& value, Vector2 m)
{
	bool changed = false;
	Rectangle hitArea = { bar.x - 6, bar.y - 10, bar.width + 12, bar.height + 20 };
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
	DrawCircle((int)(bar.x + bar.width * value), (int)(bar.y + bar.height / 2), 9, RAYWHITE);
	return changed;
}

bool UiButton(Rectangle r, const char* label, Vector2 m)
{
	bool hover = CheckCollisionPointRec(m, r);
	DrawRectangleRec(r, hover ? Color{ 90, 120, 200, 255 } : Color{ 50, 70, 130, 255 });
	DrawRectangleLinesEx(r, 2, RAYWHITE);
	int fs = 22;
	DrawText(label, (int)(r.x + r.width / 2 - MeasureText(label, fs) / 2), (int)(r.y + r.height / 2 - fs / 2), fs, RAYWHITE);
	return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
