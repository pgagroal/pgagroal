===============
pgagroal-config
===============

----------------------------------
Configuration utility for pgagroal
----------------------------------

:Manual section: 1

SYNOPSIS
========

pgagroal-config [ -o OUTPUT_FILE ] [ -q ] [ -F ] [ -V ] [ -? ] [ COMMAND ]

DESCRIPTION
===========

pgagroal-config is a command line utility for generating and managing pgagroal configurations.

OPTIONS
=======

-o, --output OUTPUT_FILE
  Set the output file path. Default is ./pgagroal.conf.

-q, --quiet
  In init mode, generate a configuration file with default values without prompting the user.

-F, --force
  Force overwriting the output file if it already exists.

-V, --version
  Display version information.

-?, --help
  Display help.

COMMANDS
========

init
  Initialize a new configuration file. The utility will prompt the user for various configuration options unless the --quiet flag is used.

get <FILE> <SECTION> <KEY>
  Get the value of a configuration key from a specific section in the configuration file.

set <FILE> <SECTION> <KEY> <VALUE>
  Set or update the value of a configuration key in a specific section. If the section or key does not exist, they will be created.

del <FILE> <SECTION> [KEY]
  Delete a configuration key or an entire section.

ls <FILE> [SECTION]
  List all sections in the configuration file, or list all keys in a specific section.

EXAMPLES
========

Generate a new configuration file:

  pgagroal-config -o pgagroal.conf init

Get a configuration value:

  pgagroal-config get pgagroal.conf pgagroal port

Set a configuration value:

  pgagroal-config set pgagroal.conf pgagroal max_connections 200

List all sections:

  pgagroal-config ls pgagroal.conf

REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/pgagroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal(1), pgagroal-cli(1), pgagroal-admin(1), pgagroal.conf(5)
