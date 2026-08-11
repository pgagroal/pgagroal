\newpage

# Coordinator

`pgagroal-coordinator` fronts a number of [**pgagroal**][pgagroal] instances and routes every client connection to the instance that serves the [PostgreSQL][postgresql] primary.
Clients connect to a single address and never need to know which server is the primary, so a switchover does not require any change on the client side.

The coordinator replaces the external routing layer described in the [High Availability](21-ha.md) chapter: instead of configuring a separate proxy with health checks and read/write endpoints, the coordinator discovers the topology from the [**pgagroal**][pgagroal] instances themselves.

`pgagroal-coordinator` is **optional**. A [**pgagroal**][pgagroal] installation that does not use it is not affected by it in any way.

> **Note:** All host addresses, ports, usernames, and passwords used in this chapter are examples only.
> You are free to use any values that fit your environment.

## Architecture

```
                                +--> pgagroal (node1) --> PostgreSQL (primary)
                                |
client --> pgagroal-coordinator -+
                                |
                                +--> pgagroal (node2) --> PostgreSQL (replica)
```

Each [**pgagroal**][pgagroal] instance serves one [PostgreSQL][postgresql] server, and the instances are normally installed on separate machines.

The coordinator never connects to [PostgreSQL][postgresql] directly. It only talks to the management port of each [**pgagroal**][pgagroal] instance, and it forwards client traffic to the data plane of the instance that is currently the primary.

The coordinator is a router, not a connection pool. Pooling is performed by the [**pgagroal**][pgagroal] instances, as usual.

All client traffic is routed to the primary. The instances that serve a replica are probed and reported, but they carry no client traffic until a switchover makes one of them the primary.

## Prerequisites

* [PostgreSQL][postgresql] streaming replication already configured and running
* One [**pgagroal**][pgagroal] instance per [PostgreSQL][postgresql] server
* The host/port layout used throughout this chapter:

| Component            | Role    | Host        | Port | Management |
|----------------------|---------|-------------|------|------------|
| PostgreSQL           | Primary | `10.0.0.11` | 5432 |            |
| PostgreSQL           | Replica | `10.0.0.12` | 5432 |            |
| pgagroal             | node1   | `10.0.0.11` | 2345 | 2346       |
| pgagroal             | node2   | `10.0.0.12` | 2345 | 2346       |
| pgagroal-coordinator |         | `10.0.0.10` | 6432 | 6433       |

Clients connect to `10.0.0.10:6432`, and `pgagroal-cli` administrates the coordinator on `10.0.0.10:6433`.

## Discovery

The topology is not written in the configuration file of the coordinator. Only the address and the management port of each node are configured, and everything else is discovered.

When the coordinator starts, it connects to the management port of each node and asks for its status. The reply tells the coordinator

* the port of the data plane of the node, so it knows where to forward the client traffic
* the role of the [PostgreSQL][postgresql] server of the node, so it knows which node is the primary

The node whose server reports being the primary is elected, and all client connections are routed to it.

The role is taken from a live `pg_is_in_recovery()` probe, which requires `health_check_user` to be configured on each node, as described in the [Health Check](19-health_check.md) chapter.

> **Note:** Without `health_check_user` the role of a server cannot be probed, and the coordinator falls back to the
> `primary` setting of the node configuration. Configure `health_check_user` on every node so that the elected
> primary reflects the state of [PostgreSQL][postgresql] rather than the state of a configuration file.

Discovery runs when the coordinator starts and when a switchover is performed. The coordinator does not poll the nodes, and it never changes the primary on its own.

## Configuring the nodes

Every [**pgagroal**][pgagroal] instance that is fronted by the coordinator needs four things.

**Step 1 -- Enable the management port** in `pgagroal.conf` on each node, so that the coordinator can reach it:

```ini
[pgagroal]
host = 10.0.0.11
port = 2345
management = 2346

health_check = on
health_check_user = pgagroal_health
```

**Step 2 -- Create the administration account** that the coordinator uses, on each node:

```
pgagroal-admin -f /etc/pgagroal/pgagroal_admins.conf -U admin -P admin1234 user add
```

**Step 3 -- Authorize that account** in `pgagroal_hba.conf` on each node. Remote management always uses the `admin` database, and the address is the one of the coordinator:

```
#
# TYPE  DATABASE  USER   ADDRESS         METHOD
#
host    admin     admin  10.0.0.10/32    scram-sha-256
```

**Step 4 -- Restart the node** so that the new settings are applied, and verify that the management port answers:

```
pgagroal-cli -h 10.0.0.11 -p 2346 -U admin -P admin1234 ping
```

Repeat for every node.

## Configuring the coordinator

**Step 1 -- Create the configuration file.** With your editor of choice or using `cat` from the command line, create `/etc/pgagroal/pgagroal-coordinator.conf`:

```
cd /etc/pgagroal
cat > pgagroal-coordinator.conf
[pgagroal-coordinator]
host = 0.0.0.0
port = 6432
management = 6433
user = admin

log_type = file
log_level = info
log_path = /tmp/pgagroal-coordinator.log

ev_backend = auto

[node1]
host = 10.0.0.11
management = 2346

[node2]
host = 10.0.0.12
management = 2346
```

and press `Ctrl-d` (if running `cat`) to save the file.

Clients connect to `port`, and `pgagroal-cli` connects to `management`. The two must be different.

Each node section describes one [**pgagroal**][pgagroal] instance through its address and its management port. The port of the data plane and the role are discovered, so they are not configured.

The `user` of the `[pgagroal-coordinator]` section is the account presented to the management port of every node. A node that does not share the account of the cluster can define its own:

```ini
[node2]
host = 10.0.0.12
management = 2346
user = node2admin
```

**Step 2 -- Create the users file**, which holds the password of the account used against the nodes:

```
pgagroal-admin -f /etc/pgagroal/pgagroal-coordinator-users.conf -U admin -P admin1234 user add
```

The account and the password must match the one created on the nodes in Step 2 of the previous section.

**Step 3 -- Create the admins file**, which holds the accounts allowed to administrate the coordinator itself:

```
pgagroal-admin -f /etc/pgagroal/pgagroal-coordinator-admins.conf -U admin -P admin1234 user add
```

> **Note:** The two files serve opposite directions. The users file is what the coordinator presents to the nodes,
> and the admins file is what the coordinator accepts from `pgagroal-cli`. They can use different accounts.

See [the documentation about `pgagroal-coordinator.conf` for more details](https://github.com/pgagroal/pgagroal/blob/master/doc/COORDINATOR.md).

## Start the coordinator

```
pgagroal-coordinator -c /etc/pgagroal/pgagroal-coordinator.conf \
                     -u /etc/pgagroal/pgagroal-coordinator-users.conf \
                     -A /etc/pgagroal/pgagroal-coordinator-admins.conf
```

or as a daemon by adding `-d`.

The log reports the elected primary:

```
INFO  pgagroal-coordinator 2.2.0: Started on 0.0.0.0:6432 (management 6433)
INFO  pgagroal-coordinator: Node [node1] is the primary
```

## Verify the setup

Ask the coordinator for its view of the cluster:

```
pgagroal-cli -h 10.0.0.10 -p 6433 -U admin -P admin1234 status
```

```
Response:
  ActiveConnections: 0
  CurrentPrimary: node1
  Management: 6433
  Mode: read_write
  Nodes:
    node1:
      Health: UP
      Host: 10.0.0.11
      Management: 2346
      Port: 2345
      Server: node1
      State: Primary
    node2:
      Health: UP
      Host: 10.0.0.12
      Management: 2346
      Port: 2345
      Server: node2
      State: Replica
  NumberOfNodes: 2
  Port: 6432
  ServerVersion: 2.2.0
  Status: Running
```

| Field            | Meaning                                                              |
|------------------|----------------------------------------------------------------------|
| `CurrentPrimary` | The node that receives the client traffic, or `None`                  |
| `Health`         | `UP` if the node answered the last probe, otherwise `DOWN`/`UNKNOWN`  |
| `Port`           | The discovered data plane port of the node                            |
| `State`          | The discovered role of the [PostgreSQL][postgresql] server of the node |

Then connect a client through the coordinator and confirm that it reaches the primary:

```
psql -h 10.0.0.10 -p 6432 -U myuser -d mydb -c "SELECT pg_is_in_recovery()"
```

```
 pg_is_in_recovery
-------------------
 f
```

## Switchover

A switchover consists of two separate actions, and the coordinator performs only one of them.

Promoting a [PostgreSQL][postgresql] replica changes **which server accepts writes**. Running `switch-to` changes **where the coordinator sends the client traffic**. The coordinator does not promote [PostgreSQL][postgresql], and it never changes the primary on its own, so both actions are performed by the operator.

| Action                                                | Performed by                                        |
|-------------------------------------------------------|-----------------------------------------------------|
| Promoting the [PostgreSQL][postgresql] replica         | The operator, or the failover tooling of the cluster |
| Choosing the node that receives the client traffic     | The operator, through `switch-to`                    |
| Routing the new client connections                     | The coordinator                                      |
| Terminating the client connections of the old primary  | The coordinator                                      |
| Stopping the old [PostgreSQL][postgresql] server       | The operator                                         |

Promote first, then tell the coordinator to follow. The reason is that the coordinator routes traffic to whichever node it is told to use, without verifying that the server behind it accepts writes, so pointing it at a server that is still a replica sends the clients to a read-only server.

**Step 1 -- Promote the [PostgreSQL][postgresql] server** of the node that is to become the primary, as described in the [Failover](16-failover.md) chapter. The command runs on the machine of that node:

```
pg_ctl promote -D /var/lib/pgsql/data
```

Confirm that the promotion completed before continuing:

```
psql -h 10.0.0.12 -p 5432 -U myuser -d mydb -c "SELECT pg_is_in_recovery()"
```

The query must return `f`.

**Step 2 -- Point the coordinator at the new primary:**

```
pgagroal-cli -h 10.0.0.10 -p 6433 -U admin -P admin1234 switch-to node2
```

The coordinator performs the following, in order:

1. It verifies that `node2` exists in its configuration, and probes it. If it does not exist, or cannot be
   reached, the command fails and **nothing is changed**
2. It records `node2` as the current primary. From this moment every **new** client connection is routed to `node2`
3. It probes the remaining nodes, so that `status` reports the current role, health and port of each of them
4. It terminates the client connections that were routed to the previous primary

The log reports the switch:

```
INFO  pgagroal-coordinator: Switching the primary from [node1] to [node2]
```

If the target cannot be reached, the command fails and the primary is left unchanged:

```
ERROR pgagroal-coordinator: Node [node2] cannot be reached, the primary is unchanged
```

**Step 3 -- Verify** that the new primary is in use:

```
pgagroal-cli -h 10.0.0.10 -p 6433 -U admin -P admin1234 status
psql -h 10.0.0.10 -p 6432 -U myuser -d mydb -c "SELECT pg_is_in_recovery()"
```

`CurrentPrimary` must report `node2`, its `State` must be `Primary`, its `Health` must be `UP`, and the query must return `f`.

### Effect on the clients

The connections that were open through the previous primary are **terminated**, they are not drained. A client that is running a statement receives a broken connection, and the transaction it was in is rolled back by [PostgreSQL][postgresql].

This is intended. The coordinator forwards bytes without interpreting them, so it cannot wait for a safe point in the session, and leaving the connections open would keep the clients on the old server.

Applications should therefore reconnect and retry on a lost connection, which is the same requirement as for any [PostgreSQL][postgresql] failover. The connection that is opened after the switchover reaches the new primary.

Only the connections of the previous primary are terminated. Running `switch-to` for the node that is **already** the primary is reported as successful, and it does not terminate anything, so the command is safe to repeat:

```
INFO  pgagroal-coordinator: Node [node2] is already the primary
```

### The old primary

The coordinator does not stop, promote or reconfigure the old node. It stops sending client traffic to it, and it reports it through `status`, nothing more.

Preventing the old [PostgreSQL][postgresql] server from accepting writes, and rebuilding it as a replica of the new primary, are part of the failover procedure of the cluster and remain the responsibility of the operator.

> **Note:** If the old server is promoted, or was never demoted, two servers accept writes at the same time.
> The coordinator reports this as `Split brain, 2 nodes report being the primary` and keeps using the node it
> was told to use, because choosing between two primaries automatically would risk directing the clients to
> the wrong one.

### If the order is reversed

If `switch-to` is run before the promotion, the command succeeds, because the coordinator routes to the node it is told to use. The clients then reach a server that is still in recovery, and every write fails:

```
ERROR:  cannot execute INSERT in a read-only transaction
```

`status` shows the cause, since the `State` of the node reports `Replica` rather than `Primary`. Promote the server and run `switch-to` again to recover.

This is why Step 3 is part of the procedure.

### Unplanned failover

The procedure is the same when the primary is lost rather than being retired. Promote the replica, then run `switch-to` for its node.

The coordinator does not detect the failure, so until the command is issued the clients are still routed to the node that is down, and their connections fail. `status` reports that node as `Health: DOWN`.

If the coordinator is restarted while no server is out of recovery, it starts and serves its management port, but it refuses the client connections until a `switch-to` is performed:

```
ERROR pgagroal-coordinator: No primary established, client connections are refused until a switch-to is performed
```

## Administration

The coordinator speaks the same management protocol as [**pgagroal**][pgagroal], so it is administrated with `pgagroal-cli`. There are no coordinator specific commands, only a different endpoint.

| Command            | Description                                                |
|--------------------|------------------------------------------------------------|
| `ping`             | Verify that the coordinator is running                     |
| `status`           | Report the nodes, their role and port, and the current primary |
| `status details`   | Same as `status`                                           |
| `switch-to <node>` | Make `<node>` the current primary                          |
| `shutdown`         | Stop the coordinator                                       |

Adding `-F json` to any command returns the reply as JSON, which is convenient for monitoring scripts.

## Troubleshooting

| Symptom                                                          | Cause                                                                      | Resolution                                                                     |
|------------------------------------------------------------------|----------------------------------------------------------------------------|--------------------------------------------------------------------------------|
| `No primary established, client connections are refused`          | No node reported being the primary                                          | Configure `health_check_user` on each node, or perform a `switch-to`            |
| Node reports `Health: DOWN`                                       | The management port of the node is not reachable                            | Verify `management` in the node configuration, that the node runs, and the firewall |
| Node reports `Health: UNKNOWN` and the log says `Bad credentials` | The account is missing on the node, or it is not authorized                 | Add the account to `pgagroal_admins.conf` of the node and allow it on the `admin` database in `pgagroal_hba.conf` |
| Node reports `State: Not init`                                    | The role could not be probed                                                | Configure `health_check_user` on the node                                       |
| Node reports `Port: 0`                                            | The node did not report its data plane port                                 | Upgrade [**pgagroal**][pgagroal] on that node                                    |
| `Split brain, 2 nodes report being the primary`                   | More than one [PostgreSQL][postgresql] server is out of recovery            | Resolve the replication topology, then perform a `switch-to`                     |
| Writes fail after a switchover                                    | The coordinator was pointed at a node whose server is still a replica       | Promote the server, then run `switch-to` again                                   |

The coordinator writes the reason of every decision to its log, so raising `log_level` to `debug5` reports the outcome of the probe of each node.

## Limitations

* up to 8 nodes
* all the client traffic is routed to the primary, so the nodes that serve a replica are only the targets of a switchover and carry no traffic
* exactly one node may report being the primary. If more than one does, the coordinator does not choose between them and waits for a switchover
* no automatic failover and no health check, so a switchover is always performed by an operator
* the promotion of [PostgreSQL][postgresql] is not performed by the coordinator
* one coordinator, so it is a single point of failure
