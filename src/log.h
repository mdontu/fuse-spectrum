// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <iomanip>
#include <iostream>

static inline void hexdump(const void* buf, size_t size)
{
	constexpr auto LINE_LENGTH = 32u;

	for (unsigned int i = 0; i < size; i++) {
		std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(static_cast<const unsigned char*>(buf)[i])
		          << " ";
		if (((i + 1) % LINE_LENGTH) == 0) {
			std::cout << " ";
			for (unsigned int j = i + 1 - LINE_LENGTH; j <= i; j++) {
				auto c = static_cast<const unsigned char*>(buf)[j];
				if (c < 32 || c > 127)
					std::cout << ".";
				else
					std::cout << c;
			}
			std::cout << "\n";
		}
	}
	if (size % LINE_LENGTH)
		std::cout << "\n";
}
