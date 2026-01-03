// SPDX-License-Identifier: GPL-2.0
#include <cerrno>
#include <iostream>
#include <mutex>
#include <stdexcept>

#include "filesystem.h"

Filesystem::Filesystem()
{
	ops_.getattr  = __getattr;
	ops_.unlink   = __unlink;
	ops_.truncate = __truncate;
	ops_.open     = __open;
	ops_.read     = __read;
	ops_.write    = __write;
	ops_.statfs   = __statfs;
	ops_.release  = __release;
	ops_.readdir  = __readdir;
	ops_.create   = __create;
}

int Filesystem::__getattr(const char* path, struct stat* buf, struct fuse_file_info* info) noexcept
{
	int ret = -EIO;

	try {
		auto __this = static_cast<Filesystem*>(fuse_get_context()->private_data);

		std::shared_lock<std::shared_mutex> lock(mutex_);
		ret = __this->getattr(path, buf, info);
	} catch (const std::exception& e) {
		std::cerr << "exception: " << e.what() << "\n";
	}

	return ret;
}

int Filesystem::__unlink(const char* path) noexcept
{
	int ret = -EIO;

	try {
		auto __this = static_cast<Filesystem*>(fuse_get_context()->private_data);

		std::unique_lock<std::shared_mutex> lock(mutex_);
		ret = __this->unlink(path);
	} catch (const std::exception& e) {
		std::cerr << "exception: " << e.what() << "\n";
	}

	return ret;
}

int Filesystem::__truncate(const char* path, off_t length, struct fuse_file_info* info) noexcept
{
	int ret = -EIO;

	try {
		auto __this = static_cast<Filesystem*>(fuse_get_context()->private_data);

		std::unique_lock<std::shared_mutex> lock(mutex_);
		ret = __this->truncate(path, length, info);
	} catch (const std::exception& e) {
		std::cerr << "exception: " << e.what() << "\n";
	}

	return ret;
}

int Filesystem::__open(const char* path, struct fuse_file_info* info) noexcept
{
	int ret = -EIO;

	try {
		auto __this = static_cast<Filesystem*>(fuse_get_context()->private_data);

		std::shared_lock<std::shared_mutex> lock(mutex_);
		ret = __this->open(path, info);
	} catch (const std::exception& e) {
		std::cerr << "exception: " << e.what() << "\n";
	}

	return ret;
}

int Filesystem::__read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* info) noexcept
{
	int ret = -EIO;

	try {
		auto __this = static_cast<Filesystem*>(fuse_get_context()->private_data);

		std::shared_lock<std::shared_mutex> lock(mutex_);
		ret = __this->read(path, buf, size, offset, info);
	} catch (const std::exception& e) {
		std::cerr << "exception: " << e.what() << "\n";
	}

	return ret;
}

int Filesystem::__write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* info) noexcept
{
	int ret = -EIO;

	try {
		auto __this = static_cast<Filesystem*>(fuse_get_context()->private_data);

		std::unique_lock<std::shared_mutex> lock(mutex_);
		ret = __this->write(path, buf, size, offset, info);
	} catch (const std::exception& e) {
		std::cerr << "exception: " << e.what() << "\n";
	}

	return ret;
}

int Filesystem::__statfs(const char* path, struct statvfs* buf) noexcept
{
	int ret = -EIO;

	try {
		auto __this = static_cast<Filesystem*>(fuse_get_context()->private_data);

		std::shared_lock<std::shared_mutex> lock(mutex_);
		ret = __this->statfs(path, buf);
	} catch (const std::exception& e) {
		std::cerr << "exception: " << e.what() << "\n";
	}

	return ret;
}

int Filesystem::__release(const char* path, struct fuse_file_info* info) noexcept
{
	int ret = -EIO;

	try {
		auto __this = static_cast<Filesystem*>(fuse_get_context()->private_data);

		std::shared_lock<std::shared_mutex> lock(mutex_);
		ret = __this->release(path, info);
	} catch (const std::exception& e) {
		std::cerr << "exception: " << e.what() << "\n";
	}

	return ret;
}

int Filesystem::__readdir(const char* path, void* buf, fuse_fill_dir_t cb, off_t offset, struct fuse_file_info* info,
                          enum fuse_readdir_flags flags) noexcept
{
	int ret = -EIO;

	try {
		auto __this = static_cast<Filesystem*>(fuse_get_context()->private_data);

		std::shared_lock<std::shared_mutex> lock(mutex_);
		ret = __this->readdir(path, buf, cb, offset, info, flags);
	} catch (const std::exception& e) {
		std::cerr << "exception: " << e.what() << "\n";
	}

	return ret;
}

int Filesystem::__create(const char* path, mode_t mode, struct fuse_file_info* info) noexcept
{
	int ret = -EIO;

	try {
		auto __this = static_cast<Filesystem*>(fuse_get_context()->private_data);

		std::unique_lock<std::shared_mutex> lock(mutex_);
		ret = __this->create(path, mode, info);
	} catch (const std::exception& e) {
		std::cerr << "exception: " << e.what() << "\n";
	}

	return ret;
}

int Filesystem::main(std::span<char*> args)
{
	return fuse_main(args.size(), args.data(), &ops_, this);
}
