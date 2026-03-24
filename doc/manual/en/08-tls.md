\newpage

# Transport Level Security (TLS)

**Creating Certificates**

This tutorial will show you how to create self-signed certificate for the server, valid for 365 days, use the following OpenSSL command, replacing `dbhost.yourdomain.com` with the server's host name, here `localhost`:

```
openssl req -new -x509 -days 365 -nodes -text -out server.crt \
  -keyout server.key -subj "/CN=dbhost.yourdomain.com"
```

then do -

```
chmod og-rwx server.key
```

because the server will reject the file if its permissions are more liberal than this. For more details on how to create your server private key and certificate, refer to the OpenSSL documentation.

For the purpose of this tutorial we will assume the client certificate and key same as the server certificate and server key and therefore, these equations always holds -

* `</path/to/client.crt>` = `</path/to/server.crt>`
* `</path/to/client.key>` = `</path/to/server.key>`
* `</path/to/server_root_ca.crt>` = `</path/to/server.crt>`
* `</path/to/client_root_ca.crt>` = `</path/to/server_root_ca.crt>`

**TLS in `pgagroal`**

**Modify the `pgagroal` configuration**

It is now time to modify the [pgagroal] section of configuration file `/etc/pgagroal/pgagroal.conf`, with your editor of choice by adding the following lines in the [pgagroal] section.

```
tls = on
tls_cert_file = </path/to/server.crt>
tls_key_file = </path/to/server.key>
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
