#include "particles.h"
#include "gameplay_helpers.h"

ParticleEmitter::ParticleEmitter(std::vector<Particle>& particleList,
	const bool& reduceMotionSetting, const SkinIndex& selectedSkin)
	: particles(particleList), reduceMotion(reduceMotionSetting), skinIndex(selectedSkin)
{
}

void ParticleEmitter::Burst(float x, float y, ParticleType kind, int count)
{
	if (reduceMotion) count = count * 3 / 10;   // reduce-motion: keep only ~30% of the particles
	// non-feather kinds use the plain table-driven burst; feathers need per-particle skin tinting + gravity
	if (kind != ParticleType::Feather)
	{
		EmitBurst(particles, x, y, kind, count);
		return;
	}

	for (int i = 0; i < count; i++)
	{
		Particle particle{};
		particle.pos = { x, y };
		particle.kind = ParticleType::Feather;
		const float angle = Randf(0, 6.2832f);
		const float speed = Randf(80, 150);
		particle.vel = { cosf(angle) * speed, sinf(angle) * speed };
		particle.maxLife = particle.life = 1.2f;
		particle.size = 4.0f;
		// tint feathers to match the bird: rainbow gets a random hue, orange/yellow get their solid color
		particle.color = skinIndex == SkinIndex::RAINBOW_BIRD ? ColorFromHSV((float)GetRandomValue(0, 359), 0.85f, 1.0f)
			: skinIndex == SkinIndex::ORANGE_BIRD ? ORANGE
			: Color{ 250, 220, 60, 255 };
		SpawnParticle(particles, particle);
	}
}

void ParticleEmitter::Text(float x, float y, const char* text, Color color)
{
	SpawnText(particles, x, y, text, color);
}

void ParticleEmitter::BloomFlash(float x, float y, Color color, float size)
{
	// reduce-motion still shows the flash, at half scale + shorter life
	const float scale = reduceMotion ? 0.5f : 1.0f;
	const float life = reduceMotion ? 0.25f : 0.45f;
	// inner tight core: small, bright, short-lived
	{
		Particle p{};
		p.pos = { x, y };
		p.vel = { 0.0f, 0.0f };
		p.kind = ParticleType::BloomFlash;
		p.color = color;
		p.size = size * 0.45f * scale;
		p.maxLife = p.life = life * 0.7f;
		SpawnParticle(particles, p);
	}
	// outer diffuse glow: larger, half-alpha, longer-lived
	{
		Particle p{};
		p.pos = { x, y };
		p.vel = { 0.0f, 0.0f };
		p.kind = ParticleType::BloomFlash;
		p.color = { color.r, color.g, color.b, (unsigned char)(color.a / 2) };
		p.size = size * scale;
		p.maxLife = p.life = life;
		SpawnParticle(particles, p);
	}
}

// push p into the pool, but hard-cap at 256 so a heavy frame can't grow it unbounded (extras are silently dropped)
void SpawnParticle(std::vector<Particle>& particles, Particle p)
{
	if (particles.size() < 256) particles.push_back(p);
}

// spawn n particles of one kind at (x, y); each kind sets its own velocity / size / color / life below
void EmitBurst(std::vector<Particle>& particles, float x, float y, ParticleType kind, int n)
{
	for (int i = 0; i < n; i++)
	{
		Particle p{};
		p.pos = { x, y };
		p.kind = kind;
		switch (kind)
		{
		case ParticleType::Puff:
			p.vel = { Randf(-80, -40), Randf(-20, 20) };
			p.maxLife = p.life = 0.4f; p.size = 4.0f; p.color = WHITE;
			break;
		case ParticleType::Spark:
		{
			float a = Randf(0, 6.2832f), s = Randf(150, 250);
			p.vel = { cosf(a) * s, sinf(a) * s };
			p.maxLife = p.life = 0.35f; p.size = 3.0f;
			p.color = Color{ (unsigned char)GetRandomValue(220,255), (unsigned char)GetRandomValue(170,240), 60, 255 };
			break;
		}
		case ParticleType::Feather:
		{
			float a = Randf(0, 6.2832f), s = Randf(80, 150);
			p.vel = { cosf(a) * s, sinf(a) * s - 50 };
			p.maxLife = p.life = 0.8f; p.size = 5.0f; p.color = RAYWHITE;
			break;
		}
		case ParticleType::DownArrow:
		{
			// time-orb "slowing" feedback: fan the arrows out around the orb (the i term centers the row) and drop them
			p.pos = { x + (i - (n - 1) * 0.5f) * 14.0f, y };
			p.vel = { 0.0f, Randf(50, 85) };
			p.maxLife = p.life = 0.9f; p.size = 8.0f;
			p.color = Color{ 1, 204, 251, 255 };
			break;
		}
		case ParticleType::UpArrow:
		{
			// "speeding" feedback: same fan, moving up, warmer color so the two outcomes read differently
			p.pos = { x + (i - (n - 1) * 0.5f) * 14.0f, y };
			p.vel = { 0.0f, Randf(-95, -60) };
			p.maxLife = p.life = 0.9f; p.size = 8.0f;
			p.color = Color{ 255, 180, 55, 255 };
			break;
		}
		case ParticleType::Text:
			break;   // text particles come from SpawnText, not here
		}
		SpawnParticle(particles, p);
	}
}

void SpawnText(std::vector<Particle>& particles, float x, float y, const char* txt, Color col)
{
	Particle p{};
	p.pos = { x, y }; p.vel = { 0, -50 }; p.maxLife = p.life = 0.6f;   // drifts straight up as it fades
	p.kind = ParticleType::Text; p.color = col; p.text = txt;
	SpawnParticle(particles, p);
}

void UpdateParticles(std::vector<Particle>& particles, float dt)
{
	for (size_t i = 0; i < particles.size(); )
	{
		Particle& p = particles[i];
		p.life -= dt;
		// swap-and-pop dead particles: O(1) removal that doesn't preserve order (fine here), so don't i++ on removal
		if (p.life <= 0.0f) { particles[i] = particles.back(); particles.pop_back(); continue; }
		p.pos.x += p.vel.x * dt;
		p.pos.y += p.vel.y * dt;
		if (p.kind == ParticleType::Feather) p.vel.y += 600.0f * dt;   // only feathers feel gravity; the rest drift
		i++;
	}
}
