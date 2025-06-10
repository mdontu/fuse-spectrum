// SPDX-License-Identifier: GPL-2.0
#include <algorithm>
#include <fstream>
#include <stdexcept>

#include "diskpos.h"
#include "dsk.h"
#include "version.h"

constexpr auto DATA_ALIGNMENT   = 256l;
constexpr auto SECTOR_SIZE_UNIT = 256u;

DSK::DSK(const fs::path& path)
{
	std::ifstream in(path);
	if (!in)
		throw std::runtime_error(std::format("failed to read {}", path.string()));

	std::array<char, 34> buf{};
	in.read(buf.data(), buf.size());
	if (!in)
		throw std::runtime_error("failed to read the file header");

	// Jump over the creator string
	in.seekg(14, std::ios_base::cur);

	const auto tracks = Disk::read8(in);
	const auto sides  = Disk::read8(in);

	// Jump over EDSK unused bytes (track size)
	in.seekg(2, std::ios_base::cur);

	if (std::equal(stag.begin(), stag.end(), buf.begin())) {
		// Jump over unused bytes (track sizes for EDSK)
		in.seekg(204, std::ios_base::cur);

		tracks_.reserve(tracks);

		for (unsigned char i = 0; i < tracks; i++) {
			std::array<char, trackTag.size()> tag{};
			const auto trackPos = in.tellg();

			in.read(tag.data(), tag.size());
			if (!in || !std::equal(trackTag.begin(), trackTag.end(), tag.begin()))
				throw std::runtime_error("unexpected track tag");

			// Jump over unused bytes
			in.seekg(4, std::ios_base::cur);

			Track track;

			track.track_ = Disk::read8(in);
			track.side_  = Disk::read8(in);

			// Jump over unused bytes
			in.seekg(2, std::ios_base::cur);

			track.sectorSize_  = Disk::read8(in);
			track.sectorCount_ = Disk::read8(in);
			track.gap_         = Disk::read8(in);
			track.filler_      = Disk::read8(in);

			track.sectorInfos_.reserve(track.sectorCount_);

			for (unsigned char j = 0; j < track.sectorCount_; j++) {
				SectorInfo info;

				info.track_ = Disk::read8(in);
				info.side_  = Disk::read8(in);
				info.id_    = Disk::read8(in);
				info.size_  = Disk::read8(in);
				info.sreg1_ = Disk::read8(in);
				info.sreg2_ = Disk::read8(in);

				// Jump over unused bytes
				in.seekg(2, std::ios_base::cur);

				track.sectorInfos_.push_back(info);
			}

			// Jump to the first sector data
			in.seekg(trackPos + DATA_ALIGNMENT, std::ios_base::beg);

			track.sectors_.reserve(track.sectorInfos_.size());

			for (const auto& info : track.sectorInfos_) {
				std::vector<unsigned char> data(info.size_ * SECTOR_SIZE_UNIT);

				in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));

				Sector sector(std::move(data));

				track.sectors_.push_back(std::move(sector));
			}

			tracks_.push_back(std::move(track));
		}
	} else if (std::equal(etag.begin(), etag.end(), buf.begin())) {
		extended_ = true;

		trackSizes_.reserve(tracks * sides);

		for (unsigned int i = 0; i < tracks * sides; i++)
			trackSizes_.push_back(Disk::read8(in));

		// Position on the first track data
		in.seekg(DATA_ALIGNMENT, std::ios_base::beg);

		tracks_.reserve(tracks);

		for (unsigned char t = 0; t < tracks; t++) {
			for (unsigned char s = 0; s < sides; s++) {
				if (!trackSizes_.at(t * sides + s))
					continue;

				std::array<char, trackTag.size()> tag{};
				const auto trackPos = in.tellg();

				in.read(tag.data(), tag.size());
				if (!in || !std::equal(trackTag.begin(), trackTag.end(), tag.begin()))
					throw std::runtime_error("unexpected track tag");

				// Jump over unused bytes
				in.seekg(4, std::ios_base::cur);

				Track track;

				track.track_ = Disk::read8(in);
				track.side_  = Disk::read8(in);

				// Jump over unused bytes
				in.seekg(2, std::ios_base::cur);

				track.sectorSize_  = Disk::read8(in);
				track.sectorCount_ = Disk::read8(in);
				track.gap_         = Disk::read8(in);
				track.filler_      = Disk::read8(in);

				track.sectorInfos_.reserve(track.sectorCount_);

				for (unsigned char j = 0; j < track.sectorCount_; j++) {
					SectorInfo info;

					info.track_      = Disk::read8(in);
					info.side_       = Disk::read8(in);
					info.id_         = Disk::read8(in);
					info.size_       = Disk::read8(in);
					info.sreg1_      = Disk::read8(in);
					info.sreg2_      = Disk::read8(in);
					info.dataLength_ = Disk::read16(in);

					track.sectorInfos_.push_back(info);
				}

				// Jump to the first sector data
				in.seekg(trackPos + DATA_ALIGNMENT, std::ios_base::beg);

				track.sectors_.reserve(track.sectorInfos_.size());

				for (const auto& info : track.sectorInfos_) {
					std::vector<unsigned char> data(info.dataLength_);

					in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));

					Sector sector(std::move(data));

					track.sectors_.push_back(std::move(sector));
				}

				tracks_.push_back(std::move(track));
			}
		}
	}

	unsigned int sectorCount = 0;
	unsigned int sectorSize  = 0;

	for (const auto& track : tracks_) {
		sectorCount = std::max(sectorCount, static_cast<unsigned int>(track.sectorCount_));
		sectorSize  = std::max(sectorSize, track.sectorSize_ * SECTOR_SIZE_UNIT);
	}

	properties_ = DiskProperties(tracks, sides, sectorCount, sectorSize);

	for (auto& track : tracks_) {
		auto i = track.sectorInfos_.cbegin();
		auto j = track.sectors_.begin();

		for (; i != track.sectorInfos_.cend() && j != track.sectors_.end(); ++i, ++j) {
			const DiskPos dpos(properties_, i->track_, i->side_, i->id_ - 1);
			sectors_[dpos.pos()] = &*j;
		}
	}
}

const Sector& DSK::read(unsigned int pos) const
{
	auto it = sectors_.find(pos);
	if (it != sectors_.end())
		return *it->second;

	static const Sector empty;
	return empty;
}

void DSK::write(unsigned int pos, const Sector& sector)
{
	if (pos > properties_.maxPos())
		throw std::runtime_error(std::format("invalid sector position: {} (max: {})", pos, properties_.maxPos()));

	if (!sector.data().empty() && sector.data().size() != properties_.sectorSize())
		throw std::runtime_error(std::format("invalid sector size: {} (expected: {})", sector.data().size(), properties_.sectorSize()));

	auto it = sectors_.find(pos);
	if (it != sectors_.end())
		*it->second = sector;
	else {
		const DiskPos dpos(properties_, pos);

		Track track;

		track.track_       = dpos.track();
		track.side_        = dpos.head();
		track.sectorSize_  = properties_.sectorSize() / SECTOR_SIZE_UNIT;
		track.sectorCount_ = properties_.sectors();

		// PC-compatible disk controllers do not use a gap but drivers
		// specify 0x1b (27) just in case.
		track.gap_ = 0x1b;

		track.filler_ = 0xe5;

		track.sectorInfos_.reserve(track.sectorCount_);

		for (unsigned char i = 0; i < track.sectorCount_; i++) {
			SectorInfo info;

			info.track_ = dpos.track();
			info.side_  = dpos.head();
			info.id_    = i + 1;
			info.size_  = properties_.sectorSize() / SECTOR_SIZE_UNIT;

			if (extended_)
				info.dataLength_ = properties_.sectorSize();

			track.sectorInfos_.push_back(info);
		}

		track.sectors_.resize(track.sectorCount_);
		track.sectors_.at(dpos.sector()) = sector;

		for (unsigned char i = 0; i < track.sectorCount_; i++) {
			const DiskPos __dpos(properties_, track.track_, track.side_, i);
			sectors_[__dpos.pos()] = &track.sectors_.at(i);
		}

		tracks_.push_back(std::move(track));
	}

	modified_ = true;
}

void DSK::save(const fs::path& path) const
{
	std::ofstream of(path, std::ios_base::trunc);
	if (!of)
		throw std::runtime_error(std::format("failed to write {}", path.string()));

	if (extended_)
		of.write(etag.data(), etag.size());
	else
		of.write(stag.data(), stag.size());

	std::array<char, 14> creator{};
	std::snprintf(creator.data(), creator.size(), "fsp %u.%u.%u", FUSE_SPECTRUM_VERSION_MAJOR, FUSE_SPECTRUM_VERSION_MINOR, FUSE_SPECTRUM_VERSION_PATCH);
	of.write(creator.data(),  creator.size());

	unsigned char tracks = properties_.tracks();
	of.write(reinterpret_cast<const char*>(&tracks), sizeof(tracks));

	unsigned char sides = properties_.heads();
	of.write(reinterpret_cast<const char*>(&sides), sizeof(sides));

	if (extended_) {
		static const unsigned short trackSize = 0;
		of.write(reinterpret_cast<const char*>(&trackSize), sizeof(trackSize));
	} else {
		const unsigned short trackSize = properties_.sectors() * properties_.sectorSize() + SECTOR_SIZE_UNIT;
		const unsigned char low        = trackSize & 0xff;
		const unsigned char high       = (trackSize & 0xff00) >> 8u;
		of.write(reinterpret_cast<const char*>(&low), sizeof(low));
		of.write(reinterpret_cast<const char*>(&high), sizeof(high));
	}

	if (extended_) {
		for (const auto size : trackSizes_)
			of.write(reinterpret_cast<const char*>(&size), sizeof(size));
	} else {
		static const std::array<char, 204> unused{};
		of.write(unused.data(), unused.size());
	}

	if (of.tellp() % DATA_ALIGNMENT) {
		const auto padBytes = DATA_ALIGNMENT - of.tellp() % DATA_ALIGNMENT;
		for (std::remove_const_t<decltype(padBytes)> i = 0; i < padBytes; i++) {
			char c = '\0';
			of.write(&c, sizeof(c));
		}
	}

	for (const auto& track : tracks_) {
		const auto trackPos = of.tellp();

		of.write(trackTag.data(), trackTag.size());

		static const std::array<char, 4> unused0{};
		of.write(unused0.data(), unused0.size());

		of.write(reinterpret_cast<const char*>(&track.track_), sizeof(track.track_));
		of.write(reinterpret_cast<const char*>(&track.side_), sizeof(track.side_));

		if (extended_) {
			static const std::array<char, 2> unused1{0x00, 0x00};
			of.write(unused1.data(), unused1.size());
		} else {
			static const std::array<char, 2> unused1{0x01, 0x00};
			of.write(unused1.data(), unused1.size());
		}

		of.write(reinterpret_cast<const char*>(&track.sectorSize_), sizeof(track.sectorSize_));
		of.write(reinterpret_cast<const char*>(&track.sectorCount_), sizeof(track.sectorCount_));
		of.write(reinterpret_cast<const char*>(&track.gap_), sizeof(track.gap_));
		of.write(reinterpret_cast<const char*>(&track.filler_), sizeof(track.filler_));

		for (const auto& info : track.sectorInfos_) {
			of.write(reinterpret_cast<const char*>(&info.track_), sizeof(info.track_));
			of.write(reinterpret_cast<const char*>(&info.side_), sizeof(info.side_));
			of.write(reinterpret_cast<const char*>(&info.id_), sizeof(info.id_));
			of.write(reinterpret_cast<const char*>(&info.size_), sizeof(info.size_));
			of.write(reinterpret_cast<const char*>(&info.sreg1_), sizeof(info.sreg1_));
			of.write(reinterpret_cast<const char*>(&info.sreg2_), sizeof(info.sreg2_));
			of.write(reinterpret_cast<const char*>(&info.dataLength_), sizeof(info.dataLength_));
		}

		of.seekp(trackPos + DATA_ALIGNMENT, std::ios_base::beg);

		for (const auto& sector : track.sectors_)
			of.write(reinterpret_cast<const char*>(sector.data().data()), static_cast<std::streamsize>(sector.data().size()));
	}
}

bool DSK::detect(const fs::path& path)
{
	auto standardFmt = [path]() -> bool {
		std::ifstream in(path);
		if (in) {
			std::array<char, stag.size()> buf{};

			in.read(buf.data(), buf.size());

			return in && buf == stag;
		}

		return false;
	};

	auto extendedFmt = [path]() -> bool {
		std::ifstream in(path);
		if (in) {
			std::array<char, etag.size()> buf{};

			in.read(buf.data(), buf.size());

			return in && buf == etag;
		}

		return false;
	};

	return standardFmt() || extendedFmt();
}
