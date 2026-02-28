\newpage

# Introduction

[**pgagroal**][pgagroal] is a high-performance protocol-native connection pool for [PostgreSQL][postgresql].

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
* Grafana 12 dashboard
* Remote management
* Authentication query support
* Failover support
* Transport Layer Security (TLS) v1.2+ support
* Daemon mode
* User vault

## Platforms

The supported platforms are

* [Fedora][fedora] 42+
* [RHEL][rhel] 10 / RockyLinux 10
* [FreeBSD][freebsd]
* [OpenBSD][openbsd]

## Migration

### From 2.0.x to 2.1.0

#### Vault Encryption

The key derivation for vault file encryption has been upgraded to
`PKCS5_PBKDF2_HMAC` (SHA-256, random 16-byte salt, 600,000 iterations).

This is a **breaking change**. Existing vault files encrypted with the
old method cannot be decrypted by version 2.1.0.

**Action required:**

1. Stop pgagroal
2. Delete the existing user files:
   - `pgagroal_users.conf`
   - `pgagroal_frontend_users.conf`.
   - `pgagroal_admins.conf`
   - `pgagroal_superuser.conf`
   - Vault users file (if applicable)
3. Delete the existing master key:
   ```
   rm ~/.pgagroal/master.key
   ```
4. Regenerate the master key:
   ```
   pgagroal-admin master-key
   ```
5. Re-add all users:
   ```
   pgagroal-admin user add -f <users_file>
   ```
6. Restart pgagroal

## How to Use This Manual

This manual is organized to guide you from initial setup to advanced usage and development. Use the table below to quickly find the section most relevant to your needs:

> **Navigation Note**: Each entry has two links separated by `|`:
> - **First link (Chapter)**: Use when reading the PDF manual (jumps to page)
> - **Second link (File)**: Use when browsing individual markdown files
> - File links will not work in PDF format

| Chapter                                                           | File                                                              | Description                                                                   |
|-------------------------------------------------------------------|-------------------------------------------------------------------|-------------------------------------------------------------------------------|
| [Installation](#installation)                                    | [02-installation.md](02-installation.md)                         | Step-by-step setup for Rocky Linux, PostgreSQL 18, and pgagroal             |
| [Getting Started](#getting-started)                              | [03-gettingstarted.md](03-gettingstarted.md)                     | Quick introduction to basic pgagroal usage and initial configuration        |
| [Configuration](#configuration)                                  | [04-configuration.md](04-configuration.md)                       | Comprehensive guide to all configuration files and options                   |
| [Prefill](#prefill)                                              | [05-prefill.md](05-prefill.md)                                   | How to configure and use connection prefill for better performance          |
| [Remote Management](#remote-administration)                      | [06-remote_management.md](06-remote_management.md)               | Setting up and using remote management features for pgagroal                |
| [Split Security](#security-model)                                | [07-split_security.md](07-split_security.md)                     | Implementing split security models for authentication and access control    |
| [TLS](#transport-level-security-tls)                             | [08-tls.md](08-tls.md)                                           | Configuring Transport Layer Security (TLS) for secure connections           |
| [Database Alias](#database-aliases)                              | [09-database_alias.md](09-database_alias.md)                     | Using database aliases for flexible client connections                      |
| [Vault](#vault)                                                  | [10-vault.md](10-vault.md)                                       | Managing user credentials and secrets with the pgagroal vault               |
| [Prometheus](#prometheus)                                        | [11-prometheus.md](11-prometheus.md)                             | Integrating Prometheus metrics and monitoring                               |
| [Docker](#docker)                                                | [12-docker.md](12-docker.md)                                     | Running pgagroal in Docker containers                                       |
| [Command Line Tools](#command-line-tools)                        | [13-cli-tools.md](13-cli-tools.md)                               | Comprehensive CLI tools reference (pgagroal-cli, pgagroal-admin)           |
| [Performance](#performance)                                      | [14-performance.md](14-performance.md)                           | Performance benchmarks, tuning, and optimization                            |
| [Failover](#failover)                                            | [15-failover.md](15-failover.md)                                 | Failover configuration and scripting                                        |
| [Pipelines](#pipelines)                                          | [16-pipelines.md](16-pipelines.md)                               | Pipeline types and configuration                                             |
| [Security](#security)                                            | [17-security.md](17-security.md)                                 | Comprehensive security hardening guide                                      |
| [Development](#developers)                                       | [70-dev.md](70-dev.md)                                           | Development environment setup and contribution guidelines                   |
| [Git](#git-guide)                                                | [71-git.md](71-git.md)                                           | Git workflow and version control practices for the project                  |
| [Architecture](#architecture)                                    | [72-architecture.md](72-architecture.md)                         | High-level architecture and design of pgagroal                              |
| [RPM](#rpm)                                                      | [73-rpm.md](73-rpm.md)                                           | Building and using RPM packages                                             |
| [Building](#building-pgagroal)                                   | [74-building.md](74-building.md)                                 | Compiling pgagroal from source                                              |
| [Code Coverage](#code-coverage)                                  | [75-codecoverage.md](75-codecoverage.md)                         | Code coverage analysis and testing practices                                |
| [Event Loop](#event-loop)                                        | [76-eventloop.md](76-eventloop.md)                               | Understanding the event loop implementation                                  |
| [Core API](#core-apis)                                           | [77-core_api.md](77-core_api.md)                                 | Reference for core API functions                                            |
| [Testing](#test-suite)                                           | [78-test.md](78-test.md)                                         | Testing frameworks and procedures                                           |
| [Distribution Installation](#distribution-specific-installation) | [79-distributions.md](79-distributions.md)                       | Platform-specific installation notes                                        |
| [Acknowledgements](#acknowledgement)                             | [97-acknowledgement.md](97-acknowledgement.md)                   | Credits and contributors                                                     |
| [Licenses](#license)                                             | [98-licenses.md](98-licenses.md)                                 | License information                                                          |
| [References](#references)                                        | [99-references.md](99-references.md)                             | Additional resources and references                                          |
