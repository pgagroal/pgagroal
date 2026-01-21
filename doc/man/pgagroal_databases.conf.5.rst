=======================
pgagroal_databases.conf
=======================

------------------------------------------------
Limits for databases, users or both for pgagroal
------------------------------------------------

:Manual section: 5

DESCRIPTION
===========

The pgagroal_databases.conf configuration file defines limits for a database or a user or both. It also supports database aliases, allowing clients to connect using alternative names for configured databases.

FORMAT
======

DATABASE
  Specifies the database for the rule. Either specific name or all for all databases. 
  Can include aliases using the format: database_name=alias1,alias2,alias3

USER
  Specifies the user for the rule. Either specific name or all for all users

MAX_SIZE
  Specifies the maximum pool size for the entry. all for all connections

INITIAL_SIZE
  Specifies the initial pool size for the entry. Default is 0. Requires a pgagroal_users.conf configuration

MIN_SIZE
  Specifies the minimum pool size for the entry. Default is 0. Requires a pgagroal_users.conf configuration

DATABASE ALIASES
================

Database aliases allow clients to connect using alternative names for a configured database. This is useful for application migrations, multi-tenancy, or providing user-friendly names.

Aliases are specified in the DATABASE field using the format: database_name=alias1,alias2,alias3

ALIAS RULES:
  - Maximum 8 aliases per database
  - Aliases must be globally unique across all database entries
  - Alias names cannot conflict with any real database name
  - Database names cannot conflict with any alias name
  - The special database name "all" cannot have aliases
  - Empty alias names are not allowed

When a client connects using an alias, pgagroal transparently maps it to the real database name for all backend operations.

EXAMPLE
=======

::
   
  #
  # DATABASE [=alias, alias, ...]     USER         MAX_SIZE  INITIAL_SIZE  MIN_SIZE
  #
  all                                  all            all
  production_db=prod,main              myuser         10       5            2
  development_db=dev,test,qa           devuser        5        2            1
  legacy_db                            legacyuser     8        3            1

In this example:
- Clients can connect to "production_db", "prod", or "main" - all will use the same backend database
- The development database has three aliases: "dev", "test", and "qa"
- The legacy database has no aliases and uses the standard format

REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/pgagroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal.conf(5), pgagroal_hba.conf(5), pgagroal_vault.conf(5), pgagroal(1), pgagroal-cli(1), pgagroal-admin(1), pgagroal-vault(1)