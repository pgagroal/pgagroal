\newpage

# Docker

You can run [**pgagroal**][pgagroal] using Docker instead of compiling it manually.

**Prerequisites**

* [**Docker**][docker] or [**Podman**][podman] must be installed on the server where PostgreSQL is running.
* Ensure PostgreSQL is configured to allow external connections.

**Update the configuration file if needed:**

```ini
[pgagroal]
host = *
port = 2345
metrics = 2346
log_type = file
log_level = debug
log_path = /tmp/pgagroal.log
ev_backend = auto

max_connections = 100
idle_timeout = 600
validation = off
unix_socket_dir = /tmp/

[primary]
host = host.docker.internal
port = 5432
```

**pgagroal_hba.conf**
```ini
#
# TYPE  DATABASE USER  ADDRESS  METHOD
#
host    all      all   all      all
```

**Step 1: Enable External PostgreSQL Access**

Modify the local PostgreSQL server's `postgresql.conf` file to allow connections from outside:
```ini
listen_addresses = '*'
```

Update `pg_hba.conf` to allow remote connections:
```ini
host    all    all    0.0.0.0/0    scram-sha-256
```

Follow [GETTING STARTED](https://github.com/pgagroal/pgagroal/blob/master/doc/GETTING_STARTED.md) for further server setup

Then, restart PostgreSQL for the changes to take effect:
```sh
sudo systemctl restart postgresql
```

**Step 2: Clone the Repository**

```sh
git clone https://github.com/pgagroal/pgagroal.git
cd pgagroal
```

**Step 3: Build the Docker Image**

There are two Dockerfiles available:
1. **Alpine-based image**

**Using Docker**
```sh
docker build -t pgagroal:latest -f ./contrib/docker/Dockerfile.alpine .
```

**Using Podman**

```sh
podman build -t pgagroal:latest -f ./contrib/docker/Dockerfile.alpine .
```

2. **Rocky Linux 9-based image**

**Using Docker**
```sh
docker build -t pgagroal:latest -f ./contrib/docker/Dockerfile.rocky9 .
```

**Using Podman**

```sh
podman build -t pgagroal:latest -f ./contrib/docker/Dockerfile.rocky9 .
```

**Step 4: Run pgagroal as a Docker Container**

Once the image is built, run the container using:

- **Using Docker**

```sh
docker run -d --name pgagroal \
  -p 2345:2345 \
  -p 2346:2346 \
  --add-host=host.docker.internal:host-gateway \
  pgagroal:latest
```

- **Using Podman**

```sh
podman run -d --name pgagroal \
  -p 2345:2345 \
  -p 2346:2346 \
  --add-host=host.docker.internal:host-gateway \
  pgagroal:latest
```

**Step 5: Verify the Container**

Check if the container is running:

- **Using Docker**

```sh
docker ps | grep pgagroal -->
```

- **Using Podman**
```sh
podman ps | grep pgagroal
```

Check logs for any errors:


- **Using Docker**

```sh
docker logs pgagroal
```

- **Using Podman**

```sh
podman logs pgagroal
```

You can also inspect the exposed metrics at:
```
http://localhost:5001/metrics
```

You can stop the container using

- **Using Docker**

```sh
docker stop ppgagroal
```

- **Using Podman**

```sh
podman stop ppgagroal
```

---

We will assume that we have a user called `test` with the password `test` in our
[PostgreSQL](https://www.postgresql.org) instance. See their
[documentation](https://www.postgresql.org/docs/current/index.html) on how to setup
[PostgreSQL](https://www.postgresql.org), [add a user](https://www.postgresql.org/docs/current/app-createuser.html)
and [add a database](https://www.postgresql.org/docs/current/app-createdb.html).

We will connect to [**pgagroal**](https://github.com/pgagroal/pgagroal) using the [psql](https://www.postgresql.org/docs/current/app-psql.html)
application.

```
psql -h localhost -p 2345 -U test test
```

---

You can exec into the container and run the cli commands as

```sh
docker exec -it pgagroal /bin/bash
#or using podman
podman exec -it pgagroal /bin/bash

cd /etc/pgagroal
/usr/local/bin/pgagroal-cli -c pgagroal.conf shutdown
```

See [this](https://github.com/pgagroal/pgagroal/blob/main/doc/manual/user-10-cli.md) for more cli commands.

You can access the three binaries at `/usr/local/bin`
