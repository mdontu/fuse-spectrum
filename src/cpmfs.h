// SPDX-License-Identifier: GPL-2.0
#pragma once

#include "disk.h"
#include "filesystem.h"

class CPMFS final : public Filesystem {
	static constexpr auto CPMFS_RECORD_SIZE          = 128u;
	static constexpr auto CPMFS_BLOCK_SIZE           = 2048u;
	static constexpr unsigned char CPMFS_FREE_BYTE   = 0xe5;
	static constexpr auto CPMFS_FILENAME_MAXSIZE     = 11u;
	static constexpr auto CPMFS_MAX_ALLOCATION_UNITS = 8u;

#pragma pack(push, 1)
	struct FATEntry {
		unsigned char userCode_{};
		std::array<char, CPMFS_FILENAME_MAXSIZE> name_{};
		unsigned char exLo_{};
		unsigned char reserved_{};
		unsigned char exHi_{};
		unsigned char recordCount_{};
		std::array<unsigned short, CPMFS_MAX_ALLOCATION_UNITS> allocationUnits_{};

		void clear()
		{
			userCode_ = CPMFS_FREE_BYTE;

			name_.fill(' ');

			exLo_        = 0;
			reserved_    = 0;
			exHi_        = 0;
			recordCount_ = 0;

			allocationUnits_.fill(0);
		}

		bool free() const
		{
			return (userCode_ == CPMFS_FREE_BYTE);
		}

		bool extent() const
		{
			return !!exLo_;
		}

		bool full() const
		{
			return (recordCount_ >= (std::size(allocationUnits_) * CPMFS_BLOCK_SIZE / 128));
		}

		std::string name() const
		{
			std::string ret;

			for (const auto& c : name_)
				ret += c & 0x7f;

			while (!ret.empty() && ret.back() == ' ')
				ret.pop_back();

			for (auto it = ret.begin(); it != ret.end(); ++it) {
				if (*it == '/')
					*it = '?';
			}

			return ret;
		}

		void setName(const std::string& name)
		{
			name_.fill(' ');
			std::copy_n(name.begin(), std::min(name.size(), name_.size()), name_.begin());
		}

		bool operator==(const std::string& other) const
		{
			return name() == other;
		}

		unsigned int size() const
		{
			return recordCount_ * 128;
		}

		unsigned int blocks() const
		{
			return (std::size(allocationUnits_) - std::count(allocationUnits_.begin(), allocationUnits_.end(), 0));
		}
	};
#pragma pack(pop)

	static constexpr auto interleave_ = std::to_array<unsigned char>({0, 2, 4, 6, 8, 1, 3, 5, 7});

	// CP/M 2.2 3.5" format
	inline static const DiskParameterBlock dpb_
	    = {.spt_ = 32, .bsh_ = 4, .blm_ = 15, .exm_ = 0, .dsm_ = 341, .drm_ = 127, .al0_ = 0xc0, .al1_ = 0, .cks_ = 0, .off_ = 2};

	std::vector<FATEntry> fatEntries_;

	Disk* disk_{};

	const unsigned int firstBlock_{};

	unsigned int ipos(unsigned int pos) const;

	void readBlock(unsigned int block, std::vector<unsigned char>& buf) const;

	void writeBlock(unsigned int block, const std::vector<unsigned char>& buf) const;

	void loadFAT();

	void saveFAT() const;

	std::optional<std::reference_wrapper<FATEntry>> find(const std::string& path);

public:
	CPMFS(Disk* disk);

	~CPMFS() override;

	int getattr(const char* path, struct stat* buf, struct fuse_file_info* info) override;

	int unlink(const char* path) override;

	int truncate(const char* path, off_t length, struct fuse_file_info* info) override;

	int open(const char* path, struct fuse_file_info* info) override;

	int read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* info) override;

	int write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* info) override;

	int statfs(const char* path, struct statvfs* buf) override;

	int release(const char* path, struct fuse_file_info* info) override;

	int readdir(const char* path, void* buf, fuse_fill_dir_t cb, off_t offset, struct fuse_file_info* info,
	            enum fuse_readdir_flags flags) override;

	int create(const char* path, mode_t mode, struct fuse_file_info* info) override;

	void dumpFAT() const override;

	void printFAT() const override;
};
