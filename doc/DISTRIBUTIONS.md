# Compiling `pgagroal` from sources

[**pgagroal**](https://github.com/pgagroal/pgagroal) requires the following dependencies:

* a C compiler like [gcc 8+](https://gcc.gnu.org) (C17) or [clang 8+](https://clang.llvm.org/)
* [cmake](https://cmake.org)
* [GNU make](https://www.gnu.org/software/make/) or BSD `make`
* [libev](http://software.schmorp.de/pkg/libev.html)
* [OpenSSL](http://www.openssl.org/)
* [rst2man](https://docutils.sourceforge.io/)
* [libatomic](https://gcc.gnu.org/wiki/Atomic)
* [Doxygen](https://doxygen.nl/index.html)
* [pdflatex](https://tug.org/texlive/)
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
            libev libev-devel     \
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
            libev libevent 	\
            py311-docutils 	\
            lzlib           \
            liblz4          \
            lbizp2          \
            texlive-formats \
            binutils
```
