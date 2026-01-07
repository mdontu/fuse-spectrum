// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <optional>
#include <string>

#include "disk.h"
#include "filesystem.h"

class HCFS final : public Filesystem {
	static constexpr auto HCFS_RECORD_SIZE          = 128u;
	static constexpr auto HCFS_BLOCK_SIZE           = 2048u;
	static constexpr unsigned char HCFS_FREE_BYTE   = 0xe5;
	static constexpr auto HCFS_FILENAME_MAXSIZE     = 11u;
	static constexpr auto HCFS_MAX_ALLOCATION_UNITS = 8u;

#pragma pack(push, 1)
	struct FATEntry {
		unsigned char userCode_{};
		std::array<char, HCFS_FILENAME_MAXSIZE> name_{};
		unsigned char exLo_{};
		unsigned char reserved_{};
		unsigned char exHi_{};
		unsigned char recordCount_{};
		std::array<unsigned short, HCFS_MAX_ALLOCATION_UNITS> allocationUnits_{};

		void clear()
		{
			userCode_ = HCFS_FREE_BYTE;

			name_.fill(' ');

			exLo_        = 0;
			reserved_    = 0;
			exHi_        = 0;
			recordCount_ = 0;

			allocationUnits_.fill(0);
		}

		bool free() const
		{
			return (userCode_ == HCFS_FREE_BYTE);
		}

		bool extent() const
		{
			return exLo_ || exHi_;
		}

		bool full() const
		{
			return (recordCount_ >= (allocationUnits_.size() * HCFS_BLOCK_SIZE / HCFS_RECORD_SIZE));
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
			return recordCount_ * HCFS_RECORD_SIZE;
		}

		unsigned int blocks() const
		{
			return std::count_if(allocationUnits_.begin(), allocationUnits_.end(), [](const auto& v) {
				return v != 0;
			});
		}
	};
#pragma pack(pop)

	static constexpr auto interleave640_ = std::to_array<unsigned char>({0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15});
	static constexpr auto interleave320_ = std::to_array<unsigned char>({0, 2, 4, 6, 8, 1, 3, 5, 7});

	// BASIC 3.5" format
	// clang-format off
	inline static const DiskParameterBlock dpb_ = {
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
	// clang-format on

	std::vector<FATEntry> fatEntries_;

	Disk* disk_{};

	unsigned int ipos(unsigned int pos) const;

	void readBlock(unsigned int block, std::vector<unsigned char>& buf) const;

	void writeBlock(unsigned int block, const std::vector<unsigned char>& buf) const;

	void loadFAT();

	void saveFAT() const;

	std::optional<std::reference_wrapper<FATEntry>> find(const std::string& path);

public:
	HCFS(Disk* disk);

	~HCFS() override;

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
