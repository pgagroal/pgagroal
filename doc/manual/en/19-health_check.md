\newpage

# Health Check

[**pgagroal**][pgagroal] can periodically check the health of the [PostgreSQL][postgresql] instances in the pool. If a server is found to be down, it will be marked as such, and connections will be routed to other available servers.

## Configuration

The health check behavior is controlled by the following parameters in the `[pgagroal]` section of `pgagroal.conf`:

* `health_check`: Enables or disables periodic health checks (`on`/`off`)
* `health_check_period`: The interval in seconds between health check scans
* `health_check_timeout`: The timeout for each health check probe
* `health_check_user`: The user used to connect to the database for health checks. **This parameter is mandatory when `health_check` is enabled.**

## Security and User Setup

Health checks are performed by connecting to a database within the target cluster, which requires user credentials. Using an unprivileged user to connect is the recommended and more secure way to configure the health check feature.

### Option 1: Superuser

Using the existing database superuser (e.g., `postgres`) is the simplest way to get started.

#### Pros
* No additional setup required on the PostgreSQL side
* Guaranteed to have permissions to connect and run basic queries on any database

#### Cons
* **High Security Risk**: Credentials for the superuser are stored in `pgagroal_users.conf`. If the machine running [**pgagroal**][pgagroal] is compromised, the attacker gains full administrative access to your database.
* **Violates Least Privilege**: The health check only needs to verify "liveness," which does not require superuser privileges.

### Option 2: Dedicated Unprivileged User

Creating a dedicated, restricted user is the recommended and more secure approach.

#### Pros
* **Improved Security**: Limits the impact if credentials are leaked
* **Better Auditing**: Health check connections are clearly identifiable in [PostgreSQL][postgresql] logs

#### Cons
* Requires manual setup in [PostgreSQL][postgresql] and HBA configuration

### Setup Steps

To set up a dedicated health check user:

1. **Create the user in PostgreSQL**:
   Connected as a superuser, run:
   ```sql
   CREATE ROLE pgagroal_health WITH LOGIN PASSWORD 'your_secure_password' CONNECTION LIMIT 1;
   CREATE DATABASE pgagroal_health WITH OWNER pgagroal_health;
   ```

2. **Register the user with pgagroal**:
   Use `pgagroal-admin` to add the user to your `pgagroal_users.conf`:
   ```bash
   pgagroal-admin -f pgagroal_users.conf -U pgagroal_health -P your_secure_password user add
   ```

3. **Configure pgagroal.conf**:
   ```ini
   [pgagroal]
   health_check = on
   health_check_user = pgagroal_health
   ```

4. **Update HBA settings**:
   Ensure `pgagroal_hba.conf` allows the health check user to connect from the [**pgagroal**][pgagroal] host:
   ```
   # TYPE  DATABASE          USER             ADDRESS         METHOD
   host    pgagroal_health   pgagroal_health  127.0.0.1/32    scram-sha-256
   ```
   *Note: Ensure the backend [PostgreSQL][postgresql] `pg_hba.conf` also allows this connection.*

## Monitoring

The health status of each server is exposed via the Prometheus metrics endpoint (`pgagroal_server_health`).

* `1`: Server is UP
* `0`: Server is DOWN
* `2`: State is UNKNOWN (initial state or pending first check)

The metric includes an `auth` label identifying the authentication method used during the last successful probe (e.g., `trust`, `md5`, `scram-sha-256`).
