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
