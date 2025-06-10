// SPDX-License-Identifier: GPL-2.0
#include "disk.h"
#include "dsk.h"
#include "imd.h"

std::unique_ptr<Disk> Disk::create(const fs::path& path)
{
	if (IMD::detect(path))
		return std::make_unique<IMD>(path);

	if (DSK::detect(path))
		return std::make_unique<DSK>(path);

	return {};
}
