// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <filesystem>
#include <map>
#include <vector>

#include "disk.h"
#include "sector.h"

namespace fs = std::filesystem;

// clang-format off
enum class DataTransferRate : unsigned char {
	DTR_500_FM  = 0,
	DTR_300_FM  = 1,
	DTR_250_FM  = 2,
	DTR_500_MFM = 3,
	DTR_300_MFM = 4,
	DTR_250_MFM = 5,
	DTR_INVALID = 255
};

enum class SectorSize : unsigned char {
	SS_128     = 0,
	SS_256     = 1,
	SS_512     = 2,
	SS_1024    = 3,
	SS_2048    = 4,
	SS_4096    = 5,
	SS_8192    = 6,
	SS_INVALID = 255
};
// clang-format on

class IMD final : public Disk {
	struct Track {
		DataTransferRate mode_{DataTransferRate::DTR_INVALID};
		unsigned char cylinder_{};
		unsigned char head_{};
		unsigned char nsectors_{};
		SectorSize ssize_{SectorSize::SS_INVALID};
		std::vector<unsigned char> numberingMap_;
		std::vector<unsigned char> cylinderMap_;
		std::vector<unsigned char> headMap_;
		std::vector<Sector> sectors_;
	};

	DiskProperties properties_;
	std::vector<Track> tracks_;
	std::map<unsigned int, Sector*> sectors_;
	bool modified_{};

	static unsigned int ss2size(SectorSize ss)
	{
		unsigned int size = 0;

		switch (ss) {
		case SectorSize::SS_128:
			size = 128;
			break;
		case SectorSize::SS_256:
			size = 256;
			break;
		case SectorSize::SS_512:
			size = 512;
			break;
		case SectorSize::SS_1024:
			size = 1024;
			break;
		case SectorSize::SS_2048:
			size = 2048;
			break;
		case SectorSize::SS_4096:
			size = 4096;
			break;
		case SectorSize::SS_8192:
			size = 8192;
			break;
		default:
			break;
		}

		return size;
	}

	static SectorSize size2ss(unsigned int size)
	{
		SectorSize ssize = SectorSize::SS_INVALID;

		switch (size) {
		case 128:
			ssize = SectorSize::SS_128;
			break;
		case 256:
			ssize = SectorSize::SS_256;
			break;
		case 512:
			ssize = SectorSize::SS_512;
			break;
		case 1024:
			ssize = SectorSize::SS_1024;
			break;
		case 2048:
			ssize = SectorSize::SS_2048;
			break;
		case 4096:
			ssize = SectorSize::SS_4096;
			break;
		case 8192:
			ssize = SectorSize::SS_8192;
			break;
		default:
			break;
		}

		return ssize;
	}

public:
	IMD(const fs::path& path);

	~IMD() override = default;

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
