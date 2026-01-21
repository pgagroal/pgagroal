========
pgagroal
========

-----------------------------------------------
High-performance connection pool for PostgreSQL
-----------------------------------------------

:Manual section: 1

SYNOPSIS
========

pgagroal [ -c CONFIG_FILE ] [ -a HBA_FILE ] [ -d ]

DESCRIPTION
===========

pgagroal is a high-performance protocol-native connection pool for PostgreSQL.

OPTIONS
=======

-c, --config CONFIG_FILE
  Set the path to the pgagroal.conf file

-a, --hba HBA_FILE
  Set the path to the pgagroal_hba.conf file

-l, --limit LIMIT_FILE
  Set the path to the pgagroal_databases.conf file

-u, --users USERS_FILE
  Set the path to the pgagroal_users.conf file

-A, --admins ADMINS_FILE
  Set the path to the pgagroal_admins.conf file

-S, --superuser SUPERUSER_FILE
  Set the path to the pgagroal_superuser.conf file

-D, --directory DIRECTORY_PATH
  Set the path to load configuration files

-d, --daemon
  Run as a daemon

-V, --version
  Display version information

-?, --help
  Display help

REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/pgagroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal.conf(5), pgagroal_hba.conf(5), pgagroal_databases.conf(5), pgagroal_vault.conf(5), pgagroal-cli(1), pgagroal-admin(1), pgagroal-vault(1)
