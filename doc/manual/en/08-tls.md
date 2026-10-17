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

**Client Certificate Authentication**

Use the `cert` method in `pgagroal_hba.conf` to authenticate a client by its certificate instead of a password.
Like PostgreSQL's `cert` method, the client must present a certificate signed by the CA in `tls_ca_file`, and
the Common Name (CN) of the certificate must be the requested user name. It is only allowed on `hostssl` entries.

```
hostssl  all  myuser  all  cert
```

pgagroal then authenticates to PostgreSQL with the password of the user in `pgagroal_users.conf`. A user without
an entry there still needs a matching certificate, and then authenticates to PostgreSQL directly.

**Certificate Revocation**

Add `tls_crl_file` to reject revoked certificates. In the `[pgagroal]` section it applies to client certificates,
and in a server section to the certificate of that PostgreSQL server. It requires `tls_ca_file` in the same section.

```
tls_ca_file = </path/to/client_root_ca.crt>
tls_crl_file = </path/to/client_root_ca.crl>
```

The CRL must be in PEM format. Every certificate in the chain is checked, so the file must hold the CRL of each CA
in the chain. Verification fails once a CRL is past its next update time, so renew the CRL before then.
