// SPDX-License-Identifier: GPL-2.0
#pragma once

#include "diskproperties.h"

class DiskPos {
	unsigned int track_{};
	unsigned int head_{};
	unsigned int sector_{};
	unsigned int pos_{};

public:
	DiskPos() = default;

	DiskPos(const DiskProperties& props, unsigned int track, unsigned int head, unsigned int sector)
	    : track_{track}
	    , head_{head}
	    , sector_{sector}
	    , pos_{track_ * props.sectorsPerTrack() + head_ * props.sectors() + sector_}
	{
		props.validate(track_, head_, sector_);
	}

	DiskPos(const DiskProperties& props, unsigned int pos)
	    : track_{pos / props.sectorsPerTrack()}
	    , head_{(pos % props.sectorsPerTrack()) / props.sectors()}
	    , sector_{(pos % props.sectorsPerTrack()) % props.sectors()}
	    , pos_{pos}
	{
		props.validate(track_, head_, sector_);
	}

	auto track() const
	{
		return track_;
	}

	auto head() const
	{
		return head_;
	}

	auto sector() const
	{
		return sector_;
	}

	auto pos() const
	{
		return pos_;
	}
};
