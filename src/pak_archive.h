#pragma once

// single-file asset archive (data.pak); writer + format docs live in tools/build_pak.cpp
// optional — with no data.pak beside the exe, loaders fall back to raw ./resources/... files,
// so one code path serves both dev (loose files) and release (packed) builds
//
// memory: only the table of contents (paths + offsets + sizes) stays resident; asset bytes stream
// from the file per Read(), so a multi-MB archive costs a few KB of RAM plus one open handle

#include "raylib.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// runtime reader for data.pak; the writer is the separate tools/build_pak.cpp
class PakArchive
{
public:
	PakArchive() = default;
	~PakArchive();

	PakArchive(const PakArchive&) = delete;
	PakArchive& operator=(const PakArchive&) = delete;

	// open + parse the TOC; on failure the archive stays empty and Has() returns false for everything
	bool Open(const std::string& path);

	bool IsOpen() const { return open_; }
	bool Has(const std::string& assetPath) const;

	// copy the asset bytes into out (XOR-decoded if the archive is obfuscated); false if the asset is absent
	bool Read(const std::string& assetPath, std::vector<unsigned char>& out) const;

private:
	// one TOC record: where an asset's bytes sit within the data section
	struct Entry
	{
		uint64_t offset = 0;
		uint32_t size = 0;
	};

	// raw path -> the canonical form stored in the TOC (same rule as NormalizeAssetPath)
	std::string PathFor(const std::string& rawPath) const;

	bool open_ = false;
	bool xorObfuscated_ = false;
	uint32_t xorSeed_ = 0;
	// absolute file offset where the data section starts (just past the TOC); Entry offsets are relative to this
	std::streamoff dataSectionStart_ = 0;
	// handle kept open for the archive's lifetime so each Read() is just seek + read;
	// mutable because Read() is const but still advances the stream
	mutable std::ifstream stream_;
	std::vector<std::pair<std::string, Entry>> entries_;  // sorted by path for binary search
};

// process-wide archive, opened in main() before any assets and queried by the wrappers in assets.cpp
PakArchive& GlobalPakArchive();

// canonicalize an asset path: strip a leading "./" and turn backslashes into forward slashes;
// the PakArchive stores and looks up paths in exactly this form
std::string NormalizeAssetPath(const std::string& path);

// asset loaders that try the archive first and fall back to disk when it's closed or missing the asset —
// use these in assets.cpp instead of raylib's LoadTexture / LoadImage / ... directly
Texture2D LoadTextureViaPak(const char* path);
Image LoadImageViaPak(const char* path);
Shader LoadShaderViaPak(const char* vsPath, const char* fsPath);
Font LoadFontExViaPak(const char* path, int fontSize, int* fontChars, int glyphCount);
Sound LoadSoundViaPak(const char* path);
// raylib streams music from the source buffer, so the bytes must outlive the Music handle;
// pak-loaded music buffers live in process-wide storage in pak_archive.cpp and free at exit
Music LoadMusicStreamViaPak(const char* path);
