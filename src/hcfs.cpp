// SPDX-License-Identifier: GPL-2.0
#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <sys/stat.h>
#include <sys/vfs.h>

#include "diskpos.h"
#include "hcfs.h"
#include "log.h"

namespace fs = std::filesystem;

const DiskParameterBlock HCFS::dpb_{
	.spt_ = 32,
	.bsh_ = 4,
	.blm_ = 15,
	.exm_ = 0,
	.dsm_ = 320,
	.drm_ = 127,
	.al0_ = 0xc0,
	.al1_ = 0,
	.cks_ = 0,
	.off_ = 0
};

HCFS::HCFS(Disk* disk)
    : disk_{disk}
{
	if (interleave640_.size() != disk_->properties().sectors() && interleave320_.size() != disk_->properties().sectors())
		throw std::runtime_error(std::format("no sector interleave available for the current number of sectors ({})", disk_->properties().sectors()));

	loadFAT();
}

HCFS::~HCFS()
{
	saveFAT();
}

unsigned int HCFS::ipos(unsigned int pos) const
{
	const DiskPos apos(disk_->properties(), pos);
	const DiskPos bpos(disk_->properties(), apos.track(), apos.head(), interleave640_.size() == disk_->properties().sectors() ? interleave640_.at(apos.sector()) : interleave320_.at(apos.sector()));

	return bpos.pos();
}

void HCFS::readBlock(unsigned int block, std::vector<unsigned char>& buf) const
{
	buf.clear();
	buf.reserve(HCFS_BLOCK_SIZE);

	const auto start = block * HCFS_BLOCK_SIZE / disk_->properties().sectorSize();
	for (unsigned int i = start; i < (start + HCFS_BLOCK_SIZE / disk_->properties().sectorSize()); i++) {
		auto& sector = disk_->read(ipos(i));

		if (sector.data().empty())
			buf.insert(buf.end(), 0, disk_->properties().sectorSize());
		else
			std::copy(sector.data().begin(), sector.data().end(), std::back_inserter(buf));
	}
}

void HCFS::writeBlock(unsigned int block, const std::vector<unsigned char>& buf) const
{
	unsigned int nsect = 0;
	std::vector<unsigned char> __buf;

	__buf.reserve(disk_->properties().sectorSize());

	const auto start = block * HCFS_BLOCK_SIZE / disk_->properties().sectorSize();
	for (const auto b : buf) {
		__buf.insert(__buf.end(), b);
		if (__buf.size() == disk_->properties().sectorSize()) {
			const Sector sector(std::move(__buf));

			disk_->write(ipos(start + nsect), sector);

			nsect++;

			__buf.clear();
			__buf.reserve(disk_->properties().sectorSize());
		}
	}

	if (!__buf.empty()) {
		const Sector sector(std::move(__buf));

		disk_->write(ipos(start + nsect), sector);
	}
}

void HCFS::loadFAT()
{
	fatEntries_.clear();
	fatEntries_.reserve(2 * HCFS_BLOCK_SIZE / sizeof(fatEntries_.front()));

	std::vector<unsigned char> buf;

	const unsigned int start = dpb_.off_ * disk_->properties().sectorsPerTrack() * disk_->properties().sectorSize() / HCFS_BLOCK_SIZE;
	readBlock(start, buf);

	for (unsigned int i = 0; i < (buf.size() / sizeof(fatEntries_.front())); i++)
		fatEntries_.push_back(reinterpret_cast<decltype(&fatEntries_.front())>(buf.data())[i]);

	readBlock(start + 1, buf);

	for (unsigned int i = 0; i < (buf.size() / sizeof(fatEntries_.front())); i++)
		fatEntries_.push_back(reinterpret_cast<decltype(&fatEntries_.front())>(buf.data())[i]);
}

void HCFS::saveFAT() const
{
	if (!disk_->modified())
		return;

	// initialize all free blocks
	std::vector<bool> freeBlocks(disk_->properties().size() / HCFS_BLOCK_SIZE, true);

	freeBlocks.at(0) = false;
	freeBlocks.at(1) = false;

	for (const auto& entry : fatEntries_) {
		if (entry.free())
			continue;

		for (const auto& au : entry.allocationUnits_)
			freeBlocks.at(au) = false;
	}

	unsigned int block = 0;
	for (const auto& fb : freeBlocks) {
		if (fb) {
			static const std::vector<unsigned char> buf(HCFS_BLOCK_SIZE, HCFS_FREE_BYTE);
			writeBlock(block, buf);
		}
		block++;
	}

	// write back all FAT entries
	std::vector<unsigned char> buf;

	buf.reserve(fatEntries_.size() * sizeof(fatEntries_.front()));

	for (const auto& entry : fatEntries_)
		buf.insert(buf.end(), reinterpret_cast<const unsigned char*>(&entry), reinterpret_cast<const unsigned char*>(&entry) + sizeof(entry));

	for (unsigned int i = 0; i < (buf.size() / HCFS_BLOCK_SIZE); i++)
		writeBlock(i, {buf.data() + i * HCFS_BLOCK_SIZE, buf.data() + i * HCFS_BLOCK_SIZE + HCFS_BLOCK_SIZE});

	auto r = buf.size() % HCFS_BLOCK_SIZE;
	if (r)
		writeBlock(buf.size() / HCFS_BLOCK_SIZE + 1, {buf.data() + buf.size() - r, buf.data() + buf.size()});
}

std::optional<std::reference_wrapper<FATEntry>> HCFS::find(const std::string& path)
{
	const auto it = std::find_if(fatEntries_.begin(), fatEntries_.end(), [&path](const auto& entry) {
		return !entry.free() && !entry.extent() && entry == path;
	});

	if (it != fatEntries_.end())
		return *it;

	return {};
}

void HCFS::printFAT() const
{
	unsigned int n = 0;

	for (const auto& entry : fatEntries_) {
		if (!entry.free()) {
			std::cout << "entry: " << n++ << "\n";
			std::cout << "\tname: \"" << entry.name() << "\"";

			if (entry.name_.at(entry.name_.size() - 3) & 0x80)
				std::cout << " (read-only)";

			if (entry.name_.at(entry.name_.size() - 2) & 0x80)
				std::cout << " (hidden)";

			if (entry.extent())
				std::cout << " (extent)";

			std::cout << "\n";

			std::cout << "\trecord count: " << static_cast<unsigned int>(entry.recordCount_) << "\n";
			std::cout << "\tallocation units: ";

			for (const auto unit : entry.allocationUnits_)
				std::cout << std::hex << std::setw(4) << std::setfill('0') << unit << " ";

			std::cout << std::dec << "\n";
		}
	}
}

int HCFS::getattr(const char* path, struct stat* buf, struct fuse_file_info* /* info */)
{
	const fs::path __path{path};

	if (__path.parent_path() != "/")
		return -ENOENT;

	if (__path == "/") {
		const unsigned int n = std::count_if(fatEntries_.begin(), fatEntries_.end(), [](const auto& entry) {
			return !entry.free() && !entry.extent();
		});

		std::memset(buf, 0, sizeof(*buf));
		buf->st_mode    = S_IFDIR | S_IXUSR | S_IRUSR | S_IWUSR | S_IXGRP | S_IRGRP | S_IXOTH | S_IROTH;
		buf->st_nlink   = 1;
		buf->st_size    = n * 2;
		buf->st_blksize = disk_->properties().sectorSize();
		buf->st_blocks  = HCFS_BLOCK_SIZE * 2 / 512;

		return 0;
	}

	int err              = 0;
	unsigned int entries = 0;
	unsigned int size    = 0;

	for (const auto& entry : fatEntries_) {
		if (entry.free())
			continue;

		if (entry == __path.filename()) {
			size += entry.size();

			entries++;

			if (!entry.full())
				break;
		}
	}

	if (entries) {
		std::memset(buf, 0, sizeof(*buf));
		buf->st_mode    = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
		buf->st_nlink   = 1;
		buf->st_size    = size;
		buf->st_blksize = disk_->properties().sectorSize();
		buf->st_blocks  = buf->st_size / 512 + (buf->st_size % 512 ? 1 : 0);
	} else
		err = -ENOENT;

	return err;
}

int HCFS::unlink(const char* path)
{
	const fs::path __path{path};

	if (__path.parent_path() != "/")
		return -ENOENT;

	auto entry = find(__path.filename());

	if (!entry)
		return -ENOENT;

	entry.value().get().clear();

	return 0;
}

int HCFS::truncate(const char* path, off_t length, struct fuse_file_info* /* info */)
{
	const fs::path __path{path};

	if (__path.parent_path() != "/")
		return -ENOENT;

	unsigned int size    = 0;
	unsigned int blocks  = 0;
	unsigned int entries = 0;

	for (const auto& entry : fatEntries_) {
		if (entry.free())
			continue;

		if (entry != __path.filename())
			continue;

		size   += entry.size();
		blocks += entry.blocks();

		entries++;

		if (!entry.full())
			break;
	}

	if (!entries)
		return -ENOENT;

	if (length == size)
		return 0;

	if (length < size) {
		unsigned int n = length / HCFS_BLOCK_SIZE + (length % HCFS_BLOCK_SIZE ? 1 : 0);
		n = blocks - n;

		for (auto it = fatEntries_.rbegin(); it != fatEntries_.rend(); ++it) {
			auto& entry = *it;

			if (entry.free())
				continue;

			if (entry != __path.filename())
				continue;

			auto aunits = entry.allocationUnits_.size();

			while (aunits > 0 && n > 0) {
				if (entry.allocationUnits_.at(aunits - 1)) {
					entry.allocationUnits_.at(aunits - 1) = 0;
					n--;
				}
				aunits--;
			}

			entry.recordCount_ = aunits * HCFS_BLOCK_SIZE / HCFS_RECORD_SIZE;
			if (!entry.recordCount_ && n)
				entry.clear();
		}

		return (n ? -ENOENT : 0);
	}

	std::vector<bool> blockMap(disk_->properties().size() / HCFS_BLOCK_SIZE, true);

	blockMap.at(0) = false;
	blockMap.at(1) = false;

	for (const auto& entry : fatEntries_) {
		if (entry.free())
			continue;

		for (const auto& au : entry.allocationUnits_)
			blockMap.at(au) = false;
	}

	auto getFreeBlock = [&blockMap]() -> unsigned short {
		const auto it = std::find(blockMap.begin(), blockMap.end(), true);
		if (it == blockMap.end())
			return 0;
		*it = false;
		return it - blockMap.begin();
	};

	unsigned int n = length / HCFS_BLOCK_SIZE + (length % HCFS_BLOCK_SIZE ? 1 : 0);
	n -= blocks;

	bool full = false;
	unsigned char extents = 0;
	for (auto& entry : fatEntries_) {
		if (!full) {
			if (entry.free())
				continue;

			if (entry != __path.filename())
				continue;

			extents++;

			if (entry.full())
				continue;
		} else {
			if (!entry.free())
				continue;

			entry.clear();
			entry.userCode_ = 0;
			entry.setName(__path.filename());
			entry.exLo_ = extents++;
		}

		unsigned int aunits = 0;
		for (; aunits < entry.allocationUnits_.size() && n > 0; aunits++) {
			if (entry.allocationUnits_.at(aunits))
				continue;

			entry.allocationUnits_.at(aunits) = getFreeBlock();
			if (!entry.allocationUnits_.at(aunits))
				break;

			// wipe the block's contents
			const std::vector<unsigned char> buf(HCFS_BLOCK_SIZE, HCFS_FREE_BYTE);
			writeBlock(entry.allocationUnits_.at(aunits), buf);

			n--;
		}

		entry.recordCount_ = aunits * HCFS_BLOCK_SIZE / HCFS_RECORD_SIZE;

		full = entry.full();
	}

	return (n ? -ENOSPC : 0);
}

int HCFS::open(const char* path, struct fuse_file_info* /* info */)
{
	const fs::path __path{path};

	if (__path.parent_path() != "/")
		return -ENOENT;

	if (find(__path.filename()))
		return 0;

	return -ENOENT;
}

int HCFS::read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* /* info */)
{
	const fs::path __path{path};

	if (__path.parent_path() != "/")
		return -ENOENT;

	unsigned int totalSize = 0;

	for (const auto& entry : fatEntries_) {
		if (entry.free())
			continue;

		if (entry != __path.filename())
			continue;

		totalSize += entry.size();
	}

	if (offset >= totalSize)
		return 0;

	unsigned int blockPos = offset / HCFS_BLOCK_SIZE;
	unsigned int blockOffset = offset % HCFS_BLOCK_SIZE;
	size_t remaining = size;

	for (const auto& entry : fatEntries_) {
		if (entry.free())
			continue;

		if (entry != __path.filename())
			continue;

		const auto blocks = entry.blocks();
		if (blockPos > blocks)
			blockPos -= blocks;
		else {
			while (remaining > 0 && totalSize > 0 && blockPos < blocks) {
				std::vector<unsigned char> __buf;

				readBlock(entry.allocationUnits_.at(blockPos++), __buf);

				unsigned int sz = std::min(remaining, __buf.size() - blockOffset);
				sz = std::min(sz, totalSize);

				std::memcpy(buf + size - remaining, __buf.data() + blockOffset, sz);

				remaining -= sz;
				totalSize -= sz;

				blockOffset = 0;
			}
			blockPos = 0;
		}
	}

	return static_cast<int>(size - remaining);
}

int HCFS::write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* info)
{
	const fs::path __path{path};

	if (__path.parent_path() != "/")
		return -ENOENT;

	unsigned int totalSize = 0;

	for (const auto& entry : fatEntries_) {
		if (entry.free())
			continue;

		if (entry != __path.filename())
			continue;

		totalSize += entry.size();
	}

	if (offset + size > totalSize) {
		auto ret = truncate(path, static_cast<off_t>(offset + size), info);
		if (ret < 0)
			return ret;
		totalSize = ((offset + size) / HCFS_BLOCK_SIZE + ((offset + size) % HCFS_BLOCK_SIZE ? 1 :0)) * HCFS_BLOCK_SIZE;
	}

	unsigned int blockPos = offset / HCFS_BLOCK_SIZE;
	unsigned int blockOffset = offset % HCFS_BLOCK_SIZE;
	size_t remaining = size;

	for (const auto& entry : fatEntries_) {
		if (entry.free())
			continue;

		if (entry != __path.filename())
			continue;

		const auto blocks = entry.blocks();
		if (blockPos > blocks)
			blockPos -= blocks;
		else {
			while (remaining > 0 && totalSize > 0 && blockPos < blocks) {
				std::vector<unsigned char> __buf;

				readBlock(entry.allocationUnits_.at(blockPos), __buf);

				unsigned int sz = std::min(remaining, __buf.size() - blockOffset);
				sz = std::min(sz, totalSize);

				std::memcpy(__buf.data() + blockOffset, buf + (size - remaining), sz);

				writeBlock(entry.allocationUnits_.at(blockPos++), __buf);

				remaining -= sz;
				totalSize -= sz;

				blockOffset = 0;
			}
			blockPos = 0;
		}
	}

	return static_cast<int>(size - remaining);
}

int HCFS::statfs(const char* path, struct statvfs* buf)
{
	const fs::path __path{path};

	if (__path != "/")
		return -ENOENT;

	unsigned int usedBlocks  = 0;
	unsigned int freeEntries = 0;

	for (const auto& entry : fatEntries_) {
		if (entry.free())
			freeEntries++;
		else
			usedBlocks += entry.blocks();
	}

	const unsigned int totalBlocks = disk_->properties().size() / HCFS_BLOCK_SIZE - 2;

	std::memset(buf, 0, sizeof(*buf));
	buf->f_bsize   = HCFS_BLOCK_SIZE;
	buf->f_frsize  = HCFS_BLOCK_SIZE;
	buf->f_blocks  = totalBlocks;
	buf->f_bfree   = totalBlocks - usedBlocks;
	buf->f_bavail  = buf->f_bfree;
	buf->f_files   = fatEntries_.size();
	buf->f_ffree   = freeEntries;
	buf->f_favail  = buf->f_ffree;
	buf->f_namemax = sizeof(FATEntry::name_);

	return 0;
}

int HCFS::release(const char* path, struct fuse_file_info* /* info */)
{
	const fs::path __path{path};

	if (__path.parent_path() != "/")
		return -ENOENT;

	if (find(__path.filename()))
		return 0;

	return -ENOENT;
}

int HCFS::readdir(const char* path, void* buf, fuse_fill_dir_t cb, off_t /* offset */, struct fuse_file_info* /* info */, enum fuse_readdir_flags /* flags */)
{
	const fs::path __path{path};

	if (__path != "/")
		return -ENOENT;

	int err = -ENOENT;

	for (const auto& entry : fatEntries_) {
		if (entry.free() || entry.extent())
			continue;

		struct stat st{};

		st.st_mode    = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
		st.st_nlink   = 1;
		st.st_size    = entry.size();
		st.st_blksize = disk_->properties().sectorSize();
		st.st_blocks  = st.st_size / 512 + (st.st_size % 512 ? 1 : 0);

		if (cb(buf, entry.name().c_str(), &st, 0, static_cast<fuse_fill_dir_flags>(0)))
			break;

		err = 0;
	}

	return err;
}

int HCFS::create(const char* path, mode_t /* mode */, struct fuse_file_info* /* info */)
{
	const fs::path __path{path};

	if (__path.parent_path() != "/")
		return -ENOENT;

	auto entry = find(path);
	if (entry)
		return -EEXIST;

	for (auto& entry : fatEntries_) {
		if (!entry.free())
			continue;

		entry.clear();
		entry.userCode_ = 0;
		entry.setName(__path.filename());

		return 0;
	}

	return -ENOSPC;
}

void HCFS::dumpFAT() const
{
	const unsigned int start = dpb_.off_ * disk_->properties().sectorsPerTrack() * disk_->properties().sectorSize() / HCFS_BLOCK_SIZE;

	std::vector<unsigned char> buf;

	readBlock(start, buf);
	if (buf.empty())
		std::cerr << "Warning: no data read for block #1\n";
	else
		hexdump(buf.data(), buf.size());

	readBlock(start + 1, buf);
	if (buf.empty())
		std::cerr << "Warning: no data read for block #2\n";
	else
		hexdump(buf.data(), buf.size());
}
