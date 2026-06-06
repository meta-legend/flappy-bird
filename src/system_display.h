#pragma once

#include "raylib.h"
#include "save.h"

// counts frames actually presented since launch; ReconcileStartupDisplay uses it to wait until the
// window has settled before forcing the saved fullscreen/borderless mode
struct StartupDisplayTracker
{
	int presentedFrames = 0;
};

// set the multi-resolution window icon from the embedded app icons
void SetFlappyWindowIcons();
// drop to a windowed winW x winH (optionally maximized), e.g. when leaving fullscreen
void RestoreWindowedMode(int winW, int winH, bool maximized = false);
// apply the saved display mode at startup (windowed / borderless / fullscreen) at winW x winH
void ApplyStartupDisplayMode(ResIndex mode, int winW, int winH);
// apply the vsync flag and the frame-rate cap
void ApplyFpsSettings(bool vsync, int fpsCap);
// call once per early frame: defers the saved fullscreen/borderless mode until the window has presented
// enough frames to settle (some WMs ignore mode changes before then); returns true once reconciliation is done
bool ReconcileStartupDisplay(StartupDisplayTracker& tracker, const SaveData& save);
// write the current window size / maximized state back into save for next launch
void CaptureWindowState(SaveData& save);
// flip borderless on/off and persist the choice into save
void ToggleBorderlessMode(SaveData& save);
// destination rect mapping the virtual render texture onto the window (letterbox bars or fill)
Rectangle ComputePresentationRect();
// how many logical (virtual) pixels fill mode crops off the bottom; bottom-anchored UI shifts up by this
float FillModeBottomCrop();
// map a window-space mouse position into virtual 1067x600 space, given the presentation rect dstRec
Vector2 WindowToVirtualMouse(Rectangle dstRec);
