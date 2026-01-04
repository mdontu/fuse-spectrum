// SPDX-License-Identifier: GPL-2.0
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>

#include "cpmfs.h"
#include "disk.h"
#include "hcfs.h"
#include "version.h"

static void version()
{
	std::cout << std::format("Fuse-Spectrum version {}.{}.{}\n", FUSE_SPECTRUM_VERSION_MAJOR, FUSE_SPECTRUM_VERSION_MINOR,
	                         FUSE_SPECTRUM_VERSION_PATCH);
}

static void help(const char* progname)
{
	version();
	std::cout << "Usage: " << progname << " [options] <mountpoint>\n";
	std::cout << "    --file=<disk-image>    The path to the disk image to load\n";
	std::cout << "    --filesystem=<fs>      The filesystem type (default: hc)\n\n";
}

int main(int argc, char* argv[])
{
	struct {
		char* file_{};
		char* filesystem_{};
		int help_{};
		int version_{};
	} options;

	// clang-format off
	static const auto optionSpec = std::to_array<struct fuse_opt>({
		{"--file=%s"      , offsetof(decltype(options), file_)      , 0},
		{"--filesystem=%s", offsetof(decltype(options), filesystem_), 0},
		{"-h"             , offsetof(decltype(options), help_)      , 1},
		{"--help"         , offsetof(decltype(options), help_)      , 1},
		{"-V"             , offsetof(decltype(options), version_)   , 1},
		{"--version"      , offsetof(decltype(options), version_)   , 1},
		FUSE_OPT_END
	});
	// clang-format on

	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	if (fuse_opt_parse(&args, &options, optionSpec.data(), nullptr) < 0)
		return EXIT_FAILURE;

	if (options.help_) {
		help(argv[0]);
		fuse_opt_add_arg(&args, "--help");
		args.argv[0][0] = '\0';

		static const std::array<struct fuse_operations, 1> dummyOps{};
		fuse_main(args.argc, args.argv, dummyOps.data(), nullptr);

		return EXIT_SUCCESS;
	}

	if (options.version_) {
		version();
		return EXIT_SUCCESS;
	}

	if (!options.file_) {
		std::cerr << "Error: please use `--file' to indicate a disk image to load\n";
		return EXIT_FAILURE;
	}

	int ret   = EXIT_SUCCESS;
	auto disk = Disk::create(options.file_);

	if (!disk) {
		std::cerr << "Error: failed to load the disk image \"" << options.file_ << "\"\n";
		return EXIT_FAILURE;
	}

	if (!options.filesystem_) {
		static auto defaultFs = std::to_array("hc");
		options.filesystem_   = defaultFs.data();
	}

	std::unique_ptr<Filesystem> fs;

	if (std::string_view(options.filesystem_) == "cpm")
		fs = std::make_unique<CPMFS>(disk.get());
	else if (std::string_view(options.filesystem_) == "hc")
		fs = std::make_unique<HCFS>(disk.get());
	else {
		std::cerr << "Error: unsupported filesystem \"" << options.filesystem_ << "\"\n";
		return EXIT_FAILURE;
	}

	ret = fs->main(std::span(args.argv, args.argc));
	fs.reset();

	if (disk->modified())
		disk->save(options.file_);

	fuse_opt_free_args(&args);

	return ret;
}
