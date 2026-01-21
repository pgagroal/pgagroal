# pgagroal architecture

## Overview

[**pgagroal**](https://github.com/pgagroal/pgagroal) use a process model (`fork()`), where each process handles one connection to [PostgreSQL](https://www.postgresql.org).
This was done such a potential crash on one connection won't take the entire pool down.

The main process is defined in [main.c](../src/main.c). When a client connects it is processed in its own process, which
is handle in [worker.h](../src/include/worker.h) ([worker.c](../src/libpgagroal/worker.c)).

Once the client disconnects the connection is put back in the pool, and the child process is terminated.

## Shared memory

A memory segment ([shmem.h](../src/include/shmem.h)) is shared among all processes which contains the [**pgagroal**](https://github.com/pgagroal/pgagroal)
state containing the configuration of the pool, the list of servers and the state of each connection.

The configuration of [**pgagroal**](https://github.com/pgagroal/pgagroal) (`struct configuration`), the configuration of the servers (`struct server`) and
the state of each connection (`struct connection`) is initialized in this shared memory segment.
These structs are all defined in [pgagroal.h](../src/include/pgagroal.h).

The shared memory segment is created using the `mmap()` call.

## Atomic operations

The [atomic operation library](https://en.cppreference.com/w/c/atomic) is used to define the state of each of the
connection, and move them around in the connection state diagram. The state diagram has the follow states

| State name | Description |
|------------|-------------|
| `STATE_NOTINIT` | The connection has not been initialized |
| `STATE_INIT` | The connection is being initialized |
| `STATE_FREE` | The connection is free |
| `STATE_IN_USE` | The connection is in use |
| `STATE_GRACEFULLY` | The connection will be killed upon return to the pool |
| `STATE_FLUSH` | The connection is being flushed |
| `STATE_IDLE_CHECK` | The connection is being idle timeout checked |
| `STATE_MAX_CONNECTION_AGE` | The connection is being max connection age checked |
| `STATE_VALIDATION` | The connection is being validated |
| `STATE_REMOVE` | The connection is being removed |

These state are defined in [pgagroal.h](../src/include/pgagroal.h).

## Pool

The [**pgagroal**](https://github.com/pgagroal/pgagroal) pool API is defined in [pool.h](../src/include/pool.h) ([pool.c](../src/libpgagroal/pool.c)).

This API defines the functionality of the pool such as getting a connection from the pool, and returning it.
There is no ordering among processes, so a newly created process can obtain a connection before an older process.

The pool operates on the `struct connection` data type defined in [pgagroal.h](../src/include/pgagroal.h).

## Network and messages

All communication is abstracted using the `struct message` data type defined in [message.h](../src/include/message.h).

Reading and writing messages are handled in the [message.h](../src/include/message.h) ([message.c](../src/libpgagroal/message.c))
files.

Network operations are defined in [network.h](../src/include/network.h) ([network.c](../src/libpgagroal/network.c)).

## Memory

Each process uses a fixed memory block for its network communication, which is allocated upon startup of the worker.

That way we don't have to allocate memory for each network message, and more importantly free it after end of use.

The memory interface is defined in [memory.h](../src/include/memory.h) ([memory.c](../src/libpgagroal/memory.c)).

## Management

`pgagroal` has a management interface which defines the administrator abilities that can be performed when it is running.
This include for example taking a backup. The `pgagroal-cli` program is used for these operations ([cli.c](../src/cli.c)).

The management interface is defined in [management.h](../src/include/management.h). The management interface
uses its own protocol which uses JSON as its foundation.

### Write

The client sends a single JSON string to the server,

| Field         | Type   | Description                     |
| :------------ | :----- | :------------------------------ |
| `compression` | uint8  | The compression type            |
| `encryption`  | uint8  | The encryption type             |
| `length`      | uint32 | The length of the JSON document |
| `json`        | String | The JSON document               |

The server sends a single JSON string to the client,

| Field         | Type   | Description                     |
| :------------ | :----- | :------------------------------ |
| `compression` | uint8  | The compression type            |
| `encryption`  | uint8  | The encryption type             |
| `length`      | uint32 | The length of the JSON document |
| `json`        | String | The JSON document               |

### Read

The server sends a single JSON string to the client,

| Field         | Type   | Description                     |
| :------------ | :----- | :------------------------------ |
| `compression` | uint8  | The compression type            |
| `encryption`  | uint8  | The encryption type             |
| `length`      | uint32 | The length of the JSON document |
| `json`        | String | The JSON document               |

The client sends to the server a single JSON documents,

| Field         | Type   | Description                     |
| :------------ | :----- | :------------------------------ |
| `compression` | uint8  | The compression type            |
| `encryption`  | uint8  | The encryption type             |
| `length`      | uint32 | The length of the JSON document |
| `json`        | String | The JSON document               |

### Remote management

The remote management functionality uses the same protocol as the standard management method.

However, before the management packet is sent the client has to authenticate using SCRAM-SHA-256 using the
same message format that PostgreSQL uses, e.g. StartupMessage, AuthenticationSASL, AuthenticationSASLContinue,
AuthenticationSASLFinal and AuthenticationOk. The SSLRequest message is supported.

The remote management interface is defined in [remote.h](../src/include/remote.h) ([remote.c](../src/libpgagroal/remote.c)).

## I/O layer

The I/O layer interface is primarily defined in [ev.h](../src/include/ev.h) (and implemented in [ev.c](../src/libpgagroal/ev.c)).

These files contain the definition and implementation of the event loop for the three supported backends:
io_uring, epoll, and kqueue.

The backend is defined during runtime and can be set with the configuration option `ev_backend`. 
Default is `auto`, which will select the first supported backend, considering the following order: 
io_uring, epoll, kqueue.

[liburing](https://github.com/axboe/liburing) was used for setup and usage io_uring instances.

Each process has its own event loop, such that the process only gets notified when data related only to that process
is ready. The main loop handles the system wide "services" such as idle timeout checks and so on.

The I/O Layer works with a registered event watchers. Those can either be a watcher for I/O events (`io_watcher`), Timer events (`periodic_watcher`) and Signal events (`signal_watcher`).

The event interface provides ways to register and cancel watching events through the above watchers.

The events watched by the main loop is different from the events watched by the workers.

The main loop registers timers, signals and accept watchers. 

The worker registers the client watcher (responsible for receiving the message from the client and bouncing it to the server), the server watcher (responsible for watching for a message from the server and bouncing it to the client) and one signal watcher.

The event loop system supports multiple execution contexts to handle different pgagroal components:

- **Main Context** (`PGAGROAL_CONTEXT_MAIN`): Used by the main pgagroal process for connection pooling and management operations
- **Vault Context** (`PGAGROAL_CONTEXT_VAULT`): Used by pgagroal-vault for HTTP server operations and management communication

Each context uses its own configuration structure and event backend settings. The context is set explicitly before event loop initialization to ensure the correct configuration is used for backend selection and setup.

### Backend Selection

The event backend selection process varies by context:

For the **main pgagroal process**:
- Reads `ev_backend` setting from main configuration file
- Validates backend availability and TLS compatibility
- Falls back to supported alternatives if needed

For **pgagroal-vault**:
- Reads `ev_backend` setting from vault configuration file
- Uses the same validation and fallback logic as main process
- Supports all the same backends: io_uring, epoll, kqueue

Both contexts support the same configuration options:
- `auto`: Automatically selects the best available backend
- `io_uring`: Linux-specific, high-performance backend (not supported with TLS)
- `epoll`: Linux-specific, traditional event notification
- `kqueue`: BSD/macOS event notification mechanism

The implementation is done in [ev.h](../src/include/ev.h) and [ev.c](../src/libpgagroal/ev.c).

## Pipeline

[**pgagroal**](https://github.com/pgagroal/pgagroal) has the concept of a pipeline that defines how communication is routed from the client through [**pgagroal**](https://github.com/pgagroal/pgagroal) to
[PostgreSQL](https://www.postgresql.org). Likewise in the other direction.

A pipeline is defined by

```C
struct pipeline
{
   initialize initialize;
   start start;
   callback client;
   callback server;
   stop stop;
   destroy destroy;
   periodic periodic;
};
```

in [pipeline.h](../src/include/pipeline.h).

The functions in the pipeline are defined as

| Function | Description |
|----------|-------------|
| `initialize` | Global initialization of the pipeline, may return a pointer to a shared memory segment |
| `start` | Called when the pipeline instance is started |
| `client` | Client to [**pgagroal**](https://github.com/pgagroal/pgagroal) communication |
| `server` | [PostgreSQL](https://www.postgresql.org) to [**pgagroal**](https://github.com/pgagroal/pgagroal) communication |
| `stop` | Called when the pipeline instance is stopped |
| `destroy` | Global destruction of the pipeline |
| `periodic` | Called periodic |

The functions `start`, `client`, `server` and `stop` has access to the following information

```C
struct worker_io
{
   struct io_watcher io; /* The base type for io operations */
   int client_fd;        /* The client descriptor */
   int server_fd;        /* The server descriptor */
   int slot;             /* The slot */
   SSL* client_ssl;      /* The client SSL context */
   SSL* server_ssl;      /* The server SSL context */
};
```
defined in [worker.h](../src/include/worker.h).

### Performance pipeline

One of the goals for [**pgagroal**](https://github.com/pgagroal/pgagroal) is performance, so the performance pipeline will only look for the
[`Terminate`](https://www.postgresql.org/docs/11/protocol-message-formats.html) message from the client and act on that.
Likewise the performance pipeline will only look for `FATAL` errors from the server. This makes the pipeline very fast, since there
is a minimum overhead in the interaction.

The pipeline is defined in [pipeline_perf.c](../src/libpgagroal/pipeline_perf.c) in the functions

| Function | Description |
|----------|-------------|
| `performance_initialize` | Nothing |
| `performance_start` | Nothing |
| `performance_client` | Client to [**pgagroal**](https://github.com/pgagroal/pgagroal) communication |
| `performance_server` | [PostgreSQL](https://www.postgresql.org) to [**pgagroal**](https://github.com/pgagroal/pgagroal) communication |
| `performance_stop` | Nothing |
| `performance_destroy` | Nothing |
| `performance_periodic` | Nothing |

### Session pipeline

The session pipeline works like the performance pipeline with the exception that it checks if
a Transport Layer Security (TLS) transport should be used.

The pipeline is defined in [pipeline_session.c](../src/libpgagroal/pipeline_session.c) in the functions

| Function | Description |
|----------|-------------|
| `session_initialize` | Initialize memory segment if disconnect_client is active |
| `session_start` | Prepares the client segment if disconnect_client is active |
| `session_client` | Client to [**pgagroal**](https://github.com/pgagroal/pgagroal) communication |
| `session_server` | [PostgreSQL](https://www.postgresql.org) to [**pgagroal**](https://github.com/pgagroal/pgagroal) communication |
| `session_stop` | Updates the client segment if disconnect_client is active |
| `session_destroy` | Destroys memory segment if initialized |
| `session_periodic` | Checks if clients should be disconnected |

### Transaction pipeline

The transaction pipeline will return the connection to the server after each transaction. The pipeline supports
Transport Layer Security (TLS).

The pipeline uses the [ReadyForQuery](https://www.postgresql.org/docs/current/protocol-message-formats.html) message
to check the status of the transaction, and therefore needs to maintain track of the message headers.

The pipeline has a management interface in order to receive the socket descriptors from the parent process when a new
connection is added to the pool. The pool will retry if the client in question doesn't consider the socket descriptor valid.

The pipeline is defined in [pipeline_transaction.c](../src/libpgagroal/pipeline_transaction.c) in the functions

| Function | Description |
|----------|-------------|
| `transaction_initialize` | Nothing |
| `transaction_start` | Setup process variables and returns the connection to the pool |
| `transaction_client` | Client to [**pgagroal**](https://github.com/pgagroal/pgagroal) communication. Obtain connection if needed |
| `transaction_server` | [PostgreSQL](https://www.postgresql.org) to [**pgagroal**](https://github.com/pgagroal/pgagroal) communication. Keep track of message headers |
| `transaction_stop` | Return connection to the pool if needed. Possible rollback of active transaction |
| `transaction_destroy` | Nothing |
| `transaction_periodic` | Nothing |

## Signals

The main process of [**pgagroal**](https://github.com/pgagroal/pgagroal) supports the following signals `SIGTERM`, `SIGINT` and `SIGALRM`
as a mechanism for shutting down. The `SIGTRAP` signal will put [**pgagroal**](https://github.com/pgagroal/pgagroal) into graceful shutdown, meaning that
exisiting connections are allowed to finish their session. The `SIGABRT` is used to request a core dump (`abort()`).
The `SIGHUP` signal will trigger a full reload of the configuration. When `SIGHUP` is received, [**pgagroal**](https://github.com/pgagroal/pgagroal) will re-read the configuration from the configuration files on disk and apply any changes that can be handled at runtime. This is the standard way to apply changes made to the configuration files.

In contrast, the `SIGUSR1` signal will trigger a service reload, but **does not** re-read the configuration files. Instead, `SIGUSR1` restarts sockets and listeners using the current in-memory configuration. This is useful for applying certain changes (such as re-opening sockets or refreshing listeners) without modifying or reloading the configuration from disk. Any changes made to the configuration files will **not** be picked up when using `SIGUSR1`; only the configuration already loaded in memory will be used.

Use `SIGHUP` when you want to apply changes from updated configuration files.  
Use `SIGUSR1` when you want to restart services without changing the current configuration.

The child processes support `SIGQUIT` as a mechanism to shutdown. This will not shutdown the pool itself.

It should not be needed to use `SIGKILL` for [**pgagroal**](https://github.com/pgagroal/pgagroal). Please, consider using `SIGABRT` instead, and share the
core dump and debug logs with the [**pgagroal**](https://github.com/pgagroal/pgagroal) community.

## Reload

The `SIGHUP` signal will trigger a reload of the configuration.

However, some configuration settings requires a full restart of [**pgagroal**](https://github.com/pgagroal/pgagroal) in order to take effect. These are

* `hugepage`
* `ev_backend`
* `log_path`
* `log_type`
* `max_connections`
* `pipeline`
* `unix_socket_dir`
* `pidfile`
* Limit rules defined by `pgagroal_databases.conf`
* TLS rules defined by server section

The configuration can also be reloaded using `pgagroal-cli -c pgagroal.conf conf reload`. The command is only supported
over the local interface, and hence doesn't work remotely.

## Prometheus

pgagroal has support for [Prometheus](https://prometheus.io/) when the `metrics` port is specified. 

**Note:** It is crucial to carefully initialize Prometheus memory in any program files for example functions like `pgagroal_init_prometheus()` and `pgagroal_init_prometheus_cache()` should only be invoked if `metrics` is greater than 0.

The module serves two endpoints

* `/` - Overview of the functionality (`text/html`)
* `/metrics` - The metrics (`text/plain`)

All other URLs will result in a 403 response.

The metrics endpoint supports `Transfer-Encoding: chunked` to account for a large amount of data.

The implementation is done in [prometheus.h](../src/include/prometheus.h) and
[prometheus.c](../src/libpgagroal/prometheus.c).

## Failover support

pgagroal can failover a PostgreSQL instance if clients can't write to it.

This is done using an external script provided by the user.

The implementation is done in [server.h](../src/include/server.h) and
[server.c](../src/libpgagroal/server.c).

## Logging

Simple logging implementation based on a `atomic_schar` lock.

The implementation is done in [logging.h](../src/include/logging.h) and
[logging.c](../src/libpgagroal/logging.c).

## Protocol

The protocol interactions can be debugged using [Wireshark](https://www.wireshark.org/) or
[pgprtdbg](https://github.com/jesperpedersen/pgprtdbg).

## Database Alias

A **database alias** in pgagroal allows clients to connect using an alternative name for a configured database. This is useful for scenarios such as application migrations, multi-tenancy, or providing user-friendly names without exposing the actual backend database name.

### How it works

- Each database entry in the limits configuration (`pgagroal_databases.conf`) can specify up to eight aliases.
- When a client connects using an alias, pgagroal transparently maps the alias to the real database name before establishing or reusing a backend connection.
- Aliases are resolved during both pooled and unpooled connection handling, ensuring that connections are matched and authenticated against the correct backend database.


