\newpage

# Transport Level Security (TLS)

**Creating Certificates**

This tutorial will show you how to create self-signed certificate for the server, valid for 365 days, use the following OpenSSL command, replacing `dbhost.yourdomain.com` with the server's host name, here `localhost`:

```
openssl req -new -x509 -days 365 -nodes -text -out pgagroal.crt \
  -keyout pgagroal.key -subj "/CN=dbhost.yourdomain.com"
```

then do -

```
chmod og-rwx pgagroal.key
```

because the server will reject the file if its permissions are more liberal than this. For more details on how to create your server private key and certificate, refer to the OpenSSL documentation.

For the purpose of this tutorial we will assume the client certificate and key same as the server certificate and server key and therefore, these equations always holds -

* `</path/to/client.crt>` = `</path/to/pgagroal.crt>`
* `</path/to/client.key>` = `</path/to/pgagroal.key>`
* `</path/to/server_root_ca.crt>` = `</path/to/pgagroal.crt>`
* `</path/to/client_root_ca.crt>` = `</path/to/pgagroal_root_ca.crt>`

**TLS in `pgagroal`**

**Modify the `pgagroal` configuration**

It is now time to modify the [pgagroal] section of configuration file `/etc/pgagroal/pgagroal.conf`, with your editor of choice by adding the following lines in the [pgagroal] section.

```
tls = on
tls_cert_file = </path/to/pgagroal.crt>
tls_key_file = </path/to/pgagroal.key>
```

**Only Server Authentication**

If you wish to do only server authentication the aforementioned configuration suffice.

**Client Request**

```
PGSSLMODE=verify-full PGSSLROOTCERT=</path/to/server_root_ca.crt> psql -h localhost -p 2345 -U <postgres_user> <postgres_database>
```

**Full Client and Server Authentication**

To enable the server to request the client certificates add the following configuration lines

```
tls = on
tls_cert_file = </path/to/server.crt>
tls_key_file = </path/to/server.key>
tls_ca_file = </path/to/client_root_ca.crt>
```

**Client Request**

```
PGSSLMODE=verify-full PGSSLCERT=</path/to/client.crt> PGSSLKEY=</path/to/client.key> PGSSLROOTCERT=</path/to/server_root_ca.crt> psql -h localhost -p 2345 -U <postgres_user> <postgres_database>
```

**TLS in `pgagroal` to `PostgreSQL`**

To enforce tls along the whole path, from client to `pgagroal` to `PostgreSQL`, create X509 certicates for pgagroal to PostgreSQL seperately, replacing `dbhost.yourdomain.com` with the server's host name, here `localhost`:

```
openssl req -new -x509 -days 365 -nodes -text -out postgres.crt \
  -keyout postgres.key -subj "/CN=dbhost.yourdomain.com"
```

then do -

```
chmod og-rwx postgres.key
```

**Modify Configuration**

Add the following lines in `postgresql.conf` (Generally can be found in `/etc/postgresql/<version_number>/main` directory)

```
...
ssl = on
ssl_cert_file = </path/to/postgres.crt>
ssl_key_file = </path/to/postgres.key>
...
```

and make the contents of `pg_hba.conf` -

```
hostssl all all all md5
```

Restart the cluster to apply these changes.

Modify the contents of `pgagroal.conf` to enable tls the whole way -

```
[pgagroal]
host = localhost
port = 2345

log_type = console
log_level = debug5
log_path = 

max_connections = 100
idle_timeout = 600
validation = off
unix_socket_dir = /tmp/
ev_backend = auto

tls = on
tls_cert_file = </path/to/pgagroal.crt>
tls_key_file = </path/to/pgagroal.key>

[primary]
host = localhost
port = 5432
tls = on
tls_ca_file = </path/to/postgres.crt>
```

**Client Request**

```
PGSSLMODE=verify-ca PGSSLROOTCERT=</path/to/pgagroal.crt> psql -h localhost -p 2345 -U <username> <database>
```
