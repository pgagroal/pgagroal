\newpage

# Developers

This chapter provides comprehensive guidance for developers who want to contribute to pgagroal, understand its architecture, or extend its functionality.

## Documentation Guide

pgagroal documentation is organized to serve different audiences and use cases. Use this guide to quickly find the information you need.

> **Note**: This manual contains the core documentation chapters. Additional standalone documentation files are located in the `doc/` directory, project root, and `contrib/` directory of the source repository. File paths shown below are relative to the project root directory.

### Quick Reference

> **Navigation Note**: Each entry has two links separated by `|`:
> - **First link (Chapter)**: Use when reading the PDF manual (jumps to page)
> - **Second link (File)**: Use when browsing individual markdown files
> - File links will not work in PDF format

| What you want to do                    | Where to look                                                                                                    |
|-----------------------------------------|------------------------------------------------------------------------------------------------------------------|
| **Get started quickly**                | [Getting started](#getting-started) \| [03-gettingstarted.md](03-gettingstarted.md)                           |
| **Install pgagroal**                   | [Installation](#installation) \| [02-installation.md](02-installation.md)                                      |
| **Configure pgagroal**                 | [Configuration](#configuration) \| [04-configuration.md](04-configuration.md)                                  |
| **Set up development environment**     | [Building pgagroal](#building-pgagroal) \| [74-building.md](74-building.md)                                    |
| **Understand the architecture**        | [Architecture](#architecture) \| [72-architecture.md](72-architecture.md)                                      |
| **Write tests**                        | [Test Suite](#test-suite) \| [78-test.md](78-test.md)                                                          |
| **Use Git workflow**                   | [Git guide](#git-guide) \| [71-git.md](71-git.md)                                                              |
| **Build RPM packages**                 | [RPM](#rpm) \| [73-rpm.md](73-rpm.md)                                                                          |
| **Analyze code coverage**              | [Code Coverage](#code-coverage) \| [75-codecoverage.md](75-codecoverage.md)                                    |
| **Work with core APIs**                | [Core API](#core-apis) \| [77-core_api.md](77-core_api.md)                                                     |
| **Understand event loop**              | [Event Loop](#event-loop) \| [76-eventloop.md](76-eventloop.md)                                                |
| **Configure security/TLS**             | [Transport Level Security (TLS)](#transport-level-security-tls) \| [08-tls.md](08-tls.md)                     |
| **Use command-line tools**             | [Command Line Tools](#command-line-tools) \| [13-cli-tools.md](13-cli-tools.md)                               |
| **Set up monitoring**                  | [Prometheus](#prometheus) \| [11-prometheus.md](11-prometheus.md)                                              |
| **Optimize performance**               | [Performance](#performance) \| [14-performance.md](14-performance.md)                                          |
| **Configure failover**                 | [Failover](#failover) \| [15-failover.md](15-failover.md)                                                      |
| **Choose pipeline type**               | [Pipelines](#pipelines) \| [16-pipelines.md](16-pipelines.md)                                                  |
| **Harden security**                    | [Security](#security) \| [17-security.md](17-security.md)                                                      |
| **Deploy with Docker**                 | [Docker](#docker) \| [12-docker.md](12-docker.md)                                                              |
| **Configure database aliases**         | [Database Aliases](#database-aliases) \| [09-database_alias.md](09-database_alias.md)                         |
| **Manage user credentials**            | [Vault](#vault) \| [10-vault.md](10-vault.md)                                                                  |
| **Contribute to project**              | [Git guide](#git-guide) \| [71-git.md](71-git.md), see also `CONTRIBUTING.md` in project root                |
| **Report issues or get help**          | GitHub Issues: https://github.com/pgagroal/pgagroal/issues                                                       |

### User Documentation

#### Manual Chapters (Comprehensive Guide)

> **Navigation Note**: Each table entry has two links - a **Chapter** link and a **File** link:
> - **If reading the PDF manual**: Use the **Chapter** links (first column) to navigate within the PDF
> - **If reading individual markdown files**: Use the **File** links (second column) to open specific files
> - The File links will not work in PDF format, and Chapter links may not work when browsing individual files

**User-Focused Chapters (01-17):**

| Chapter                                                           | File                                                              | Description                                                                   |
|-------------------------------------------------------------------|-------------------------------------------------------------------|-------------------------------------------------------------------------------|
| [Introduction](#introduction)                                    | [01-introduction.md](01-introduction.md)                         | Overview of pgagroal features and manual structure                           |
| [Installation](#installation)                                    | [02-installation.md](02-installation.md)                         | Step-by-step setup for Rocky Linux, PostgreSQL 17, and pgagroal             |
| [Getting Started](#getting-started)                              | [03-gettingstarted.md](03-gettingstarted.md)                     | Quick introduction to basic pgagroal usage and configuration                 |
| [Configuration](#configuration)                                  | [04-configuration.md](04-configuration.md)                       | Comprehensive guide to all configuration files and options                   |
| [Prefill](#prefill)                                              | [05-prefill.md](05-prefill.md)                                   | How to configure and use connection prefill for performance                  |
| [Remote Management](#remote-administration)                      | [06-remote_management.md](06-remote_management.md)               | Setting up and using remote management features                              |
| [Security Model](#security-model)                                | [07-split_security.md](07-split_security.md)                     | Implementing split security models for authentication                        |
| [Transport Level Security](#transport-level-security-tls)        | [08-tls.md](08-tls.md)                                           | Configuring TLS for secure connections                                       |
| [Database Aliases](#database-aliases)                            | [09-database_alias.md](09-database_alias.md)                     | Using database aliases for flexible client connections                       |
| [Vault](#vault)                                                  | [10-vault.md](10-vault.md)                                       | Managing user credentials and secrets with pgagroal vault                    |
| [Prometheus](#prometheus)                                        | [11-prometheus.md](11-prometheus.md)                             | Integrating Prometheus metrics and monitoring                                |
| [Docker](#docker)                                                | [12-docker.md](12-docker.md)                                     | Running pgagroal in Docker containers                                        |
| [Command Line Tools](#command-line-tools)                        | [13-cli-tools.md](13-cli-tools.md)                               | Comprehensive CLI tools reference (pgagroal-cli, pgagroal-admin)            |
| [Performance](#performance)                                      | [14-performance.md](14-performance.md)                           | Performance benchmarks, tuning, and optimization                             |
| [Failover](#failover)                                            | [15-failover.md](15-failover.md)                                 | Failover configuration and scripting                                         |
| [Pipelines](#pipelines)                                          | [16-pipelines.md](16-pipelines.md)                               | Pipeline types and configuration                                              |
| [Security](#security)                                            | [17-security.md](17-security.md)                                 | Comprehensive security hardening guide                                       |

**Developer-Focused Chapters (70-79):**

| Chapter                                                           | File                                                              | Description                                                                   |
|-------------------------------------------------------------------|-------------------------------------------------------------------|-------------------------------------------------------------------------------|
| [Developers](#developers)                                        | [70-dev.md](70-dev.md)                                           | Development environment setup and contribution guidelines (this chapter)     |
| [Git Guide](#git-guide)                                          | [71-git.md](71-git.md)                                           | Git workflow and version control practices for the project                   |
| [Architecture](#architecture)                                    | [72-architecture.md](72-architecture.md)                         | High-level architecture and design of pgagroal                               |
| [RPM](#rpm)                                                      | [73-rpm.md](73-rpm.md)                                           | Building and using RPM packages                                              |
| [Building pgagroal](#building-pgagroal)                          | [74-building.md](74-building.md)                                 | Compiling pgagroal from source                                               |
| [Code Coverage](#code-coverage)                                  | [75-codecoverage.md](75-codecoverage.md)                         | Code coverage analysis and testing practices                                 |
| [Event Loop](#event-loop)                                        | [76-eventloop.md](76-eventloop.md)                               | Understanding the event loop implementation                                   |
| [Core API](#core-apis)                                           | [77-core_api.md](77-core_api.md)                                 | Reference for core API functions                                             |
| [Test Suite](#test-suite)                                        | [78-test.md](78-test.md)                                         | Testing frameworks and procedures                                             |
| [Distribution Installation](#distribution-specific-installation) | [79-distributions.md](79-distributions.md)                       | Platform-specific installation notes                                         |

**Reference Chapters (97-99):**

| Chapter                                   | File                                                              | Description                                                                   |
|-------------------------------------------|-------------------------------------------------------------------|-------------------------------------------------------------------------------|
| [Acknowledgements](#acknowledgement)     | [97-acknowledgement.md](97-acknowledgement.md)                   | Credits and contributors                                                      |
| [Licenses](#license)                     | [98-licenses.md](98-licenses.md)                                 | License information                                                           |
| [References](#references)                | [99-references.md](99-references.md)                             | Additional resources and references                                           |

#### Additional User Resources

The manual chapters above provide comprehensive coverage. Additional standalone files in `doc/` directory provide supplementary information:

- **doc/GETTING_STARTED.md** - Alternative quick start guide (supplements [Getting Started](#getting-started) | [03-gettingstarted.md](03-gettingstarted.md))
- **doc/VAULT.md** - Additional vault examples (supplements [Vault](#vault) | [10-vault.md](10-vault.md))

### Administrator Documentation

All administration topics are covered in this manual:

> **Navigation Note**: Use **Chapter** links when reading the PDF manual, **File** links when browsing individual markdown files.

| Chapter                                                   | File                                                              | Description                                                       |
|-----------------------------------------------------------|-------------------------------------------------------------------|-------------------------------------------------------------------|
| [Configuration](#configuration)                          | [04-configuration.md](04-configuration.md)                       | Complete configuration reference                                  |
| [Remote Management](#remote-administration)              | [06-remote_management.md](06-remote_management.md)               | Remote management setup                                           |
| [Transport Level Security](#transport-level-security-tls)| [08-tls.md](08-tls.md)                                           | TLS configuration                                                 |
| [Vault](#vault)                                          | [10-vault.md](10-vault.md)                                       | User credential management                                        |
| [Command Line Tools](#command-line-tools)                | [13-cli-tools.md](13-cli-tools.md)                               | Complete CLI reference (pgagroal-cli, pgagroal-admin)           |
| [Performance](#performance)                              | [14-performance.md](14-performance.md)                           | Performance tuning and benchmarks                                |
| [Failover](#failover)                                    | [15-failover.md](15-failover.md)                                 | Failover configuration and procedures                            |
| [Pipelines](#pipelines)                                  | [16-pipelines.md](16-pipelines.md)                               | Pipeline configuration and usage                                  |
| [Security](#security)                                    | [17-security.md](17-security.md)                                 | Security hardening and best practices                            |
| [Prometheus](#prometheus)                                | [11-prometheus.md](11-prometheus.md)                             | Monitoring and metrics                                            |
| [Docker](#docker)                                        | [12-docker.md](12-docker.md)                                     | Container deployment                                              |

**Legacy standalone documentation files (now superseded by manual chapters):**

| Legacy File              | Superseded By                                                     | Chapter                                                           |
|--------------------------|-------------------------------------------------------------------|-------------------------------------------------------------------|
| doc/CLI.md               | [Command Line Tools](#command-line-tools)                        | [13-cli-tools.md](13-cli-tools.md)                               |
| doc/ADMIN.md             | [Command Line Tools](#command-line-tools)                        | [13-cli-tools.md](13-cli-tools.md)                               |
| doc/PERFORMANCE.md       | [Performance](#performance)                                      | [14-performance.md](14-performance.md)                           |
| doc/FAILOVER.md          | [Failover](#failover)                                            | [15-failover.md](15-failover.md)                                 |
| doc/PIPELINES.md         | [Pipelines](#pipelines)                                          | [16-pipelines.md](16-pipelines.md)                               |
| doc/SECURITY.md          | [Security](#security)                                            | [17-security.md](17-security.md)                                 |
| doc/DISTRIBUTIONS.md     | [Distribution Installation](#distribution-specific-installation) | [79-distributions.md](79-distributions.md)                       |

### Developer Documentation

Essential reading for contributors and developers:

> **Navigation Note**: Use **Chapter** links when reading the PDF manual, **File** links when browsing individual markdown files.

| Chapter                                   | File                                                              | Description                                                       |
|-------------------------------------------|-------------------------------------------------------------------|-------------------------------------------------------------------|
| [Architecture](#architecture)            | [72-architecture.md](72-architecture.md)                         | High-level architecture and design of pgagroal                   |
| [Building pgagroal](#building-pgagroal)  | [74-building.md](74-building.md)                                 | Compiling pgagroal from source with development options          |
| [Git Guide](#git-guide)                  | [71-git.md](71-git.md)                                           | Git workflow and version control practices for the project       |
| [Test Suite](#test-suite)                | [78-test.md](78-test.md)                                         | Testing frameworks and procedures                                 |
| [Code Coverage](#code-coverage)          | [75-codecoverage.md](75-codecoverage.md)                         | Code coverage analysis and testing practices                     |
| [Event Loop](#event-loop)                | [76-eventloop.md](76-eventloop.md)                               | Understanding the event loop implementation                       |
| [Core API](#core-apis)                   | [77-core_api.md](77-core_api.md)                                 | Reference for core API functions                                 |
| [RPM](#rpm)                              | [73-rpm.md](73-rpm.md)                                           | Building and using RPM packages                                  |

**Additional developer resources (supplements manual chapters):**

| File                    | Supplements                                                                   | Description                                                       |
|-------------------------|-------------------------------------------------------------------------------|-------------------------------------------------------------------|
| doc/DEVELOPERS.md       | [Building pgagroal](#building-pgagroal) \| [74-building.md](74-building.md) | Detailed development environment setup                            |
| doc/ARCHITECTURE.md     | [Architecture](#architecture) \| [72-architecture.md](72-architecture.md)   | Extended architecture documentation                               |
| doc/TEST.md             | [Test Suite](#test-suite) \| [78-test.md](78-test.md)                       | Extended testing documentation                                    |

### Project Management & Planning

Files in the project root directory:

| File                | Description                                       |
|---------------------|---------------------------------------------------|
| CONTRIBUTING.md     | Contribution guidelines and legal information     |
| README.md           | Project overview and quick start                  |
| AUTHORS             | List of project contributors                      |
| LICENSE             | Project license information                       |
| CODE_OF_CONDUCT.md  | Community guidelines and conduct policies         |

### Security & Certificate Documentation

**Security topics covered in this manual:**

> **Navigation Note**: Use **Chapter** links when reading the PDF manual, **File** links when browsing individual markdown files.

| Chapter                                                          | File                                              | Description                                       |
|------------------------------------------------------------------|---------------------------------------------------|---------------------------------------------------|
| [Transport Level Security (TLS)](#transport-level-security-tls) | [08-tls.md](08-tls.md)                           | Complete TLS configuration and certificate setup |
| [Security Model](#security-model)                               | [07-split_security.md](07-split_security.md)     | Advanced security models                         |

### Testing & Development Scripts

**Testing covered in this manual:**

> **Navigation Note**: Use **Chapter** links when reading the PDF manual, **File** links when browsing individual markdown files.

| Chapter                               | File                                              | Description                                |
|---------------------------------------|---------------------------------------------------|--------------------------------------------|
| [Test Suite](#test-suite)            | [78-test.md](78-test.md)                         | Complete testing procedures and frameworks |
| [Code Coverage](#code-coverage)      | [75-codecoverage.md](75-codecoverage.md)         | Code coverage analysis                     |

**Additional testing documentation in project root:**

| File    | Description                       |
|---------|-----------------------------------|
| TEST.md | Root-level testing documentation |

### Configuration Examples & Templates

Configuration file templates and examples in `doc/etc/`:

| File                           | Description                           |
|--------------------------------|---------------------------------------|
| doc/etc/pgagroal.conf         | Main configuration file template     |
| doc/etc/pgagroal_hba.conf     | Host-based authentication template   |
| doc/etc/pgagroal_vault.conf   | Vault configuration template         |
| doc/etc/pgagroal.service      | Systemd service file                 |
| doc/etc/pgagroal.socket       | Systemd socket file                  |

### Contrib Directory (Additional Tools & Examples)

Community contributions and additional tools in `contrib/`:

| Path                             | Description                                           |
|----------------------------------|-------------------------------------------------------|
| contrib/docker/                  | Docker configuration examples and Dockerfiles       |
| contrib/grafana/README.md        | Grafana dashboard setup and configuration            |
| contrib/prometheus_scrape/README.md | Prometheus metrics documentation generator           |
| contrib/valgrind/README.md       | Valgrind memory debugging configuration              |
| contrib/shell_comp/              | Shell completion scripts for bash and zsh           |

### Man Pages (Reference Documentation)

Complete command-line and configuration reference in `doc/man/`:

| File                                   | Description                             |
|----------------------------------------|-----------------------------------------|
| doc/man/pgagroal.1.rst                | Main pgagroal command reference        |
| doc/man/pgagroal-cli.1.rst            | CLI tool reference                      |
| doc/man/pgagroal-admin.1.rst          | Admin tool reference                    |
| doc/man/pgagroal-vault.1.rst          | Vault tool reference                    |
| doc/man/pgagroal.conf.5.rst           | Main configuration file reference      |
| doc/man/pgagroal_hba.conf.5.rst       | HBA configuration reference            |
| doc/man/pgagroal_databases.conf.5.rst | Database limits configuration reference |
| doc/man/pgagroal_vault.conf.5.rst     | Vault configuration reference          |

### Reference Materials

> **Navigation Note**: Use **Chapter** links when reading the PDF manual, **File** links when browsing individual markdown files.

| Chapter                                   | File                                                  | Description                         |
|-------------------------------------------|-------------------------------------------------------|-------------------------------------|
| [Acknowledgements](#acknowledgement)     | [97-acknowledgement.md](97-acknowledgement.md)       | Credits and contributors            |
| [Licenses](#license)                     | [98-licenses.md](98-licenses.md)                     | License information                 |
| [References](#references)                | [99-references.md](99-references.md)                 | Additional resources and references |

## Development Environment Setup

For detailed development environment setup, see [Building pgagroal](#building-pgagroal) | [74-building.md](74-building.md). Here's a quick overview:

### Prerequisites

For Fedora-based systems:
```bash
dnf install git gcc cmake make liburing liburing-devel openssl openssl-devel systemd systemd-devel python3-docutils libatomic zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel bzip2 bzip2-devel libasan libasan-static binutils clang clang-analyzer clang-tools-extra
```

### Quick Build

```bash
git clone https://github.com/pgagroal/pgagroal.git
cd pgagroal
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
make install
```

## Contributing Workflow

1. **Fork the repository** on GitHub
2. **Clone your fork** locally
3. **Create a feature branch** from main
4. **Make your changes** following the coding guidelines
5. **Test your changes** thoroughly
6. **Submit a pull request** with a clear description

For detailed Git workflow, see [Git guide](#git-guide) | [71-git.md](71-git.md).

## Key Development Principles

- **Security First**: Always consider security implications
- **Performance Matters**: pgagroal is a high-performance connection pool
- **Code Quality**: Follow established patterns and write tests
- **Documentation**: Update documentation with your changes
- **Community**: Engage with the community for feedback

## Getting Help

- **GitHub Discussions** (https://github.com/pgagroal/pgagroal/discussions) - Ask questions
- **GitHub Issues** (https://github.com/pgagroal/pgagroal/issues) - Report bugs or request features
- **Code of Conduct** (see `CODE_OF_CONDUCT.md` in project root) - Community guidelines
