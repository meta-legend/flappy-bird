#include "system_display.h"

#include "constants.h"
#include "pak_archive.h"
#include "types.h"

// raylib's high-level API has no runtime vsync toggle in this version, so we reach into GLFW directly
#include <GLFW/glfw3.h>

void SetFlappyWindowIcons()
{
	// build the standard icon sizes from one source image; nearest-neighbor keeps the pixel art crisp
	Image src = LoadImageViaPak("./resources/images/ui/icon.png");
	Image sizes[4] = { ImageCopy(src), ImageCopy(src), ImageCopy(src), ImageCopy(src) };
	ImageResizeNN(&sizes[0], 16, 16);
	ImageResizeNN(&sizes[1], 32, 32);
	ImageResizeNN(&sizes[2], 48, 48);
	ImageResizeNN(&sizes[3], 64, 64);
	SetWindowIcons(sizes, 4);
	SetWindowIcon(sizes[1]);   // the single-icon API wants one; 32px is the taskbar size
	for (int i = 0; i < 4; i++) UnloadImage(sizes[i]);
	UnloadImage(src);
}

void RestoreWindowedMode(int winW, int winH, bool maximized)
{
	if (winW < 320) winW = 800;
	if (winH < 240) winH = 600;
	// drop any fullscreen/borderless state first, then settle into a plain resizable window
	if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE))
		ToggleBorderlessWindowed();
	if (IsWindowFullscreen())
		ToggleFullscreen();
	ClearWindowState(FLAG_FULLSCREEN_MODE | FLAG_BORDERLESS_WINDOWED_MODE | FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TOPMOST | FLAG_WINDOW_MAXIMIZED);
	SetWindowState(FLAG_WINDOW_RESIZABLE);
	SetWindowSize(winW, winH);

	// center on the current monitor
	int mon = GetCurrentMonitor();
	int posX = (GetMonitorWidth(mon) - winW) / 2;
	int posY = (GetMonitorHeight(mon) - winH) / 2;
	if (posX < 0) posX = 0;
	// leave room for the title bar, or it clips above y=0 when the windowed size matches the monitor height
	if (posY < 40) posY = 40;
	SetWindowPosition(posX, posY);

	// re-maximize if that's the state we came from, rather than dropping to the plain winW x winH size
	if (maximized) MaximizeWindow();
}

void ApplyStartupDisplayMode(ResIndex mode, int winW, int winH)
{
	if (mode == ResIndex::BORDERLESS && IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE))
		return;   // already where we want to be

	// settle into a clean windowed baseline at the saved size, then toggle into the persisted target
	// (only Windowed and Borderless exist now)
	RestoreWindowedMode(winW, winH);
	if (mode == ResIndex::BORDERLESS) ToggleBorderlessWindowed();
}

void ApplyFpsSettings(bool vsync, int fpsCap)
{
	// FLAG_VSYNC_HINT at InitWindow turns the GPU swap interval on once; it stays on for the window's life unless we
	// toggle it. SetTargetFPS is only a CPU-side frame limiter — it does NOT control vsync. glfwSwapInterval is the
	// real lever (1 = vsync on, 0 = off)
	glfwSwapInterval(vsync ? 1 : 0);
	// IMPORTANT: with vsync on, do NOT also engage the SetTargetFPS limiter. two limiters fight: the software sleep
	// targets 1/refresh using imprecise OS sleep, then hardware vsync blocks until vblank. under the heavier in-game
	// load the sleep occasionally overshoots a vblank and waits for the NEXT one — a beat pattern of double-length
	// frames, so GetFrameTime() oscillates and the scroll visibly judders. letting hardware vsync pace alone keeps
	// deltaTime clean, so SetTargetFPS(0) removes the software limiter entirely
	SetTargetFPS(vsync ? 0 : (fpsCap > 0 ? fpsCap : 0));
}

bool ReconcileStartupDisplay(StartupDisplayTracker& tracker, const SaveData& save)
{
	if (tracker.presentedFrames < 0) return true;    // -1 = already reconciled
	if (++tracker.presentedFrames < 3) return false; // wait 3 presented frames for the WM to settle before forcing the mode

	tracker.presentedFrames = -1;
	const bool isBorderless = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
	if (save.resIndex == ResIndex::BORDERLESS && !isBorderless) ToggleBorderlessWindowed();
	else if (save.resIndex == ResIndex::WINDOWED)
	{
		if (isBorderless) ToggleBorderlessWindowed();
		const bool isMaximized = IsWindowMaximized();
		if (save.winMaximized && !isMaximized) MaximizeWindow();
		else if (!save.winMaximized && isMaximized) RestoreWindow();
	}
	return true;
}

void CaptureWindowState(SaveData& save)
{
	if (IsWindowFullscreen() || IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) return;   // only a windowed size is worth saving

	const bool maximized = IsWindowMaximized();
	if (!maximized)
	{
		// record the live size only when un-maximized, so winW/winH stay the "restore" size, not the maximized one
		const int width = GetScreenWidth();
		const int height = GetScreenHeight();
		if (width >= 320 && height >= 240)
		{
			save.winW = width;
			save.winH = height;
		}
	}
	save.winMaximized = maximized;
}

void ToggleBorderlessMode(SaveData& save)
{
	const bool wasBorderless = save.resIndex == ResIndex::BORDERLESS;
	CaptureWindowState(save);   // snapshot the current windowed size first, so we can restore it later

	if (wasBorderless)
	{
		// borderless -> windowed: restore the prior windowed size + maximized state
		RestoreWindowedMode(save.winW, save.winH, save.winMaximized);
		save.resIndex = ResIndex::WINDOWED;
	}
	else
	{
		ToggleBorderlessWindowed();
		save.resIndex = ResIndex::BORDERLESS;
	}
}

// max logical px the fill-width present may crop off the bottom before it gives up and letterboxes instead. a normal
// 16:9 maximized window (title bar + taskbar eating the height) needs ~40-44px cropped to fill the width, so the cap
// must clear that or maximized wrongly falls into letterbox. 50 covers every 16:9 desktop config while still rejecting
// genuine ultrawide (21:9 maximized wants 150px+). the ground band is 80px tall (BaseTop = VIRTUAL_H-80), so 50px still
// leaves ground under the bird
static constexpr float kFillMaxBottomCrop = 50.0f;

// how many logical pixels the fill-width present is cropping off the BOTTOM right now (0 when letterboxing or an exact
// fit); bottom-anchored UI (the menu icon row) shifts up by this to stay clear of the crop
float FillModeBottomCrop()
{
	const float winW = (float)GetScreenWidth();
	const float winH = (float)GetScreenHeight();
	const float scaleX = winW / VIRTUAL_W;
	const float bottomCrop = VIRTUAL_H - winH / scaleX;   // > 0 only when the window is wider than 16:9
	return (bottomCrop > 0.0f && bottomCrop <= kFillMaxBottomCrop) ? bottomCrop : 0.0f;
}

Rectangle ComputePresentationRect()
{
	const float winW = (float)GetScreenWidth();
	const float winH = (float)GetScreenHeight();
	const float scaleX = winW / VIRTUAL_W;   // scale that fills the width exactly
	const float scaleY = winH / VIRTUAL_H;   // scale that fills the height exactly

	// prefer "fill the width" so there are never side bars. when the window is wider than the 16:9 canvas (e.g. a
	// maximized window minus its title bar) the overflow is cropped entirely off the BOTTOM (top-aligned), so the HUD
	// up top is never clipped — only sky/ground at the very bottom. only while that crop stays under the cap; past it
	// (ultrawide, or a window taller than the canvas) fall back to centered letterboxing instead of eating the playfield
	const float bottomCrop = VIRTUAL_H - winH / scaleX;
	if (bottomCrop >= 0.0f && bottomCrop <= kFillMaxBottomCrop)
		return Rectangle{ 0.0f, 0.0f, VIRTUAL_W * scaleX, VIRTUAL_H * scaleX };   // top-aligned, crop the bottom

	const float scale = (scaleX < scaleY) ? scaleX : scaleY;   // letterbox = min scale, centered
	return Rectangle{
		(winW - VIRTUAL_W * scale) * 0.5f,
		(winH - VIRTUAL_H * scale) * 0.5f,
		VIRTUAL_W * scale,
		VIRTUAL_H * scale
	};
}

Vector2 WindowToVirtualMouse(Rectangle dstRec)
{
	// invert ComputePresentationRect: subtract the rect origin, then divide by the present scale
	float scale = dstRec.width / VIRTUAL_W;
	return Vector2{
		(GetMousePosition().x - dstRec.x) / scale,
		(GetMousePosition().y - dstRec.y) / scale
	};
}
