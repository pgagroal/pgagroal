\newpage

# Security model

**Create frontend users**

Frontend users are stored into the `pgagroal_frontend_users.conf` file, that can be managed via the `pgagroal-admin` command line tool.
See [the documentation on frontend users](https://github.com/pgagroal/pgagroal/blob/master/doc/CONFIGURATION.md#pgagroal_frontend_users-configuration) for more details.

As an example, consider the user `myuser` that has the `mypassword` password defined on the PostgreSQL side. It is possible to *remap* the user password on the [**pgagroal**][pgagroal] side,
so that an application can connect to the [**pgagroal**][pgagroal] using a different password, like `application_password`. In turn, [**pgagroal**][pgagroal] will connect to PostgreSQL
using the `mypassword` password. Therefore, the application doesn't not know the *real* password used to connect to PostgreSQL.

To achieve this, as [**pgagroal**][pgagroal] operating system run the following command:

```
pgagroal-admin -f /etc/pgagroal/pgagroal_frontend_users.conf -U myuser -P application_password user add
```

You will need a password mapping for each user defined in the `pgagroal_users.conf` configuration file.

**Restart pgagroal**

In order to apply changes, you need to restart [**pgagroal**][pgagroal] so do:

```
pgagroal-cli shutdown
pgagroal -d
```

**Connect to PostgreSQL**

You can now use the "application password" to access the PostgreSQL instance. As an example,
run the following as any operatng system user:

```
psql -h localhost -p 2345 -U myuser mydb
```

using `application_password` as the password.
As already explained, [**pgagroal**][pgagroal] will then use the `mypassword` password against PostgreSQL.

This **split security model** allows you to avoid sharing password between applications and PostgreSQL,
letting the [**pgagroal**][pgagroal] to be the secret-keeper. This not only improves security, but also allows you
to change the PostgreSQL password without having the application change its configuration.
