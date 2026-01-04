// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>

#include "diskproperties.h"
#include "sector.h"

namespace fs = std::filesystem;

// https://www.seasip.info/Cpm/format22.html
struct DiskParameterBlock {
	unsigned short spt_{}; // number of 128-byte records per track
	unsigned char bsh_{};  // block shift; 3 => 1k, 4 => 2k, 5 => 4k ...
	unsigned char blm_{};  // block mask; 7 => 1k, 0fh => 2k, 1Fh => 4k ...
	unsigned char exm_{};  // extent mask
	unsigned short dsm_{}; // (no. of blocks on the disc) - 1
	unsigned short drm_{}; // (no. of directory entries) - 1
	unsigned char al0_{};  // directory allocation bitmap, first byte
	unsigned char al1_{};  // directory allocation bitmap, second byte
	unsigned short cks_{}; // checksum vector size, 0 for a fixed disc; no. directory entries / 4, rounded up
	unsigned short off_{}; // offset, number of reserved tracks
};

class Disk {
public:
	Disk() = default;

	virtual ~Disk() = default;

	virtual const DiskProperties& properties() const = 0;

	virtual const Sector& read(unsigned int pos) const = 0;

	virtual void write(unsigned int pos, const Sector& sector) = 0;

	virtual void save(const fs::path& path) const = 0;

	virtual bool modified() const = 0;

	static std::unique_ptr<Disk> create(const fs::path& path);

	static std::uint8_t read8(std::ifstream& in)
	{
		char buf = '\0';

		in.read(&buf, sizeof(buf));

		return std::bit_cast<std::uint8_t>(buf);
	}

	static std::uint16_t read16(std::ifstream& in)
	{
		std::array<char, sizeof(std::uint16_t)> buf{};

		in.read(buf.data(), buf.size());

		return *std::bit_cast<std::uint16_t*>(buf.data());
	}
};
