# pgagroal security

## Pass-through security

pgagroal use pass-through security by default.

This means that pgagroal delegates to PostgreSQL to determine if the credentials used are valid.

Once a connection is obtained pgagroal will replay the previous communication sequence to verify
the new client. This only works for connections using `trust`, `password` or `md5` authentication
methods, so `scram-sha-256` based connections are not cached.

Note, that this can lead to replay attacks against the `md5` based connections since the hash
doesn't change. Make sure that pgagroal is deployed on a private trusted network, but consider
using either a user vault or authentication query instead.

## User vault

A user vault is a vault which defines the known users and their password.

The vault is static, and is managed through the `pgagroal-admin` tool.

The user vault is specified using the `-u` or `--users` command line parameter.

### Frontend users

The `-F` or `--frontend` command line parameter allows users to be defined for the client to
[**pgagroal**](https://github.com/pgagroal/pgagroal) authentication. This allows the setup to use different passwords for the [**pgagroal**](https://github.com/pgagroal/pgagroal) to
PostgreSQL authentication.

All users defined in the frontend authentication must be defined in the user vault (`-u`).

Frontend users (`-F`) requires a user vault (`-u`) to be defined.

## Authentication query

Authentication query will use the below defined function to query the database
for the user password

```
CREATE FUNCTION public.pgagroal_get_password(
  IN  p_user     name,
  OUT p_password text
) RETURNS text
LANGUAGE sql SECURITY DEFINER SET search_path = pg_catalog AS
$$SELECT passwd FROM pg_shadow WHERE usename = p_user$$;
```

This function needs to be installed in each database.

The function requires a user that is able to execute it, like

```
-- Create a role used for the authentication query
CREATE ROLE pgagroal LOGIN;
-- Set the password
\password pgagroal

-- Only allow access to "pgagroal"
REVOKE EXECUTE ON FUNCTION public.pgagroal_get_password(name) FROM PUBLIC;
GRANT EXECUTE ON FUNCTION public.pgagroal_get_password(name) TO pgagroal;
```

Make sure that the user is different from the actual application users accessing
the database. The user accessing the function needs to have its credential present
in the vault passed to the `-S` or `--superuser` command line parameter.

The user executing the authentication query must use either a MD5 or a SCRAM-SHA-256
password protected based account.

Note, that authentication query doesn't support user vaults - user vault (`-u`) and frontend users (`-F`) -
as well as limits (`-l`).

## Database Alias

A **database alias** in pgagroal allows clients to connect using an alternative name for a configured database. This is useful for scenarios such as application migrations, multi-tenancy, or providing user-friendly names without exposing the actual backend database name.

### How it works

- Each database entry in the limits configuration (`pgagroal_databases.conf`) can specify one or more aliases using the format `database_name=alias1,alias2,alias3`.
- When a client connects using an alias, pgagroal transparently maps the alias to the real database name before establishing or reusing a backend connection.
- Aliases are resolved during both pooled and unpooled connection handling, ensuring that connections are matched and authenticated against the correct backend database.

### Configuration Format

Aliases are defined directly in the database field of `pgagroal_databases.conf` using the following syntax:

```
database_name=alias1,alias2,alias3   username   max_size   initial_size   min_size
```

### Configuration Examples

```
# Database with aliases
production_db=prod,main,primary     myuser    10    5    2

# Database without aliases 
legacy_db                          legacyuser 8     3    1
```

### Alias Rules and Constraints

1. **Format**: Aliases are specified using `database_name=alias1,alias2,alias3` format
2. **Optional**: Aliases are completely optional - databases can be configured without any aliases
3. **Limit**: Maximum of 8 aliases per database entry
4. **Global Uniqueness**: 
   - All alias names must be globally unique across all database entries
   - Alias names cannot conflict with any real database name
   - Database names cannot conflict with any alias name
5. **Special Restriction**: The special database name `all` cannot have aliases
6. **No Empty Aliases**: Empty alias names are not allowed
7. **No Duplicates**: No duplicate aliases within the same database entry

### Implementation Details

- The alias mapping is stored in the configuration structure ([`struct configuration`](../src/include/pgagroal.h))
- Alias resolution is performed by the [`resolve_database_alias`](../src/libpgagroal/security.c) and [`resolve_database_name`](../src/libpgagroal/pool.c) functions
- When a client connects, the alias is resolved to the real database name before:
  - Creating a new backend connection (unpooled)
  - Matching an existing connection in the pool (pooled)
  - Performing authentication queries (if enabled)
  - HBA (Host-Based Authentication) checking
- The pool logic ensures that connections established with the real database name can be reused by clients connecting with any of its aliases

### Example Flow

1. **Client connects using an alias:**  
   The client specifies `prod` as the database.
2. **Alias resolution:**  
   pgagroal resolves `prod` to `production_db` using the configuration.
3. **Connection handling:**  
   - If a pooled connection to `production_db` exists, it is reused.
   - If not, a new connection to `production_db` is established.
4. **Authentication and routing:**  
   All authentication and backend communication use the real database name `production_db`.

### Configuration Validation

The configuration system validates aliases to ensure:
- No duplicate aliases within the same entry
- No alias conflicts with main database names in other entries  
- No duplicate aliases across different entries
- Aliases are not empty strings
- The special database `all` does not have aliases
- Total alias count does not exceed the maximum limit (8 per database)

### CLI Support

You can view all configured aliases using the CLI command:
```sh
pgagroal-cli conf alias
```

This displays all databases with their configured aliases in a readable format.

### Notes

- Changes to aliases can be reloaded without restarting pgagroal, making it easy to add or modify aliases for existing databases
- Aliases work seamlessly with all pgagroal features including connection pooling, authentication, and monitoring
- For HBA configuration, you can use either the real database name or any of its aliases in the database field

For more details, see the implementation in [`configuration.c`](../src/libpgagroal/configuration.c), [`security.c`](../src/libpgagroal/security.c), and [`pool.c`](../src/libpgagroal/pool.c).