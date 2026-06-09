#include "assets.h"
#include "constants.h"
#include "font.h"
#include "gameplay_helpers.h"
#include "pak_archive.h"
#include "save.h"

#include <algorithm>
#include <ctime>
#include <string>

// extract the bottom 45% of the image as the near parallax band
Texture2D MakeMidBand(const Image& full)
{
	int bandH = full.height * 45 / 100;
	Image band = ImageFromImage(full, Rectangle{ 0, (float)(full.height - bandH), (float)full.width, (float)bandH });
	Texture2D t = LoadTextureFromImage(band);
	UnloadImage(band);
	return t;
}

// 3x nearest-neighbor upscale (keeps the pixel art crisp), then crop to the bottom VIRTUAL_H so the horizon sits right
Image BakePackBackground(const char* path)
{
	Image img = LoadImageViaPak(path);
	ImageResizeNN(&img, img.width * 3, img.height * 3);
	ImageCrop(&img, Rectangle{ 0, (float)(img.height - VIRTUAL_H), (float)img.width, (float)VIRTUAL_H });
	return img;
}

// 3x nearest-neighbor upscale; the caller (MakeMidBand / mid layer) places it
Image BakePackForeground(const char* path)
{
	Image img = LoadImageViaPak(path);
	ImageResizeNN(&img, img.width * 3, img.height * 3);
	return img;
}

// bake the small day/night thumbnails the Customize theme picker shows; cheap raw-source downscales, exact crop fidelity
// doesn't matter at ~120x90 px
static void BakeThemeThumbnails(Theme& t)
{
	auto makeThumb = [](const std::string& path) -> Texture2D {
		Image img = LoadImageViaPak(path.c_str());
		ImageResizeNN(&img, 120, 90);
		Texture2D tex = LoadTextureFromImage(img);
		UnloadImage(img);
		return tex;
	};
	t.dayThumb = makeThumb(t.dayPath);
	t.nightThumb = t.nightPath.empty() ? makeThumb(t.dayPath) : makeThumb(t.nightPath);   // fall back to the day art if there's no night source
}

void LoadThemeVisuals(Theme& t)
{
	if (t.visualsLoaded) return;   // idempotent: the active theme is ensured every frame, so this must no-op once loaded

	Image day;
	Image night = { 0 };
	if (t.isClassic)
	{
		day = LoadImageViaPak(t.dayPath.c_str());
		night = LoadImageViaPak(t.nightPath.c_str());
	}
	else
	{
		day = BakePackBackground(t.dayPath.c_str());
		if (!t.nightPath.empty()) night = BakePackBackground(t.nightPath.c_str());
		else { night = ImageCopy(day); ImageColorBrightness(&night, -70); }   // no night art: darken the day image instead
	}
	t.bg = LoadTextureFromImage(day);
	t.bgNight = LoadTextureFromImage(night);

	if (t.hasMid)
	{
		// near layer comes from explicit *_near art if provided, else the bottom band of the background
		if (!t.dayNearPath.empty())
		{
			Image dn = BakePackForeground(t.dayNearPath.c_str());
			t.mid = LoadTextureFromImage(dn);
			UnloadImage(dn);
		}
		else t.mid = MakeMidBand(day);

		if (!t.nightNearPath.empty())
		{
			Image nn = BakePackForeground(t.nightNearPath.c_str());
			t.midNight = LoadTextureFromImage(nn);
			UnloadImage(nn);
		}
		else t.midNight = MakeMidBand(night);
	}
	else
	{
		Image blank = GenImageColor(1, 1, BLANK);   // no mid layer: a 1x1 blank keeps the draw code uniform
		t.mid = LoadTextureFromImage(blank);
		t.midNight = LoadTextureFromImage(blank);
		UnloadImage(blank);
	}
	UnloadImage(day);
	UnloadImage(night);
	// left at the POINT/REPEAT default. smooth scrolling now comes from the supersampled render target (RENDER_SUPERSAMPLE),
	// not per-texture bilinear. bilinear here was actively harmful on the near/mid layer: its hill silhouette has binary
	// alpha (transparent texels are (0,0,0,0) black), so the filter bled the edge toward black and drew a dark outline —
	// the "border" between near and far layers. POINT samples cleanly, so the seam is gone
	t.visualsLoaded = true;
}

void UnloadThemeVisuals(Theme& t)
{
	if (!t.visualsLoaded) return;
	UnloadTexture(t.bg);       t.bg = Texture2D{};
	UnloadTexture(t.bgNight);  t.bgNight = Texture2D{};
	UnloadTexture(t.mid);      t.mid = Texture2D{};
	UnloadTexture(t.midNight); t.midNight = Texture2D{};
	t.visualsLoaded = false;
}

Theme MakeClassicTheme()
{
	// construction sets only the lightweight bits: name, source spec, ground strip, and picker thumbnails. the full
	// bg/mid textures bake on demand in LoadThemeVisuals when this theme becomes active; pipes come from ApplyPipeChoice
	Theme t{};
	t.name = "Classic";
	t.isClassic = true;
	t.dayPath = "./resources/images/backgrounds/classic_day.png";
	t.nightPath = "./resources/images/backgrounds/classic_night.png";
	t.hasMid = true;
	t.base = LoadTextureViaPak("./resources/images/ground/classic.png");   // POINT/REPEAT default; supersampling smooths the scroll
	BakeThemeThumbnails(t);
	return t;
}

Theme BakePackTheme(const char* name, const char* dayPath, const char* nightPath,
	const char* basePath, bool hasMid,
	const char* dayNearPath, const char* nightNearPath)
{
	Theme t{};
	t.name = name;
	t.isClassic = false;
	t.dayPath = dayPath ? dayPath : "";
	t.nightPath = nightPath ? nightPath : "";
	t.dayNearPath = dayNearPath ? dayNearPath : "";
	t.nightNearPath = nightNearPath ? nightNearPath : "";
	t.hasMid = hasMid;
	t.base = LoadTextureViaPak(basePath);   // POINT/REPEAT default; supersampling smooths the scroll
	BakeThemeThumbnails(t);
	return t;
}

// procedurally recolor one bird frame into the rainbow skin: a vertical hue gradient (top->bottom sweeps 0..300 deg)
// multiplied by each pixel's original brightness, so the bird's shading/outline survive. shared by the playable Rainbow
// skin, the Customize cover button, and the rainbow pipe
static Texture2D MakeRainbowTexture(const char* framePath)
{
	Image src = LoadImageViaPak(framePath);
	ImageFormat(&src, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	int rbW = src.width, rbH = src.height;
	Color* rbpx = (Color*)src.data;
	for (int y = 0; y < rbH; y++)
	{
		float hue = (float)y / (rbH - 1) * 300.0f;
		Color tint = ColorFromHSV(hue, 0.85f, 1.0f);
		for (int x = 0; x < rbW; x++)
		{
			Color& p = rbpx[y * rbW + x];
			if (p.a < 8) continue;   // skip (near-)transparent pixels so the silhouette stays clean
			int maxc = p.r > p.g ? (p.r > p.b ? p.r : p.b) : (p.g > p.b ? p.g : p.b);
			float origB = maxc / 255.0f;   // original brightness = the max channel, preserves shading
			p.r = (unsigned char)(tint.r * origB);
			p.g = (unsigned char)(tint.g * origB);
			p.b = (unsigned char)(tint.b * origB);
		}
	}
	Texture2D t = LoadTextureFromImage(src);
	UnloadImage(src);
	return t;
}

std::vector<BirdSkin> LoadBirdSkins()
{
	std::vector<BirdSkin> skins;
	skins.push_back(BirdSkin{ {
		LoadTextureViaPak("./resources/images/birds/yellow/upflap.png"),
		LoadTextureViaPak("./resources/images/birds/yellow/midflap.png"),
		LoadTextureViaPak("./resources/images/birds/yellow/downflap.png") }, "Yellow" });
	skins.push_back(BirdSkin{ {
		LoadTextureViaPak("./resources/images/birds/orange/upflap.png"),
		LoadTextureViaPak("./resources/images/birds/orange/midflap.png"),
		LoadTextureViaPak("./resources/images/birds/orange/downflap.png") }, "Orange" });
	skins.push_back(BirdSkin{ {
		LoadTextureViaPak("./resources/images/birds/blue/upflap.png"),
		LoadTextureViaPak("./resources/images/birds/blue/midflap.png"),
		LoadTextureViaPak("./resources/images/birds/blue/downflap.png") }, "Blue" });
	skins.push_back(BirdSkin{ {
		LoadTextureViaPak("./resources/images/birds/green/upflap.png"),
		LoadTextureViaPak("./resources/images/birds/green/midflap.png"),
		LoadTextureViaPak("./resources/images/birds/green/downflap.png") }, "Green" });
	skins.push_back(BirdSkin{ {
		LoadTextureViaPak("./resources/images/birds/red/upflap.png"),
		LoadTextureViaPak("./resources/images/birds/red/midflap.png"),
		LoadTextureViaPak("./resources/images/birds/red/downflap.png") }, "Red" });
	skins.push_back(BirdSkin{ {
		LoadTextureViaPak("./resources/images/birds/purple/upflap.png"),
		LoadTextureViaPak("./resources/images/birds/purple/midflap.png"),
		LoadTextureViaPak("./resources/images/birds/purple/downflap.png") }, "Purple" });
	skins.push_back(BirdSkin{ {
		LoadTextureViaPak("./resources/images/birds/pink/upflap.png"),
		LoadTextureViaPak("./resources/images/birds/pink/midflap.png"),
		LoadTextureViaPak("./resources/images/birds/pink/downflap.png") }, "Pink" });
	// rainbow: procedurally hue-shifted from the yellow frames (no separate art)
	skins.push_back(BirdSkin{ {
		MakeRainbowTexture("./resources/images/birds/yellow/upflap.png"),
		MakeRainbowTexture("./resources/images/birds/yellow/midflap.png"),
		MakeRainbowTexture("./resources/images/birds/yellow/downflap.png") }, "Rainbow" });
	return skins;
}

std::vector<Theme> LoadThemes()
{
	// pipe textures come from the lazy pipe library + ApplyPipeChoice, not from any art-pack sheet, so no pipe sheet is
	// loaded here. each theme is built lightweight (spec + thumbnails); the full bg/mid bake on activation
	std::vector<Theme> themes;
	themes.push_back(MakeClassicTheme());
	themes.push_back(BakePackTheme("Skyline",
		"./resources/images/backgrounds/skyline_day.png", "./resources/images/backgrounds/skyline_night.png",
		"./resources/images/ground/skyline.png"));
	themes.push_back(BakePackTheme("Sunset",
		"./resources/images/backgrounds/sunset_day.png", "./resources/images/backgrounds/sunset_night.png",
		"./resources/images/ground/sunset.png"));
	themes.push_back(BakePackTheme("Canyon",
		"./resources/images/backgrounds/canyon_day.png", "./resources/images/backgrounds/canyon_night.png",
		"./resources/images/ground/canyon.png", true,
		"./resources/images/backgrounds/canyon_near_day.png",
		"./resources/images/backgrounds/canyon_near_night.png"));
	themes.push_back(BakePackTheme("Meadow",
		"./resources/images/backgrounds/meadow_day.png", "./resources/images/backgrounds/meadow_night.png",
		"./resources/images/ground/meadow.png", true,
		"./resources/images/backgrounds/meadow_near_day.png",
		"./resources/images/backgrounds/meadow_near_night.png"));

	// per-theme dusk tints (peak at nightAmount = 0.5 inside the day/night shader): warm orange for arid/golden themes,
	// cooler purple for the others
	themes[0].duskTint = { 1.00f, 0.85f, 0.70f };
	themes[1].duskTint = { 0.95f, 0.80f, 0.95f };
	themes[2].duskTint = { 1.10f, 0.70f, 0.55f };
	themes[3].duskTint = { 1.10f, 0.75f, 0.60f };
	themes[4].duskTint = { 0.95f, 0.85f, 1.00f };
	return themes;
}

UiTextures LoadUiTextures()
{
	UiTextures textures{};
	textures.splashScreen = LoadTextureViaPak("./resources/images/ui/splash.png");
	textures.getReady = LoadTextureViaPak("./resources/images/ui/getready.png");
	textures.preview = LoadTextureViaPak("./resources/images/ui/preview.png");
	textures.gameOver = LoadTextureViaPak("./resources/images/ui/gameover.png");
	textures.settingsIcon = LoadTextureViaPak("./resources/images/ui/settings.png");
	textures.playButton = LoadTextureViaPak("./resources/images/ui/play_button.png");
	SetTextureFilter(textures.playButton, TEXTURE_FILTER_POINT);   // POINT keeps the up-scaled pixel art sharp
	textures.exitIcon = LoadTextureViaPak("./resources/images/ui/exit.png");
	textures.iconLeaderboard = LoadTextureViaPak("./resources/images/ui/icon_leaderboard.png");
	textures.iconTrophy = LoadTextureViaPak("./resources/images/ui/icon_trophy.png");
	textures.iconAchievements = LoadTextureViaPak("./resources/images/ui/icon_achievements.png");
	textures.iconStats = LoadTextureViaPak("./resources/images/ui/icon_stats.png");
	textures.iconInfo = LoadTextureViaPak("./resources/images/ui/icon_info.png");
	SetTextureFilter(textures.iconLeaderboard, TEXTURE_FILTER_POINT);
	SetTextureFilter(textures.iconTrophy, TEXTURE_FILTER_POINT);
	SetTextureFilter(textures.iconAchievements, TEXTURE_FILTER_POINT);
	SetTextureFilter(textures.iconStats, TEXTURE_FILTER_POINT);
	SetTextureFilter(textures.iconInfo, TEXTURE_FILTER_POINT);
	textures.orbShield = LoadTextureViaPak("./resources/images/ui/orb_shield.png");
	textures.orbSlowMo = LoadTextureViaPak("./resources/images/ui/orb_slowmo.png");
	textures.orbPortal = LoadTextureViaPak("./resources/images/ui/orb_portal.png");
	textures.moon = LoadTextureViaPak("./resources/images/ui/moon.png");
	textures.windFrames[0] = LoadTextureViaPak("./resources/images/ui/wind_0.png");
	textures.windFrames[1] = LoadTextureViaPak("./resources/images/ui/wind_1.png");
	textures.windFrames[2] = LoadTextureViaPak("./resources/images/ui/wind_2.png");
	textures.scorePanel = LoadTextureViaPak("./resources/images/ui/scorepanel.png");
	textures.newBadge = LoadTextureViaPak("./resources/images/ui/new.png");
	return textures;
}

static Texture2D LoadRainbowBirdTexture()
{
	return MakeRainbowTexture("./resources/images/birds/yellow/midflap.png");
}

// the four menu buttons below are baked once into 128x128 render textures (procedural art, no PNGs)
static RenderTexture2D BakeCustomizeButton(Texture2D rainbowBird)
{
	RenderTexture2D tex = LoadRenderTexture(128, 128);
	BeginTextureMode(tex);
	ClearBackground(BLANK);
	DrawRectangle(0, 0, 128, 128, Color{ 42, 36, 68, 255 });
	DrawRectangle(0, 0, 128, 6, Color{ 255, 255, 255, 55 });
	const Color stripe[7] = {
		{255, 225, 80, 255}, {255, 145, 45, 255}, {80, 150, 235, 255},
		{105, 220, 90, 255}, {235, 70, 70, 255}, {170, 85, 220, 255}, {235, 90, 155, 255}
	};
	// color swatches span the full inner width (inside the 4px border) so there are no gaps at the sides
	for (int i = 0; i < 7; i++)
	{
		int x0 = 4 + i * 120 / 7;
		int x1 = 4 + (i + 1) * 120 / 7;
		DrawRectangle(x0, 114, x1 - x0, 10, stripe[i]);
	}
	float bs = 90.0f / (float)std::max(rainbowBird.width, rainbowBird.height);
	float bw = rainbowBird.width * bs, bh = rainbowBird.height * bs;
	DrawTextureEx(rainbowBird, Vector2{ 64.0f - bw / 2.0f, 64.0f - bh / 2.0f - 6.0f }, 0.0f, bs, WHITE);
	DrawRectangleLinesEx(Rectangle{ 0, 0, 128, 128 }, 4, RAYWHITE);
	EndTextureMode();
	return tex;
}

static RenderTexture2D BakeTwoPlayerButton(const std::vector<BirdSkin>& skins,
	SkinIndex playerOneSkin, SkinIndex playerTwoSkin)
{
	RenderTexture2D tex = LoadRenderTexture(128, 128);
	BeginTextureMode(tex);
	ClearBackground(BLANK);
	// split diagonally: blue (P1) top-left triangle, red (P2) bottom-right, with each player's bird in its corner
	DrawTriangle(Vector2{ 0, 0 }, Vector2{ 0, 128 }, Vector2{ 128, 0 }, Color{ 50, 85, 175, 255 });
	DrawTriangle(Vector2{ 128, 0 }, Vector2{ 0, 128 }, Vector2{ 128, 128 }, Color{ 165, 55, 70, 255 });
	DrawRectangle(0, 0, 128, 6, Color{ 255, 255, 255, 50 });
	Texture2D b1 = skins[EnumIndex(playerOneSkin)].frames[static_cast<int>(BirdFrame::MID_FLAP)];
	Texture2D b2 = skins[EnumIndex(playerTwoSkin)].frames[static_cast<int>(BirdFrame::MID_FLAP)];
	constexpr float birdWidth = 56.0f;
	const float birdHeight = birdWidth * 12.0f / 17.0f;
	const float pad = 12.0f;
	const Rectangle birdSource = { 0, 0, (float)b1.width, (float)b1.height };
	DrawTexturePro(b1, birdSource, Rectangle{ pad, pad, birdWidth, birdHeight }, Vector2{ 0, 0 }, 0.0f, WHITE);
	DrawTexturePro(b2, birdSource, Rectangle{ 128.0f - pad - birdWidth, 128.0f - pad - birdHeight, birdWidth, birdHeight }, Vector2{ 0, 0 }, 0.0f, WHITE);
	DrawRectangleLinesEx(Rectangle{ 0, 0, 128, 128 }, 4, RAYWHITE);
	EndTextureMode();
	return tex;
}

static RenderTexture2D BakeClassicButton(const std::vector<BirdSkin>& skins)
{
	RenderTexture2D tex = LoadRenderTexture(128, 128);
	BeginTextureMode(tex);
	ClearBackground(BLANK);
	DrawRectangle(0, 0, 128, 128, Color{ 25, 75, 80, 255 });
	DrawRectangle(0, 0, 128, 6, Color{ 255, 255, 255, 55 });
	Texture2D b = skins[EnumIndex(SkinIndex::YELLOW_BIRD)].frames[static_cast<int>(BirdFrame::MID_FLAP)];
	float bs = 52.0f / (float)std::max(b.width, b.height);
	float bw = b.width * bs, bh = b.height * bs;
	DrawTextureEx(b, Vector2{ 64.0f - bw / 2.0f, 50.0f - bh / 2.0f }, 0.0f, bs, Color{ 255, 215, 90, 255 });
	const char* lbl = "Classic";
	float fs = 20.0f;
	Vector2 sz = MeasureTextEx(GetFontDefault(), lbl, fs, 2.0f);
	DrawRectangle(0, 94, 128, 32, Color{ 15, 50, 55, 255 });
	DrawTextEx(GetFontDefault(), lbl, Vector2{ 64.0f - sz.x / 2.0f, 101.0f }, fs, 2.0f, RAYWHITE);
	DrawRectangleLinesEx(Rectangle{ 0, 0, 128, 128 }, 4, RAYWHITE);
	EndTextureMode();
	return tex;
}

static RenderTexture2D BakeDailyButton()
{
	RenderTexture2D tex = LoadRenderTexture(128, 128);
	BeginTextureMode(tex);
	ClearBackground(BLANK);
	// a little tear-off calendar showing the current UTC month + day-of-month
	DrawRectangle(0, 0, 128, 128, Color{ 240, 235, 220, 255 });
	DrawRectangle(0, 0, 128, 28, Color{ 180, 50, 60, 255 });
	DrawRectangle(0, 96, 128, 28, Color{ 15, 50, 55, 255 });
	DrawRectangle(28, -4, 8, 16, Color{ 200, 200, 200, 255 });   // the two binder rings up top
	DrawRectangle(92, -4, 8, 16, Color{ 200, 200, 200, 255 });
	static const char* kMonths[12] = { "JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC" };
	time_t tnow = time(nullptr);
	struct tm g;
#ifdef _WIN32
	gmtime_s(&g, &tnow);
#else
	gmtime_r(&tnow, &g);
#endif
	const char* mon = kMonths[g.tm_mon < 0 ? 0 : (g.tm_mon > 11 ? 11 : g.tm_mon)];
	Vector2 ms = MeasureTextEx(gPixelFont, mon, 13.0f, 1.0f);
	DrawTextEx(gPixelFont, mon, Vector2{ 64.0f - ms.x / 2.0f, 9.0f }, 13.0f, 1.0f, RAYWHITE);
	std::string ds = ((TodayYMD() % 100) < 10 ? "0" : "") + std::to_string(TodayYMD() % 100);
	Vector2 dsSz = MeasureTextEx(GetFontDefault(), ds.c_str(), 52.0f, 2.0f);
	DrawTextEx(GetFontDefault(), ds.c_str(), Vector2{ 64.0f - dsSz.x / 2.0f, 39.0f }, 52.0f, 2.0f, Color{ 50, 50, 50, 255 });
	Vector2 ls = MeasureTextEx(GetFontDefault(), "Daily", 20.0f, 2.0f);
	DrawTextEx(GetFontDefault(), "Daily", Vector2{ 64.0f - ls.x / 2.0f, 100.0f }, 20.0f, 2.0f, RAYWHITE);
	DrawRectangleLinesEx(Rectangle{ 0, 0, 128, 128 }, 4, RAYWHITE);
	EndTextureMode();
	return tex;
}

MenuButtonTextures LoadMenuButtonTextures(const std::vector<BirdSkin>& skins)
{
	MenuButtonTextures buttons{};
	buttons.rainbowBird = LoadRainbowBirdTexture();
	buttons.customize = BakeCustomizeButton(buttons.rainbowBird);
	buttons.twoPlayer = BakeTwoPlayerButton(skins, SkinIndex::YELLOW_BIRD, SkinIndex::ORANGE_BIRD);
	buttons.classic = BakeClassicButton(skins);
	buttons.daily = BakeDailyButton();
	return buttons;
}

void RebuildTwoPlayerButton(MenuButtonTextures& buttons, const std::vector<BirdSkin>& skins,
	SkinIndex playerOneSkin, SkinIndex playerTwoSkin)
{
	UnloadRenderTexture(buttons.twoPlayer);   // re-bake with the new skins (free the old render texture first)
	buttons.twoPlayer = BakeTwoPlayerButton(skins, playerOneSkin, playerTwoSkin);
}

void EnsurePipeLoaded(Texture2D (&pipeTex)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], Texture2D (&pipeTex180)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], int style, int color)
{
	if (style < 0 || style >= (int)Constants::Customization::PipeStyleCount) return;
	if (color < 0 || color >= (int)Constants::Customization::PipeColorCount) return;
	if (pipeTex[style][color].id != 0) return;   // already resident (a nonzero GL id marks a loaded texture)
	// rainbow: no art file — procedurally hue-shift this style's yellow pipe (same vertical gradient the Rainbow bird
	// uses), for both the top and the flipped frame
	if (color == (int)EnumIndex(PipeColorIndex::RAINBOW_PIPE))
	{
		const std::string yb = std::string("./resources/images/pipes/") + Constants::Customization::PipeStyleFolders[style] + "/yellow";
		pipeTex[style][color] = MakeRainbowTexture((yb + ".png").c_str());
		pipeTex180[style][color] = MakeRainbowTexture((yb + "180.png").c_str());
		return;
	}
	// pipes stay POINT-filtered (raylib's default). they're hard-edged pixel art, and bilinear makes the outline/highlight
	// columns strobe as the pipe scrolls sub-pixel — a flickering line down the side. the ground + parallax layers DO use
	// bilinear (continuous surfaces, where a 1px snap reads as a shake); pipes are discrete enough not to need it
	//
	// Classic style + Green is the original Flappy Bird pipe — use the legacy texture (pipes/pipe.png) rather than the
	// recolored "classic/green", which reads as a noticeably different green
	if (style == (int)EnumIndex(PipeStyleIndex::CLASSIC) && color == (int)EnumIndex(PipeColorIndex::GREEN_PIPE))
	{
		// the legacy textures use the opposite cap orientation from the classic/* set: pipe.png has its cap at the TOP,
		// pipe180.png at the bottom. theme.pipe (the top pipe) needs the cap at the bottom (facing the gap) and theme.pipe180
		// the reverse, so the two are swapped here — otherwise the caps land at the screen edges and the gap openings look
		// like cap-less tube ends
		pipeTex[style][color] = LoadTextureViaPak("./resources/images/pipes/pipe180.png");
		pipeTex180[style][color] = LoadTextureViaPak("./resources/images/pipes/pipe.png");
		return;
	}
	std::string base = std::string("./resources/images/pipes/") + Constants::Customization::PipeStyleFolders[style] + "/" + Constants::Customization::PipeColorNames[color];
	pipeTex[style][color] = LoadTextureViaPak((base + ".png").c_str());
	pipeTex180[style][color] = LoadTextureViaPak((base + "180.png").c_str());
}

void UnloadPipesExcept(Texture2D (&pipeTex)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], Texture2D (&pipeTex180)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], int keepStyle, int keepColor)
{
	// free every loaded pipe cell except the one the active themes reference (the gameplay pair). cells never loaded
	// (id == 0) are skipped; the kept cell stays resident so theme.pipe / theme.pipe180 keep pointing at a valid texture
	for (int s = 0; s < (int)Constants::Customization::PipeStyleCount; s++)
		for (int c = 0; c < (int)Constants::Customization::PipeColorCount; c++)
		{
			if (s == keepStyle && c == keepColor) continue;
			if (pipeTex[s][c].id != 0) { UnloadTexture(pipeTex[s][c]); pipeTex[s][c] = Texture2D{}; }
			if (pipeTex180[s][c].id != 0) { UnloadTexture(pipeTex180[s][c]); pipeTex180[s][c] = Texture2D{}; }
		}
}

void LoadScoreDigits(Texture2D (&scoreBig)[10], Texture2D (&scoreSmall)[10])
{
	for (int i = 0; i < 10; i++)
	{
		std::string bp = "./resources/images/score/" + std::to_string(i) + ".png";
		std::string sp = "./resources/images/score/small" + std::to_string(i) + ".png";
		scoreBig[i] = LoadTextureViaPak(bp.c_str());
		scoreSmall[i] = LoadTextureViaPak(sp.c_str());
		SetTextureFilter(scoreBig[i], TEXTURE_FILTER_POINT);   // crisp digits
		SetTextureFilter(scoreSmall[i], TEXTURE_FILTER_POINT);
	}
}

void LoadMedalTextures(Texture2D (&medalTextures)[Constants::Medals::Count])
{
	const char* paths[6] = {
		"./resources/images/medals/bronze.png",
		"./resources/images/medals/silver.png",
		"./resources/images/medals/gold.png",
		"./resources/images/medals/platinum.png",
		"./resources/images/medals/diamond.png",
		"./resources/images/medals/ruby.png"
	};
	for (int i = 0; i < 6; i++) medalTextures[i] = LoadTextureViaPak(paths[i]);
}

void UnloadBirdSkins(std::vector<BirdSkin>& skins)
{
	for (auto& s : skins)
		for (Texture2D frame : s.frames) UnloadTexture(frame);
}

void UnloadThemes(std::vector<Theme>& themes)
{
	for (auto& t : themes)
	{
		UnloadThemeVisuals(t);   // bg/bgNight/mid/midNight are only resident for the active theme
		// t.pipe / t.pipe180 are shared copies owned by the pipe library (assigned by ApplyPipeChoice); it frees them, not us
		UnloadTexture(t.base);
		if (t.dayThumb.id != 0) UnloadTexture(t.dayThumb);
		if (t.nightThumb.id != 0) UnloadTexture(t.nightThumb);
	}
}

void UnloadUiTextures(UiTextures& textures)
{
	UnloadTexture(textures.splashScreen);
	UnloadTexture(textures.getReady);
	UnloadTexture(textures.preview);
	UnloadTexture(textures.gameOver);
	UnloadTexture(textures.settingsIcon);
	UnloadTexture(textures.playButton);
	UnloadTexture(textures.exitIcon);
	UnloadTexture(textures.iconLeaderboard);
	UnloadTexture(textures.iconTrophy);
	UnloadTexture(textures.iconAchievements);
	UnloadTexture(textures.iconStats);
	UnloadTexture(textures.iconInfo);
	UnloadTexture(textures.orbShield);
	UnloadTexture(textures.orbSlowMo);
	UnloadTexture(textures.orbPortal);
	UnloadTexture(textures.moon);
	for (int wf = 0; wf < 3; wf++) UnloadTexture(textures.windFrames[wf]);
	UnloadTexture(textures.scorePanel);
	UnloadTexture(textures.newBadge);
}

void UnloadMenuButtonTextures(MenuButtonTextures& buttons)
{
	UnloadTexture(buttons.rainbowBird);
	UnloadRenderTexture(buttons.customize);
	UnloadRenderTexture(buttons.twoPlayer);
	UnloadRenderTexture(buttons.classic);
	UnloadRenderTexture(buttons.daily);
}

void UnloadPipeLibrary(Texture2D (&pipeTex)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount], Texture2D (&pipeTex180)[Constants::Customization::PipeStyleCount][Constants::Customization::PipeColorCount])
{
	for (int s = 0; s < (int)Constants::Customization::PipeStyleCount; s++)
		for (int c = 0; c < (int)Constants::Customization::PipeColorCount; c++)
		{
			// lazy loading means many cells were never populated (id == 0)
			if (pipeTex[s][c].id != 0) UnloadTexture(pipeTex[s][c]);
			if (pipeTex180[s][c].id != 0) UnloadTexture(pipeTex180[s][c]);
		}
}

void UnloadScoreDigits(Texture2D (&scoreBig)[10], Texture2D (&scoreSmall)[10])
{
	for (int i = 0; i < 10; i++)
	{
		UnloadTexture(scoreBig[i]);
		UnloadTexture(scoreSmall[i]);
	}
}

void UnloadMedalTextures(Texture2D (&medalTextures)[Constants::Medals::Count])
{
	for (std::size_t m = 0; m < Constants::Medals::Count; m++) UnloadTexture(medalTextures[m]);
}
