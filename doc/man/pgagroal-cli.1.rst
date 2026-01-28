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

-v, --verbose
  Output text string of result

-V, --version
  Display version information

-?, --help
  Display help

COMMANDS
========

flush [mode] [database]
  Flush connections according to [mode].
  Allowed modes are:

    - 'gracefully' (default) to flush all connections gracefully
    - 'idle' to flush only idle connections
    - 'all' to flush all connections. USE WITH CAUTION!

  If no [database] name is specified, applies to all databases.

ping
  Verifies if pgagroal is up and running and checks connectivity to configured PostgreSQL servers

enable [database]
  Enable the specified database, or all databases if not specified

disable [database]
  Disable the specified database, or all databases if not specified

shutdown [mode]
  Stops pgagroal pooler. The [mode] can be:
    - 'gracefully' (default): waits for active connections to quit
    - 'immediate': forces connections to close and terminate
    - 'cancel': avoid a previously issued 'shutdown gracefully'

status [details]
  Status of pgagroal, with optional details

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
