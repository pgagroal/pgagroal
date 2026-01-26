\newpage

# Installation

## Rocky Linux

We can download the [Rocky Linux](https://www.rockylinux.org/) distruction from their web site

```
https://rockylinux.org/download
```

The installation and setup is beyond the scope of this guide.

Ideally, you would use dedicated user accounts to run [**PostgreSQL**][postgresql] and [**pgagroal**][pgagroal]

```
useradd postgres
usermod -a -G wheel postgres
useradd pgagroal
usermod -a -G wheel pgagroal
```

Add a configuration directory for [**pgagroal**][pgagroal]

```
mkdir /etc/pgagroal
chown -R pgagroal:pgagroal /etc/pgagroal
```

and lets open the ports in the firewall that we will need

```
firewall-cmd --permanent --zone=public --add-port=2345/tcp
firewall-cmd --permanent --zone=public --add-port=2346/tcp
```

## PostgreSQL

We will install PostgreSQL 18 from the official [YUM repository][yum] with the community binaries,

**x86_64**

```
dnf -qy module disable postgresql
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-10-x86_64/pgdg-redhat-repo-latest.noarch.rpm
```

**aarch64**

```
dnf -qy module disable postgresql
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-10-aarch64/pgdg-redhat-repo-latest.noarch.rpm
```

and do the install via

```
dnf install -y postgresql18 postgresql18-server postgresql18-contrib
```

First, we will update `~/.bashrc` with

```
cat >> ~/.bashrc
export PGHOST=/tmp
export PATH=/usr/pgsql-18/bin/:$PATH
```

then Ctrl-d to save, and

```
source ~/.bashrc
```

to reload the Bash environment.

Then we can do the PostgreSQL initialization

```
mkdir DB
initdb -k DB
```

and update configuration - for a 8 GB memory machine.

**postgresql.conf**
```
listen_addresses = '*'
port = 5432
max_connections = 100
unix_socket_directories = '/tmp'
password_encryption = scram-sha-256
shared_buffers = 2GB
huge_pages = try
max_prepared_transactions = 100
work_mem = 16MB
dynamic_shared_memory_type = posix
wal_level = replica
wal_log_hints = on
max_wal_size = 16GB
min_wal_size = 2GB
log_destination = 'stderr'
logging_collector = on
log_directory = 'log'
log_filename = 'postgresql.log'
log_rotation_age = 0
log_rotation_size = 0
log_truncate_on_rotation = on
log_line_prefix = '%p [%m] [%x] '
log_timezone = UTC
datestyle = 'iso, mdy'
timezone = UTC
lc_messages = 'en_US.UTF-8'
lc_monetary = 'en_US.UTF-8'
lc_numeric = 'en_US.UTF-8'
lc_time = 'en_US.UTF-8'
summarize_wal = on
wal_summary_keep_time = '0'
```

Please, check with other sources in order to create a setup for your local setup.

Now, we are ready to start PostgreSQL

```
pg_ctl -D DB -l /tmp/ start
```

## pgagroal

We will install [**pgagroal**][pgagroal] from the official [YUM repository][yum] as well,

```
dnf install -y pgagroal
```

First, we will need to create a master security key for the [**pgagroal**][pgagroal] installation, by

```
pgagroal-admin -g master-key
```

By default, this will ask for a key interactively. Alternatively, a key can be provided using either the
`--password` command line argument, or the `PGAGROAL_PASSWORD` environment variable. Note that passing the
key using the command line might not be secure.

Then we will create the configuration for [**pgagroal**][pgagroal],

```
cat > /etc/pgagroal/pgagroal.conf
[pgagroal]
host = *
port = 2345

metrics = 2346

log_type = file
log_level = info
log_path = /tmp/pgagroal.log

max_connections = 100
idle_timeout = 600
validation = off
unix_socket_dir = /tmp/

[primary]
host = localhost
port = 5432
```

and end with a Ctrl-d to save the file.

Start [**pgagroal**][pgagroal] now, by

```
pgagroal -d
```
