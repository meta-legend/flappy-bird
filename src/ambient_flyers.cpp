#include "ambient_flyers.h"

#include "gameplay_helpers.h"   // Randf
#include "types.h"              // Theme, VIRTUAL_W/H

#include <cmath>

namespace
{
	constexpr float kDrawScale = 1.6f;    // sprite art is ~16-24px; this keeps flyers small/distant on the 1067-wide canvas
	constexpr float kFlapSeconds = 0.16f; // frame toggle rate
	constexpr float kAircraftY = 66.0f;   // aircraft hold this single altitude (planes/helis don't scatter)

	// spawn a flyer group entering from one side. aircraft = 1 at a fixed altitude; birds = a scattered flock of 2-3.
	// speeds are well above the sky's parallax scroll so even right-to-left flyers read as clearly moving (not stationary)
	void Spawn(AmbientFlyerSystem& sys, int flyerCount, bool aircraft)
	{
		const bool rightward = GetRandomValue(0, 1) != 0;   // travel direction (and thus facing)
		const float dir = rightward ? 1.0f : -1.0f;
		const int count = aircraft ? 1 : GetRandomValue(2, 3);
		const float speed = aircraft ? Randf(110.0f, 165.0f) : Randf(60.0f, 95.0f);
		const float startX = rightward ? -40.0f : (float)VIRTUAL_W + 40.0f;
		const float flockY = aircraft ? kAircraftY : Randf(40.0f, VIRTUAL_H * 0.32f);
		const int species = GetRandomValue(0, flyerCount - 1);   // aircraft themes may hold e.g. jet + helicopter

		int placed = 0;
		for (int i = 0; i < AmbientFlyerSystem::kMax && placed < count; i++)
		{
			AmbientFlyer& f = sys.flyers[i];
			if (f.active) continue;
			f.active = true;
			f.species = species;
			f.facingRight = rightward;
			f.vx = dir * speed * (1.0f + placed * 0.05f);        // trailing birds drift a touch faster so the flock spreads
			f.x = startX - dir * (placed * Randf(20.0f, 36.0f)); // stagger back along the travel direction
			// aircraft hold altitude exactly; birds scatter into a loose V
			f.y = aircraft ? flockY : flockY + placed * Randf(6.0f, 16.0f) * (GetRandomValue(0, 1) ? 1.0f : -1.0f);
			f.bobPhase = Randf(0.0f, 6.2832f);
			f.animTimer = Randf(0.0f, kFlapSeconds);
			f.frame = GetRandomValue(0, 1);
			placed++;
		}
	}
}

void UpdateAmbientFlyers(AmbientFlyerSystem& sys, int themeIndex, int flyerCount, bool aircraft, bool reduceMotion, float dt)
{
	if (flyerCount <= 0 || reduceMotion)
	{
		for (AmbientFlyer& f : sys.flyers) f.active = false;   // reduce-motion suppresses ambient motion, matching the shake/particle toggles
		return;
	}

	if (themeIndex != sys.lastThemeIndex)
	{
		// switched themes: retire the old flock immediately so one theme's species never renders under another's theme
		for (AmbientFlyer& f : sys.flyers) f.active = false;
		sys.lastThemeIndex = themeIndex;
		sys.spawnCountdown = Randf(3.0f, 8.0f);
	}

	int activeCount = 0;
	for (AmbientFlyer& f : sys.flyers)
	{
		if (!f.active) continue;
		f.x += f.vx * dt;
		f.bobPhase += dt * 2.0f;
		f.animTimer += dt;
		if (f.animTimer >= kFlapSeconds) { f.animTimer = 0.0f; f.frame ^= 1; }
		// cull once fully past the far edge (generous margin so the wingspan clears before removal)
		if ((f.vx > 0.0f && f.x > VIRTUAL_W + 45.0f) || (f.vx < 0.0f && f.x < -45.0f)) f.active = false;
		else activeCount++;
	}

	sys.spawnCountdown -= dt;
	if (sys.spawnCountdown <= 0.0f)
	{
		sys.spawnCountdown = Randf(10.0f, 22.0f);   // sparse: something crosses every ~10-22s
		// only spawn when the sky is empty (one group at a time) and the chance roll passes, so they stay occasional
		if (activeCount == 0 && GetRandomValue(0, 99) < 75) Spawn(sys, flyerCount, aircraft);
	}
}

void DrawAmbientFlyers(const AmbientFlyerSystem& sys, const Theme& theme)
{
	if (theme.flyerCount <= 0) return;
	for (const AmbientFlyer& f : sys.flyers)
	{
		if (!f.active) continue;
		const int species = (f.species >= 0 && f.species < theme.flyerCount) ? f.species : 0;
		const Texture2D& tex = theme.flyer[species][f.frame];
		if (tex.id == 0) continue;
		const float w = tex.width * kDrawScale;
		const float h = tex.height * kDrawScale;
		// aircraft hold a steady altitude; only birds get the gentle vertical bob
		const float bob = theme.flyerAircraft ? 0.0f : std::sin(f.bobPhase) * 2.0f;
		// negative source width mirrors the (left-facing) art when the flyer travels right
		const Rectangle src = { 0.0f, 0.0f, (f.facingRight ? -1.0f : 1.0f) * tex.width, (float)tex.height };
		const Rectangle dst = { f.x - w * 0.5f, f.y + bob - h * 0.5f, w, h };
		DrawTexturePro(tex, src, dst, Vector2{ 0.0f, 0.0f }, 0.0f, Fade(WHITE, 0.9f));
	}
}
