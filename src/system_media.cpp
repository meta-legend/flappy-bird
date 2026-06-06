#include "system_audio.h"
#include "font.h"
#include "pak_archive.h"

Font gPixelFont = { 0 };

// load the pixel font oversized (48px) so shrinking it via PX_SCALE stays crisp; bilinear smooths the scaled glyphs
void LoadGameFont()
{
	gPixelFont = LoadFontExViaPak("./resources/font/PublicPixel-0W5Kv.ttf", 48, nullptr, 0);
	SetTextureFilter(gPixelFont.texture, TEXTURE_FILTER_BILINEAR);
}

void UnloadGameFont()
{
	UnloadFont(gPixelFont);
	gPixelFont = { 0 };
}

GameAudio LoadGameAudio()
{
	GameAudio audio{};
	audio.hit = LoadSoundViaPak("./resources/sounds/sfx/sfx_hit.wav");
	audio.die = LoadSoundViaPak("./resources/sounds/sfx/sfx_die.wav");
	audio.restart = LoadSoundViaPak("./resources/sounds/sfx/sfx_point.wav");   // restart reuses the point chime
	audio.start = LoadSoundViaPak("./resources/sounds/sfx/sfx_swooshing.wav");
	audio.jump = LoadSoundViaPak("./resources/sounds/sfx/sfx_wing.wav");
	audio.score = LoadSoundViaPak("./resources/sounds/sfx/sfx_point.wav");
	// pickup shares the point sample on its own channel (pitched up so it reads as distinct) — scoring and
	// collecting an orb land at the same spot, so they must not restart or cut each other
	audio.pickup = LoadSoundAlias(audio.score);
	SetSoundPitch(audio.pickup, 1.25f);
	audio.windBurst = LoadSoundViaPak("./resources/sounds/sfx/sfx_wind_burst.wav");
	audio.theme = LoadMusicStreamViaPak("./resources/sounds/music/theme.mp3");
	return audio;
}

void ApplyAudioVolumes(GameAudio& audio, float musicVolume, float sfxVolume)
{
	SetMusicVolume(audio.theme, musicVolume);
	SetSoundVolume(audio.hit, sfxVolume);
	SetSoundVolume(audio.die, sfxVolume);
	SetSoundVolume(audio.restart, sfxVolume);
	SetSoundVolume(audio.start, sfxVolume);
	SetSoundVolume(audio.jump, sfxVolume);
	SetSoundVolume(audio.score, sfxVolume);
	SetSoundVolume(audio.pickup, sfxVolume);
	SetSoundVolume(audio.windBurst, sfxVolume);
}

void UnloadGameAudio(GameAudio& audio)
{
	UnloadSound(audio.hit);
	UnloadSound(audio.die);
	UnloadSound(audio.restart);
	UnloadSound(audio.start);
	UnloadSound(audio.jump);
	UnloadSoundAlias(audio.pickup);   // free the alias handle before the sound that owns the shared buffer
	UnloadSound(audio.score);
	UnloadSound(audio.windBurst);
	UnloadMusicStream(audio.theme);
	audio = {};
}
