#include "system_audio.h"
#include "system_display.h"
#include "font.h"
#include "game.h"
#include "net.h"
#include "pak_archive.h"
#include "save.h"
#include "types.h"
#include "raylib.h"

int main()
{
	// run relative to the exe so ./data.pak and ./resources resolve no matter where it's launched from
	ChangeDirectory(GetApplicationDirectory());

	// open the asset archive; if data.pak is absent, every loader falls back to ./resources/* on disk (the dev path)
	GlobalPakArchive().Open("./data.pak");

	// peek at the save before creating the window, so we can open straight into the user's last display mode
	SaveData startupSave;
	const bool hasSave = LoadSave(startupSave, SaveDir() + "/save.bin");
	const ResIndex startupResolution = hasSave ? startupSave.resIndex : ResIndex::BORDERLESS;
	const bool startupMaximized = hasSave && startupSave.winMaximized;

	int configFlags = FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE;
	if (startupResolution == ResIndex::WINDOWED && startupMaximized) configFlags |= FLAG_WINDOW_MAXIMIZED;
	SetConfigFlags(configFlags);

	InitWindow(VIRTUAL_W, VIRTUAL_H, "Flappy Bird");
	SetExitKey(KEY_NULL);   // disable raylib's built-in ESC-to-quit; the game owns its own exit flow
	SetFlappyWindowIcons();
	LoadGameFont();

	// the scene renders into this oversized target (VIRTUAL_* x RENDER_SUPERSAMPLE) and is bilinear-downscaled
	// to the window on present (see Draw()); the bilinear filter set here is that downscale step
	RenderTexture2D screen = LoadRenderTexture(VIRTUAL_W * RENDER_SUPERSAMPLE, VIRTUAL_H * RENDER_SUPERSAMPLE);
	SetTextureFilter(screen.texture, TEXTURE_FILTER_BILINEAR);

	InitAudioDevice();
	GameAudio audio = LoadGameAudio();
	GamePlatform platform{ screen, audio };

	// inner scope so the game (and every GPU handle it owns) destructs before we tear down audio + the window below
	{
		FlappyGame game(platform);
		while (game.ShouldContinue())
		{
			game.RunFrame();
		}
	}

	UnloadGameAudio(audio);
	CloseAudioDevice();
	UnloadRenderTexture(screen);
	UnloadGameFont();
	CloseWindow();
	return 0;
}
