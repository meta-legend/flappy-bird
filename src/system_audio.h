#pragma once

#include "raylib.h"

// every loaded sound effect plus the looping theme music
struct GameAudio
{
	Sound hit{};
	Sound die{};
	Sound restart{};
	Sound start{};
	Sound jump{};
	Sound score{};
	Sound pickup{};   // a second copy of the score sound on its own channel, so collecting an orb doesn't cut the score chime
	Sound windBurst{};
	Music theme{};
};

// load every sound effect + the theme music (from the pak, or disk fallback)
GameAudio LoadGameAudio();
// set music and sfx gains in one pass; both volumes are 0..1
void ApplyAudioVolumes(GameAudio& audio, float musicVolume, float sfxVolume);
// free every handle in audio
void UnloadGameAudio(GameAudio& audio);
