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
	const float kContentH = 1450.0f;
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

	// font-size constants for this screen: headings biggest, row labels middle, hint/detail smallest
	constexpr int kHeadingFs = 32;   // section headings (Profile, Audio, ...)
	constexpr int kLabelFs   = 24;   // row labels (Name:, Music:, VSync:, ...)
	constexpr int kHintFs    = 16;   // detail / hint text under or beside rows

	// shared x coords: headings + row labels all align at kLabelX. right-side hints sit further inboard at kHintX
	// so they don't crowd the scrollbar (trackX=765 in content coords)
	constexpr int kLabelX = 90;
	constexpr int kHintX  = 460;

	DrawText("Profile", kLabelX, (int)(80 + ofs), kHeadingFs, DARKBLUE);
	DrawText("Name:", kLabelX, (int)(135 + ofs), kLabelFs, RAYWHITE);
	{
		std::string nameDisp = storage.save.playerName.empty() ? std::string("(not set)") : storage.save.playerName;
		DrawText(nameDisp.c_str(), 210, (int)(135 + ofs), kLabelFs, SKYBLUE);
		if (UiButton(Rectangle{ 500, 127 + ofs, 140, 36 }, "Change", virtualMouse))
		{
			interfaceState.mainMenu.editingName = true;   // name entry lives on the main menu, so bounce there to edit
			GoToState(GameState::MENU);
		}
	}

	DrawText("Audio", kLabelX, (int)(210 + ofs), kHeadingFs, DARKBLUE);
	DrawText("Music:", kLabelX, (int)(265 + ofs), kLabelFs, RAYWHITE);
	if (UiSlider(Rectangle{ 270, 273 + ofs, 220, 12 }, interfaceState.musicVolume, virtualMouse))
	{
		// dragging sets a new chosen level and unmutes
		storage.save.muted = false;
		interfaceState.savedMusicVolume = interfaceState.musicVolume;
		ApplyVolumes();
		interfaceState.settingsDirty = true;
	}
	DrawText("SFX:", kLabelX, (int)(310 + ofs), kLabelFs, RAYWHITE);
	if (UiSlider(Rectangle{ 270, 318 + ofs, 220, 12 }, interfaceState.sfxVolume, virtualMouse))
	{
		storage.save.muted = false;
		interfaceState.savedSfxVolume = interfaceState.sfxVolume;
		ApplyVolumes();
		interfaceState.settingsDirty = true;
	}
	if (UiButton(Rectangle{ 530, 277 + ofs, 140, 36 }, storage.save.muted ? "Muted" : "Mute", virtualMouse))
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

	DrawText("Display", kLabelX, (int)(380 + ofs), kHeadingFs, DARKBLUE);
	{
		bool borderless = storage.save.resIndex == ResIndex::BORDERLESS;
		DrawText("VSync:", kLabelX, (int)(435 + ofs), kLabelFs, RAYWHITE);
		if (UiButton(Rectangle{ 240, 427 + ofs, 100, 36 }, storage.save.vsync ? "ON" : "OFF", virtualMouse))
		{
			storage.save.vsync = !storage.save.vsync;
			ApplyFpsSettings(storage.save.vsync, storage.save.fpsCap);
			interfaceState.settingsDirty = true;
		}
		const char* fsLabel = borderless ? "Borderless" : "Windowed";
		DrawText("Display:", kLabelX, (int)(485 + ofs), kLabelFs, RAYWHITE);
		if (UiButton(Rectangle{ 240, 477 + ofs, 180, 36 }, fsLabel, virtualMouse))
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
		DrawText("Borderless recommended (F11)", kHintX, (int)(477 + ofs), kHintFs, SKYBLUE);
		DrawText("Windowed-max crops the bottom", kHintX, (int)(499 + ofs), kHintFs, SKYBLUE);
		DrawText("Show FPS:", kLabelX, (int)(535 + ofs), kLabelFs, RAYWHITE);
		if (UiButton(Rectangle{ 240, 527 + ofs, 100, 36 }, storage.save.showFps ? "ON" : "OFF", virtualMouse))
		{
			storage.save.showFps = !storage.save.showFps;
			interfaceState.settingsDirty = true;
		}
		DrawText("(only while playing)", 360, (int)(535 + ofs), kHintFs, SKYBLUE);
		DrawText("FPS Cap:", kLabelX, (int)(585 + ofs), kLabelFs, RAYWHITE);
		{
			// the button cycles through these preset caps; it's a no-op while vsync drives the pacing
			static const int caps[] = { 0, 30, 60, 90, 120, 144, 165, 180, 240 };
			int idx = 0;
			for (int k = 0; k < (int)(sizeof(caps) / sizeof(caps[0])); k++) if (caps[k] == storage.save.fpsCap) { idx = k; break; }
			char buf[16];
			if (storage.save.fpsCap == 0) snprintf(buf, sizeof(buf), "Unlimited");
			else snprintf(buf, sizeof(buf), "%d", storage.save.fpsCap);
			if (UiButton(Rectangle{ 240, 577 + ofs, 180, 36 }, storage.save.vsync ? "(VSync on)" : buf, virtualMouse) && !storage.save.vsync)
			{
				idx = (idx + 1) % (int)(sizeof(caps) / sizeof(caps[0]));
				storage.save.fpsCap = caps[idx];
				ApplyFpsSettings(storage.save.vsync, storage.save.fpsCap);
				interfaceState.settingsDirty = true;
			}
			if (storage.save.vsync) DrawText("ignored while VSync is on", kHintX, (int)(585 + ofs), kHintFs, SKYBLUE);
		}
	}

	DrawText("Controls", kLabelX, (int)(660 + ofs), kHeadingFs, DARKBLUE);
	// hint on its own line, well below the heading so they don't collide
	DrawText("Click a key to bind", kLabelX, (int)(705 + ofs), kHintFs, SKYBLUE);
	// each control row: clicking it arms a rebind (the label reads "press a key" until the next press is captured up top).
	// buttons sit at x=290 (not 240) so the wide "Photo Mode:" label has room to render without bleeding under the button
	DrawText("P1 Flap:", kLabelX, (int)(745 + ofs), kLabelFs, RAYWHITE);
	if (UiButton(Rectangle{ 290, 737 + ofs, 190, 36 }, interfaceState.settings.rebindTarget == RebindTarget::PLAYER_ONE_FLAP ? "press a key" : KeyName(storage.save.keyFlapP1), virtualMouse))
		interfaceState.settings.rebindTarget = RebindTarget::PLAYER_ONE_FLAP;
	DrawText("P2 Flap:", kLabelX, (int)(790 + ofs), kLabelFs, RAYWHITE);
	if (UiButton(Rectangle{ 290, 782 + ofs, 190, 36 }, interfaceState.settings.rebindTarget == RebindTarget::PLAYER_TWO_FLAP ? "press a key" : KeyName(storage.save.keyFlapP2), virtualMouse))
		interfaceState.settings.rebindTarget = RebindTarget::PLAYER_TWO_FLAP;
	DrawText("Pause:", kLabelX, (int)(835 + ofs), kLabelFs, RAYWHITE);
	if (UiButton(Rectangle{ 290, 827 + ofs, 190, 36 }, interfaceState.settings.rebindTarget == RebindTarget::PAUSE ? "press a key" : KeyName(storage.save.keyPause), virtualMouse))
		interfaceState.settings.rebindTarget = RebindTarget::PAUSE;
	DrawText("Photo Mode:", kLabelX, (int)(880 + ofs), kLabelFs, RAYWHITE);
	if (UiButton(Rectangle{ 290, 872 + ofs, 190, 36 }, interfaceState.settings.rebindTarget == RebindTarget::PHOTO_MODE ? "press a key" : KeyName(storage.save.keyPhotoMode), virtualMouse))
		interfaceState.settings.rebindTarget = RebindTarget::PHOTO_MODE;
	DrawText("Restart:", kLabelX, (int)(925 + ofs), kLabelFs, RAYWHITE);
	if (UiButton(Rectangle{ 290, 917 + ofs, 190, 36 }, interfaceState.settings.rebindTarget == RebindTarget::RESTART ? "press a key" : KeyName(storage.save.keyRestart), virtualMouse))
		interfaceState.settings.rebindTarget = RebindTarget::RESTART;
	// reset every keybind to its canonical default, read from a default-constructed SaveData so it never drifts from save.h
	if (UiButton(Rectangle{ 510, 825 + ofs, 230, 40 }, "Reset Keybinds", virtualMouse))
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

	DrawText("Screenshots", kLabelX, (int)(990 + ofs), kHeadingFs, DARKBLUE);
	DrawText("Tap the Photo Mode key to capture", kLabelX, (int)(1045 + ofs), 18, RAYWHITE);
	DrawText("Hold it to enter; drag/arrows pan, wheel or +/- zoom", kLabelX, (int)(1075 + ofs), 18, RAYWHITE);
	DrawText("Saved to your Pictures folder and copied to the clipboard", kLabelX, (int)(1105 + ofs), kHintFs, SKYBLUE);

	DrawText("Gameplay", kLabelX, (int)(1170 + ofs), kHeadingFs, DARKBLUE);
	{
		DrawText("Show ghost", kLabelX, (int)(1225 + ofs), kLabelFs, RAYWHITE);
		if (UiButton(Rectangle{ 500, 1217 + ofs, 100, 36 }, storage.save.showGhost ? "ON" : "OFF", virtualMouse))
		{
			storage.save.showGhost = !storage.save.showGhost;
			interfaceState.settingsDirty = true;
		}
		DrawText("Shows your best-score run", kLabelX, (int)(1260 + ofs), kHintFs, SKYBLUE);

		DrawText("Ghost opacity:", kLabelX, (int)(1300 + ofs), kLabelFs, RAYWHITE);
		float ghostOpacity = storage.save.ghostOpacity / 100.0f;   // slider is 0..1; stored as a 0..100 percent
		if (UiSlider(Rectangle{ 360, 1308 + ofs, 200, 12 }, ghostOpacity, virtualMouse))
		{
			storage.save.ghostOpacity = (int)(ghostOpacity * 100.0f + 0.5f);
			interfaceState.settingsDirty = true;
		}
		DrawText(TextFormat("%d%%", storage.save.ghostOpacity), 580, (int)(1300 + ofs), kLabelFs, SKYBLUE);

		// two plain on/off toggles, driven through pointers so the row layout doesn't repeat
		bool* flags[2] = { &storage.save.reduceMotion, &storage.save.largerHud };
		const char* labels[2] = { "Reduce motion", "Larger HUD text" };
		for (int i = 0; i < 2; i++)
		{
			float y = 1345.0f + i * 42.0f + ofs;
			DrawText(labels[i], kLabelX, (int)y + 4, kLabelFs, RAYWHITE);
			if (UiButton(Rectangle{ 500, y, 100, 36 }, *flags[i] ? "ON" : "OFF", virtualMouse))
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
