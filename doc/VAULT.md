# pgagroal-vault configuration

The configuration which is mandatory is loaded from either the path specified by the `-c` flag or `/etc/pgagroal/pgagroal_vault.conf`.

The configuration of `pgagroal-vault` is split into sections using the `[` and `]` characters.

The pgagroal-vault section, called `[pgagroal-vault]`, is where you configure the overall properties of the vault's server.

The other section provide configuration for the management port of pgagroal. For now there can be only one pgagroal management port to connect.
This section don't have any requirements to their naming so you can give them
meaningful names but generally named as `[main]`.

All properties within a section are in the format `key = value`.

The characters `#` and `;` can be used for comments. A line is totally ignored if the
very first non-space character is a comment one, but it is possible to put a comment at the end of a line.
The `Bool` data type supports the following values: `on`, `yes`, `1`, `true`, `off`, `no`, `0` and `false`.

See a more complete [sample](./etc/pgagroal_vault.conf) configuration for running `pgagroal-vault` on `localhost`.

## [pgagroal-vault]

This section is mandatory and the pooler will refuse to start if the configuration file does not specify one and only one. Usually this section is place on top of the configuration file, but its position within the file does not really matter.
The available keys and their accepted values are reported in the table below.

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The bind address for pgagroal-vault |
| port | | Int | Yes | The bind port for pgagroal-vault |
| metrics | 0 | Int | No | The metrics port (disable = 0) |
| metrics_cache_max_age | 0 | String | No | The amount of time to keep a Prometheus (metrics) response in cache. If this value is specified without units, it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks. (disable = 0) |
| metrics_cache_max_size | 256k | String | No | The maximum amount of data to keep in cache when serving Prometheus responses. Changes require restart. This parameter determines the size of memory allocated for the cache even if `metrics_cache_max_age` or `metrics` are disabled. Its value, however, is taken into account only if `metrics_cache_max_age` is set to a non-zero value. Supports suffixes: 'B' (bytes), the default if omitted, 'K' or 'KB' (kilobytes), 'M' or 'MB' (megabytes), 'G' or 'GB' (gigabytes).|
| authentication_timeout | 5 | String | No | The amount of time the process will wait for valid credentials. If this value is specified without units, it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks. |
| log_type | console | String | No | The logging type (console, file, syslog) |
| log_level | info | String | No | The logging level, any of the (case insensitive) strings `FATAL`, `ERROR`, `WARN`, `INFO` and `DEBUG` (that can be more specific as `DEBUG1` thru `DEBUG5`). Debug level greater than 5 will be set to `DEBUG5`. Not recognized values will make the log_level be `INFO` |
| log_path | pgagroal.log | String | No | The log file location. Can be a strftime(3) compatible string. |
| log_rotation_age | 0 | String | No | The amount of time after which log file rotation is triggered. If this value is specified without units, it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks. (disable = 0) |
| log_rotation_size | 0 | String | No | The size of the log file that will trigger a log rotation. Supports suffixes: 'B' (bytes), the default if omitted, 'K' or 'KB' (kilobytes), 'M' or 'MB' (megabytes), 'G' or 'GB' (gigabytes). A value of `0` (with or without suffix) disables. |
| log_line_prefix | %Y-%m-%d %H:%M:%S | String | No | A strftime(3) compatible string to use as prefix for every log line. Must be quoted if contains spaces. |
| log_mode | append | String | No | Append to or create the log file (append, create) |
| log_connections | `off` | Bool | No | Log connects |
| log_disconnections | `off` | Bool | No | Log disconnects |
| ev_backend | auto | String | No | The event backend to use for the vault's I/O operations (`auto`, `io_uring`, `epoll`, `kqueue`). Default is `auto`, which selects the best available backend for the platform. On Linux, the preference order is: io_uring, epoll. On other platforms, kqueue is used if available. |
| hugepage | `try` | String | No | Huge page support (`off`, `try`, `on`) |
| tls | `off` | Bool | No | Enable Transport Layer Security (TLS) |
| tls_cert_file | | String | No | Certificate file for TLS. This file must be owned by either the user running pgagroal or root. |
| tls_key_file | | String | No | Private key file for TLS. This file must be owned by either the user running pgagroal or root. Additionally permissions must be at least `0640` when owned by root or `0600` otherwise. |
| tls_ca_file | | String | No | Certificate Authority (CA) file for TLS. This file must be owned by either the user running pgagroal or root.  |
| tls_cert_auth_mode | verify-ca | String | No | Certificate authentication mode when `tls_ca_file` is set. `verify-ca` verifies only the CA signature. `verify-full` additionally verifies that the certificate's CN or SAN matches the username. |

### Certificate Identity Extraction

When using `verify-full` mode, the vault extracts the username from the client certificate using this priority order:

1. **Subject Alternative Name (SAN)** - If present, checks DNS, Email, then URI types in order. Uses the first valid value found and **stops immediately** (CN is never checked).
2. **Common Name (CN)** - Only checked if the certificate has no SAN extension at all, or all SAN values are invalid.

**Important:** 
- CN is **completely ignored** if any valid SAN value exists in the certificate
- Identity matching is case-sensitive
- Once any valid identity is found, the search stops

**Recommendation:** For simplicity, use `CN=username` with no SAN entries, or a SAN entry with a single DNS value.

## [main]

The section with a name different from `pgagroal-vault` will be treated as a main section.

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The address of the pgagroal running the management server |
| port | | Int | Yes | The management port of pgagroal |
| user | | String | Yes | The admin user of the pgagroal remote management service | 

Note: For `pgagroal-vault` to function and connect properly to pgagroal, the remote server for management of the [**pgagroal**](https://github.com/pgagroal/pgagroal) should be enabled i.e. `management` should be greater than 0.

# Enable SSL connection with management port

The SSL handshake has to be initiated between `vault` and `remote` of `pgagroal` to enable secured SSL connection between the both. 

The `vault` serves as the client, while the `remote` functions as the server. The `vault` initiates the SSL/TLS handshake with the `remote`, and concurrently, the `remote` accepts the SSL/TLS request from the `vault`.

## SSL configuration at the server side

Update the `[pgagroal]` section in main configuration file by including the following:-

- Enable management port
- Enable `tls` to `on`
- Add `tls_cert_file`, `tls_key_file` and `tls_ca_file` fields

```
management = 2347
tls = on
tls_cert_file = /path/to/server_cert_file
tls_key_file = /path/to/server_key_file
tls_ca_file = /path/to/CA_root_cert_file
```

## SSL configuration at the client side

For client side authentication, add the required certificates like the certificate of `vault` in `pgagroal.crt`, private key of `vault` in `pgagroal.key` and the root certificate of CA in `root.crt` in the `.pgagroal` directory.

Some permissions requirements:-

- The certificate file `pgagroal.crt` must be owned by either the user running pgagroal or root.
- The key file `pgagroal.key` must be owned by either the user running pgagroal or root. Additionally permissions must be at least `0640` when owned by root or `0600` otherwise.
- The Certificate Authority (CA) file `root.crt` must be owned by either the user running pgagroal or root.
