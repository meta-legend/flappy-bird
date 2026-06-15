#include "game.h"

#include "constants.h"
#include "system_display.h"
#include "gameplay_helpers.h"
#include "save.h"
#include "types.h"
#include "ui.h"
#include "rlgl.h"

#include <cstdio>

// the scrollable settings list: rows are drawn into a vertical scissor, with the 800-wide content matrix-translated to center it
void FlappyGame::DrawSettingsScreen(Vector2 virtualMouse){

	DrawText("Settings", VIRTUAL_W / 2 - MeasureText("Settings", 46) / 2, 18, 46, DARKBLUE);

	// while waiting to rebind, the next key pressed becomes the new binding (ESC cancels)
	if (interfaceState.settings.rebindTarget != RebindTarget::NONE)
	{
		int k = GetKeyPressed();
		if (k != 0)
		{
			if (k == KEY_ESCAPE) interfaceState.settings.rebindTarget = RebindTarget::NONE;
			else
			{
				if (interfaceState.settings.rebindTarget == RebindTarget::PLAYER_ONE_FLAP) storage.save.keyFlapP1 = k;
				else if (interfaceState.settings.rebindTarget == RebindTarget::PLAYER_TWO_FLAP) storage.save.keyFlapP2 = k;
				else if (interfaceState.settings.rebindTarget == RebindTarget::PAUSE) storage.save.keyPause = k;
				else if (interfaceState.settings.rebindTarget == RebindTarget::RESTART) storage.save.keyRestart = k;
				else
				{
					storage.save.keyPhotoMode = k;
					// re-binding the photo key mid-press: swallow this press so it doesn't instantly trigger a capture
					interfaceState.photoMode.screenshotHoldSeconds = 0.0f;
					interfaceState.photoMode.suppressActivationUntilRelease = true;
				}
				interfaceState.settings.rebindTarget = RebindTarget::NONE;
				interfaceState.settingsDirty = true;
			}
		}
	}

	// content (kContentH) is taller than the viewport, so the list scrolls; wheel / arrows / page keys all drive it
	const float kViewTop = 70.0f;
	const float kViewBot = 510.0f;
	const float kViewH = kViewBot - kViewTop;
	const float kContentH = 1080.0f;
	float maxScroll = (kContentH > kViewH) ? (kContentH - kViewH) : 0.0f;
	interfaceState.settings.scroll -= GetMouseWheelMove() * 30.0f;
	if (interfaceState.settings.rebindTarget == RebindTarget::NONE)   // don't scroll while capturing a key (arrows would be eaten)
	{
		if (IsKeyDown(KEY_DOWN)) interfaceState.settings.scroll += 400.0f * deltaTime;
		if (IsKeyDown(KEY_UP))   interfaceState.settings.scroll -= 400.0f * deltaTime;
		if (IsKeyPressed(KEY_PAGE_DOWN)) interfaceState.settings.scroll += kViewH;
		if (IsKeyPressed(KEY_PAGE_UP))   interfaceState.settings.scroll -= kViewH;
	}
	if (interfaceState.settings.scroll < 0.0f) interfaceState.settings.scroll = 0.0f;
	if (interfaceState.settings.scroll > maxScroll) interfaceState.settings.scroll = maxScroll;
	float ofs = -interfaceState.settings.scroll;   // every row adds ofs to its y to scroll as a block

	// translate the whole content block right by ix to center the 800-wide column on the wider canvas; the mouse x is
	// shifted to match so hit-testing stays aligned (undone at the end before the bottom buttons)
	const float ix = (VIRTUAL_W - 800) / 2.0f;
	rlPushMatrix();
	rlTranslatef(ix, 0.0f, 0.0f);
	virtualMouse.x -= ix;

	BeginScaledScissor(0, (int)kViewTop, VIRTUAL_W, (int)kViewH);   // clip the rows to the scroll viewport

	DrawText("Profile", 90, (int)(80 + ofs), 22, DARKBLUE);
	DrawText("Name:", 110, (int)(114 + ofs), 18, RAYWHITE);
	{
		std::string nameDisp = storage.save.playerName.empty() ? std::string("(not set)") : storage.save.playerName;
		DrawText(nameDisp.c_str(), 220, (int)(114 + ofs), 18, SKYBLUE);
		if (UiButton(Rectangle{ 500, 108 + ofs, 130, 28 }, "Change", virtualMouse))
		{
			interfaceState.mainMenu.editingName = true;   // name entry lives on the main menu, so bounce there to edit
			GoToState(GameState::MENU);
		}
	}

	DrawText("Audio", 90, (int)(166 + ofs), 22, DARKBLUE);
	DrawText("Music:", 110, (int)(200 + ofs), 18, RAYWHITE);
	if (UiSlider(Rectangle{ 220, 204 + ofs, 220, 12 }, interfaceState.musicVolume, virtualMouse))
	{
		// dragging sets a new chosen level and unmutes
		storage.save.muted = false;
		interfaceState.savedMusicVolume = interfaceState.musicVolume;
		ApplyVolumes();
		interfaceState.settingsDirty = true;
	}
	DrawText("SFX:", 110, (int)(228 + ofs), 18, RAYWHITE);
	if (UiSlider(Rectangle{ 220, 232 + ofs, 220, 12 }, interfaceState.sfxVolume, virtualMouse))
	{
		storage.save.muted = false;
		interfaceState.savedSfxVolume = interfaceState.sfxVolume;
		ApplyVolumes();
		interfaceState.settingsDirty = true;
	}
	if (UiButton(Rectangle{ 500, 198 + ofs, 130, 28 }, storage.save.muted ? "Muted" : "Mute", virtualMouse))
	{
		// capture the current levels before muting so unmute animates back to them; the fade to/from 0 is in UpdateVolumeAnimation()
		if (!storage.save.muted)
		{
			interfaceState.savedMusicVolume = interfaceState.musicVolume;
			interfaceState.savedSfxVolume = interfaceState.sfxVolume;
		}
		storage.save.muted = !storage.save.muted;
		interfaceState.settingsDirty = true;
	}

	DrawText("Display", 90, (int)(270 + ofs), 22, DARKBLUE);
	{
		bool borderless = storage.save.resIndex == ResIndex::BORDERLESS;
		DrawText("VSync:", 110, (int)(304 + ofs), 18, RAYWHITE);
		if (UiButton(Rectangle{ 240, 298 + ofs, 90, 28 }, storage.save.vsync ? "ON" : "OFF", virtualMouse))
		{
			storage.save.vsync = !storage.save.vsync;
			ApplyFpsSettings(storage.save.vsync, storage.save.fpsCap);
			interfaceState.settingsDirty = true;
		}
		const char* fsLabel = borderless ? "Borderless" : "Windowed";
		DrawText("Display:", 110, (int)(338 + ofs), 18, RAYWHITE);
		if (UiButton(Rectangle{ 240, 332 + ofs, 170, 28 }, fsLabel, virtualMouse))
		{
			const ResIndex nextMode = borderless ? ResIndex::WINDOWED : ResIndex::BORDERLESS;
			// snapshot the windowed size/maximized state before leaving it, so returning to Windowed restores it
			// instead of a default 800x600 window
			CaptureWindowState(storage.save);
			RestoreWindowedMode(storage.save.winW, storage.save.winH, storage.save.winMaximized);
			if (nextMode == ResIndex::BORDERLESS) ToggleBorderlessWindowed();
			storage.save.resIndex = nextMode;
			interfaceState.settingsDirty = true;
		}
		DrawText("Borderless recommended (F11)", 425, (int)(330 + ofs), 13, SKYBLUE);
		DrawText("Windowed-max crops the bottom", 425, (int)(348 + ofs), 13, SKYBLUE);
		DrawText("Show FPS:", 110, (int)(372 + ofs), 18, RAYWHITE);
		if (UiButton(Rectangle{ 240, 366 + ofs, 90, 28 }, storage.save.showFps ? "ON" : "OFF", virtualMouse))
		{
			storage.save.showFps = !storage.save.showFps;
			interfaceState.settingsDirty = true;
		}
		DrawText("(only while playing)", 350, (int)(372 + ofs), 14, SKYBLUE);
		DrawText("FPS Cap:", 110, (int)(406 + ofs), 18, RAYWHITE);
		{
			// the button cycles through these preset caps; it's a no-op while vsync drives the pacing
			static const int caps[] = { 0, 30, 60, 90, 120, 144, 165, 180, 240 };
			int idx = 0;
			for (int k = 0; k < (int)(sizeof(caps) / sizeof(caps[0])); k++) if (caps[k] == storage.save.fpsCap) { idx = k; break; }
			char buf[16];
			if (storage.save.fpsCap == 0) snprintf(buf, sizeof(buf), "Unlimited");
			else snprintf(buf, sizeof(buf), "%d", storage.save.fpsCap);
			if (UiButton(Rectangle{ 240, 400 + ofs, 170, 28 }, storage.save.vsync ? "(VSync on)" : buf, virtualMouse) && !storage.save.vsync)
			{
				idx = (idx + 1) % (int)(sizeof(caps) / sizeof(caps[0]));
				storage.save.fpsCap = caps[idx];
				ApplyFpsSettings(storage.save.vsync, storage.save.fpsCap);
				interfaceState.settingsDirty = true;
			}
			if (storage.save.vsync) DrawText("ignored while VSync is on", 430, (int)(406 + ofs), 14, GRAY);
		}
	}

	DrawText("Controls", 90, (int)(454 + ofs), 22, DARKBLUE);
	DrawText("click a key to bind", 200, (int)(458 + ofs), 14, DARKGRAY);
	// each control row: clicking it arms a rebind (the label reads "press a key" until the next press is captured up top)
	DrawText("P1 Flap:", 110, (int)(488 + ofs), 18, RAYWHITE);
	if (UiButton(Rectangle{ 220, 482 + ofs, 180, 28 }, interfaceState.settings.rebindTarget == RebindTarget::PLAYER_ONE_FLAP ? "press a key" : KeyName(storage.save.keyFlapP1), virtualMouse))
		interfaceState.settings.rebindTarget = RebindTarget::PLAYER_ONE_FLAP;
	DrawText("P2 Flap:", 110, (int)(522 + ofs), 18, RAYWHITE);
	if (UiButton(Rectangle{ 220, 516 + ofs, 180, 28 }, interfaceState.settings.rebindTarget == RebindTarget::PLAYER_TWO_FLAP ? "press a key" : KeyName(storage.save.keyFlapP2), virtualMouse))
		interfaceState.settings.rebindTarget = RebindTarget::PLAYER_TWO_FLAP;
	DrawText("Pause:", 110, (int)(556 + ofs), 18, RAYWHITE);
	if (UiButton(Rectangle{ 220, 550 + ofs, 180, 28 }, interfaceState.settings.rebindTarget == RebindTarget::PAUSE ? "press a key" : KeyName(storage.save.keyPause), virtualMouse))
		interfaceState.settings.rebindTarget = RebindTarget::PAUSE;
	DrawText("Photo Mode:", 110, (int)(590 + ofs), 18, RAYWHITE);
	if (UiButton(Rectangle{ 220, 584 + ofs, 180, 28 }, interfaceState.settings.rebindTarget == RebindTarget::PHOTO_MODE ? "press a key" : KeyName(storage.save.keyPhotoMode), virtualMouse))
		interfaceState.settings.rebindTarget = RebindTarget::PHOTO_MODE;
	DrawText("Restart:", 110, (int)(624 + ofs), 18, RAYWHITE);
	if (UiButton(Rectangle{ 220, 618 + ofs, 180, 28 }, interfaceState.settings.rebindTarget == RebindTarget::RESTART ? "press a key" : KeyName(storage.save.keyRestart), virtualMouse))
		interfaceState.settings.rebindTarget = RebindTarget::RESTART;
	// reset every keybind to its canonical default, read from a default-constructed SaveData so it never drifts from save.h
	if (UiButton(Rectangle{ 410, 540 + ofs, 225, 32 }, "Reset Keybinds", virtualMouse))
	{
		const SaveData defaults{};
		storage.save.keyFlapP1 = defaults.keyFlapP1;
		storage.save.keyFlapP2 = defaults.keyFlapP2;
		storage.save.keyPause = defaults.keyPause;
		storage.save.keyPhotoMode = defaults.keyPhotoMode;
		storage.save.keyRestart = defaults.keyRestart;
		interfaceState.settings.rebindTarget = RebindTarget::NONE;
		interfaceState.settingsDirty = true;
	}

	DrawText("Screenshots", 90, (int)(666 + ofs), 22, DARKBLUE);
	DrawText("Tap the Photo Mode key to capture", 110, (int)(702 + ofs), 16, RAYWHITE);
	DrawText("Hold it to enter; drag/arrows pan, wheel or +/- zoom", 110, (int)(730 + ofs), 16, RAYWHITE);
	DrawText("Saved to your Pictures folder and copied to the clipboard", 110, (int)(758 + ofs), 14, SKYBLUE);

	DrawText("Gameplay", 90, (int)(794 + ofs), 22, DARKBLUE);
	{
		DrawText("Show ghost", 110, (int)(832 + ofs), 18, RAYWHITE);
		if (UiButton(Rectangle{ 470, 828 + ofs, 90, 26 }, storage.save.showGhost ? "ON" : "OFF", virtualMouse))
		{
			storage.save.showGhost = !storage.save.showGhost;
			interfaceState.settingsDirty = true;
		}
		DrawText("Shows your best-score run", 110, (int)(860 + ofs), 14, SKYBLUE);

		DrawText("Ghost opacity:", 110, (int)(894 + ofs), 18, RAYWHITE);
		float ghostOpacity = storage.save.ghostOpacity / 100.0f;   // slider is 0..1; stored as a 0..100 percent
		if (UiSlider(Rectangle{ 280, 898 + ofs, 220, 12 }, ghostOpacity, virtualMouse))
		{
			storage.save.ghostOpacity = (int)(ghostOpacity * 100.0f + 0.5f);
			interfaceState.settingsDirty = true;
		}
		DrawText(TextFormat("%d%%", storage.save.ghostOpacity), 520, (int)(894 + ofs), 18, SKYBLUE);

		// two plain on/off toggles, driven through pointers so the row layout doesn't repeat
		bool* flags[2] = { &storage.save.reduceMotion, &storage.save.largerHud };
		const char* labels[2] = { "Reduce motion", "Larger HUD text" };
		for (int i = 0; i < 2; i++)
		{
			float y = 934.0f + i * 34.0f + ofs;
			DrawText(labels[i], 110, (int)y + 4, 18, RAYWHITE);
			if (UiButton(Rectangle{ 470, y, 90, 26 }, *flags[i] ? "ON" : "OFF", virtualMouse))
			{
				*flags[i] = !*flags[i];
				interfaceState.settingsDirty = true;
			}
		}
	}

	EndScissorMode();

	// scrollbar down the right edge: handle height tracks the visible fraction; draggable, and clicking the track jumps to it
	if (maxScroll > 0.0f)
	{
		const float trackX = 765.0f, trackW = 14.0f;
		Rectangle track = { trackX, kViewTop, trackW, kViewH };
		DrawRectangleRec(track, Color{ 40, 40, 60, 200 });
		float handleH = kViewH * (kViewH / kContentH);
		if (handleH < 24.0f) handleH = 24.0f;
		float handleY = kViewTop + (interfaceState.settings.scroll / maxScroll) * (kViewH - handleH);
		Rectangle handle = { trackX, handleY, trackW, handleH };
		if (!interfaceState.settings.dragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
		{
			if (CheckCollisionPointRec(virtualMouse, handle))
			{
				interfaceState.settings.dragging = true;
				interfaceState.settings.dragGrab = virtualMouse.y - handleY;   // grab offset so the handle doesn't jump
			}
			else if (CheckCollisionPointRec(virtualMouse, track))
			{
				interfaceState.settings.dragging = true;
				interfaceState.settings.dragGrab = handleH * 0.5f;
				float t = (virtualMouse.y - interfaceState.settings.dragGrab - kViewTop) / (kViewH - handleH);
				if (t < 0) t = 0;
				if (t > 1) t = 1;
				interfaceState.settings.scroll = t * maxScroll;
			}
		}
		if (interfaceState.settings.dragging)
		{
			float t = (virtualMouse.y - interfaceState.settings.dragGrab - kViewTop) / (kViewH - handleH);
			if (t < 0) t = 0;
			if (t > 1) t = 1;
			interfaceState.settings.scroll = t * maxScroll;
			if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) interfaceState.settings.dragging = false;
		}
		bool handleHover = CheckCollisionPointRec(virtualMouse, handle);
		DrawRectangleRec(handle, interfaceState.settings.dragging ? GOLD : (handleHover ? Color{ 220, 220, 220, 255 } : RAYWHITE));
	}
	else
	{
		interfaceState.settings.dragging = false;
	}

	// end the centered content block: restore the mouse + matrix so the bottom buttons draw at true screen-center
	virtualMouse.x += ix;
	rlPopMatrix();

	// lift the bottom button row out of the fill-width bottom crop (maximized windowed). these sit above the very bottom
	// edge, so a half lift clears the crop without leaving a big gap under them
	const float bottomBtnY = 524.0f - FillModeBottomCrop() * 0.5f;
	if (UiButton(Rectangle{ VIRTUAL_W / 2 - 250, bottomBtnY, 240, 40 }, "Replay tutorial", virtualMouse))
	{
		// drop into the tutorial flow (its Skip lands on the main menu). reset settingsReturnTo so a later Settings
		// visit from the menu doesn't try to resume the run we're abandoning here
		interfaceState.settingsReturnTo = GameState::MENU;
		interfaceState.tutorialPage = TutorialPage::WELCOME;
		GoToState(GameState::TUTORIAL);
	}
	if (UiButton(Rectangle{ VIRTUAL_W / 2 + 10, bottomBtnY, 240, 40 }, "Save & Back", virtualMouse))
	{
		interfaceState.settings.rebindTarget = RebindTarget::NONE;
		SaveSettings();
		const GameState returnState = interfaceState.settingsReturnTo;
		interfaceState.settingsReturnTo = GameState::MENU;
		GoToState(returnState);
	}
}
