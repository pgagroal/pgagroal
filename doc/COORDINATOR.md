# pgagroal-coordinator configuration

`pgagroal-coordinator` is an optional daemon that fronts a number of `pgagroal` instances and routes the client
traffic to the instance that serves the primary. A `pgagroal` installation that does not use the coordinator is
not affected by it.

The configuration which is mandatory is loaded from either the path specified by the `-c` flag or
`/etc/pgagroal/pgagroal-coordinator.conf`.

The configuration of `pgagroal-coordinator` is split into sections using the `[` and `]` characters.

The pgagroal-coordinator section, called `[pgagroal-coordinator]`, is where you configure the overall properties
of the coordinator.

The other sections describe the `pgagroal` instances that are fronted by the coordinator. These sections don't
have any requirements to their naming so you can give them meaningful names, but generally named as `[node1]`,
`[node2]` and so on. At most 8 node sections are supported.

All properties within a section are in the format `key = value`.

The characters `#` and `;` can be used for comments. A line is totally ignored if the
very first non-space character is a comment one, but it is possible to put a comment at the end of a line.
The `Bool` data type supports the following values: `on`, `yes`, `1`, `true`, `off`, `no`, `0` and `false`.

See a more complete [sample](./etc/pgagroal-coordinator.conf) configuration for running `pgagroal-coordinator`.

## [pgagroal-coordinator]

This section is mandatory and the coordinator will refuse to start if the configuration file does not specify one
and only one.

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The bind address for pgagroal-coordinator |
| port | | Int | Yes | The bind port for pgagroal-coordinator, where the clients connect |
| management | | Int | Yes | The remote management port, where pgagroal-cli connects. Must be different from `port` |
| user | | String | No | The users file entry presented to the management port of the nodes. Used by every node that does not define its own `user` |
| backlog | 16 | Int | No | The backlog for listen(). Minimum `16` |
| ev_backend | auto | String | No | Select the event backend: `auto`, `io_uring`, `epoll` or `kqueue` |
| tls | `off` | Bool | No | Enable Transport Layer Security (TLS) |
| tls_cert_file | | String | No | Certificate file for TLS |
| tls_key_file | | String | No | Private key file for TLS |
| tls_ca_file | | String | No | Certificate Authority (CA) file for TLS |
| authentication_timeout | 5 | Int | No | The time in which a client must authenticate (in seconds) |
| log_type | console | String | No | The logging type (console, file, syslog) |
| log_level | info | String | No | The logging level, any of the (case insensitive) strings `FATAL`, `ERROR`, `WARN`, `INFO` and `DEBUG` (`DEBUG1` thru `DEBUG5`) |
| log_path | pgagroal-coordinator.log | String | No | The log file location |
| log_rotation_age | 0 | String | No | The time after which log file rotation is triggered |
| log_rotation_size | 0 | String | No | The size of the log file that will trigger a log rotation |
| log_line_prefix | %Y-%m-%d %H:%M:%S | String | No | A strftime(3) compatible string to use as prefix for every log line |
| log_connections | `off` | Bool | No | Log connects |
| log_disconnections | `off` | Bool | No | Log disconnects |
| log_mode | append | String | No | Append to or create the log file (append, create) |

## Node sections

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The address of the pgagroal instance |
| management | | Int | Yes | The remote management port of the pgagroal instance |
| user | | String | No | The users file entry presented to the management port of this node. Overrides the `user` of the main section |

The port of the data plane and the role of a node are **not** configured. They are discovered through the
management protocol when the node is probed, and they are reported by `pgagroal-cli status`.

## Users and admins

The coordinator uses two credential files, both created with `pgagroal-admin`.

The users file, given by `-u` and by default `/etc/pgagroal/pgagroal-coordinator-users.conf`, holds the accounts
that the coordinator presents to the management port of the nodes.

```
pgagroal-admin -f /etc/pgagroal/pgagroal-coordinator-users.conf -U admin -P admin1234 user add
```

The admins file, given by `-A` and by default `/etc/pgagroal/pgagroal-coordinator-admins.conf`, holds the accounts
that are allowed to administrate the coordinator through its management port.

```
pgagroal-admin -f /etc/pgagroal/pgagroal-coordinator-admins.conf -U admin -P admin1234 user add
```

## Requirements of the nodes

Each `pgagroal` instance that is fronted by the coordinator must

* define `management` in its `pgagroal.conf` file
* define `health_check_user` in its `pgagroal.conf` file, so that the role of its server can be probed through
  `pg_is_in_recovery()`. Without it the role is reported as unknown and the coordinator falls back to the
  `primary` setting of the node
* have the account of the coordinator in its `pgagroal_admins.conf` file
* accept that account on the `admin` database in its `pgagroal_hba.conf` file

```
host    admin    admin    10.0.0.10/32    scram-sha-256
```

## Administration

The coordinator speaks the same management protocol as `pgagroal`, so it is administrated with `pgagroal-cli`
against its management port. There are no coordinator specific commands, only a different endpoint.

| Command | Description |
|---------|-------------|
| `ping` | Verify that the coordinator is running |
| `status` | Report the nodes, their role and port, and the current primary |
| `status details` | Same as `status` |
| `switch-to <node>` | Make `<node>` the current primary |
| `shutdown` | Stop the coordinator |

```
pgagroal-cli -h coordinator-host -p 6433 -U admin -P admin1234 status
```

## Switchover

`switch-to` makes another node the current primary, runs the discovery again, and terminates the client
connections of the previous primary so that the clients reconnect to the new one.

It does **not** promote PostgreSQL. The replica has to be promoted first, and then the coordinator is told to
follow, so the order of a planned switchover is

1. promote the PostgreSQL server of the node
2. `pgagroal-cli ... switch-to <node>`
3. the clients reconnect and reach the new primary

## Limitations

* up to 8 nodes
* all the client traffic is routed to the primary, so the nodes that serve a replica are only the targets of a
  switchover and carry no traffic
* exactly one node may report being the primary. If more than one does, the coordinator does not choose between
  them and waits for a switchover
* no automatic failover, no health check
* the promotion of PostgreSQL is not performed by the coordinator
* one coordinator, so it is a single point of failure
