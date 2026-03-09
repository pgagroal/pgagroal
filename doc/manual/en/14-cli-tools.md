\newpage

# Command Line Tools

This chapter provides comprehensive reference for pgagroal's command-line utilities.

## pgagroal-cli

`pgagroal-cli` is a command line interface to interact with [**pgagroal**][pgagroal].
The executable accepts a set of options, as well as a command to execute.
If no command is provided, the program will show the help screen.

The `pgagroal-cli` utility has the following synopsis:

```
pgagroal-cli [ OPTIONS ] [ COMMAND ]
```

### Options

Available options are the following ones:

```
-c, --config CONFIG_FILE Set the path to the pgagroal.conf file
-h, --host HOST          Set the host name
-p, --port PORT          Set the port number
-U, --user USERNAME      Set the user name
-P, --password PASSWORD  Set the password
-L, --logfile FILE       Set the log file
-F, --format text|json   Set the output format
-v, --verbose            Output text string of result
-V, --version            Display version information
-?, --help               Display help
```

Options can be specified either in short or long form, in any position of the command line.

By default the command output, if any, is reported as text. It is possible to specify JSON as the output format,
and this is the suggested format if there is the need to automatically parse the command output, since the text format
could be subject to changes in future releases.

### Commands

#### flush
The `flush` command performs a connection flushing.
It accepts a *mode* to operate the actual flushing:
- `gracefully` (the default if not specified), flush connections when possible;
- `idle` to flush only connections in state *idle*;
- `all` to flush all the connections (**use with caution!**).

The command accepts a database name, that if provided, restricts the scope of
`flush` only to connections related to such database.
If no database is provided, the `flush` command is operated against all databases.

Command:
```
pgagroal-cli flush [gracefully|idle|all] [*|<database>]
```

Examples:
```
pgagroal-cli flush           # pgagroal-cli flush gracefully '*'
pgagroal-cli flush idle      # pgagroal-cli flush idle '*'
pgagroal-cli flush all       # pgagroal-cli flush all '*'
pgagroal-cli flush pgbench   # pgagroal-cli flush gracefully pgbench
```

#### ping
The `ping` command checks if [**pgagroal**][pgagroal] is running.
In case of success, the command does not print anything on the standard output unless the `--verbose` flag is used.

Command:
```
pgagroal-cli ping
```

Example:
```
pgagroal-cli ping --verbose  # pgagroal-cli: Success (0)
pgagroal-cli ping            # $? = 0
```

#### enable
Enables a database (or all databases).

Command:
```
pgagroal-cli enable [<database>|*]
```

Example:
```
pgagroal-cli enable
```

#### disable
Disables a database (or all databases).

Command:
```
pgagroal-cli disable [<database>|*]
```

Example:
```
pgagroal-cli disable
```

#### status
The `status` command reports the current status of the [**pgagroal**][pgagroal] pooler.
Without any subcommand, `status` reports back a short set of information about the pooler.

Command:
```
pgagroal-cli status [details]
```

With the `details` subcommand, a more verbose output is printed with a detail about every connection.

Example:
```
pgagroal-cli status details
```

#### switch-to
Switch to another primary server.

Command:
```
pgagroal-cli switch-to <server>
```

Example:
```
pgagroal-cli switch-to replica
```

#### shutdown
The `shutdown` command is used to stop the connection pooler.
It supports the following operating modes:
- `gracefully` (the default) closes the pooler as soon as no active connections are running;
- `immediate` forces an immediate stop.

If the `gracefully` mode is requested, chances are the system will take some time to perform the effective shutdown, and therefore it is possible to abort the request issuing another `shutdown` command with the mode `cancel`.

Command:
```
pgagroal-cli shutdown [gracefully|immediate|cancel]
```

Examples:
```
pgagroal-cli shutdown   # pgagroal-cli shutdown gracefully
...
pgagroal-cli shutdown cancel  # stops the above command
```

#### conf
Manages the configuration of the running instance.
This command requires one subcommand, that can be:
- `reload` issue a reload of the configuration, applying at runtime any changes from the configuration files;
- `get` provides a configuration parameter value;
- `set` modifies a configuration parameter at runtime;
- `ls` prints where the configuration files are located;
- `alias` shows all databases with their aliases and usernames.

Command:
```
pgagroal-cli conf <what>
```

Examples:
```
pgagroal-cli conf reload
pgagroal-cli conf get max_connections
pgagroal-cli conf set max_connections 25
```

##### conf get

The `conf get` command retrieves the current value of a configuration parameter from the running instance.

**Syntax:**
```
pgagroal-cli conf get <parameter_name> [--verbose] [--format json]
```

**Parameter Name Format:**
The parameter name can be specified in several forms using dot notation:
- `key`: Refers to a global configuration parameter (e.g., `log_level`)
- `section.key`: Refers to a parameter within a named section (e.g., `server.venkman.port`)
- `section.context.key`: Refers to a parameter within a context, such as a limit or HBA entry (e.g., `limit.pgbench.max_size`, `hba.myuser.method`)

**Supported Namespaces:**
- `pgagroal` (optional): Main configuration namespace, e.g., `pgagroal.log_level` or simply `log_level`
- `server`: Refers to a specific server, e.g., `server.venkman.port`
- `limit`: Refers to a specific limit entry, e.g., `limit.pgbench.max_size`
- `hba`: Refers to a specific HBA entry, e.g., `hba.myuser.method`

**Context Rules:**
- For `limit`, the context is the database name as found in `pgagroal_databases.conf`.
- For `hba`, the context is the username as found in `pgagroal_hba.conf`.
- For `server`, the context is the server name as found in `pgagroal.conf`.
- For `pgagroal`, the context must be empty.

> **Important:**  
> If there are multiple entries with the same database name in the `limit` section or the same username in the `hba` section, **the last matching entry in the configuration file is used** for `conf get`.

**Examples:**
```
pgagroal-cli conf get pipeline
performance

pgagroal-cli conf get limit.pgbench.max_size
2

pgagroal-cli conf get server.venkman.primary
off

pgagroal-cli conf get hba.myuser.method
scram-sha-256
```

**Verbose Output:**
If the `--verbose` option is specified, the output is more descriptive:
```
pgagroal-cli conf get max_connections --verbose
max_connections = 4
Success (0)
```

**Full Configuration Output:**

If you run the `conf get` command without specifying any parameter name, the command will return the complete configuration, including all main settings, servers, limits, and HBA entries.

```
pgagroal-cli conf get
```

You can also retrieve a specific section by specifying only the section name:

- To get all limit entries:
  ```
  pgagroal-cli conf get limit
  ```
- To get all HBA entries:
  ```
  pgagroal-cli conf get hba
  ```
- To get all Server entries:
  ```
  pgagroal-cli conf get server
  ```

> **Important:**  
> When viewing the full configuration or a section (such as `limit` or `hba`), **only a single entry will be present for each database name in `limit` and for each username in `hba`**.  
> If your configuration file contains multiple entries with the same database name (in `limit`) or the same username (in `hba`), **only the last entry in the configuration file will be shown**.

##### conf set

The `conf set` command allows you to change a configuration parameter at run-time, if possible.

**Syntax:**
```
pgagroal-cli conf set <parameter_name> <parameter_value>
```

**Examples:**
```
pgagroal-cli conf set log_level debug
pgagroal-cli conf set server.venkman.port 6432
pgagroal-cli conf set limit.pgbench.max_size 2
```

The syntax for setting parameters is the same as for the `conf get` command. Parameters are organized into namespaces:
- `pgagroal` (optional): the main configuration namespace, e.g., `pgagroal.log_level` or simply `log_level`
- `server`: refers to a specific server, e.g., `server.venkman.port`
- `limit`: refers to a specific limit entry, e.g., `limit.pgbench.max_size`
- `hba`: refers to a specific HBA entry, e.g., `hba.myuser.method`

**Output:**

When executed, the `conf set` command returns a detailed result describing the outcome of the operation.

**Case 1: Successful Application**

If the parameter is set and applied to the running instance:

```
Configuration change applied successfully
   Parameter: log_level
   Old value: info
   New value: debug
   Status: Active (applied to running instance)
```

**Case 2: Restart Required**

If the parameter change requires a full service restart to take effect:

```
Configuration change requires manual restart
   Parameter: max_connections
   Current value: 40 (unchanged in running instance)
   Requested value: 100
   Status: Requires full service restart
```

**Case 3: Invalid Key or Syntax**

If the parameter name is invalid or the syntax is incorrect:

```
Configuration change failed
   Invalid key format: 'max_connectionz'
   Valid formats: 'key', 'section.key', or 'section.context.key'
```

> **Warning:**  
> When changing critical parameters such as the **main port**, **metrics port**, **management port**, or **unix_socket_dir**, you must choose values carefully.
>
> If you set a port or socket to a value that is already in use or unavailable, the reload will fail. In this case, the CLI may still report "success" because the configuration was accepted, but the server will **not** be listening on the new port or socket.
>
> **How to recover:**  
> If you accidentally set a port or socket to an unavailable value, simply use the `conf set` command again to set it to a valid, available value.
>
> **Multiple Entries in `limit` or `hba`:**  
> If your configuration contains multiple entries with the **same database name** in the `limit` section or the **same username** in the `hba` section, **the `conf set` command will apply the change to the first matching entry** (topmost in the configuration file).

##### conf ls

The command `conf ls` provides information about the location of the configuration files.

Example:
```
pgagroal-cli conf ls

Main Configuration file:   /etc/pgagroal/pgagroal.conf
HBA file:                  /etc/pgagroal/pgagroal_hba.conf
Limit file:                /etc/pgagroal/pgagroal_databases.conf
Frontend users file:       /etc/pgagroal/pgagroal_frontend_users.conf
Admins file:               /etc/pgagroal/pgagroal_admins.conf
Superuser file:
Users file:                /etc/pgagroal/pgagroal_users.conf
```

##### conf alias

The command `conf alias` shows all the databases in `pgagroal_databases.conf` along with their aliases and usernames.

```
pgagroal-cli conf alias

# DATABASE=ALIASES                           USER           MAX   INIT   MIN
#--------------------------------------------------------------------------
production_db=prod,main,primary             myuser            10    5    2
```

#### clear
Resets different parts of the pooler. It accepts an operational mode:
- `prometheus` resets the metrics provided without altering the pooler status;
- `server` resets the specified server status.

Command:
```
pgagroal-cli clear [prometheus|server <server>]
```

Examples:
```
pgagroal-cli clear spengler            # pgagroal-cli clear server spengler
pgagroal-cli clear prometheus
```

### Shell Completions

pgagroal provides shell completion support for both `pgagroal-cli` and `pgagroal-admin` commands in bash and zsh shells.

#### Installation

The shell completion scripts are located in the `contrib/shell_comp/` directory:
- `pgagroal_comp.bash` - Bash completion script
- `pgagroal_comp.zsh` - Zsh completion script

##### Bash Completion

For current session only:
```bash
source /path/to/pgagroal/contrib/shell_comp/pgagroal_comp.bash
```

For permanent installation, add to your `~/.bashrc`:
```bash
echo "source /path/to/pgagroal/contrib/shell_comp/pgagroal_comp.bash" >> ~/.bashrc
```

If pgagroal is installed via package manager:
```bash
source /usr/share/doc/pgagroal/shell_comp/pgagroal_comp.bash
```

##### Zsh Completion

For current session only:
```zsh
source /path/to/pgagroal/contrib/shell_comp/pgagroal_comp.zsh
```

For permanent installation, add to your `~/.zshrc`:
```zsh
echo "source /path/to/pgagroal/contrib/shell_comp/pgagroal_comp.zsh" >> ~/.zshrc
```

#### Usage

Once enabled, you can use tab completion with pgagroal commands:

**pgagroal-cli commands:**
```bash
pgagroal-cli <TAB>
```
Shows: `flush ping enable disable shutdown status switch-to conf clear`

**pgagroal-cli subcommands:**
```bash
pgagroal-cli flush <TAB>
```
Shows: `gracefully idle all`

**pgagroal-admin commands:**
```bash
pgagroal-admin <TAB>
```
Shows available admin commands

The completion scripts provide intelligent suggestions for:
- Available commands and subcommands
- Command options and flags
- Database names (where applicable)

### JSON Output Format

It is possible to obtain the output of a command in a JSON format by specifying the `-F` (`--format`) option on the command line.
Supported output formats are:
- `text` (the default)
- `json`

As an example, the following are invocations of commands with different output formats:

```
pgagroal-cli status     # defaults to text output format

pgagroal-cli status --format text  # same as above
pgagroal-cli status -F text        # same as above

pgagroal-cli status --format json  # outputs as JSON text
pgagroal-cli status -F json        # same as above
```

Whenever a command produces output, the latter can be obtained in a JSON format.
Every command output consists of an object that contains two other objects:
- a `command` object, with all the details about the command and its output;
- an `application` object, with all the details about the executable that launched the command (e.g., `pgagroal-cli`).

#### The `application` object

The `application` object is made by the following attributes:
- `name` a string representing the name of the executable that launched the command;
- `version` a string representing the version of the executable;
- `major`, `minor`, `patch` are integers representing every single part of the version of the application.

As an example, when `pgagroal-cli` launches a command, the output includes an `application` object like the following:

```json
"application": {
    "name": "pgagroal-cli",
    "major": 1,
    "minor": 6,
    "patch": 0,
    "version": "1.6.0"
}
```

#### The `command` object

The `command` object represents the launched command and contains also the answer from [**pgagroal**][pgagroal].
The object is made by the following attributes:
- `name` a string representing the command launched (e.g., `status`);
- `status` a string that contains either "OK" or an error string if the command failed;
- `error` an integer value used as a flag to indicate if the command was in error or not, where `0` means success and `1` means error;
- `exit-status` an integer that contains zero if the command run successfully, another value depending on the specific command in case of failure;
- `output` an object that contains the details of the executed command.

The `output` object is *the variable part* in the JSON command output, that means its effective content depends on the launched command.

Whenever the command output includes an array of stuff, for example a connection list, such array is wrapped into a `list` JSON array with a sibling named `count` that contains the integer size of the array (number of elements).

#### JSON Examples

The following are a few examples of commands that provide output in JSON:

```json
pgagroal-cli ping --format json
{
    "command": {
        "name": "ping",
        "status": "OK",
        "error": 0,
        "exit-status": 0,
        "output": {
            "status": 1,
            "message": "running"
        }
    },
    "application": {
        "name": "pgagroal-cli",
        "major": 1,
        "minor": 6,
        "patch": 0,
        "version": "1.6.0"
    }
}
```

```json
pgagroal-cli status --format json
{
    "command": {
        "name": "status",
        "status": "OK",
        "error": 0,
        "exit-status": 0,
        "output": {
            "status": {
                "message": "Running",
                "status": 1
            },
            "connections": {
                "active": 0,
                "total": 2,
                "max": 15
            },
            "databases": {
                "disabled": {
                    "count": 0,
                    "state": "disabled",
                    "list": []
                }
            }
        }
    },
    "application": {
        "name": "pgagroal-cli",
        "major": 1,
        "minor": 6,
        "patch": 0,
        "version": "1.6.0"
    }
}
```

As an example, the following is the output of a faulty `conf set` command (note the `status`, `error` and `exit-status` values):

```json
pgagroal-cli conf set max_connections 1000 --format json
{
    "command": {
        "name": "conf set",
        "status": "Current and expected values are different",
        "error": true,
        "exit-status": 2,
        "output": {
            "key": "max_connections",
            "value": "15",
            "expected": "1000"
        }
    },
    "application": {
        "name": "pgagroal-cli",
        "major": 1,
        "minor": 6,
        "patch": 0,
        "version": "1.6.0"
    }
}
```

The `conf ls` command returns an array named `files` where each entry is made by a couple `description` and `path`, where the former is the mnemonic name of the configuration file, and the latter is the value of the configuration file used:

```json
pgagroal-cli conf ls --format json
{
    "command": {
        "name": "conf ls",
        "status": "OK",
        "error": 0,
        "exit-status": 0,
        "output": {
            "files": {
                "list": [{
                    "description": "Main Configuration file",
                    "path": "/etc/pgagroal/pgagroal.conf"
                }, {
                    "description": "HBA File",
                    "path": "/etc/pgagroal/pgagroal_hba.conf"
                }, {
                    "description": "Limit file",
                    "path": "/etc/pgagroal/pgagroal_databases.conf"
                }, {
                    "description": "Frontend users file",
                    "path": "/etc/pgagroal/pgagroal_frontend_users.conf"
                }, {
                    "description": "Admins file",
                    "path": "/etc/pgagroal/pgagroal_admins.conf"
                }, {
                    "description": "Superuser file",
                    "path": ""
                }, {
                    "description": "Users file",
                    "path": "/etc/pgagroal/pgagroal_users.conf"
                }]
            }
        }
    },
    "application": {
        "name": "pgagroal-cli",
        "major": 1,
        "minor": 6,
        "patch": 0,
        "version": "1.6.0"
    }
}
```

## pgagroal-admin

`pgagroal-admin` is a command line interface to manage users known
to the [**pgagroal**][pgagroal] connection pooler.
The executable accepts a set of options, as well as a command to execute.
If no command is provided, the program will show the help screen.

The `pgagroal-admin` utility has the following synopsis:

```
pgagroal-admin [ OPTIONS ] [ COMMAND ]
```

### Options

Available options are the following ones:

```
  -f, --file FILE         Set the path to a user file
  -U, --user USER         Set the user name
  -P, --password PASSWORD Set the password for the user
  -g, --generate          Generate a password
  -l, --length            Password length
  -V, --version           Display version information
  -?, --help              Display help
```

Options can be specified either in short or long form, in any position of the command line.

The `-f` option is mandatory for every operation that involves user management. If no
user file is specified, `pgagroal-admin` will silently use the default one (`pgagroal_users.conf`).

The password can be passed using the environment variable `PGAGROAL_PASSWORD` instead of `-P`, however the command line argument will have precedence.

### Commands

#### master-key
Create or update the master key for all users.

Command:
```
pgagroal-admin master-key
```

#### user
The `user` command allows the management of the users known to the connection pooler.
The command accepts the following subcommands:
- `add` to add a new user to the system;
- `del` to remove an existing user from the system;
- `edit` to change the credentials of an existing user;
- `ls` to list all known users within the system.

The command will edit the `pgagroal_users.conf` file or any file specified by means of the `-f` option flag.

Unless the command is run with the `-U` and/or `-P` flags, the execution will be interactive.

Command:
```
pgagroal-admin user <subcommand>
```

Examples:
```
pgagroal-admin user add -U simon -P secret
pgagroal-admin user del -U simon
pgagroal-admin -f pgagroal_users.conf user add
pgagroal-admin -f pgagroal_users.conf user ls
pgagroal-admin -f pgagroal_users.conf user del -U myuser
```

### Deprecated Commands

The following commands have been deprecated and will be removed in later releases of [**pgagroal**][pgagroal].
For each command, this is the corresponding current mapping to the working command:

- `add-user` is now `user add`;
- `remove-user` is now `user del`;
- `update-user` is now `user edit`;
- `list-users` is now `user ls`.

Whenever you use a deprecated command, the `pgagroal-admin` will print on standard error a warning message.
If you don't want to get any warning about deprecated commands, you can redirect the `stderr` to `/dev/null` or any other location with:

```
pgagroal-admin user-add -U luca -P strongPassword 2>/dev/null
```
