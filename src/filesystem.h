// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <shared_mutex>
#include <span>
#include <fuse3/fuse.h>

class Filesystem {
	struct fuse_operations ops_{};
	inline static std::shared_mutex mutex_;

	static int __getattr(const char* path, struct stat* buf, struct fuse_file_info* info) noexcept;

	static int __unlink(const char* path) noexcept;

	static int __truncate(const char* path, off_t length, struct fuse_file_info* info) noexcept;

	static int __open(const char* path, struct fuse_file_info* info) noexcept;

	static int __read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* info) noexcept;

	static int __write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* info) noexcept;

	static int __statfs(const char* path, struct statvfs* buf) noexcept;

	static int __release(const char* path, struct fuse_file_info* info) noexcept;

	static int __readdir(const char* path, void* buf, fuse_fill_dir_t cb, off_t offset, struct fuse_file_info* info,
	                     enum fuse_readdir_flags flags) noexcept;

	static int __create(const char* path, mode_t mode, struct fuse_file_info* info) noexcept;

public:
	Filesystem();

	virtual ~Filesystem() = default;

	int main(std::span<char*> args);

	virtual int getattr(const char* path, struct stat* buf, struct fuse_file_info* info) = 0;

	virtual int unlink(const char* path) = 0;

	virtual int truncate(const char* path, off_t length, struct fuse_file_info* info) = 0;

	virtual int open(const char* path, struct fuse_file_info* info) = 0;

	virtual int read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* info) = 0;

	virtual int write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* info) = 0;

	virtual int statfs(const char* path, struct statvfs* buf) = 0;

	virtual int release(const char* path, struct fuse_file_info* info) = 0;

	virtual int readdir(const char* path, void* buf, fuse_fill_dir_t cb, off_t offset, struct fuse_file_info* info, enum fuse_readdir_flags flags)
	    = 0;

	virtual int create(const char* path, mode_t mode, struct fuse_file_info* info) = 0;
};
