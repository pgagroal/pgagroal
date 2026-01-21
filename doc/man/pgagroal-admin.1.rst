==============
pgagroal-admin
==============

-----------------------------------
Administration utility for pgagroal
-----------------------------------

:Manual section: 1

SYNOPSIS
========

pgagroal-admin [ -f FILE ] [ COMMAND ]

DESCRIPTION
===========

pgagroal-admin is an administration utility for pgagroal.

OPTIONS
=======

-f, --file FILE
  Set the path to a user file

-U, --user USER
  Set the user name

-P, --password PASSWORD
  Set the password for the user

-g, --generate
  Generate a password

-l, --length
  Password length

-V, --version
  Display version information

-?, --help
  Display help

COMMANDS
========

master-key
  Create or update the master key. The master key will be created in the pgagroal user home directory under ~/.pgagroal

user add
  Add a user

user edit
  Update a user

user del
  Delete a user

user ls
  List all users

ENVIRONMENT VARIABLES
=====================

PGAGROAL_PASSWORD
  Provide either a key for use with the `master-key` command, or a user password for use with the `user add` or `user edit` commands.
  If provided, `pgagroal-admin` will not ask for the key/password interactively.
  Note that a password provided using the `--password` command line argument will have precedence over this variable.

REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/pgagroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal.conf(5), pgagroal_hba.conf(5), pgagroal_databases.conf(5), pgagroal_vault.conf(5), pgagroal(1), pgagroal-cli(1), pgagroal-vault(1)
