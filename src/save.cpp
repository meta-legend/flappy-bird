#include "save.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cctype>

// 4-byte signature, so we can quickly reject other files / earlier formats
static const char SAVE_MAGIC[4] = { 'F', 'B', 'S', 'V' };
// current save format. there is no legacy save to migrate from, so the loader rejects anything newer than it
// knows how to read; bump this whenever the field layout changes. v2 dropped high-contrast, v3 added daily best
static const int32_t SAVE_VERSION = 3;
static const int32_t MIN_READABLE_SAVE_VERSION = 2;
// FNV-1a is fast + small; XOR-ing the result with a per-build constant means you can't just copy another
// save.bin's checksum byte-for-byte and graft it on
static const uint32_t SAVE_SECRET = 0xa9b714f3u;

static uint32_t Fnv1a(const uint8_t* d, size_t n)
{
	uint32_t h = 0x811c9dc5u;
	for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 0x01000193u; }
	return h;
}

// little-endian writers (the save is fixed-endian; these run only on the platforms we ship, all little-endian)
static void W32(std::ostream& o, int32_t v)        { o.write((char*)&v, 4); }
static void Wu32(std::ostream& o, uint32_t v)      { o.write((char*)&v, 4); }
static void W64(std::ostream& o, uint64_t v)       { o.write((char*)&v, 8); }
static void WD(std::ostream& o, double v)          { o.write((char*)&v, 8); }
// length-prefixed string: uint16 count, then the bytes (names over 0xFFFF are truncated, never overflow)
static void WStr(std::ostream& o, const std::string& s)
{
	uint16_t n = (uint16_t)(s.size() < 0xFFFF ? s.size() : 0xFFFF);
	o.write((char*)&n, 2);
	o.write(s.data(), n);
}

static std::string TrimAsciiWhitespace(std::string text)
{
	auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
	text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
	text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
	return text;
}

// read a scoped enum back as its int32 underlying value and advance position
template <typename Enum>
static void ReadEnum(const std::vector<char>& buffer, size_t& position, Enum& value)
{
	static_assert(std::is_same_v<std::underlying_type_t<Enum>, std::int32_t>);
	int32_t raw = 0;
	memcpy(&raw, buffer.data() + position, sizeof(raw));
	position += sizeof(raw);
	value = static_cast<Enum>(raw);
}

void WriteSave(const SaveData& sd, const std::string& path)
{
	// build the whole payload in memory first, so the checksum is computed over the exact bytes about to hit disk.
	// always stamp the latest format, regardless of which version originally populated this SaveData
	std::ostringstream buf(std::ios::binary);
	buf.write(SAVE_MAGIC, 4);
	W32(buf, SAVE_VERSION);  // current save format version
	WStr(buf, sd.playerName);
	W32(buf, sd.bestScore);
	W32(buf, sd.bestClassicScore);
	W32(buf, sd.bestDailyScore);
	W32(buf, EnumValue(sd.themeIndex));
	W32(buf, EnumValue(sd.skinIndex));
	W32(buf, EnumValue(sd.skinIndex2));
	W32(buf, EnumValue(sd.pipeStyleIndex));
	W32(buf, EnumValue(sd.pipeColorIndex));
	W64(buf, sd.unlockedSkins);
	W64(buf, sd.unlockedThemes);
	W32(buf, sd.musicVolume);
	W32(buf, sd.sfxVolume);
	W32(buf, sd.muted ? 1 : 0);
	W32(buf, EnumValue(sd.resIndex));
	W32(buf, sd.winW);
	W32(buf, sd.winH);
	W32(buf, sd.winMaximized ? 1 : 0);
	W32(buf, sd.vsync ? 1 : 0);
	W32(buf, sd.showFps ? 1 : 0);
	W32(buf, sd.fpsCap);
	W32(buf, sd.keyFlapP1);
	W32(buf, sd.keyFlapP2);
	W32(buf, sd.keyPause);
	W32(buf, sd.showGhost ? 1 : 0);
	W32(buf, sd.ghostOpacity);
	W32(buf, sd.reduceMotion ? 1 : 0);
	W32(buf, sd.largerHud ? 1 : 0);
	W64(buf, sd.totalFlaps);
	W64(buf, sd.totalDeaths);
	W64(buf, sd.totalPipes);
	W64(buf, sd.totalGames);
	WD(buf, sd.playtimeSeconds);
	W64(buf, sd.pipesPerSkin[0]);
	W64(buf, sd.pipesPerSkin[1]);
	W64(buf, sd.deathsPerSkin[0]);
	W64(buf, sd.deathsPerSkin[1]);
	W64(buf, sd.achMask);
	W32(buf, sd.lastDailyDate);
	W32(buf, sd.lastDailyScore);
	W32(buf, sd.dailyCount);
	W32(buf, sd.keyPhotoMode);
	W32(buf, sd.keyRestart);

	std::string body = buf.str();
	uint32_t hash = Fnv1a((const uint8_t*)body.data(), body.size()) ^ SAVE_SECRET;

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) return;
	out.write(body.data(), body.size());
	out.write((char*)&hash, 4);   // checksum trailer
}

bool LoadSave(SaveData& sd, const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) return false;
	in.seekg(0, std::ios::end);
	std::streamoff sz = in.tellg();
	if (sz < 12) return false;   // too small to hold magic + version + checksum
	in.seekg(0);
	std::vector<char> buf((size_t)sz);
	in.read(buf.data(), sz);
	if (!in) return false;

	if (memcmp(buf.data(), SAVE_MAGIC, 4) != 0) return false;
	size_t bodyLen = (size_t)sz - 4;   // last 4 bytes are the checksum
	uint32_t stored;
	memcpy(&stored, buf.data() + bodyLen, 4);
	uint32_t calc = Fnv1a((const uint8_t*)buf.data(), bodyLen) ^ SAVE_SECRET;
	if (calc != stored) return false;   // tampered or corrupt

	size_t p = 4;   // skip the magic; cursor walks the body as the readers consume fields
	auto rI = [&](int32_t& v) { memcpy(&v, buf.data() + p, 4); p += 4; };
	auto rL = [&](uint64_t& v) { memcpy(&v, buf.data() + p, 8); p += 8; };
	auto rD = [&](double& v) { memcpy(&v, buf.data() + p, 8); p += 8; };
	auto rB = [&](bool& v) { int32_t t; rI(t); v = (t != 0); };
	auto rS = [&](std::string& v) { uint16_t n; memcpy(&n, buf.data() + p, 2); p += 2; v.assign(buf.data() + p, n); p += n; };

	int32_t ver = 0;
	rI(ver); sd.version = ver;
	if (ver < MIN_READABLE_SAVE_VERSION || ver > SAVE_VERSION) return false;
	rS(sd.playerName);
	sd.playerName = TrimAsciiWhitespace(sd.playerName);
	rI(sd.bestScore);
	rI(sd.bestClassicScore);
	if (ver >= 3) rI(sd.bestDailyScore);   // field added in v3; older files don't have it (migrated below)
	ReadEnum(buf, p, sd.themeIndex);
	ReadEnum(buf, p, sd.skinIndex);
	ReadEnum(buf, p, sd.skinIndex2);
	ReadEnum(buf, p, sd.pipeStyleIndex);
	ReadEnum(buf, p, sd.pipeColorIndex);
	rL(sd.unlockedSkins);
	rL(sd.unlockedThemes);
	rI(sd.musicVolume);
	rI(sd.sfxVolume);
	rB(sd.muted);
	ReadEnum(buf, p, sd.resIndex);
	rI(sd.winW); rI(sd.winH);
	rB(sd.winMaximized);
	rB(sd.vsync);
	rB(sd.showFps);
	rI(sd.fpsCap);
	rI(sd.keyFlapP1);
	rI(sd.keyFlapP2);
	rI(sd.keyPause);
	rB(sd.showGhost);
	rI(sd.ghostOpacity);
	rB(sd.reduceMotion);
	rB(sd.largerHud);
	rL(sd.totalFlaps);
	rL(sd.totalDeaths);
	rL(sd.totalPipes);
	rL(sd.totalGames);
	rD(sd.playtimeSeconds);
	rL(sd.pipesPerSkin[0]);
	rL(sd.pipesPerSkin[1]);
	rL(sd.deathsPerSkin[0]);
	rL(sd.deathsPerSkin[1]);
	rL(sd.achMask);
	rI(sd.lastDailyDate);
	rI(sd.lastDailyScore);
	rI(sd.dailyCount);
	if (ver < 3) sd.bestDailyScore = sd.lastDailyScore;   // v2 migration: seed the new daily best from the old last-daily
	rI(sd.keyPhotoMode);
	rI(sd.keyRestart);
	// defensive clamps: the checksum already rejects tampered/truncated files, but a genuine bug could still write an
	// out-of-range value. counters above ~100M are physically impossible, so zero them rather than show garbage on the
	// Stats screen, and keep enum/key fields in range so they can't index arrays out of bounds
	const uint64_t SANE_MAX = 100000000ull;
	if (sd.totalFlaps > SANE_MAX) sd.totalFlaps = 0;
	if (sd.totalDeaths > SANE_MAX) sd.totalDeaths = 0;
	if (sd.totalPipes > SANE_MAX) sd.totalPipes = 0;
	if (sd.totalGames > SANE_MAX) sd.totalGames = 0;
	if (sd.playtimeSeconds < 0 || sd.playtimeSeconds > 1.0e9) sd.playtimeSeconds = 0;
	if (sd.pipesPerSkin[0] > SANE_MAX) sd.pipesPerSkin[0] = 0;
	if (sd.pipesPerSkin[1] > SANE_MAX) sd.pipesPerSkin[1] = 0;
	if (sd.deathsPerSkin[0] > SANE_MAX) sd.deathsPerSkin[0] = 0;
	if (sd.deathsPerSkin[1] > SANE_MAX) sd.deathsPerSkin[1] = 0;
	if (sd.bestScore < 0 || sd.bestScore > 1000000) sd.bestScore = 0;
	if (sd.bestClassicScore < 0 || sd.bestClassicScore > 1000000) sd.bestClassicScore = 0;
	if (sd.bestDailyScore < 0 || sd.bestDailyScore > 1000000) sd.bestDailyScore = 0;
	if (sd.dailyCount < 0 || sd.dailyCount > 1000) sd.dailyCount = 0;
	if (!EnumInRange(sd.skinIndex, EnumIndex(SkinIndex::COUNT))) sd.skinIndex = SkinIndex::YELLOW_BIRD;
	// keybinds: raylib key codes are roughly 32..348; anything outside that or zero is corrupt, so reset to the struct default
	auto saneKey = [](int k, int def) { return (k < 32 || k > 348) ? def : k; };
	sd.keyFlapP1 = saneKey(sd.keyFlapP1, KEY_SPACE);
	sd.keyFlapP2 = saneKey(sd.keyFlapP2, KEY_UP);
	sd.keyPause  = saneKey(sd.keyPause,  KEY_P);
	sd.keyPhotoMode = saneKey(sd.keyPhotoMode, KEY_T);
	sd.keyRestart = saneKey(sd.keyRestart, KEY_R);
	// unlock masks: with N entries only bits 0..N-1 are meaningful; anything past 0xFF is corruption, so reset to defaults
	if (sd.unlockedSkins > 0xFFull)  sd.unlockedSkins = 3;   // yellow + orange are always available
	if (sd.unlockedThemes > 0xFFull) sd.unlockedThemes = 1;
	if (sd.unlockedSkins == 0)  sd.unlockedSkins = 3;
	if (sd.unlockedThemes == 0) sd.unlockedThemes = 1;
	// daily date is YYYYMMDD; sane range is roughly 19700101..29991231
	if (sd.lastDailyDate < 0 || sd.lastDailyDate > 29991231) sd.lastDailyDate = 0;
	if (sd.lastDailyScore < 0 || sd.lastDailyScore > 1000000) sd.lastDailyScore = 0;
	// fps cap: 0 (unlimited) or a sane range; anything weird falls back to unlimited
	if (sd.fpsCap < 0 || sd.fpsCap > 1000) sd.fpsCap = 0;
	if (sd.ghostOpacity < 0 || sd.ghostOpacity > 100) sd.ghostOpacity = 15;
	// audio: clamp to [0, 100]; a corrupt value goes back to the normal defaults rather than silence/blast
	if (sd.musicVolume < 0 || sd.musicVolume > 100) sd.musicVolume = 30;
	if (sd.sfxVolume  < 0 || sd.sfxVolume  > 100) sd.sfxVolume  = 100;
	if (!EnumInRange(sd.skinIndex2, EnumIndex(SkinIndex::COUNT))) sd.skinIndex2 = SkinIndex::ORANGE_BIRD;
	if (!EnumInRange(sd.pipeStyleIndex, EnumIndex(PipeStyleIndex::COUNT))) sd.pipeStyleIndex = PipeStyleIndex::CLASSIC;
	if (!EnumInRange(sd.pipeColorIndex, EnumIndex(PipeColorIndex::COUNT))) sd.pipeColorIndex = PipeColorIndex::GREEN_PIPE;
	if (!EnumInRange(sd.themeIndex, EnumIndex(ThemeIndex::COUNT))) sd.themeIndex = ThemeIndex::CLASSIC;
	switch (sd.resIndex)
	{
	case ResIndex::WINDOWED:
	case ResIndex::BORDERLESS:
		break;
	default:
		// legacy values 1..4 were windowed-size presets, 6 was exclusive fullscreen (removed); any unknown
		// value sanitizes to windowed rather than leaking an invalid enum into runtime
		sd.resIndex = ResIndex::WINDOWED;
		break;
	}
	if (sd.winW < 320 || sd.winW > 7680) sd.winW = 800;
	if (sd.winH < 240 || sd.winH > 4320) sd.winH = 600;
	return true;
}
