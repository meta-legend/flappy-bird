#pragma once
#include "raylib.h"
#include "save.h"
#include <vector>

// how a particle is drawn; Text uses the text field, the arrows are time-warp direction hints
enum class ParticleType { Puff, Spark, Feather, Text, BloomFlash, DownArrow, UpArrow };

// one live particle; life counts down from maxLife and life/maxLife drives the fade-out alpha
struct Particle
{
	Vector2 pos, vel;
	float life, maxLife, size;
	Color color;
	ParticleType kind;
	const char* text = "";   // only read when kind == Text
};

// spawns particles into a shared pool, honoring the player's reduce-motion and skin choices
class ParticleEmitter
{
public:
	// captures references — the pool, the reduce-motion flag, and the skin stay owned by the caller and are read live
	ParticleEmitter(std::vector<Particle>& particles, const bool& reduceMotion, const SkinIndex& skinIndex);
	// emit count particles of kind at (x, y); reduce-motion trims the count to ~30%
	void Burst(float x, float y, ParticleType kind, int count);
	// floating label that drifts up and fades (e.g. the "+1" on a score)
	void Text(float x, float y, const char* text, Color color);
	// additive-blend pickup flash at (x, y): a tight bright core plus a wider diffuse glow growing to radius size;
	// reduce-motion still shows it, at half scale and shorter life
	void BloomFlash(float x, float y, Color color, float size = 96.0f);

private:
	std::vector<Particle>& particles;
	const bool& reduceMotion;
	const SkinIndex& skinIndex;   // Feather bursts tint to match the selected bird
};

// low-level pool ops, used by the emitter and by a few call sites that don't have one
void SpawnParticle(std::vector<Particle>& particles, Particle p);
void EmitBurst(std::vector<Particle>& particles, float x, float y, ParticleType kind, int n);
void SpawnText(std::vector<Particle>& particles, float x, float y, const char* txt, Color col);
// advance every particle by dt and drop the ones whose life has hit zero
void UpdateParticles(std::vector<Particle>& particles, float dt);
