#include "pak_archive.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <utility>

namespace
{
	constexpr unsigned char MAGIC[4] = { 'F', 'B', 'P', 'K' };   // file signature
	constexpr uint32_t SUPPORTED_VERSION = 1;
	constexpr uint32_t FLAG_XOR = 0x1;   // header flag: data section is XOR-obfuscated

	// xorshift32 PRNG; packer and reader share it to (de)obfuscate the data section
	uint32_t Xorshift32(uint32_t state)
	{
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}

	// decode an XOR'd byte range in place. the packer streams one keystream across the WHOLE data section
	// sequentially, so to decode bytes at data-section offset N..N+S we advance the keystream N times first,
	// then XOR each of the S bytes — O(offset + size) per file
	void DecodeXorInPlace(unsigned char* buf, uint64_t offset, uint32_t size, uint32_t seed)
	{
		uint32_t state = seed != 0 ? seed : 0xCAFEBABEu;   // a 0 seed would make xorshift stick at 0 forever
		for (uint64_t i = 0; i < offset; ++i)
			state = Xorshift32(state);
		for (uint32_t i = 0; i < size; ++i)
		{
			state = Xorshift32(state);
			buf[i] ^= static_cast<unsigned char>(state & 0xFFu);
		}
	}

	// file extension including the leading dot (".png"), or "" if none; raylib's *FromMemory loaders need it to pick a decoder
	const char* ExtensionFromPath(const char* path)
	{
		const char* dot = std::strrchr(path, '.');
		return dot ? dot : "";
	}
}

PakArchive::~PakArchive() = default;

bool PakArchive::Open(const std::string& path)
{
	open_ = false;
	entries_.clear();
	if (stream_.is_open()) stream_.close();
	stream_.clear();

	stream_.open(path, std::ios::binary);
	if (!stream_.is_open()) return false;

	stream_.seekg(0, std::ios::end);
	const std::streamoff total = stream_.tellg();
	if (total < 20) { stream_.close(); return false; }   // smaller than the 20-byte header means it isn't a pak
	stream_.seekg(0);

	// header layout: MAGIC[4] then four uint32s — version, flags, seed, entryCount
	unsigned char header[20] = {};
	stream_.read(reinterpret_cast<char*>(header), sizeof(header));
	if (std::memcmp(header, MAGIC, 4) != 0) { stream_.close(); return false; }

	uint32_t version, flags, seed, count;
	std::memcpy(&version, header + 4,  4);
	std::memcpy(&flags,   header + 8,  4);
	std::memcpy(&seed,    header + 12, 4);
	std::memcpy(&count,   header + 16, 4);
	if (version != SUPPORTED_VERSION) { stream_.close(); return false; }

	// read only the table of contents into memory; the bulk data section stays on disk and streams per Read()
	entries_.reserve(count);
	for (uint32_t i = 0; i < count; ++i)
	{
		uint16_t pathLen = 0;
		stream_.read(reinterpret_cast<char*>(&pathLen), 2);
		if (!stream_ || pathLen == 0) { stream_.close(); return false; }
		std::string p(pathLen, '\0');
		stream_.read(p.data(), pathLen);
		Entry e;
		stream_.read(reinterpret_cast<char*>(&e.offset), 8);
		stream_.read(reinterpret_cast<char*>(&e.size),   4);
		if (!stream_) { stream_.close(); return false; }
		entries_.emplace_back(std::move(p), e);
	}

	// the data section begins right after the TOC; every Entry.offset is relative to here
	dataSectionStart_ = stream_.tellg();
	if (dataSectionStart_ < 0) { stream_.close(); return false; }

	// sort by path so Has()/Read() can binary-search
	std::sort(entries_.begin(), entries_.end(),
		[](const auto& a, const auto& b) { return a.first < b.first; });

	xorObfuscated_ = (flags & FLAG_XOR) != 0;
	xorSeed_ = seed;
	open_ = true;
	return true;
}

std::string PakArchive::PathFor(const std::string& rawPath) const
{
	return NormalizeAssetPath(rawPath);
}

bool PakArchive::Has(const std::string& assetPath) const
{
	if (!open_) return false;
	const std::string key = PathFor(assetPath);
	auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
		[](const auto& e, const std::string& k) { return e.first < k; });
	return it != entries_.end() && it->first == key;
}

bool PakArchive::Read(const std::string& assetPath, std::vector<unsigned char>& out) const
{
	if (!open_ || !stream_.is_open()) return false;
	const std::string key = PathFor(assetPath);
	auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
		[](const auto& e, const std::string& k) { return e.first < k; });
	if (it == entries_.end() || it->first != key) return false;

	const Entry& e = it->second;
	out.resize(e.size);
	if (e.size == 0) return true;

	// stream the byte range off disk: seek to dataSectionStart + entry offset, read size bytes, then XOR-decode
	stream_.clear();   // clear any EOF/fail flag left by a previous read
	stream_.seekg(dataSectionStart_ + static_cast<std::streamoff>(e.offset));
	stream_.read(reinterpret_cast<char*>(out.data()), e.size);
	if (stream_.gcount() != static_cast<std::streamsize>(e.size)) return false;   // short read means truncated/corrupt

	if (xorObfuscated_)
	{
		DecodeXorInPlace(out.data(), e.offset, e.size, xorSeed_);
	}
	return true;
}

PakArchive& GlobalPakArchive()
{
	static PakArchive archive;
	return archive;
}

std::string NormalizeAssetPath(const std::string& path)
{
	std::string out = path;
	// strip a leading "./" (every asset path in assets.cpp uses it) and then any leading "/"
	while (out.size() >= 2 && out[0] == '.' && out[1] == '/') out.erase(0, 2);
	if (!out.empty() && out[0] == '/') out.erase(0, 1);
	// drop a leading "resources/" so paths match the packer, which stores them relative to the resources folder
	const std::string prefix = "resources/";
	if (out.compare(0, prefix.size(), prefix) == 0) out.erase(0, prefix.size());
	for (char& c : out) if (c == '\\') c = '/';   // canonical separator
	return out;
}

// the loaders below all share one shape: if the pak has the asset, decode it from memory; otherwise fall back to
// raylib's on-disk loader (the dev path, when there's no data.pak)

Texture2D LoadTextureViaPak(const char* path)
{
	const PakArchive& pak = GlobalPakArchive();
	if (pak.IsOpen() && pak.Has(path))
	{
		std::vector<unsigned char> bytes;
		if (pak.Read(path, bytes))
		{
			Image img = LoadImageFromMemory(ExtensionFromPath(path), bytes.data(), static_cast<int>(bytes.size()));
			if (IsImageValid(img))
			{
				Texture2D t = LoadTextureFromImage(img);
				UnloadImage(img);
				return t;
			}
		}
	}
	return LoadTexture(path);
}

Image LoadImageViaPak(const char* path)
{
	const PakArchive& pak = GlobalPakArchive();
	if (pak.IsOpen() && pak.Has(path))
	{
		std::vector<unsigned char> bytes;
		if (pak.Read(path, bytes))
		{
			Image img = LoadImageFromMemory(ExtensionFromPath(path), bytes.data(), static_cast<int>(bytes.size()));
			if (IsImageValid(img)) return img;
		}
	}
	return LoadImage(path);
}

Sound LoadSoundViaPak(const char* path)
{
	const PakArchive& pak = GlobalPakArchive();
	if (pak.IsOpen() && pak.Has(path))
	{
		std::vector<unsigned char> bytes;
		if (pak.Read(path, bytes))
		{
			Wave wave = LoadWaveFromMemory(ExtensionFromPath(path), bytes.data(), static_cast<int>(bytes.size()));
			if (IsWaveValid(wave))
			{
				Sound s = LoadSoundFromWave(wave);
				UnloadWave(wave);
				return s;
			}
		}
	}
	return LoadSound(path);
}

namespace
{
	// raylib's LoadMusicStreamFromMemory keeps a raw pointer into the source buffer and reads from it during
	// playback, so the bytes must outlive the Music handle (here, the whole process). the unique_ptr indirection
	// keeps each buffer's address stable even when this outer vector reallocates
	std::vector<std::unique_ptr<std::vector<unsigned char>>>& MusicBytesStorage()
	{
		static std::vector<std::unique_ptr<std::vector<unsigned char>>> storage;
		return storage;
	}
}

Music LoadMusicStreamViaPak(const char* path)
{
	const PakArchive& pak = GlobalPakArchive();
	if (pak.IsOpen() && pak.Has(path))
	{
		auto owned = std::make_unique<std::vector<unsigned char>>();
		if (pak.Read(path, *owned))
		{
			Music m = LoadMusicStreamFromMemory(ExtensionFromPath(path), owned->data(), static_cast<int>(owned->size()));
			if (IsMusicValid(m))
			{
				MusicBytesStorage().push_back(std::move(owned));   // retain the bytes for the stream's lifetime
				return m;
			}
		}
	}
	return LoadMusicStream(path);
}

Font LoadFontExViaPak(const char* path, int fontSize, int* fontChars, int glyphCount)
{
	const PakArchive& pak = GlobalPakArchive();
	if (pak.IsOpen() && pak.Has(path))
	{
		std::vector<unsigned char> bytes;
		if (pak.Read(path, bytes))
		{
			return LoadFontFromMemory(ExtensionFromPath(path), bytes.data(),
				static_cast<int>(bytes.size()), fontSize, fontChars, glyphCount);
		}
	}
	return LoadFontEx(path, fontSize, fontChars, glyphCount);
}

Shader LoadShaderViaPak(const char* vsPath, const char* fsPath)
{
	const PakArchive& pak = GlobalPakArchive();
	std::string vsCode, fsCode;
	auto readText = [&](const char* p, std::string& out) -> bool {
		if (!p || !pak.IsOpen() || !pak.Has(p)) return false;
		std::vector<unsigned char> bytes;
		if (!pak.Read(p, bytes)) return false;
		out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		return true;
	};
	// either stage may independently come from the pak; passing nullptr code tells raylib to use its default stage
	bool vsFromPak = readText(vsPath, vsCode);
	bool fsFromPak = readText(fsPath, fsCode);
	if (vsFromPak || fsFromPak)
	{
		return LoadShaderFromMemory(vsFromPak ? vsCode.c_str() : nullptr,
		                            fsFromPak ? fsCode.c_str() : nullptr);
	}
	return LoadShader(vsPath, fsPath);
}
