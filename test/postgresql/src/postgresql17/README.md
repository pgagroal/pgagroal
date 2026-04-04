# pgagroal-test-postgresql17

This project contains the PostgreSQL 17 image for pgagroal testing.

## Getting Started

`Makefile` targets

* `make build` - Build the image
* `make clean` - Remove the container + image
* `make all` - Do everything

```bash
# podman can be replaced with docker
# Run, and register the container under postgresql-primary
podman run -p 5432:5432 --name pgagroal-test-postgresql17 -d \
-e PG_DATABASE=mydb \
-e PG_USER_NAME=myuser \
-e PG_USER_PASSWORD=password \
-e PG_REPL_USER_NAME=repl \
-e PG_REPL_PASSWORD=password \
-e PG_LOG_LEVEL=debug5 \
pgagroal-test-postgresql17-rocky10

# psql to pgagroal-test-postgresql17
psql -h localhost -p 5432 -U myuser mydb

# Shell to pgagroal-test-postgresql17
podman exec -it pgagroal-test-postgresql17 /usr/bin/bash

# Get the IP address of the pgagroal-test-postgresql17 container; if empty use host IP address
podman inspect --format='{{.NetworkSettings.IPAddress}}' pgagroal-test-postgresql17

# Start the container
podman start pgagroal-test-postgresql17

# Stop the container
podman stop pgagroal-test-postgresql17

# Remove the container
podman rm pgagroal-test-postgresql17
```

## Configuration

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| PG_DATABASE | | | Yes | The name of the database |
| PG_USER_NAME | | | Yes | The user name |
| PG_USER_PASSWORD | | | Yes | The password for the user |
| PG_REPL_USER_NAME | | | YES | The replication user name |
| PG_REPL_PASSWORD | | | YES | The password for the replication user |
| PG_MAX_CONNECTIONS | 100 | | | `max_connections` setting |
| PG_SHARED_BUFFERS | 256 | MB | | `shared_buffers` setting |
| PG_WORK_MEM | 8 | MB | | `work_mem` setting |
| PG_MAX_PARALLEL_WORKERS | 8 | | | `max_parallel_workers` setting |
| PG_EFFECTIVE_CACHE_SIZE | 1 | GB | | `effective_cache_size` setting |
| PG_MAX_WAL_SIZE | 1 | GB | | `max_wal_size` setting |
| PG_PASSWORD_ENCRYPTION | scram-sha-256 | | | `password_encryption` setting |
| PG_LOG_LEVEL | debug5 | | | `log_min_messages` setting |

## Volumes

| Name | Description |
|------|-------------|
| `/pgconf` | Volume for SSL configuration |
| `/pgdata` | PostgreSQL data directory |
| `/pgwal` | PostgreSQL Write-Ahead Log (WAL) |
| `/pglog` | PostgreSQL log directory |

## Access postgres

```
psql -h /tmp postgres
```