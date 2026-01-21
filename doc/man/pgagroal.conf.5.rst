=============
pgagroal.conf
=============

------------------------------------
Main configuration file for pgagroal
------------------------------------

:Manual section: 5

DESCRIPTION
===========

pgagroal.conf is the main configuration file for pgagroal.

The file is split into different sections specified by the ``[`` and ``]`` characters. The main section is called ``[pgagroal]``.

Other sections specifies the PostgreSQL server configuration.

All properties are in the format ``key = value``.

The characters ``#`` and ``;`` can be used for comments; must be the first character on the line.
The ``Bool`` data type supports the following values: ``on``, ``1``, ``true``, ``off``, ``0`` and ``false``.

OPTIONS
=======

The options for the main section are

host
  The bind address for pgagroal. Mandatory

port
  The bind port for pgagroal. Mandatory

unix_socket_dir
  The Unix Domain Socket location. Mandatory

metrics
  The metrics port. Default is 0 (disabled)

metrics_cache_max_age
  The amount of time to keep a Prometheus (metrics) response in cache. If this value is specified without units,
  it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes,
  'H' for hours, 'D' for days, and 'W' for weeks.
  Default is 0 (disabled)

metrics_cache_max_size
  The maximum amount of data to keep in cache when serving Prometheus responses. Changes require restart.
  This parameter determines the size of memory allocated for the cache even if ``metrics_cache_max_age`` or
  ``metrics`` are disabled. Its value, however, is taken into account only if ``metrics_cache_max_age`` is set
  to a non-zero value. Supports suffixes: ``B`` (bytes), the default if omitted, ``K`` or ``KB`` (kilobytes),
  ``M`` or ``MB`` (megabytes), ``G`` or ``GB`` (gigabytes).
  Default is 256k

management
  The remote management port. Default is 0 (disabled)

log_type
  The logging type (console, file, syslog). Default is console

log_level
  The logging level, any of the (case insensitive) strings ``FATAL``, ``ERROR``, ``WARN``, ``INFO`` and ``DEBUG``
  (that can be more specific as ``DEBUG1`` thru ``DEBUG5``). Debug level greater than 5 will be set to ``DEBUG5``.
  Not recognized values will make the ``log_level`` be ``INFO``. Default is info

log_path
  The log file location. Default is pgagroal.log. Can be a strftime(3) compatible string

log_rotation_age
  The amount of time after which log file rotation is triggered. If this value is specified without units, it is taken as seconds.
  It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks.
  Default is 0 (disabled)

log_rotation_size
  The size of the log file that will trigger a log rotation. Supports suffixes: ``B`` (bytes), the default if omitted,
  ``K`` or ``KB`` (kilobytes), ``M`` or ``MB`` (megabytes), ``G`` or ``GB`` (gigabytes). A value of ``0`` (with or without suffix) disables.
  Default is 0

log_line_prefix
  A strftime(3) compatible string to use as prefix for every log line. Must be quoted if contains spaces.
  Default is ``%Y-%m-%d %H:%M:%S``

log_mode
  Append to or create the log file (append, create). Default is append

log_connections
  Log connects. Default is off

log_disconnections
  Log disconnects. Default is off

blocking_timeout
  The amount of time the process will be blocking for a connection. If this value is specified without units,
  it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes,
  'H' for hours, 'D' for days, and 'W' for weeks.
  (disable = 0) Default is 30

idle_timeout
  The amount of time a connection is kept alive. If this value is specified without units, it is taken as seconds.
  It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days,
  and 'W' for weeks. Default is 0 (disabled)

rotate_frontend_password_timeout 
  The amount of time after which the passwords of frontend users are updated periodically. If this value is specified without units,
  it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours,
  'D' for days, and 'W' for weeks.
  Default is 0 (disabled)

rotate_frontend_password_length 
  The length of randomized frontend passwords. Default is 8

max_connection_age
  The maximum amount of time that a connection will live. If this value is specified without units, it is taken as seconds.
  It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and
  'W' for weeks.
  Default is 0 (disabled)

validation
  Should connection validation be performed. Valid options: off, foreground and background. Default is off

background_interval
  The interval between background validation scans. If this value is specified without units, it is taken as seconds.
  It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days,
  and 'W' for weeks. Default is 300

max_retries
  The maximum number of iterations to obtain a connection. Default is 5

max_connections
  The maximum number of connections (max 1000). Default is 1000

allow_unknown_users
  Allow unknown users to connect. Default is true

authentication_timeout
  The amount of time the process will wait for valid credentials. If this value is specified without units,
  it is taken as seconds. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes,
  'H' for hours, 'D' for days, and 'W' for weeks. Default is 5

pipeline
  The pipeline type. Valid options are auto, performance, session and transaction. Default is auto

auth_query
  Enable authentication query. Default is false

failover
  Enable failover support. Default is false

failover_script
  The failover script

tls
  Enable Transport Layer Security (TLS). Default is false. Changes require restart in the server section.

tls_cert_file
  Certificate file for TLS. Changes require restart in the server section.

tls_key_file
  Private key file for TLS. Changes require restart in the server section.

tls_ca_file
  Certificate Authority (CA) file for TLS. Changes require restart in the server section.

metrics_cert_file
  Certificate file for TLS for Prometheus metrics

metrics_key_file
  Private key file for TLS for Prometheus metrics

metrics_ca_file
  Certificate Authority (CA) file for TLS for Prometheus metrics

ev_backend
  The event handling backend to use. Valid options are auto, io_uring, epoll, and kqueue. Default is auto

keep_alive
  Have SO_KEEPALIVE on sockets. Default is on

nodelay
  Have TCP_NODELAY on sockets. Default is on

backlog
  The backlog for listen(). Minimum 16. Default is max_connections / 4

hugepage
  Huge page support. Default is try

tracker
  Track connection lifecycle. Default is off

track_prepared_statements
  Track prepared statements (transaction pooling). Default is off

pidfile
  Path to the PID file. If omitted, automatically set to ``unix_socket_dir/pgagroal.port.pid``

update_process_title
  The behavior for updating the operating system process title, mainly related to connection processes.
  Allowed settings are: ``never`` (or ``off``), does not update the process title; ``strict`` to set the
  process title without overriding the existing initial process title length; ``minimal`` to set the process
  title to ``username/database``; ``verbose`` (or ``full``) to set the process title to ``user@host:port/database``.
  Please note that ``strict`` and ``minimal`` are honored only on those systems that do not provide a native way
  to set the process title (e.g., Linux). On other systems, there is no difference between ``strict`` and ``minimal``
  and the assumed behaviour is ``minimal`` even if ``strict`` is used. ``never`` and ``verbose`` are always honored,
  on every system. On Linux systems the process title is always trimmed to 255 characters, while on system that
  provide a natve way to set the process title it can be longer

Danger zone

disconnect_client
  Disconnect clients that have been idle for more than the specified seconds. This setting DOES NOT take long running transactions into account. Default is 0

disconnect_client_force
  Disconnect clients that have been active for more than the specified seconds. This setting DOES NOT take long running transactions into account. Default is off

The options for the PostgreSQL section are

host
  The address of the PostgreSQL instance. Mandatory

port
  The port of the PostgreSQL instance. Mandatory
  
primary
  Identify the instance as the primary instance (hint)

tls
  Enable Transport Layer Security (TLS) support (Experimental - no pooling). Default is off

REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/pgagroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal_hba.conf(5), pgagroal_databases.conf(5), pgagroal_vault.conf(5), pgagroal(1), pgagroal-cli(1), pgagroal-admin(1), pgagroal-vault(1)
