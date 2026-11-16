# Compiling `pgagroal` from sources

[**pgagroal**](https://github.com/pgagroal/pgagroal) requires the following dependencies:

* a C compiler like [gcc 8+](https://gcc.gnu.org) (C17) or [clang 8+](https://clang.llvm.org/)
* [cmake](https://cmake.org)
* [GNU make](https://www.gnu.org/software/make/) or BSD `make`
* [liburing](https://github.com/axboe/liburing) (on Linux)
* [OpenSSL 3.0+](http://www.openssl.org/)
* [rst2man](https://docutils.sourceforge.io/)
* [libatomic](https://gcc.gnu.org/wiki/Atomic)
* [Doxygen](https://doxygen.nl/index.html)
* [xelatex](https://tug.org/texlive/)
* [zlib](https://zlib.net)
* [zstd](http://www.zstd.net)
* [lz4](https://lz4.github.io/lz4/)
* [bzip2](http://sourceware.org/bzip2/)
* [binutils](https://www.gnu.org/software/binutils/)
* on Linux platforms, there is also the need for
  * [systemd](https://www.freedesktop.org/wiki/Software/systemd/)



## Compiling on Rocky Linux

All the dependencies can be installed via `dnf(8)` as follows:

```sh
dnf install git gcc cmake make    \
            liburing liburing-devel \
            openssl openssl-devel \
            systemd systemd-devel \
            python3-docutils      \
            libatomic             \
            zlib zlib-devel       \
            libzstd libzstd-devel \
            lz4 lz4-devel         \
            bzip2 bzip2-devel     \
            binutils              \
            clang clang-analyzer clang-tools-extra
```

Please note that, on Rocky Linux, in order to install the `python3-docutils`
package (that provides `rst2man` executable), you need to enable the `crb` repository:

```sh
dnf config-manager --set-enabled crb
```

## Compiling on FreeBSD

All the dependencies can be installed via `pkg(8)` as follows:

```sh
pkg install cmake          	\
            py311-docutils 	\
            lzlib           \
            liblz4          \
            lbizp2          \
            texlive-formats \
            binutils
```

## Compiling on NetBSD

First, ensure `pkgin` is installed. If you are using a fresh NetBSD installation, you can bootstrap `pkgin` and set the package path as follows:

```sh
export PKG_PATH=https://cdn.netbsd.org/pub/pkgsrc/packages/NetBSD/$(uname -p)/$(uname -r | cut -d_ -f1)/All
pkg_add pkgin
pkgin update
```

All the dependencies can then be installed via pkgin(8) as follows:

```sh
pkgin install gcc        \ 
              clang      \
              cmake      \      
              pkg-config \
              openssl    \
              zlib       \
              zstd       \
              lz4        \
              bzip2      \
              python313  \
              py313-pip  \
              doxygen
```

`rst2man` is provided by `docutils`. If `py313-docutils` is not available for
your pkgsrc branch, install it with `pip(1)` instead:

```sh
python3.13 -m pip install docutils
```

## Compiling on Ubuntu / Debian

All the dependencies can be installed via `apt(8)` as follows:

```sh
apt install git gcc cmake make \
            libssl-dev liburing-dev \
            systemd libsystemd-dev \
            python3-docutils \
            libatomic1 \
            zlib1g-dev \
            libzstd-dev \
            liblz4-dev \
            libbz2-dev \
            binutils \
            clang clang-tools
```
