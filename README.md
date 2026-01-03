# fuse driver for ZX Spectrum disk images

![Build](https://github.com/mdontu/fuse-spectrum/workflows/CI/badge.svg)

## Usage

```shell
sudo fuse-spectrum --file=<disk-image> -o allow_other,uid=$(id -u),gid=$(id -g) <mount-point>
```

**WARNING**: If changes are made, the command above will overwrite the indicated disk image with a new one at unmount time! Mount the image read-only or make sure you have backups!

## Build and install

### Requirements

* cmake >= 3.20
* fuse >= 3.0
* gcc >= 13.0
* make

### Release

```shell
cmake -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build -v
sudo cmake --install build -v
```

### Debug

```shell
cmake -DCMAKE_BUILD_TYPE=Debug -B build
cmake --build build -v
sudo cmake --install build -v
```

## Supported disk images

* DSK / EDSK (3.5", 5.25")
* ImageDisk (3.5")

## Supported filesystems

* HC (CP/M 2.2 variation w/o boot tracks)

## Tested on the following ZX Spectrum compatible hardware

* ICE Felix HC2000 w/ 3.5" microdrive (1995) - BASIC
