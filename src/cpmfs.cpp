// SPDX-License-Identifier: GPL-2.0
#include <cstring>
#include <iostream>

#include "cpmfs.h"
#include "diskpos.h"
#include "log.h"

unsigned int CPMFS::ipos(unsigned int pos) const
{
	const DiskPos apos(disk_->properties(), pos);
	const DiskPos bpos(disk_->properties(), apos.track(), apos.head(), interleave_.at(apos.sector()));

	return bpos.pos();
}

void CPMFS::readBlock(unsigned int block, std::vector<unsigned char>& buf) const
{
	buf.clear();
	buf.reserve(CPMFS_BLOCK_SIZE);

	const auto start = (firstBlock_ + block) * CPMFS_BLOCK_SIZE / disk_->properties().sectorSize();
	for (unsigned int i = start; i < (start + CPMFS_BLOCK_SIZE / disk_->properties().sectorSize()); i++) {
		auto& sector = disk_->read(ipos(i));

		if (sector.data().empty())
			buf.insert(buf.end(), disk_->properties().sectorSize(), 0);
		else
			std::copy(sector.data().begin(), sector.data().end(), std::back_inserter(buf));
	}
}

void CPMFS::writeBlock(unsigned int block, const std::vector<unsigned char>& buf) const
{
	unsigned int nsect = 0;
	std::vector<unsigned char> __buf;

	__buf.reserve(disk_->properties().sectorSize());

	const auto start = (firstBlock_ + block) * CPMFS_BLOCK_SIZE / disk_->properties().sectorSize();
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

void CPMFS::loadFAT()
{
	fatEntries_.clear();
	fatEntries_.reserve(2 * CPMFS_BLOCK_SIZE / sizeof(fatEntries_.front()));

	std::vector<unsigned char> buf;

	readBlock(0, buf);

	for (unsigned int i = 0; i < (buf.size() / sizeof(fatEntries_.front())); i++)
		fatEntries_.push_back(reinterpret_cast<decltype(&fatEntries_.front())>(buf.data())[i]);

	readBlock(1, buf);

	for (unsigned int i = 0; i < (buf.size() / sizeof(fatEntries_.front())); i++)
		fatEntries_.push_back(reinterpret_cast<decltype(&fatEntries_.front())>(buf.data())[i]);
}

void CPMFS::saveFAT() const
{
	if (!disk_->modified())
		return;

	// initialize all free blocks
	std::vector<bool> freeBlocks(disk_->properties().size() / CPMFS_BLOCK_SIZE - firstBlock_, true);

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
			static const std::vector<unsigned char> buf(CPMFS_BLOCK_SIZE, CPMFS_FREE_BYTE);
			writeBlock(block, buf);
		}
		block++;
	}

	// write back all FAT entries
	std::vector<unsigned char> buf;

	buf.reserve(fatEntries_.size() * sizeof(fatEntries_.front()));

	for (const auto& entry : fatEntries_)
		buf.insert(buf.end(), reinterpret_cast<const unsigned char*>(&entry), reinterpret_cast<const unsigned char*>(&entry) + sizeof(entry));

	for (unsigned int i = 0; i < (buf.size() / CPMFS_BLOCK_SIZE); i++)
		writeBlock(i, {buf.data() + i * CPMFS_BLOCK_SIZE, buf.data() + i * CPMFS_BLOCK_SIZE + CPMFS_BLOCK_SIZE});

	auto r = buf.size() % CPMFS_BLOCK_SIZE;
	if (r)
		writeBlock(buf.size() / CPMFS_BLOCK_SIZE + 1, {buf.data() + buf.size() - r, buf.data() + buf.size()});
}

std::optional<std::reference_wrapper<CPMFS::FATEntry>> CPMFS::find(const std::string& path)
{
	const auto it = std::find_if(fatEntries_.begin(), fatEntries_.end(), [&path](const auto& entry) {
		return !entry.free() && !entry.extent() && entry == path;
	});

	if (it != fatEntries_.end())
		return *it;

	return {};
}

CPMFS::CPMFS(Disk* disk)
    : disk_{disk}
    , firstBlock_{dpb_.off_ * disk->properties().sectorsPerTrack() * disk->properties().sectorSize() / CPMFS_BLOCK_SIZE}
{
	if (interleave_.size() != disk_->properties().sectors())
		throw std::runtime_error(
		    std::format("no sector interleave available for the current number of sectors ({})", disk_->properties().sectors()));

	loadFAT();
}

CPMFS::~CPMFS()
{
	saveFAT();
}

int CPMFS::getattr(const char* path, struct stat* buf, struct fuse_file_info* /* info */)
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
		buf->st_blocks  = CPMFS_BLOCK_SIZE * 2 / 512;

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

int CPMFS::unlink(const char* path)
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

int CPMFS::truncate(const char* path, off_t length, struct fuse_file_info* /* info */)
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

		size += entry.size();
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
		unsigned int n = length / CPMFS_BLOCK_SIZE + (length % CPMFS_BLOCK_SIZE ? 1 : 0);
		n              = blocks - n;

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

			entry.recordCount_ = aunits * CPMFS_BLOCK_SIZE / CPMFS_RECORD_SIZE;
			if (!entry.recordCount_ && n)
				entry.clear();
		}

		return (n ? -ENOENT : 0);
	}

	std::vector<bool> blockMap(disk_->properties().size() / CPMFS_BLOCK_SIZE - firstBlock_, true);

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

	unsigned int n = length / CPMFS_BLOCK_SIZE + (length % CPMFS_BLOCK_SIZE ? 1 : 0);
	n -= blocks;

	bool full             = false;
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
			entry.exLo_ = extents % 32;
			entry.exHi_ = extents / 32;
			extents++;
		}

		unsigned int aunits = 0;
		for (; aunits < entry.allocationUnits_.size() && n > 0; aunits++) {
			if (entry.allocationUnits_.at(aunits))
				continue;

			entry.allocationUnits_.at(aunits) = getFreeBlock();
			if (!entry.allocationUnits_.at(aunits))
				break;

			// wipe the block's contents
			const std::vector<unsigned char> buf(CPMFS_BLOCK_SIZE, CPMFS_FREE_BYTE);
			writeBlock(entry.allocationUnits_.at(aunits), buf);

			n--;
		}

		entry.recordCount_ = aunits * CPMFS_BLOCK_SIZE / CPMFS_RECORD_SIZE;

		full = entry.full();
	}

	return (n ? -ENOSPC : 0);
}

int CPMFS::open(const char* path, struct fuse_file_info* /* info */)
{
	const fs::path __path{path};

	if (__path.parent_path() != "/")
		return -ENOENT;

	if (find(__path.filename()))
		return 0;

	return -ENOENT;
}

int CPMFS::read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* /* info */)
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

	unsigned int blockPos    = offset / CPMFS_BLOCK_SIZE;
	unsigned int blockOffset = offset % CPMFS_BLOCK_SIZE;
	size_t remaining         = size;

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
				sz              = std::min(sz, totalSize);

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

int CPMFS::write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* info)
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
		totalSize = ((offset + size) / CPMFS_BLOCK_SIZE + ((offset + size) % CPMFS_BLOCK_SIZE ? 1 : 0)) * CPMFS_BLOCK_SIZE;
	}

	unsigned int blockPos    = offset / CPMFS_BLOCK_SIZE;
	unsigned int blockOffset = offset % CPMFS_BLOCK_SIZE;
	size_t remaining         = size;

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
				sz              = std::min(sz, totalSize);

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

int CPMFS::statfs(const char* path, struct statvfs* buf)
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

	const unsigned int totalBlocks = disk_->properties().size() / CPMFS_BLOCK_SIZE - firstBlock_ - 2;

	std::memset(buf, 0, sizeof(*buf));
	buf->f_bsize   = CPMFS_BLOCK_SIZE;
	buf->f_frsize  = CPMFS_BLOCK_SIZE;
	buf->f_blocks  = totalBlocks;
	buf->f_bfree   = totalBlocks - usedBlocks;
	buf->f_bavail  = buf->f_bfree;
	buf->f_files   = fatEntries_.size();
	buf->f_ffree   = freeEntries;
	buf->f_favail  = buf->f_ffree;
	buf->f_namemax = sizeof(FATEntry::name_);

	return 0;
}

int CPMFS::release(const char* path, struct fuse_file_info* /* info */)
{
	const fs::path __path{path};

	if (__path.parent_path() != "/")
		return -ENOENT;

	if (find(__path.filename()))
		return 0;

	return -ENOENT;
}

int CPMFS::readdir(const char* path, void* buf, fuse_fill_dir_t cb, off_t /* offset */, struct fuse_file_info* /* info */,
                   enum fuse_readdir_flags /* flags */)
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

int CPMFS::create(const char* path, mode_t /* mode */, struct fuse_file_info* /* info */)
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

void CPMFS::dumpFAT() const
{
	std::vector<unsigned char> buf;

	readBlock(0, buf);
	if (buf.empty())
		std::cerr << "Warning: no data read for block #1\n";
	else
		hexdump(buf.data(), buf.size());

	readBlock(1, buf);
	if (buf.empty())
		std::cerr << "Warning: no data read for block #2\n";
	else
		hexdump(buf.data(), buf.size());
}

void CPMFS::printFAT() const
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
