# pgagroal-config

`pgagroal-config` is a command-line utility designed to simplify the generation and management of the `pgagroal.conf` configuration file. It provides an interactive way to initialize a configuration and a non-interactive way to modify existing configuration files.

## Initialization

To create a new `pgagroal.conf` file, use the `init` command:

Command:
```
pgagroal-config init
```

By default, this will create a file named `pgagroal.conf` in the current directory. You can specify a different output path using the `-o` or `--output` flag:

Example:
```
pgagroal-config -o /etc/pgagroal/pgagroal.conf init
```

### Quiet Mode

If you want to generate a configuration file with default values without any user interaction, use the `-q` or `--quiet` flag:

Example:
```
pgagroal-config -q init
```

### Overwriting

If the output file already exists, `pgagroal-config` will ask for confirmation before overwriting it. To force an overwrite, use the `-F` or `--force` flag:

Example:
```
pgagroal-config -F init
```

## Configuration Management

`pgagroal-config` can also be used to query and modify existing configuration files.

### Listing Sections

To list all sections in a configuration file:

Command:
```
pgagroal-config ls pgagroal.conf
```

### Listing Keys

To list all keys in a specific section:

Command:
```
pgagroal-config ls pgagroal.conf pgagroal
```

### Getting a Value

To retrieve the value of a specific configuration key:

Command:
```
pgagroal-config get pgagroal.conf pgagroal port
```

### Setting a Value

To set or update the value of a configuration key:

Command:
```
pgagroal-config set pgagroal.conf pgagroal max_connections 200
```

If the section or key does not exist, it will be created automatically.

### Deleting a Key

To delete a specific configuration key:

Command:
```
pgagroal-config del pgagroal.conf pgagroal ev_backend
```

### Deleting a Section

To delete an entire section:

Command:
```
pgagroal-config del pgagroal.conf primary
```

## File Permissions

`pgagroal-config` ensures that all created or modified configuration files have secure permissions (`0600`), meaning they are only readable and writable by the owner.

## Root User Check

For safety reasons, `pgagroal-config` cannot be run as the `root` user.
