# pgagroal

[**pgagroal**](https://github.com/pgagroal/pgagroal) is a high-performance protocol-native connection pool for [PostgreSQL](https://www.postgresql.org).

Pronounced: p-g-a-gro-al, named after [A](https://www.visitportugal.com/en/content/praia-fluvial-do-pgagroal)[gro](https://www.google.com/maps/place/pgagroal,+Portugal/@39.6775431,-8.4486056,14z/)[al](https://www.infatima.pt/en/nearby/sun-sea/fluvial-beaches/pgagroal/) in Portugal.

## Features

* High performance
* Connection pool
* Limit connections for users and databases
* Prefill support
* Remove idle connections
* Perform connection validation
* Enable / disable database access
* Graceful / fast shutdown
* Prometheus support
* Grafana 8 dashboard
* Remote management
* Authentication query support
* Failover support
* Transport Layer Security (TLS) v1.2+ support
* Daemon mode
* User vault

See [Getting Started](./doc/GETTING_STARTED.md) on how to get started with [**pgagroal**](https://github.com/pgagroal/pgagroal).

See [Configuration](./doc/CONFIGURATION.md) on how to configure [**pgagroal**](https://github.com/pgagroal/pgagroal).

See [Performance](./doc/PERFORMANCE.md) for a performance run.

## Overview

[**pgagroal**](https://github.com/pgagroal/pgagroal) makes use of

* Process model
* Shared memory model across processes
* [liburing](https://github.com/axboe/liburing) for fast network interactions
* [Atomic operations](https://en.cppreference.com/w/c/atomic) are used to keep track of state
* The [PostgreSQL](https://www.postgresql.org) native protocol
  [v3](https://www.postgresql.org/docs/11/protocol-message-formats.html) for its communication

[**pgagroal**](https://github.com/pgagroal/pgagroal) will work with any [PostgreSQL](https://www.postgresql.org) compliant driver, for example
[pgjdbc](https://jdbc.postgresql.org/), [Npgsql](https://www.npgsql.org/) and [pq](https://github.com/lib/pq).

See [Architecture](./doc/ARCHITECTURE.md) for the architecture of [**pgagroal**](https://github.com/pgagroal/pgagroal).

## Tested platforms

* [Fedora](https://getfedora.org/) 38+
* [RHEL 9.x](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/9)
* [Rocky Linux 9.x](https://rockylinux.org/)

* [FreeBSD](https://www.freebsd.org/)
* [OpenBSD](http://www.openbsd.org/)


## Compiling from sources

[**pgagroal**](https://github.com/pgagroal/pgagroal) can be compiled from sources,
after having installed all the required dependencies:

* [gcc 8+](https://gcc.gnu.org) (C17) or [clang 8+](https://clang.llvm.org/)
* [cmake](https://cmake.org)
* [GNU make](https://www.gnu.org/software/make/) or BSD `make`
* [liburing](https://github.com/axboe/liburing)
* [OpenSSL](http://www.openssl.org/)
* [rst2man](https://docutils.sourceforge.io/)
* [libatomic](https://gcc.gnu.org/wiki/Atomic)
* [Doxygen](https://doxygen.nl/index.html)
* [pdflatex](https://tug.org/texlive/)
* [systemd](https://www.freedesktop.org/wiki/Software/systemd/) (on Linux systems)
* [zlib](https://zlib.net)
* [zstd](http://www.zstd.net)
* [lz4](https://lz4.github.io/lz4/)
* [bzip2](http://sourceware.org/bzip2/)
* [binutils](https://www.gnu.org/software/binutils/)


See the [documentation about installing the required dependencies](doc/DISTRIBUTIONS.md).
```sh
dnf install git gcc cmake make      \
            liburing liburing-devel \
            openssl openssl-devel   \
            systemd systemd-devel   \
            python3-docutils        \
            libatomic               \
            zlib zlib-devel         \
            libzstd libzstd-devel   \
            lz4 lz4-devel           \
            bzip2 bzip2-devel       \
            clang clang-analyzer clang-tools-extra
```


### Release build

The following commands will install [**pgagroal**](https://github.com/pgagroal/pgagroal) in the `/usr/local` hierarchy
and run the default configuration.

```sh
git clone https://github.com/pgagroal/pgagroal.git
cd pgagroal
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
/usr/local/bin/pgagroal -c /usr/local/etc/pgagroal/pgagroal.conf -a /usr/local/etc/pgagroal/pgagroal_hba.conf
```

See [RPM](./doc/RPM.md) for how to build a RPM of [**pgagroal**](https://github.com/pgagroal/pgagroal).

### Debug build

The following commands will create a `DEBUG` version of [**pgagroal**](https://github.com/pgagroal/pgagroal).

```sh
git clone https://github.com/pgagroal/pgagroal.git
cd pgagroal
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
cd src
cp ../../doc/etc/*.conf .
./pgagroal -c pgagroal.conf -a pgagroal_hba.conf
```

Remember to set the `log_level` configuration option to `debug5`.

## Contributing

Contributions to [**pgagroal**](https://github.com/pgagroal/pgagroal) are managed on [GitHub.com](https://github.com/pgagroal/pgagroal/)

* [Ask a question](https://github.com/pgagroal/pgagroal/discussions)
* [Raise an issue](https://github.com/pgagroal/pgagroal/issues)
* [Feature request](https://github.com/pgagroal/pgagroal/issues)
* [Code submission](https://github.com/pgagroal/pgagroal/pulls)

Contributions are most welcome !

Please, consult our [Code of Conduct](./CODE_OF_CONDUCT.md) policies for interacting in our
community.

Consider giving the project a [star](https://github.com/pgagroal/pgagroal/stargazers) on
[GitHub](https://github.com/pgagroal/pgagroal/) if you find it useful. And, feel free to follow
the project on [Twitter](https://twitter.com/pgagroal/) as well.

## License

[BSD-3-Clause](https://opensource.org/licenses/BSD-3-Clause)
