\newpage

# Configuration

The configuration is loaded from either the path specified by the `-c` flag or `/etc/pgagroal/pgagroal.conf`.

The configuration of [**pgagroal**][pgagroal] is split into sections using the `[` and `]` characters.

The main section, called `[pgagroal]`, is where you configure the overall properties of [**pgagroal**][pgagroal].

Other sections doesn't have any requirements to their naming so you can give them meaningful names like `[primary]` for the primary [PostgreSQL][postgresql] instance.

All properties are in the format `key = value`.

The characters `#` and `;` can be used for comments; must be the first character on the line.

The `Bool` data type supports the following values: `on`, `yes`, `1`, `true`, `off`, `no`, `0` and `false`.

See a [sample][sample] configuration for running [**pgagroal**][pgagroal] on `localhost`.

## pgagroal.conf

This section is mandatory and the pooler will refuse to start if the configuration file does not specify one and only one. Usually this section is place on top of the configuration file, but its position within the file does not really matter.
The available keys and their accepted values are reported in the table below.

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The bind address for pgagroal |
| port | | Int | Yes | The bind port for pgagroal |
| unix_socket_dir | | String | Yes | The Unix Domain Socket location |
| metrics | 0 | Int | No | The metrics port (disable = 0) |
| metrics_cache_max_age | 0 | String | No | The amount of time to keep a Prometheus (metrics) response in cache. If this value is specified without units, it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks. (disable = 0) |
| metrics_cache_max_size | 256k | String | No | The maximum amount of data to keep in cache when serving Prometheus responses. Changes require restart. This parameter determines the size of memory allocated for the cache even if `metrics_cache_max_age` or `metrics` are disabled. Its value, however, is taken into account only if `metrics_cache_max_age` is set to a non-zero value. Supports suffixes: 'B' (bytes), the default if omitted, 'K' or 'KB' (kilobytes), 'M' or 'MB' (megabytes), 'G' or 'GB' (gigabytes).|
| management | 0 | Int | No | The remote management port (disable = 0) |
| log_type | console | String | No | The logging type (console, file, syslog) |
| log_level | info | String | No | The logging level, any of the (case insensitive) strings `FATAL`, `ERROR`, `WARN`, `INFO` and `DEBUG` (that can be more specific as `DEBUG1` thru `DEBUG5`). Debug level greater than 5 will be set to `DEBUG5`. Not recognized values will make the log_level be `INFO` |
| log_path | pgagroal.log | String | No | The log file location. Can be a strftime(3) compatible string. |
| log_rotation_age | 0 | String | No | The amount of time after which log file rotation is triggered. If this value is specified without units, it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks. (disable = 0) |
| log_rotation_size | 0 | String | No | The size of the log file that will trigger a log rotation. Supports suffixes: 'B' (bytes), the default if omitted, 'K' or 'KB' (kilobytes), 'M' or 'MB' (megabytes), 'G' or 'GB' (gigabytes). A value of `0` (with or without suffix) disables. |
| log_line_prefix | %Y-%m-%d %H:%M:%S | String | No | A strftime(3) compatible string to use as prefix for every log line. Must be quoted if contains spaces. |
| log_mode | append | String | No | Append to or create the log file (append, create) |
| log_connections | `off` | Bool | No | Log connects |
| log_disconnections | `off` | Bool | No | Log disconnects |
| blocking_timeout | 30 | String | No | The amount of time the process will be blocking for a connection. If this value is specified without units, it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks. (disable = 0) |
| idle_timeout | 0 | String | No | The amount of time a connection is kept alive. If this value is specified without units, it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks. (disable = 0) |
| rotate_frontend_password_timeout | 0 | String | No | The amount of time after which the passwords of frontend users are updated periodically. If this value is specified without units, it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks. (disable = 0) |
| rotate_frontend_password_length | 8 | Int | No | The length of the randomized frontend password |
| max_connection_age | 0 | String | No | The maximum amount of time that a connection will live. If this value is specified without units, it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks. (disable = 0) |
| validation | `off` | String | No | Should connection validation be performed. Valid options: `off`, `foreground` and `background` |
| background_interval | 300 | String | No | The interval between background validation scans. If this value is specified without units, it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks. |
| max_retries | 5 | Int | No | The maximum number of iterations to obtain a connection |
| max_connections | 100 | Int | No | The maximum number of connections to PostgreSQL (max 10000) |
| allow_unknown_users | `true` | Bool | No | Allow unknown users to connect |
| authentication_timeout | 5 | String | No | The amount of time the process will wait for valid credentials. If this value is specified without units, it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks. |
| pipeline | `auto` | String | No | The pipeline type (`auto`, `performance`, `session`, `transaction`) |
| auth_query | `off` | Bool | No | Enable authentication query |
| failover | `off` | Bool | No | Enable failover support |
| failover_script | | String | No | The failover script to execute |
| tls | `off` | Bool | No | Enable Transport Layer Security (TLS) |
| tls_cert_file | | String | No | Certificate file for TLS. This file must be owned by either the user running pgagroal or root. |
| tls_key_file | | String | No | Private key file for TLS. This file must be owned by either the user running pgagroal or root. Additionally permissions must be at least `0640` when owned by root or `0600` otherwise. |
| tls_ca_file | | String | No | Certificate Authority (CA) file for TLS. This file must be owned by either the user running pgagroal or root.  |
| metrics_cert_file | | String | No | Certificate file for TLS for Prometheus metrics. This file must be owned by either the user running pgagroal or root. |
| metrics_key_file | | String | No | Private key file for TLS for Prometheus metrics. This file must be owned by either the user running pgagroal or root. Additionally permissions must be at least `0640` when owned by root or `0600` otherwise. |
| metrics_ca_file | | String | No | Certificate Authority (CA) file for TLS for Prometheus metrics. This file must be owned by either the user running pgagroal or root.  |
| libev | `auto` | String | No | Select the [libev](http://software.schmorp.de/pkg/libev.html) backend to use. Valid options: `auto`, `select`, `poll`, `epoll`, `iouring`, `devpoll` and `port` |
| keep_alive | on | Bool | No | Have `SO_KEEPALIVE` on sockets |
| nodelay | on | Bool | No | Have `TCP_NODELAY` on sockets |
| backlog | `max_connections` / 4 | Int | No | The backlog for `listen()`. Minimum `16` |
| hugepage | `try` | String | No | Huge page support (`off`, `try`, `on`) |
| tracker | off | Bool | No | Track connection lifecycle |
| track_prepared_statements | off | Bool | No | Track prepared statements (transaction pooling) |
| pidfile | | String | No | Path to the PID file. If omitted, automatically set to `unix_socket_dir`/pgagroal.`port`.pid |
| update_process_title | `verbose` | String | No | The behavior for updating the operating system process title, mainly related to connection processes. Allowed settings are: `never` (or `off`), does not update the process title; `strict` to set the process title without overriding the existing initial process title length; `minimal` to set the process title to `username/database`; `verbose` (or `full`) to set the process title to `user@host:port/database`. Please note that `strict` and `minimal` are honored only on those systems that do not provide a native way to set the process title (e.g., Linux). On other systems, there is no difference between `strict` and `minimal` and the assumed behaviour is `minimal` even if `strict` is used. `never` and `verbose` are always honored, on every system. On Linux systems the process title is always trimmed to 255 characters, while on system that provide a natve way to set the process title it can be longer. |
| health_check | `off` | Bool | No | Enable health check. If enabled, pgagroal will periodically check the health of the servers. |
| health_check_period | 30 | Int | No | The interval in seconds between health check scans. |
| health_check_timeout | 5 | String | No | The amount of time the process will wait for a response during a health check. If this value is specified without units, it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks. |
| health_check_user | `postgres` | String | No | The user used for connecting to the health check. This user will also be used as the database name. |


__Danger zone__

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| disconnect_client | 0 | Int | No | Disconnect clients that have been idle for more than the specified seconds. This setting __DOES NOT__ take long running transactions into account  |
| disconnect_client_force | off | Bool | No | Disconnect clients that have been active for more than the specified seconds. This setting __DOES NOT_** take long running transactions into account  |


**Server section**

Each section with a name different from [**pgagroal**](https://github.com/pgagroal/pgagroal) will be treated as an host section.
There can be up to `64` host sections, each with an unique name and different combination of `host` and `port` settings, otherwise the pooler will complain about duplicated server configuration.

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The address of the PostgreSQL instance |
| port | | Int | Yes | The port of the PostgreSQL instance |
| primary | | Bool | No | Identify the instance as primary (hint) |
| tls | `off` | Bool | No | Enable Transport Layer Security (TLS) support (Experimental - no pooling). Changes require restart. |
| tls_cert_file | | String | No | Certificate file for TLS. This file must be owned by either the user running pgagroal or root. Changes require restart. |
| tls_key_file | | String | No | Private key file for TLS. This file must be owned by either the user running pgagroal or root. Additionally permissions must be at least `0640` when owned by root or `0600` otherwise.Changes require restart. |
| tls_ca_file | | String | No | Certificate Authority (CA) file for TLS. This file must be owned by either the user running pgagroal or root. Changes require restart. |

Note, that if `host` starts with a `/` it represents a path and [**pgagroal**](https://github.com/pgagroal/pgagroal) will connect using a Unix Domain Socket.

## pgagroal_hba.conf

The `pgagroal_hba` configuration controls access to [**pgagroal**](https://github.com/pgagroal/pgagroal) through host-based authentication.

The configuration is loaded from either the path specified by the `-a` flag or `/etc/pgagroal/pgagroal_hba.conf`.

The format of the file follows the similar [PostgreSQL HBA configuration format](https://www.postgresql.org/docs/current/auth-pg-hba-conf.html), and as such looks like

```
#
# TYPE  DATABASE USER  ADDRESS  METHOD
#
host    all      all   all      all
```

| Column | Required | Description |
|--------|----------|-------------|
| TYPE   | Yes      | Specifies the access method for clients. `host` and `hostssl` are supported |
| DATABASE | Yes      | Specifies the database for the rule. Either specific name or `all` for all databases |
| USER | Yes      | Specifies the user for the rule. Either specific name or `all` for all users |
| ADDRESS | Yes      | Specifies the network for the rule. `all` for all networks, or IPv4 address with a mask (`0.0.0.0/0`) or IPv6 address with a mask (`::0/0`) |
| METHOD | Yes      | Specifies the authentication mode for the user. `all` for all methods, otherwise `trust`, `reject`, `password`, `md5` or `scram-sha-256` |

Remote management users needs to have their database set to `admin` in order for the entry to be considered.

There can be up to `64` HBA entries in the configuration file.

## pgagroal_databases.conf

The `pgagroal_databases` configuration defines limits for a database or a user or both. The limits are the number
of connections from [**pgagroal**](https://github.com/pgagroal/pgagroal) to PostgreSQL for each entry.

The file also defines the initial and minimum pool size for a database and user pair. Note, that this feature requires
a user definition file, see below.

The configuration is loaded from either the path specified by the `-l` flag or `/etc/pgagroal/pgagroal_databases.conf`.

```
#
# DATABASE USER    MAX_SIZE INITIAL_SIZE MIN_SIZE
#
mydb       myuser  all
anotherdb  userB   10           5       3
```

| Column | Required | Description |
|--------|----------|-------------|
| DATABASE | Yes | Specifies the database for the rule. `all` for all databases |
| USER | Yes | Specifies the user for the rule. `all` for all users |
| MAX_SIZE | Yes | Specifies the maximum pool size for the entry. `all` for all remaining counts from `max_connections` |
| INITIAL_SIZE | No | Specifies the initial pool size for the entry. `all` for `MAX_SIZE` connections. Default is 0 |
| MIN_SIZE | No | Specifies the minimum pool size for the entry. `all` for `MAX_SIZE` connections. Default is 0 |

**Database Aliases**

Database aliases allow clients to connect using alternative names for a configured database. This is useful for:
- Application migrations where legacy database names need to be supported
- Multi-tenancy scenarios where different clients use different logical names
- Providing user-friendly names without exposing actual backend database names

**Alias Rules and Behavior**

- **Alias Resolution**: When a client connects using an alias, pgagroal automatically resolves it to the real database name before establishing or reusing backend connections
- **Connection Pooling**: Connections established with the real database name can be reused by clients connecting with any of its aliases
- **Transparent Mapping**: All authentication queries and backend communication use the real database name
- **Uniqueness**: Aliases must be unique across all database entries and cannot conflict with any real database name
- **Limit**: Maximum 8 aliases per database entry

**Configuration Examples**

```
# Database with aliases
production_db=prod,main,primary     myuser   10  5  2
development_db=dev,test,staging,qa  devuser   5  2  1
new_app_db=legacy_app,old_db        appuser  15  8  3
```

There can be up to `64` entries in the configuration file.

In the case a limit entry has incoherent values, for example `INITIAL_SIZE` smaller than `MIN_SIZE`, the system will try to automatically adjust the settings on the fly, reporting messages in the logs.

The system will find the best match limit entry for a given `DATABASE`-`USER` pair according to the following rules:
1. Use the first entry with an exact `DATABASE` and `USER` match.
2. If there is no exact match, use the entry with a `USER` match and `DATABASE` set to `all`.
3. If Rule 2 does not apply, use the entry with a `DATABASE` match and `USER` set to `all`.

**Note**: For alias matching in Rule 1, if a client connects using an alias name, pgagroal will find the entry where the alias is defined and treat it as an exact database match.

**Alias Validation**

The configuration system validates aliases to ensure:
- No duplicate aliases within the same entry
- No alias conflicts with main database names in other entries
- No duplicate aliases across different entries
- Aliases are not empty strings
- Total alias count does not exceed the maximum limit

Changes to aliases can be reloaded without restarting pgagroal, making it easy to add or modify aliases for existing databases.

## pgagroal_users.conf

The `pgagroal_users` configuration defines the users known to the system. This file is created and managed through the `pgagroal-admin` tool.

The configuration is loaded from either the path specified by the `-u` flag or `/etc/pgagroal/pgagroal_users.conf`.

## pgagroal_frontend_users.conf

The `pgagroal_frontend_users` configuration defines the passwords for the users connecting to pgagroal.
This allows the setup to use different passwords for the [**pgagroal**](https://github.com/pgagroal/pgagroal) to PostgreSQL authentication.
This file is created and managed through the `pgagroal-admin` tool.

All users defined in the frontend authentication must be defined in the user vault (`-u`).

Frontend users (`-F`) requires a user vault (`-u`) to be defined.

The configuration is loaded from either the path specified by the `-F` flag or `/etc/pgagroal/pgagroal_frontend_users.conf`.

## pgagroal_admins.conf

The `pgagroal_admins` configuration defines the administrators known to the system. This file is created and managed through the `pgagroal-admin` tool.

The configuration is loaded from either the path specified by the `-A` flag or `/etc/pgagroal/pgagroal_admins.conf`.

If pgagroal has both Transport Layer Security (TLS) and `management` enabled then `pgagroal-cli` can connect with TLS using the files `~/.pgagroal/pgagroal.key` (must be 0600 permission), `~/.pgagroal/pgagroal.crt` and `~/.pgagroal/root.crt`.

## pgagroal_superuser.conf

The `pgagroal_superuser` configuration defines the superuser known to the system. This file is created and managed through
the `pgagroal-admin` tool. It may only have one user defined.

The configuration is loaded from either the path specified by the `-S` flag or `/etc/pgagroal/pgagroal_superuser.conf`.

## Configuration directory

You can specify a directory for all configuration files using the `-D` flag (or `--directory`).
Alternatively, you can set the `PGAGROAL_CONFIG_DIR` environment variable to define the configuration directory.

**Behavior:**
- When the directory flag (`-D`) is set, pgagroal will look for all configuration files in the specified directory.
- If a required file is not found in the specified directory, pgagroal will look for it in its default location (e.g., `/etc/pgagroal/pgagroal.conf`).
- If the file is not found in either location:
  - If the file is mandatory, pgagroal will log an error and fail to start.
  - If the file is optional, pgagroal will log a warning and continue without it.
- All file lookup attempts and missing files are logged for troubleshooting.

**Precedence Rules:**
- Individual file flags (such as `-c`, `-a`, `-l`, etc.) always take precedence over the directory flag and environment variable for their respective files.
- The directory flag (`-D`) takes precedence over the environment variable (`PGAGROAL_CONFIG_DIR`).
- If neither the directory flag nor individual file flags are set, pgagroal uses the default locations for all configuration files.

**Using the Environment Variable:**
1. Set the environment variable before starting pgagroal:
   ```
   export PGAGROAL_CONFIG_DIR=/path/to/config_dir
   pgagroal -d
   ```
2. If both the environment variable and the `-D` flag are set, the flag takes precedence.

**Example:**
```
pgagroal -D /custom/config/dir -d
```
or
```
export PGAGROAL_CONFIG_DIR=/custom/config/dir
pgagroal -d
```

Refer to logs for details about which configuration files were loaded and from which locations.
