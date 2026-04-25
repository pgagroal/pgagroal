## Preface 

This tutorial will take you through the connection path from client to `pgagroal` to `postgres` server when TLS is enforced.

## Setup

To enforce tls along the whole path, we first need to create X509 certicates for client->pgagroal and pgagroal->postgres seperately.

For the purpose of this tutorial we will create self-signed certificates and assume only server side authentication.

### Creating Certificates

We will create self-signed certificate for the server, valid for 365 days, use the following OpenSSL command, replacing `dbhost.yourdomain.com` with the server's host name, here `localhost`:

```
openssl req -new -x509 -days 365 -nodes -text -out pgagroal.crt \
  -keyout pgagroal.key -subj "/CN=dbhost.yourdomain.com"
```

for client to pgagroal side authentication and 

```
openssl req -new -x509 -days 365 -nodes -text -out postgres.crt \
  -keyout postgres.key -subj "/CN=dbhost.yourdomain.com"
```

for pgagroal to postgres side authentication.

then do -

```
chmod og-rwx pgagroal.key
chmod og-rwx postgres.key
```

because the server will reject the file if its permissions are more liberal than this. For more details on how to create your server private key and certificate, refer to the OpenSSL documentation.

### Configuration 

Modify the configuration files of postgres and pgagroal.

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

here we are choosing md5 for authenticating the requested user and database against postgres catalog

Make the contents of `pgagroal.conf` to enable tls the whole way -

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

tls = on
tls_cert_file = </path/to/pgagroal.crt>
tls_key_file = </path/to/pgagroal.key>

[primary]
host = localhost
port = 5432
tls = on
tls_ca_file = </path/to/postgres.crt>
```

### Client Request

`PGSSLMODE=verify-ca PGSSLROOTCERT=</path/to/pgagroal.crt> psql -h localhost -p 2345 -U <username> <database>`.


