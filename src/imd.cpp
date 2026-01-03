// SPDX-License-Identifier: GPL-2.0
#include <algorithm>
#include <array>
#include <ctime>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <stdexcept>

#include "diskpos.h"
#include "imd.h"
#include "version.h"

IMD::IMD(const fs::path& path)
{
	std::ifstream in(path);
	if (!in)
		throw std::runtime_error(std::format("failed to read {}", path.string()));

	// IMD v.vv: dd/mm/yyyy hh:mm:ss
	in.seekg(29);

	// skip over the comment
	char c = '\0';
	in >> c;
	while (c != 0x1a)
		in >> c;

	// read track by track and sector by sector
	for (;;) {
		Track track;

		track.mode_ = static_cast<DataTransferRate>(Disk::read8(in));
		if (!in)
			break;

		if (static_cast<unsigned char>(track.mode_) > 5)
			throw std::runtime_error(std::format("invalid mode byte: {}", static_cast<unsigned int>(track.mode_)));

		track.cylinder_ = Disk::read8(in);
		track.head_     = Disk::read8(in);
		track.nsectors_ = Disk::read8(in);
		track.ssize_    = static_cast<SectorSize>(Disk::read8(in));

		if (static_cast<unsigned char>(track.ssize_) > 6)
			throw std::runtime_error(std::format("invalid sector size: {}", static_cast<unsigned int>(track.ssize_)));

		track.numberingMap_.resize(track.nsectors_);
		in.read(reinterpret_cast<char*>(track.numberingMap_.data()), static_cast<std::streamsize>(track.numberingMap_.size()));

		if (track.head_ & 0x80) {
			track.cylinderMap_.resize(track.nsectors_);
			in.read(reinterpret_cast<char*>(track.cylinderMap_.data()), static_cast<std::streamsize>(track.cylinderMap_.size()));
		}

		if (track.head_ & 0x40) {
			track.headMap_.resize(track.nsectors_);
			in.read(reinterpret_cast<char*>(track.headMap_.data()), static_cast<std::streamsize>(track.headMap_.size()));
		}

		track.sectors_.reserve(track.nsectors_);
		for (unsigned int i = 0; i < track.nsectors_; i++) {
			unsigned char hdr = Disk::read8(in);

			if (!hdr)
				track.sectors_.push_back({});
			else if (hdr & 0x01) {
				std::vector<unsigned char> data(ss2size(track.ssize_));
				in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
				track.sectors_.push_back(std::move(data));
			} else {
				std::vector<unsigned char> data(ss2size(track.ssize_), Disk::read8(in));
				track.sectors_.push_back(std::move(data));
			}
		}
		tracks_.push_back(std::move(track));
	}

	// sort by track number
	std::sort(tracks_.begin(), tracks_.end(), [](const auto& a, const auto& b) {
		return a.cylinder_ < b.cylinder_;
	});

	unsigned int tracks     = 0;
	unsigned int heads      = 0;
	unsigned int sectors    = 0;
	unsigned int sectorSize = 0;

	for (const auto& track : tracks_) {
		tracks = std::max(tracks, static_cast<unsigned int>(track.cylinder_));
		heads  = std::max(heads, static_cast<unsigned int>(track.head_));

		if (sectors > 0 && sectors != track.nsectors_) {
			std::cerr << "warning: multiple sector counts per track are not supported (" << sectors << ", "
			          << static_cast<unsigned int>(track.nsectors_) << ")\n";
			const auto n0 = std::count_if(tracks_.begin(), tracks_.end(), [n = sectors](const auto& track) {
				return n == track.nsectors_;
			});
			const auto n1 = std::count_if(tracks_.begin(), tracks_.end(), [n = track.nsectors_](const auto& track) {
				return n == track.nsectors_;
			});
			if (n0 < n1)
				sectors = track.nsectors_;
			std::cerr << "warning: choosing the most common count: " << sectors << "\n";
		} else
			sectors = std::max(sectors, static_cast<unsigned int>(track.nsectors_));

		sectorSize = std::max(sectorSize, static_cast<unsigned int>(ss2size(track.ssize_)));
	}

	properties_ = DiskProperties(tracks + 1, heads + 1, sectors, sectorSize);

	for (auto& track : tracks_) {
		for (unsigned int i = 0; i < track.nsectors_; i++) {
			DiskPos dpos(properties_, track.cylinder_, track.head_ & 0x01, track.numberingMap_.at(i) - 1);
			sectors_[dpos.pos()] = &track.sectors_.at(i);
		}
	}
}

const Sector& IMD::read(unsigned int pos) const
{
	auto it = sectors_.find(pos);
	if (it != sectors_.end())
		return *it->second;

	static const Sector empty;
	return empty;
}

void IMD::write(unsigned int pos, const Sector& sector)
{
	if (pos > properties_.maxPos())
		throw std::runtime_error(std::format("invalid sector position: {} (max: {})", pos, properties_.maxPos()));

	if (!sector.data().empty() && sector.data().size() != properties_.sectorSize())
		throw std::runtime_error(std::format("invalid sector size: {} (expected: {})", sector.data().size(), properties_.sectorSize()));

	auto it = sectors_.find(pos);
	if (it != sectors_.end())
		*it->second = sector;
	else {
		DiskPos dpos(properties_, pos);

		Track track;

		if (tracks_.empty())
			track.mode_ = DataTransferRate::DTR_250_MFM;
		else
			track.mode_ = tracks_.front().mode_;

		track.cylinder_ = dpos.track();
		track.head_     = dpos.head();
		track.nsectors_ = properties_.sectors();

		track.ssize_ = size2ss(sector.data().size());
		if (track.ssize_ == SectorSize::SS_INVALID)
			throw std::runtime_error(std::format("unsupported sector size: {}", sector.data().size()));

		if (tracks_.empty()) {
			track.numberingMap_.resize(track.nsectors_);
			for (unsigned char i = 0; i < track.nsectors_; i++)
				track.numberingMap_.at(i) = i + 1;
		} else
			track.numberingMap_ = tracks_.front().numberingMap_;

		track.sectors_.resize(track.nsectors_);
		track.sectors_.at(dpos.sector()) = sector;

		for (unsigned int i = 0; i < track.nsectors_; i++) {
			DiskPos __dpos(properties_, track.cylinder_, track.head_, track.numberingMap_.at(i) - 1);
			sectors_[__dpos.pos()] = &track.sectors_.at(i);
		}

		tracks_.push_back(std::move(track));
	}

	modified_ = true;
}

void IMD::save(const fs::path& path) const
{
	const auto now = std::time(nullptr);
	struct tm result{};
	auto __tm = localtime_r(&now, &result);

	std::ofstream of(path, std::ios_base::trunc);
	if (!of)
		throw std::runtime_error(std::format("failed to write {}", path.string()));

	// clang-format off
	of << "IMD 1.17: "
	   << std::setw(2) << std::setfill('0') << __tm->tm_mon << "/"
	   << std::setw(2) << std::setfill('0') << __tm->tm_mday << "/"
	   << std::setw(2) << std::setfill('0') << __tm->tm_year + 1900 << " "
	   << std::setw(2) << std::setfill('0') << __tm->tm_hour << ":"
	   << std::setw(2) << std::setfill('0') << __tm->tm_min << ":"
	   << std::setw(2) << std::setfill('0') << __tm->tm_sec << "\r\n"
	   << std::format("fsp {}.{}.{}\x1a", FUSE_SPECTRUM_VERSION_MAJOR, FUSE_SPECTRUM_VERSION_MINOR, FUSE_SPECTRUM_VERSION_PATCH);
	// clang-format on

	for (const auto& track : tracks_) {
		of.write(reinterpret_cast<const char*>(&track.mode_), sizeof(track.mode_));
		of.write(reinterpret_cast<const char*>(&track.cylinder_), sizeof(track.cylinder_));
		of.write(reinterpret_cast<const char*>(&track.head_), sizeof(track.head_));
		of.write(reinterpret_cast<const char*>(&track.nsectors_), sizeof(track.nsectors_));
		of.write(reinterpret_cast<const char*>(&track.ssize_), sizeof(track.ssize_));

		for (const auto n : track.numberingMap_)
			of.write(reinterpret_cast<const char*>(&n), sizeof(n));

		if (track.head_ & 0x80) {
			for (const auto c : track.cylinderMap_)
				of.write(reinterpret_cast<const char*>(&c), sizeof(c));
		}

		if (track.head_ & 0x40) {
			for (const auto h : track.headMap_)
				of.write(reinterpret_cast<const char*>(&h), sizeof(h));
		}

		for (const auto& sector : track.sectors_) {
			if (sector.data().empty()) {
				const unsigned char hdr = 0;
				of.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
			} else {
				const auto c  = sector.data().front();
				const auto it = std::find_if_not(sector.data().begin(), sector.data().end(), [c](const auto& e) {
					return c == e;
				});
				if (it == sector.data().end()) {
					const unsigned char hdr = 2;
					of.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
					of.write(reinterpret_cast<const char*>(&c), sizeof(c));
				} else {
					const unsigned char hdr = 1;
					of.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
					of.write(reinterpret_cast<const char*>(sector.data().data()),
					         static_cast<std::streamsize>(sector.data().size()));
				}
			}
		}
	}
}

bool IMD::detect(const fs::path& path)
{
	std::ifstream in(path);
	if (in) {
		std::array<char, 11> buf{};

		in.read(buf.data(), buf.size() - 1);
		if (in) {
			static const std::regex re("IMD\\s[0-9]\\.[0-9]{2}:\\s");

			if (std::regex_search(buf.data(), re))
				return true;
		}
	}

	return false;
}
