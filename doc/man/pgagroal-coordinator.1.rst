====================
pgagroal-coordinator
====================

----------------------------------------------------------------
Coordinator that fronts multiple pgagroal instances for failover
----------------------------------------------------------------

:Manual section: 1

SYNOPSIS
========

pgagroal-coordinator [ -c CONFIG_FILE ] [ -u USERS_FILE ] [ -A ADMINS_FILE ]

DESCRIPTION
===========

**pgagroal-coordinator** is an optional daemon that sits in front of a number of
[**pgagroal**][pgagroal] instances, each of them serving one PostgreSQL server, and routes
the client traffic to the instance that serves the primary.

At startup the coordinator connects to the management port of each node and asks for its
status. The reply carries the port of the data plane of the node and the role of its
server, so the topology does not have to be described in the configuration file. The node
whose server reports being the primary is elected, and every client connection is proxied
to it.

The election is refreshed when a ``switch-to`` is performed. There is no health check and
no automatic failover: the coordinator follows the decisions of the operator.

**pgagroal-coordinator** is optional, and a [**pgagroal**][pgagroal] installation that does
not use it is not affected by it.

**Note:** the management port of each node must be open, the user of the coordinator must be
present in the ``pgagroal_admins.conf`` file of each node, and the ``pgagroal_hba.conf`` file
of each node must accept that user on the ``admin`` database. The nodes should define
``health_check_user`` so that the role of their server can be probed.

OPTIONS
=======

-c, --config CONFIG_FILE
  Set the path to the pgagroal-coordinator.conf file

-u, --users USERS_FILE
  Set the path to the pgagroal-coordinator-users.conf file

-A, --admins ADMINS_FILE
  Set the path to the pgagroal-coordinator-admins.conf file

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

pgagroal.conf(5), pgagroal_hba.conf(5), pgagroal_databases.conf(5), pgagroal-coordinator.conf(5), pgagroal-cli(1), pgagroal-admin(1), pgagroal(1)
