\newpage

# Database Aliases

## Overview

Database aliases are configured in the `pgagroal_databases.conf` file and allow multiple alternative names for a single database entry. When a client connects using an alias, [**pgagroal**][pgagroal] transparently maps it to the real database name for backend connections.

## Configuration

**Basic Configuration (Without Aliases)**

A standard database configuration without aliases looks like this:

```
# Database configuration without aliases
mydb    myuser    10    5    2
```

**Configuration with Aliases**

To add aliases to a database, use the following syntax:

```
# Database configuration with aliases
mydb=alias1,alias2,alias3    myuser    10    5    2
```

In this example:
- `mydb` is the real database name
- `alias1`, `alias2`, and `alias3` are alternative names clients can use
- Clients can connect using any of these four names: `mydb`, `alias1`, `alias2`, or `alias3`

**Important Configuration Rules**

**Maximum Aliases Per Database**

Each database can have a maximum of **8 aliases**. If you configure more than 8 aliases, only the first 8 will be accepted and the rest will be **truncated with a warning message logged**.

```sh
# Only alias1 through alias8 will be used
# alias9 and alias10 will be truncated with a warning
mydb=alias1,alias2,alias3,alias4,alias5,alias6,alias7,alias8,alias9,alias10    myuser    10    5    2
```

When this occurs, you'll see a warning message in the logs similar to:
```
WARN: Database 'mydb' has 10 aliases, but only the first 8 will be used (max limit: 8)
```

**Important**: Clients attempting to connect using truncated aliases (alias9, alias10 in the example above) will receive connection errors since those aliases are not functional.

## Validation rules

[**pgagroal**][pgagroal] enforces strict validation rules for database aliases to ensure configuration integrity:

**Global Uniqueness**

All aliases must be globally unique across the entire configuration:

```sh
# WRONG - 'shared_alias' appears twice
db1=shared_alias,alias1     user1    10    5    2
db2=shared_alias,alias2     user2    10    5    2

# CORRECT - All aliases are unique
db1=unique_alias1,alias1    user1    10    5    2
db2=unique_alias2,alias2    user2    10    5    2
```

**No Conflicts with Database Names**

Aliases cannot have the same name as any real database name in the configuration:

```sh
# WRONG - 'db2' is used as both database name and alias
db1=db2,alias1             user1    10    5    2
db2=alias2,alias3          user2    10    5    2

# CORRECT - No conflicts between database names and aliases
db1=unique_alias,alias1    user1    10    5    2
db2=alias2,alias3          user2    10    5    2
```

**Special Database Restrictions**

The special database `all` cannot have aliases:

```sh
# WRONG - 'all' database cannot have aliases
all=alias1,alias2    myuser    10    5    2

# CORRECT - 'all' database without aliases
all                  myuser    10    5    2
```

**Empty Aliases Behavior**

Empty aliases (consecutive commas or trailing commas) are automatically ignored:

```sh
# These configurations are  valid - empty aliases are skipped
mydb=alias1,,alias2       myuser    10    5    2  # Empty alias between alias1 and alias2
mydb=alias1, ,alias2      myuser    10    5    2  # Empty alias between alias1 and alias2 with space
mydb=,alias1,alias2       myuser    10    5    2  # Leading comma before alias1

# All result in the same aliases: alias1, alias2 (where applicable)
```

**Note**: While empty aliases are ignored, trailing commas may cause parsing issues in some edge cases. For best compatibility, avoid trailing commas:

```sh
# RECOMMENDED - No trailing comma
mydb=alias1,alias2,alias3    myuser    10    5    2

# NOT WORKS NOT RECOMMENDED - Trailing comma
mydb=alias1,alias2,alias3,   myuser    10    5    2
```

**Invalid Configurations**

**Empty alias section**: A database name followed by `=` with no aliases is invalid:

```sh
# INCORRECT - '=' with no aliases
mydb=                     myuser           10    5    2
mydb =                    myuser           10    5    2  # Spaces after '=' but no aliases

# CORRECT - Either use aliases or don't use '=' at all
mydb=alias1,alias2        myuser    10    5    2  # With aliases
mydb                      myuser    10    5    2  # Without aliases
```

**Trailing commas**: Any commas at the end of the alias list will cause parsing failures:

```sh
# WRONG - Trailing comma causes parsing issues
mydb=alias1,alias2,alias3,   myuser    10    5    2

# CORRECT - No trailing comma
mydb=alias1,alias2,alias3    myuser    10    5    2
```

## Spaces

**Allowed Spaces**

Spaces are permitted in specific locations within alias configurations:

```sh
# CORRECT - Spaces around '=' and commas are allowed
mydb = alias1 , alias2 , alias3    myuser    10    5    2
mydb =alias1, alias2,alias3        myuser    10    5    2
mydb= alias1,alias2 ,alias3        myuser    10    5    2

# All result in the same aliases: alias1, alias2, alias3
```

**Allowed space locations**

- Between database name and `=`: `mydb =alias1`
- Between `=` and first alias: `mydb= alias1`
- Before commas: `alias1 ,alias2`
- After commas: `alias1, alias2`
- Multiple spaces in any of these locations: `mydb  =  alias1  ,  alias2`

**Invalid Spaces**

Spaces **within individual alias names** are not allowed and will cause parsing errors:

```sh
# WRONG - Spaces within alias names
mydb=my alias,another alias       myuser    10    5    2
mydb=alias 1,alias 2              myuser    10    5    2

# CORRECT - No spaces within alias names
mydb=my_alias,another_alias       myuser    10    5    2
mydb=alias1,alias2                myuser    10    5    2
```

**Best Practices for Spaces**

1. **Use underscores or hyphens** instead of spaces in alias names:
   ```sh
   # RECOMMENDED
   mydb=user_portal,admin_panel,api_gateway    myuser    10    5    2
   mydb=user-portal,admin-panel,api-gateway    myuser    10    5    2
   ```

2. **Consistent formatting** for readability:
   ```sh
   # Clean format (recommended)
   mydb=alias1,alias2,alias3        myuser    10    5    2

   # Spaced format (also valid)
   mydb = alias1, alias2, alias3    myuser    10    5    2
   ```

**Summary**

- **Allowed**: Spaces between database name, `=`, commas, and aliases
- **Not allowed**: Spaces within individual alias names
- **Best practice**: Use `_` or `-` for multi-word aliases

## Quoted aliases

[**pgagroal**][pgagroal] treats quoted and unquoted aliases as distinct entities. The following are considered different aliases:

- `alias`
- `'alias'`
- `"alias"`

**Shell Quote Handling**

When using command-line tools like `psql`, shells (Bash, Zsh) automatically strip quotes before passing arguments to programs. This means these commands are equivalent:

```sh
psql -h localhost -p 2345 -U myuser alias
psql -h localhost -p 2345 -U myuser 'alias'
psql -h localhost -p 2345 -U myuser "alias"
```

All result in `psql` receiving the argument `alias` (without quotes).

**Using Quoted Aliases**

To connect to a quoted alias, you must escape the quotes:

```sh
# Connect to alias named "alias" (with double quotes)
psql -h localhost -p 2345 -U myuser \"alias\"

# Connect to alias named 'alias' (with single quotes)
psql -h localhost -p 2345 -U myuser \'alias\'
```

**Recommendation**: Avoid using quotes in alias names to prevent confusion and simplify client connections.

## Managing aliases

**Viewing Configured Aliases**

Use the `pgagroal-cli` command to view all configured databases and their aliases:

```sh
# Display all databases with their aliases
pgagroal-cli conf alias

# Get specific database alias information
pgagroal-cli conf get limit.mydb.aliases
pgagroal-cli conf get limit.mydb.number_of_aliases
```

**Runtime Management**

Database aliases are part of the limit configuration and can be managed through configuration reloads:

```sh
# Reload configuration to apply alias changes
pgagroal-cli conf reload
```

## Connection pooling

When using aliases, [**pgagroal**][pgagroal] ensures efficient connection reuse:

- All connections are established using the real database name
- Connections can be reused regardless of whether clients connect via the real name or any alias
- Pool statistics and monitoring use the real database name
- Authentication and authorization use the real database name

## Example configuration

Here's a complete example demonstrating various alias configurations:

```sh
# /etc/pgagroal/pgagroal_databases.conf

# Production database with environment-specific aliases
prod_db=production,live,main                myuser    20    10    5

# Development database with multiple aliases for teams
dev_db=development,staging,test,beta        devuser   10    5     2

# Legacy database with old naming convention
new_app_db=legacy_app,old_system            appuser   15    8     3

# Database without aliases
analytics                                   analyst   5     2     1
```

## Best practices

1. **Use descriptive aliases** that make sense to your application teams
2. **Avoid spaces** in alias definitions
3. **Keep aliases simple** - avoid special characters and quotes
4. **Document your alias strategy** for team members
5. **Plan for the 8-alias limit** when designing your naming scheme
6. **Use aliases for gradual migrations** when renaming databases
7. **Test alias connectivity** after configuration changes
8. **Avoid trailing commas** in alias definitions to prevent parsing edge cases

## Troubleshooting

**Alias Not Working**

- Check for spaces in the alias definition
- Verify the alias doesn't conflict with existing database names
- Ensure the alias is within the 8-alias limit
- Confirm configuration has been reloaded

**Connection Failures**

- Verify the alias exists in the configuration
- Check HBA rules allow connections for the target database
- Ensure proper escaping when using quoted aliases

[pgagroal]: https://github.com/pgagroal/pgagroal
