=========================
pgagroal-coordinator.conf
=========================

------------------------------------------------
Main configuration file for pgagroal-coordinator
------------------------------------------------

:Manual section: 5

DESCRIPTION
===========

pgagroal-coordinator.conf is the main configuration file for pgagroal-coordinator.

The file is split into different sections specified by the ``[`` and ``]`` characters. The main section is called ``[pgagroal-coordinator]``.

Other sections (generally called the ``node`` sections) specify the pgagroal instances that are fronted by the coordinator.

All properties are in the format ``key = value``.

The characters ``#`` and ``;`` can be used for comments; must be the first character on the line.
The ``Bool`` data type supports the following values: ``on``, ``1``, ``true``, ``off``, ``0`` and ``false``.

OPTIONS
=======

The options for the pgagroal-coordinator section are

host
  The bind address for pgagroal-coordinator. Mandatory

port
  The bind port for pgagroal-coordinator, where the clients connect. Mandatory

management
  The remote management port, where pgagroal-cli connects. Mandatory, and it must
  be different from ``port``

user
  The entry of the users file that is presented to the management port of the nodes.
  It is used by every node that does not define its own ``user``

backlog
  The backlog for listen(). Minimum ``16``. Default is 16

ev_backend
  Select the event backend: ``auto``, ``io_uring``, ``epoll`` or ``kqueue``. Default is auto

tls
  Enable Transport Layer Security (TLS). Default is false

tls_cert_file
  Certificate file for TLS

tls_key_file
  Private key file for TLS

tls_ca_file
  Certificate Authority (CA) file for TLS

authentication_timeout
  The amount of time in which a client must authenticate. If this value is specified without units, it is taken
  as seconds. Default is 5

log_type
  The logging type (console, file, syslog). Default is console

log_level
  The logging level, any of the (case insensitive) strings ``FATAL``, ``ERROR``, ``WARN``, ``INFO`` and ``DEBUG``
  (that can be more specific as ``DEBUG1`` thru ``DEBUG5``). Debug level greater than 5 will be set to ``DEBUG5``.
  Not recognized values will make the ``log_level`` be ``INFO``. Default is info

log_path
  The log file location. Default is pgagroal-coordinator.log. Can be a strftime(3) compatible string

log_rotation_age
  The amount of time after which log file rotation is triggered. If this value is specified without units, it is
  taken as seconds. Default is 0 (disabled)

log_rotation_size
  The size of the log file that will trigger a log rotation. Supports suffixes: ``B`` (bytes), the default if
  omitted, ``K`` or ``KB`` (kilobytes), ``M`` or ``MB`` (megabytes), ``G`` or ``GB`` (gigabytes).
  Default is 0 (disabled)

log_line_prefix
  A strftime(3) compatible string to use as prefix for every log line. Must be quoted if contains spaces.
  Default is ``%Y-%m-%d %H:%M:%S``

log_connections
  Log connects. Default is off

log_disconnections
  Log disconnects. Default is off

log_mode
  Append to or create the log file (append, create). Default is append

The options for the node sections are

host
  The address of the pgagroal instance. Mandatory

management
  The remote management port of the pgagroal instance. Mandatory

user
  The entry of the users file that is presented to the management port of this node.
  Overrides the ``user`` of the main section

The port of the data plane and the role of a node are not configured. They are discovered
through the management protocol when the node is probed, and they are reported by
``pgagroal-cli status``.

At most 8 node sections are supported.

EXAMPLE
=======

::

  [pgagroal-coordinator]
  host = 0.0.0.0
  port = 6432
  management = 6433
  user = admin

  log_type = console
  log_level = info
  log_path =

  ev_backend = auto

  [node1]
  host = 10.0.0.11
  management = 2346

  [node2]
  host = 10.0.0.12
  management = 2346

REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/pgagroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal.conf(5), pgagroal_hba.conf(5), pgagroal_databases.conf(5), pgagroal-coordinator(1), pgagroal-cli(1), pgagroal-admin(1), pgagroal(1)
