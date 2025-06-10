// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <array>
#include <filesystem>
#include <map>

#include "disk.h"
#include "sector.h"

namespace fs = std::filesystem;

class DSK final: public Disk {
	struct SectorInfo {
		unsigned char track_{};
		unsigned char side_{};
		unsigned char id_{};
		unsigned char size_{};
		unsigned char sreg1_{};
		unsigned char sreg2_{};
		unsigned short dataLength_{};
	};

	struct Track {
		unsigned char track_{};
		unsigned char side_{};
		unsigned char sectorSize_{};
		unsigned char sectorCount_{};
		unsigned char gap_{};
		unsigned char filler_{};
		std::vector<SectorInfo> sectorInfos_;
		std::vector<Sector> sectors_;
	};

	DiskProperties properties_;
	bool modified_{};
	std::vector<unsigned char> trackSizes_;
	std::vector<Track> tracks_;
	std::map<unsigned int, Sector*> sectors_;
	inline static const auto stag = std::to_array({'M', 'V', ' ', '-', ' ', 'C', 'P', 'C', 'E', 'M', 'U', ' ', 'D', 'i', 's', 'k', '-', 'F', 'i', 'l', 'e', '\r', '\n', 'D', 'i', 's', 'k', '-', 'I', 'n', 'f', 'o', '\r', '\n'}); // standard
	inline static const auto etag = std::to_array({'E', 'X', 'T', 'E', 'N', 'D', 'E', 'D', ' ', 'C', 'P', 'C', ' ', 'D', 'S', 'K', ' ', 'F', 'i', 'l', 'e', '\r', '\n', 'D', 'i', 's', 'k', '-', 'I', 'n', 'f', 'o', '\r', '\n'}); // extended
	inline static const auto trackTag = std::to_array({'T', 'r', 'a', 'c', 'k', '-', 'I', 'n', 'f', 'o', '\r', '\n'});
	bool extended_{};

public:
	DSK(const fs::path& path);

	~DSK() override = default;

	const DiskProperties& properties() const override
	{
		return properties_;
	}

	const Sector& read(unsigned int pos) const override;

	void write(unsigned int pos, const Sector& sector) override;

	void save(const fs::path& path) const override;

	bool modified() const override
	{
		return modified_;
	}

	static bool detect(const fs::path& path);
};
