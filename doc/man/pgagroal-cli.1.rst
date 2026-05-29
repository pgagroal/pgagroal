============
pgagroal-cli
============

---------------------------------
Command line utility for pgagroal
---------------------------------

:Manual section: 1

SYNOPSIS
========

pgagroal-cli [ -c CONFIG_FILE ] [ COMMAND ]

DESCRIPTION
===========

pgagroal-cli is a command line utility for pgagroal.

OPTIONS
=======

-c, --config CONFIG_FILE
  Set the path to the pgagroal.conf file

-h, --host HOST
  Set the host name

-p, --port PORT
  Set the port number

-U, --user USERNAME
  Set the user name

-P, --password PASSWORD
  Set the password

-L, --logfile FILE
  Set the logfile

-F, --format text|json|raw
  Set the output format

-C, --compress none|gz|zstd|lz4|bz2
  Compress the wire protocol

-E, --encrypt none|aes|aes256|aes192|aes128
  Encrypt the wire protocol

-T, --timeout DURATION
  Deadline for ``shutdown [gracefully]`` and ``flush [gracefully]``.
  ``DURATION`` is a non-negative number with an optional case-insensitive unit
  suffix: ``s`` (seconds, default), ``m`` (minutes), ``h`` (hours), ``d`` (days),
  ``w`` (weeks). Examples: ``30``, ``30s``, ``5m``, ``1h``, ``2d``, ``1w``.
  When omitted, falls back to ``flush_timeout`` from ``pgagroal.conf`` (built-in
  default of ``60s`` applies when ``flush_timeout`` is not set). ``--timeout 0``
  explicitly disables the timer (operation runs unbounded), same meaning as
  ``flush_timeout = 0`` in ``pgagroal.conf``.

-v, --verbose
  Output text string of result

-V, --version
  Display version information

-?, --help
  Display help

COMMANDS
========

flush [mode] [database]
  Flush connections. The [mode] can be:
    - 'gracefully' (default): flush all connections gracefully
    - 'idle': flush only idle connections
    - 'all': flush all connections (USE WITH CAUTION!)

  If no [database] is specified, applies to all databases. A graceful flush can
  be bounded with ``-T, --timeout DURATION``; on expiry remaining marked
  connections are forcibly terminated (equivalent to ``flush all`` for the
  targeted database). If no client currently holds a connection, the flush
  completes immediately and no timer is armed. ``--timeout`` is silently
  ignored for ``idle`` and ``all``.

ping
  Verifies if pgagroal is up and running and checks connectivity to configured PostgreSQL servers.
  With JSON output, the response includes per-server Host, Port, Status, Primary, and **Behind** on standbys.

enable [database]
  Enable the specified database, or all databases if not specified

disable [database]
  Disable the specified database, or all databases if not specified

shutdown [mode]
  Stops pgagroal pooler. The [mode] can be:
    - 'gracefully' (default): waits for active connections to quit
    - 'immediate': forces connections to close and terminate
    - 'cancel': avoid a previously issued 'shutdown gracefully'

  A graceful shutdown can be bounded with ``-T, --timeout DURATION``; on expiry
  pgagroal forces an immediate shutdown. If no client currently holds a
  connection, pgagroal shuts down immediately and no timer is armed.
  ``--timeout`` is silently ignored for ``immediate`` and ``cancel``.

status [details]
  Status of pgagroal, with optional details. Per-server fields match **ping**. **details** adds limits and per-connection rows.

switch-to <server>
  Switches to the specified primary server

conf <action>
  Manages the configuration. <action> can be:
    - 'reload': issue a configuration reload
    - 'ls': list the configuration files used
    - 'get': obtain information about a runtime configuration value
      Usage: conf get <parameter_name>
    - 'set': modify a configuration value
      Usage: conf set <parameter_name> <parameter_value>

clear <what>
  Resets either the Prometheus statistics or the specified server.
  <what> can be:
  
    - 'server' (default) followed by a server name
    - a server name on its own
    - 'prometheus' to reset the Prometheus metrics

REPORTING BUGS
==============

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal.conf(5), pgagroal_hba.conf(5), pgagroal_databases.conf(5), pgagroal_vault.conf(5), pgagroal(1), pgagroal-admin(1), pgagroal-vault(1)
