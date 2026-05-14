# pgagroal high availability

[pgagroal][pgagroal] achieves high availability by sitting in front of [PostgreSQL][postgresql]
streaming replication and coordinating with [HAProxy][haproxy] to route client traffic.

Two replication topologies are supported:

## Linear (Cascading)

Replicas replicate in a chain:

```
Primary -> Replica 1 -> Replica 2
```

### Backup

Replica 2 acts as a spare. HAProxy routes reads to Replica 1 under normal conditions
and promotes Replica 2 only if Replica 1 fails.

```
backend be_pgagroal_replicas
    balance leastconn
    option pgsql-check user pgagroal_health
    server pgagroal-replica1 10.0.0.12:2345 check inter 3s rise 2 fall 3
    server pgagroal-replica2 10.0.0.13:2345 check inter 3s rise 2 fall 3 backup
```

### Partner

Both replicas share read traffic. HAProxy ties their health together using `track`.
If Replica 1 fails, Replica 2 is taken offline immediately to prevent stale reads.

```
backend be_pgagroal_replicas
    balance leastconn
    option pgsql-check user pgagroal_health
    server pgagroal-replica1 10.0.0.12:2345 check inter 3s rise 2 fall 3
    server pgagroal-replica2 10.0.0.13:2345 track be_pgagroal_replicas/pgagroal-replica1
```

## Star

All replicas replicate directly from the primary:

```
        Primary
       /       \
Replica 1   Replica 2
```

### Single Level

All replicas are active and share the read load equally via `leastconn`.

```
backend be_pgagroal_replicas
    balance leastconn
    option pgsql-check user pgagroal_health
    server pgagroal-replica1 10.0.0.12:2345 check inter 3s rise 2 fall 3
    server pgagroal-replica2 10.0.0.13:2345 check inter 3s rise 2 fall 3
```

### Two Level

A second tier of replicas is added under each first-level replica.
Losing one branch does not affect the other.

```
              Primary
             /       \
       Replica 1   Replica 2
       /     \     /      \
 Replica1-1 Replica1-2  Replica2-1  Replica2-2
```

Second-level replicas use `track` so HAProxy removes them from rotation
when their upstream replica fails.

```
backend be_pgagroal_replicas
    balance leastconn
    option pgsql-check user pgagroal_health
    server pgagroal-replica1   10.0.0.12:2345 check inter 3s rise 2 fall 3
    server pgagroal-replica2   10.0.0.13:2345 check inter 3s rise 2 fall 3
    server pgagroal-replica1-1 10.0.0.14:2345 track be_pgagroal_replicas/pgagroal-replica1
    server pgagroal-replica1-2 10.0.0.15:2345 track be_pgagroal_replicas/pgagroal-replica1
    server pgagroal-replica2-1 10.0.0.16:2345 track be_pgagroal_replicas/pgagroal-replica2
    server pgagroal-replica2-2 10.0.0.17:2345 track be_pgagroal_replicas/pgagroal-replica2
```

## Health Check

A single role is used for both pgagroal and HAProxy health probes:

```sql
CREATE ROLE pgagroal_health WITH LOGIN PASSWORD 'your_secure_password' CONNECTION LIMIT 1;
CREATE DATABASE pgagroal_health WITH OWNER pgagroal_health;
```

In `pgagroal.conf`:

```
health_check = on
health_check_user = pgagroal_health
```

In HAProxy backend blocks:

```
option pgsql-check user pgagroal_health
```

See the [manual](manual/en/21-ha.md) for the full configuration reference.

[pgagroal]: https://github.com/pgagroal/pgagroal
[postgresql]: https://www.postgresql.org
[haproxy]: https://www.haproxy.org/
