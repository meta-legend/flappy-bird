// packs the game's resources into a single data.pak (the FBPK format that pak_archive.cpp reads at runtime).
// built as its own CMake target; CMakeLists.txt invokes it as a POST_BUILD step under -DPAK_PACK=ON, and you can
// also run it by hand from the build dir whenever assets change, without a full rebuild.
// usage:
//   build_pak <resources_dir> <output_pak> [--xor]

#include "networkml.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	// format constants — must stay in sync with the reader in pak_archive.cpp
	constexpr char MAGIC[4] = { 'F', 'B', 'P', 'K' };
	constexpr uint32_t VERSION = 1;
	constexpr uint32_t FLAG_XOR = 0x1;

	// the resource subfolders we pack; the runtime loader expects these same roots, so adding one means editing both sides
	const std::vector<std::string> PACK_SUBDIRS = {
		"images", "font", "shaders", "sounds"
	};

	uint32_t Xorshift32(uint32_t state)
	{
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}

	// same keystream the runtime loader decodes with: one in-place XOR pass over the ENTIRE concatenated data section,
	// the keystream advancing across all files rather than restarting per file
	void Obfuscate(std::vector<unsigned char>& data, uint32_t seed)
	{
		uint32_t state = seed != 0 ? seed : 0xCAFEBABEu;
		for (auto& byte : data)
		{
			state = Xorshift32(state);
			byte ^= static_cast<unsigned char>(state & 0xFFu);
		}
	}

	struct PackedFile
	{
		std::string relativePath;   // forward-slash, no leading slash — the key stored in the TOC
		std::string absolutePath;   // where to read the bytes from
	};

	// ML::File::listFiles returns basenames only and is non-recursive, so we walk subfolders ourselves. TOC + runtime
	// lookup paths all use forward slashes regardless of platform
	void WalkDirectory(ML::File& fm, const std::string& root,
		const std::string& subPath, std::vector<PackedFile>& out)
	{
		const std::string folder = root + "/" + subPath;
		if (!fm.isDirectory(folder)) return;
		const std::vector<std::string> entries = fm.listFiles(folder);
		for (const auto& name : entries)
		{
			const std::string fullPath = folder + "/" + name;
			const std::string relPath = subPath + "/" + name;
			if (fm.isDirectory(fullPath))
			{
				WalkDirectory(fm, root, relPath, out);
			}
			else if (fm.isFile(fullPath))
			{
				out.push_back({ relPath, fullPath });
			}
		}
	}

	// append value's raw bytes (the shipping platforms are all little-endian, matching how the reader memcpy's them back)
	template <typename T>
	void AppendLE(std::vector<unsigned char>& buf, T value)
	{
		const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
		buf.insert(buf.end(), bytes, bytes + sizeof(value));
	}

	void AppendBytes(std::vector<unsigned char>& buf, const void* data, std::size_t length)
	{
		const auto* bytes = static_cast<const unsigned char*>(data);
		buf.insert(buf.end(), bytes, bytes + length);
	}

	int Build(const std::string& resourcesDir, const std::string& outputPak, bool xorObfuscate)
	{
		ML::File fm;
		if (!fm.isDirectory(resourcesDir))
		{
			std::cerr << "build_pak: resources directory does not exist: "
				<< resourcesDir << "\n";
			return 1;
		}

		// recursively gather every file under each packed subdir
		std::vector<PackedFile> files;
		for (const auto& sub : PACK_SUBDIRS)
		{
			WalkDirectory(fm, resourcesDir, sub, files);
		}
		if (files.empty())
		{
			std::cerr << "build_pak: no files found under " << resourcesDir << "\n";
			return 1;
		}
		// sort by path so the runtime's binary-search TOC lookup works (and the output is deterministic)
		std::sort(files.begin(), files.end(),
			[](const PackedFile& a, const PackedFile& b) {
				return a.relativePath < b.relativePath;
			});

		// first pass: read each file's bytes, record (offset, size) for the TOC, and build the concatenated data section
		struct TocEntry { std::string path; uint64_t offset; uint32_t size; };
		std::vector<TocEntry> toc;
		toc.reserve(files.size());

		std::vector<unsigned char> dataBlob;
		for (const auto& f : files)
		{
			std::vector<unsigned char> contents = fm.readBytes(f.absolutePath);
			const uint64_t offset = dataBlob.size();   // offset is relative to the start of the data section
			toc.push_back({ f.relativePath, offset, static_cast<uint32_t>(contents.size()) });
			dataBlob.insert(dataBlob.end(), contents.begin(), contents.end());
		}

		uint32_t flags = 0;
		uint32_t seed = 0;
		if (xorObfuscate)
		{
			// cheap per-build seed from file count + total bytes: stable across runs with identical inputs, changes when assets change
			seed = static_cast<uint32_t>(
				(static_cast<uint64_t>(files.size()) * 2654435761ull
					+ static_cast<uint64_t>(dataBlob.size())) & 0xFFFFFFFFull);
			if (seed == 0) seed = 0xCAFEBABEu;   // 0 would make the keystream stick
			Obfuscate(dataBlob, seed);
			flags = FLAG_XOR;
		}

		// assemble the whole file in memory — header, then the TOC, then the data section — and write it in one go.
		// layout: MAGIC[4] | VERSION | flags | seed | entryCount, then per entry: pathLen(u16) path offset(u64) size(u32)
		std::vector<unsigned char> output;
		output.reserve(20 + dataBlob.size() + files.size() * 32);

		AppendBytes(output, MAGIC, 4);
		AppendLE<uint32_t>(output, VERSION);
		AppendLE<uint32_t>(output, flags);
		AppendLE<uint32_t>(output, seed);
		AppendLE<uint32_t>(output, static_cast<uint32_t>(toc.size()));
		for (const auto& e : toc)
		{
			AppendLE<uint16_t>(output, static_cast<uint16_t>(e.path.size()));
			AppendBytes(output, e.path.data(), e.path.size());
			AppendLE<uint64_t>(output, e.offset);
			AppendLE<uint32_t>(output, e.size);
		}
		output.insert(output.end(), dataBlob.begin(), dataBlob.end());

		// make sure the parent directory exists, then write the whole blob
		const std::size_t slash = outputPak.find_last_of("/\\");
		if (slash != std::string::npos)
		{
			const std::string parent = outputPak.substr(0, slash);
			if (!parent.empty()) fm.createFolders(parent);
		}
		fm.writeBytes(outputPak, output);
		if (!fm.exists(outputPak))
		{
			std::cerr << "build_pak: write failed for " << outputPak << "\n";
			return 1;
		}

		std::cout << "build_pak: wrote " << files.size() << " files -> "
			<< outputPak;
		if (xorObfuscate)
		{
			char seedBuf[16];
			std::snprintf(seedBuf, sizeof(seedBuf), "%08x", seed);
			std::cout << " (XOR seed=0x" << seedBuf << ")";
		}
		else
		{
			std::cout << " (plain)";
		}
		std::cout << "\n";
		return 0;
	}
}

int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		std::cerr << "usage: " << argv[0]
			<< " <resources_dir> <output_pak> [--xor]\n";
		return 1;
	}
	bool xorObfuscate = false;
	for (int i = 3; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (arg == "--xor") xorObfuscate = true;
		else
		{
			std::cerr << "build_pak: unknown argument: " << arg << "\n";
			return 1;
		}
	}
	return Build(argv[1], argv[2], xorObfuscate);
}
