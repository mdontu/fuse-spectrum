// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <vector>

class Sector {
	std::vector<unsigned char> data_;

public:
	Sector() = default;

	Sector(std::vector<unsigned char>&& data)
	    : data_{std::move(data)}
	{
	}

	const auto& data() const
	{
		return data_;
	}
};
