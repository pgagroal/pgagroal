\newpage

# Getting started

First of all, make sure that [**pgagroal**](https://github.com/pgagroal/pgagroal) is installed and in your path by
using `pgagroal -?`. You should see

```
pgagroal 2.0.1
  High-performance connection pool for PostgreSQL

Usage:
  pgagroal [ -c CONFIG_FILE ] [ -a HBA_FILE ] [ -d ]

Options:
  -c, --config CONFIG_FILE           Set the path to the pgagroal.conf file
  -a, --hba HBA_FILE                 Set the path to the pgagroal_hba.conf file
  -l, --limit LIMIT_FILE             Set the path to the pgagroal_databases.conf file
  -u, --users USERS_FILE             Set the path to the pgagroal_users.conf file
  -F, --frontend FRONTEND_USERS_FILE Set the path to the pgagroal_frontend_users.conf file
  -A, --admins ADMINS_FILE           Set the path to the pgagroal_admins.conf file
  -S, --superuser SUPERUSER_FILE     Set the path to the pgagroal_superuser.conf file
  -D, --directory DIRECTORY_PATH     Set the path to load configuration files
  -d, --daemon                       Run as a daemon
  -V, --version                      Display version information
  -?, --help                         Display help
```

If you don't have [**pgagroal**](https://github.com/pgagroal/pgagroal) in your path see [README](../README.md) on how to
compile and install [**pgagroal**](https://github.com/pgagroal/pgagroal) in your system.

## Configuration

Lets create a simple configuration file called `pgagroal.conf` with the content

```
[pgagroal]
host = *
port = 2345

log_type = file
log_level = info
log_path = /tmp/pgagroal.log

max_connections = 100
idle_timeout = 600
validation = off
unix_socket_dir = /tmp/

[primary]
host = localhost
port = 5432
```

In our main section called `[pgagroal]` we setup [**pgagroal**](https://github.com/pgagroal/pgagroal) to listen on all
network addresses on port 2345. Logging will be performed at `info` level and
put in a file called `/tmp/pgagroal.log`. We want a maximum of 100 connections
that are being closed if they have been idle for 10 minutes, and we also specify that
we don't want any connection validation to be performed. Last we specify the
location of the `unix_socket_dir` used for management operations.

Next we create a section called `[primary]` which has the information about our
[PostgreSQL](https://www.postgresql.org) instance. In this case it is running
on `localhost` on port `5432`.

Now we need a host based authentication (HBA) file. Create one called `pgagroal_hba.conf`
with the content

```
#
# TYPE  DATABASE USER  ADDRESS  METHOD
#
host    all      all   all      all
```

This tells [**pgagroal**](https://github.com/pgagroal/pgagroal) that it can accept connections from all network addresses
for all databases and all user names.

We are now ready to run [**pgagroal**](https://github.com/pgagroal/pgagroal).

See [Configuration](./CONFIGURATION.md) for all configuration options.

## Running

We will run [**pgagroal**](https://github.com/pgagroal/pgagroal) using the command

```
pgagroal -c pgagroal.conf -a pgagroal_hba.conf
```

If this doesn't give an error, then we are ready to connect.

We will assume that we have a user called `test` with the password `test` in our
[PostgreSQL](https://www.postgresql.org) instance. See their
[documentation](https://www.postgresql.org/docs/current/index.html) on how to setup
[PostgreSQL](https://www.postgresql.org), [add a user](https://www.postgresql.org/docs/current/app-createuser.html)
and [add a database](https://www.postgresql.org/docs/current/app-createdb.html).

We will connect to [**pgagroal**](https://github.com/pgagroal/pgagroal) using the [psql](https://www.postgresql.org/docs/current/app-psql.html)
application.

```
psql -h localhost -p 2345 -U test test
```

That should give you a password prompt where `test` should be typed in. You are now connected
to [PostgreSQL](https://www.postgresql.org) through [**pgagroal**](https://github.com/pgagroal/pgagroal).

Type `\q` to quit [psql](https://www.postgresql.org/docs/current/app-psql.html) and [**pgagroal**](https://github.com/pgagroal/pgagroal)
will now put the connection that you used into its pool.

If you type the above `psql` command again [**pgagroal**](https://github.com/pgagroal/pgagroal) will reuse the existing connection and
thereby lower the overhead of getting a connection to [PostgreSQL](https://www.postgresql.org).

Now you are ready to point your applications to use [**pgagroal**](https://github.com/pgagroal/pgagroal) instead of going directly to
[PostgreSQL](https://www.postgresql.org). [**pgagroal**](https://github.com/pgagroal/pgagroal) will work with any
[PostgreSQL](https://www.postgresql.org) compliant driver, for example [pgjdbc](https://jdbc.postgresql.org/),
[Npgsql](https://www.npgsql.org/) and [pq](https://github.com/lib/pq).

[**pgagroal**](https://github.com/pgagroal/pgagroal) is stopped by pressing Ctrl-C (`^C`) in the console where you started it, or by sending
the `SIGTERM` signal to the process using `kill <pid>`.

## Run-time administration

[**pgagroal**](https://github.com/pgagroal/pgagroal) has a run-time administration tool called `pgagroal-cli`.

You can see the commands it supports by using `pgagroal-cli -?` which will give

```
pgagroal-cli 2.0.1
  Command line utility for pgagroal

Usage:
  pgagroal-cli [ OPTIONS ] [ COMMAND ] 

Options:
  -c, --config CONFIG_FILE Set the path to the pgagroal.conf file
                           Default: /etc/pgagroal/pgagroal.conf
  -h, --host HOST          Set the host name
  -p, --port PORT          Set the port number
  -U, --user USERNAME      Set the user name
  -P, --password PASSWORD  Set the password
  -L, --logfile FILE       Set the log file
  -F, --format text|json   Set the output format
  -v, --verbose            Output text string of result
  -V, --version            Display version information
  -?, --help               Display help

Commands:
  flush [mode] [database]  Flush connections according to <mode>.
                           Allowed modes are:
                           - 'gracefully' (default) to flush all connections gracefully
                           - 'idle' to flush only idle connections
                           - 'all' to flush all connections. USE WITH CAUTION!
                           If no <database> name is specified, applies to all databases.
  ping                     Verifies if pgagroal is up and running
  enable   [database]      Enables the specified databases (or all databases)
  disable  [database]      Disables the specified databases (or all databases)
  shutdown [mode]          Stops pgagroal pooler. The <mode> can be:
                           - 'gracefully' (default) waits for active connections to quit
                           - 'immediate' forces connections to close and terminate
                           - 'cancel' avoid a previously issued 'shutdown gracefully'
  status [details]         Status of pgagroal, with optional details
  switch-to <server>       Switches to the specified primary server
  conf <action>            Manages the configuration (e.g., reloads the configuration
                           The subcommand <action> can be:
                           - 'reload' to issue a configuration reload;
                           - 'get' to obtain information about a runtime configuration value;
                                   conf get <parameter_name>
                           - 'set' to modify a configuration value;
                                   conf set <parameter_name> <parameter_value>;
                           - 'ls'  lists the configuration files used.
  clear <what>             Resets either the Prometheus statistics or the specified server.
                           <what> can be
                           - 'server' (default) followed by a server name
                           - a server name on its own
                           - 'prometheus' to reset the Prometheus metrics

pgagroal: <https://pgagroal.github.io/>
Report bugs: <https://github.com/pgagroal/pgagroal/issues>
```

This tool can be used on the machine running [**pgagroal**](https://github.com/pgagroal/pgagroal) to flush connections.

To flush all idle connections you would use

```
pgagroal-cli -c pgagroal.conf flush idle
```

To stop pgagroal you would use

```
pgagroal-cli -c pgagroal.conf stop
```

Check the outcome of the operations by verifying the exit code, like

```
echo $?
```

or by using the `-v` flag.

If pgagroal has both Transport Layer Security (TLS) and `management` enabled then `pgagroal-cli` can
connect with TLS using the files `~/.pgagroal/pgagroal.key` (must be 0600 permission),
`~/.pgagroal/pgagroal.crt` and `~/.pgagroal/root.crt`.

## Administration

[**pgagroal**](https://github.com/pgagroal/pgagroal) has an administration tool called `pgagroal-admin`, which is used to control user
registration with [**pgagroal**](https://github.com/pgagroal/pgagroal).

You can see the commands it supports by using `pgagroal-admin -?` which will give

```
pgagroal-admin 2.0.1
 Administration utility for pgagroal

Usage:
  pgagroal-admin [ -f FILE ] [ COMMAND ]

Options:
  -f, --file FILE         Set the path to a user file
                          Defaults to /etc/pgagroal/pgagroal_users.conf
  -U, --user USER         Set the user name
  -P, --password PASSWORD Set the password for the user
  -g, --generate          Generate a password
  -l, --length            Password length
  -V, --version           Display version information
  -?, --help              Display help

Commands:
  master-key              Create or update the master key
  user <subcommand>       Manage a specific user, where <subcommand> can be
                          - add  to add a new user
                          - del  to remove an existing user
                          - edit to change the password for an existing user
                          - ls   to list all available users

pgagroal: https://pgagroal.github.io/
Report bugs: https://github.com/pgagroal/pgagroal/issues
```

In order to set the master key for all users you can use

```
pgagroal-admin -g master-key
```

The master key must be at least 8 characters if provided interactively.

For scripted use, the master key can be provided using the `PGAGROAL_PASSWORD` environment variable.

Then use the other commands to add, update, remove or list the current user names, f.ex.

```
pgagroal-admin -f pgagroal_users.conf user add
```

For scripted use, the user password can be provided using the `PGAGROAL_PASSWORD` environment variable.

## Next Steps

Next steps in improving pgagroal's configuration could be

* Update `pgagroal.conf` with the required settings for your system
* Set the access rights in `pgagroal_hba.conf` for each user and database
* Add a `pgagroal_users.conf` file using `pgagroal-admin` with a list of known users
* Disable access for unknown users by setting `allow_unknown_users` to `false`
* Define a `pgagroal_databases.conf` file with the limits and prefill settings for each database
* Enable Transport Layer Security v1.2+ (TLS)
* Deploy Grafana dashboard

See [Configuration](./CONFIGURATION.md) for more information on these subjects.

Please, read the manual for a full description of all the features available.

## Contribute

The [pgagroal](https://github.com/pgagroal/pgagroal) community hopes that you find
the project interesting.

Feel free to

* [Ask a question](https://github.com/pgagroal/pgagroal/discussions)
* [Raise an issue](https://github.com/pgagroal/pgagroal/issues)
* [Submit a feature request](https://github.com/pgagroal/pgagroal/issues)
* [Write a code submission](https://github.com/pgagroal/pgagroal/pulls)

All contributions are most welcome !

Please, consult our [Code of Conduct](../CODE_OF_CONDUCT.md) policies for interacting in our
community.

Consider giving the project a [star](https://github.com/pgagroal/pgagroal/stargazers) on
[GitHub](https://github.com/pgagroal/pgagroal/) if you find it useful. And, feel free to follow
the project on [X](https://x.com/pgagroal/) as well.
