#include "run_artifacts.h"

#include "types.h"
#include <cstdint>
#include <ctime>
#include <fstream>

// ghost file format v1: [ver, finalScore, themeIndex, skinIndex, flapCount] then flapCount floats (flap timestamps)
void SaveGhostRun(const std::string& ghostPath, const std::vector<float>& recordFlaps, const SaveData& sd, int finalScore)
{
	std::ofstream out(ghostPath, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) return;

	int ver = 1;
	int count = (int)recordFlaps.size();
	out.write((char*)&ver, 4);
	out.write((char*)&finalScore, 4);
	const std::int32_t rawThemeIndex = EnumValue(sd.themeIndex);   // stored for completeness; the loader ignores it
	const std::int32_t rawSkinIndex = EnumValue(sd.skinIndex);
	out.write((char*)&rawThemeIndex, 4);
	out.write((char*)&rawSkinIndex, 4);
	out.write((char*)&count, 4);
	if (count > 0) out.write((char*)recordFlaps.data(), count * sizeof(float));
}

// load a ghost file; false on missing/short/garbage. only the skin + flap times drive playback (theme is skipped)
bool LoadGhostRun(const std::string& ghostPath, std::vector<float>& ghostFlaps, SkinIndex& ghostSkin, int maxSkinCount)
{
	ghostFlaps.clear();
	std::ifstream in(ghostPath, std::ios::binary);
	if (!in.is_open()) return false;

	int ver = 0, score = 0, themeI = 0, skinI = 0, count = 0;
	in.read((char*)&ver, 4);
	in.read((char*)&score, 4);
	in.read((char*)&themeI, 4);
	in.read((char*)&skinI, 4);
	in.read((char*)&count, 4);
	if (!in || ver != 1 || count < 0 || count > 100000) return false;   // 100k cap rejects corrupt/hostile sizes

	ghostFlaps.resize(count);
	if (count > 0) in.read((char*)ghostFlaps.data(), count * sizeof(float));
	if (!in) return false;
	// older recordings omitted the launch flap, so their ghost free-fell until the first mid-run flap;
	// normalize both formats to an explicit t=0 launch
	if (ghostFlaps.empty() || ghostFlaps.front() > 0.001f)
		ghostFlaps.insert(ghostFlaps.begin(), 0.0f);
	ghostSkin = (skinI >= 0 && skinI < maxSkinCount) ? static_cast<SkinIndex>(skinI) : SkinIndex::YELLOW_BIRD;   // clamp a stale id
	return true;
}

std::string WriteFrameScreenshotImage(Image& img, const std::string& shotDir, const std::string& playerName, int score, int bestScore)
{
	// the render target is supersampled (VIRTUAL_* x RENDER_SUPERSAMPLE); bring it back to logical size so the
	// caption below lands in the right place and exported shots stay a sensible resolution
	if (img.width != VIRTUAL_W || img.height != VIRTUAL_H) ImageResize(&img, VIRTUAL_W, VIRTUAL_H);
	// bottom-right caption: name + score on one line, best + watermark on the next
	std::string l1 = playerName + "  Score " + std::to_string(score);
	std::string l2 = "Best " + std::to_string(bestScore) + "   flappy-bird";
	ImageDrawRectangle(&img, VIRTUAL_W - 300, VIRTUAL_H - 72, 292, 62, Fade(BLACK, 0.55f));
	ImageDrawText(&img, l1.c_str(), VIRTUAL_W - 290, VIRTUAL_H - 64, 20, RAYWHITE);
	ImageDrawText(&img, l2.c_str(), VIRTUAL_W - 290, VIRTUAL_H - 38, 18, GOLD);

	// filename is the local timestamp, e.g. flappy_20260625_140312.png
	time_t now = time(nullptr);
	struct tm lt;
#ifdef _WIN32
	localtime_s(&lt, &now);
#else
	localtime_r(&now, &lt);
#endif
	char name[64];
	strftime(name, sizeof(name), "flappy_%Y%m%d_%H%M%S.png", &lt);
	std::string path = shotDir + "/" + name;
	ExportImage(img, path.c_str());
	return std::string(name);
}

std::string SaveFrameScreenshot(RenderTexture2D screen, const std::string& shotDir, const std::string& playerName, int score, int bestScore)
{
	Image img = LoadImageFromTexture(screen.texture);
	ImageFlipVertical(&img);   // GPU textures read back bottom-up, so flip to screen orientation
	const std::string name = WriteFrameScreenshotImage(img, shotDir, playerName, score, bestScore);
	UnloadImage(img);
	return name;
}
