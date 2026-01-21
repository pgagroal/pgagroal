\newpage

## Distribution-Specific Installation

This chapter provides installation instructions for different operating systems and distributions.

### Dependencies

[**pgagroal**][pgagroal] requires the following dependencies:

* a C compiler like [gcc 8+][gcc] (C17) or [clang 8+][clang]
* [cmake][cmake]
* [GNU make][make] or BSD `make`
* [libev][libev]
* [OpenSSL][openssl]
* [rst2man][rst2man]
* [libatomic](https://gcc.gnu.org/wiki/Atomic)
* [Doxygen](https://doxygen.nl/index.html)
* [pdflatex](https://tug.org/texlive/)
* [zlib](https://zlib.net)
* [zstd](http://www.zstd.net)
* [lz4](https://lz4.github.io/lz4/)
* [bzip2](http://sourceware.org/bzip2/)
* [binutils](https://www.gnu.org/software/binutils/)
* on Linux platforms, there is also the need for
  * [systemd][systemd]

### Rocky Linux / RHEL

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

### FreeBSD

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

### Fedora

For Fedora systems, use:

```sh
dnf install git gcc cmake make liburing liburing-devel openssl openssl-devel systemd systemd-devel python3-docutils libatomic zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel bzip2 bzip2-devel libasan libasan-static binutils clang clang-analyzer clang-tools-extra
```

### Ubuntu / Debian

For Ubuntu and Debian systems:

```sh
apt-get update
apt-get install build-essential cmake libev-dev libssl-dev libsystemd-dev python3-docutils libatomic1 zlib1g-dev libzstd-dev liblz4-dev libbz2-dev binutils
```

### macOS

For macOS using Homebrew:

```sh
brew install cmake libev openssl@3 docutils zlib zstd lz4 bzip2
```

Note: On macOS, you may need to set additional environment variables for OpenSSL:

```sh
export OPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
export PKG_CONFIG_PATH="$OPENSSL_ROOT_DIR/lib/pkgconfig:$PKG_CONFIG_PATH"
```

### Building from Source

After installing dependencies, build pgagroal:

```sh
git clone https://github.com/pgagroal/pgagroal.git
cd pgagroal
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
sudo make install
```

### Platform-Specific Notes

#### Linux
- Ensure `liburing` is available for optimal I/O performance
- Configure systemd service files for production deployments
- Consider using huge pages for better memory performance

#### FreeBSD
- Use the ports system for more control over build options
- Configure appropriate kernel parameters for network performance
- Consider using jails for isolation

#### macOS
- Some features may have limited support compared to Linux
- Use Homebrew for dependency management
- Consider using Docker for consistent environments

### Troubleshooting

#### Common Issues

**Missing liburing on older systems:**
```sh
# Install liburing from source if not available in package manager
git clone https://github.com/axboe/liburing.git
cd liburing
make
sudo make install
```

**OpenSSL version conflicts:**
```sh
# Specify OpenSSL path explicitly
cmake -DOPENSSL_ROOT_DIR=/usr/local/ssl ..
```

**Permission issues:**
```sh
# Ensure proper permissions for installation
sudo chown -R $(whoami) /usr/local/
```

#### Build Flags

For debug builds:
```sh
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

For release builds with optimizations:
```sh
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-O3 -march=native" ..
```