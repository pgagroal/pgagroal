# pgagroal pipelines

pgagroal supports 3 different pipelines

* Performance
* Session
* Transaction

The pipeline is defined in `pgagroal.conf` under the setting of

```
pipeline = auto
```

pgagroal will choose either the performance or the session pipeline
based on the configuration settings by default.

When `pipeline = auto` is used, the performance pipeline is selected by
default. pgagroal downgrades to the session pipeline at startup when any
of the following are configured:

* `tls = on`
* `failover = on`
* `disconnect_client` is greater than 0

# Performance

The performance pipeline is fastest pipeline as it is a minimal implementation
of the pipeline architecture.

However, it doesn't support Transport Layer Security (TLS), failover support and
the `disconnect_client` setting.

A `DISCARD ALL` query is run after each client session.

Select the performance pipeline by

```
pipeline = performance
```

# Session

The session pipeline supports all features of pgagroal.

A `DISCARD ALL` query is run after each client session.

Select the session pipeline by

```
pipeline = session
```

# Transaction

The transaction pipeline will release the connection back to the pool after each
transaction completes. This feature will support many more clients than there are
database connections.

The transaction pipeline requires that there are users defined such that connections
can be kept in the pool in all security scenarios.

However, there are some session based features of PostgreSQL that can't be supported in this
pipeline.

* `SET` / `RESET`
* `LISTEN` / `NOTIFY`
* `WITH HOLD CURSOR`
* `PREPARE` / `DEALLOCATE`

It is assumed that all clients using the same user name and database pair share the same
startup parameters towards PostgreSQL.

__`SET` / `RESET`__

The `SET` functionality is a session based feature.

__`LISTEN` / `NOTIFY`__

The `LISTEN` functionality is a session based feature.

__`WITH HOLD CURSOR`__

The `WITH HOLD CURSOR` functionality is a session based feature.

__`PREPARE` / `DEALLOCATE`__

While using `PREPARE` and `EXECUTE` can be used the prepared statements are tied to the
connection they were created in which means that clients can't be sure that they created
the prepared statement on the connection unless it is issued within the same transaction
where it is used.

pgagroal can issue a `DEALLOCATE ALL` statement before a connection is returned back to
the pool if the `track_prepared_statements` setting is set to `on`. If `off` then no
statement is issued.

Note, that pgagroal does not issue a `DISCARD ALL` statement when using the transaction
pipeline.

__Performance considerations__

Clients may need to wait for a connection between transactions leading to a higher
latency.

__Important behavioral differences__

The transaction pipeline has several behaviors that differ from the session
and performance pipelines. Users should be aware of these before enabling
it.

* `max_connection_age` does not terminate transaction-mode connections on
  return to the pool or from the periodic reaper. It is only applied to
  transaction-mode connections indirectly via the validation path when
  `validation = foreground` or `validation = background`.
* `idle_timeout` does not terminate transaction-mode connections from the
  periodic reaper. It is only applied via the validation path.
* `blocking_timeout` applies to the transaction pipeline. If the pool is
  exhausted, active clients may time out during their workload. Set
  `blocking_timeout = 0` for the transaction pipeline.
* `DISCARD ALL` is not issued when a connection is returned to the pool in
  transaction mode. Session state such as `SET`, temporary tables and
  cursors is not reset between clients sharing the same backend
  connection.
* The `disconnect_client` and `allow_unknown_users` settings are not
  supported.

It is highly recommended that you prefill all connections for each user.
Because `idle_timeout` and `max_connection_age` have limited effect in
transaction mode, it is best to set both to 0.

Select the transaction pipeline by

```
pipeline = transaction
```
