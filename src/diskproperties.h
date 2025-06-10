// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <format>
#include <stdexcept>

class DiskProperties {
	unsigned int tracks_{};
	unsigned int heads_{};
	unsigned int sectors_{};
	unsigned int sectorSize_{};
	unsigned int sectorsPerTrack_{};
	unsigned int maxPos_{};
	unsigned int size_{};

public:
	DiskProperties() = default;

	DiskProperties(unsigned int tracks, unsigned heads, unsigned int sectors, unsigned int sectorSize)
	    : tracks_{tracks}
	    , heads_{heads}
	    , sectors_{sectors}
	    , sectorSize_{sectorSize}
	    , sectorsPerTrack_{sectors * heads}
	    , size_{tracks * sectors * heads * sectorSize}
	{
		const auto p = tracks_ * sectors_ * heads_;

		if (p)
			maxPos_ = p - 1;
	}

	auto tracks() const
	{
		return tracks_;
	}

	auto heads() const
	{
		return heads_;
	}

	auto sectors() const
	{
		return sectors_;
	}

	auto sectorSize() const
	{
		return sectorSize_;
	}

	auto sectorsPerTrack() const
	{
		return sectorsPerTrack_;
	}

	auto maxPos() const
	{
		return maxPos_;
	}

	auto size() const
	{
		return size_;
	}

	void validate(unsigned int track, unsigned int head, unsigned int sector) const
	{
		if (track >= tracks_)
			throw std::runtime_error(std::format("invalid track number: {} (max: {})", track, tracks_ - 1));

		if (head >= heads_)
			throw std::runtime_error(std::format("invalid head number: {} (max: {})", head, heads_ - 1));

		if (sector >= sectors_)
			throw std::runtime_error(std::format("invalid sector number: {} (max: {})", sector, sectors_ - 1));
	}
};
