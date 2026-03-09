\newpage

# Pipelines

[**pgagroal**][pgagroal] supports 3 different pipelines that determine how connections are managed and what features are available.

The pipeline is defined in `pgagroal.conf` under the setting:

```
pipeline = auto
```

[**pgagroal**][pgagroal] will choose either the performance or the session pipeline
based on the configuration settings by default.

## Performance Pipeline

The performance pipeline is the fastest pipeline as it is a minimal implementation
of the pipeline architecture.

However, it doesn't support Transport Layer Security (TLS), failover support and
the `disconnect_client` setting.

A `DISCARD ALL` query is run after each client session.

### Configuration

Select the performance pipeline by:

```
pipeline = performance
```

### Use Cases

The performance pipeline is ideal for:
- High-throughput applications
- Scenarios where maximum performance is critical
- Environments where TLS is not required
- Simple connection pooling needs

### Limitations

- No Transport Layer Security (TLS) support
- No failover support
- No `disconnect_client` setting support
- Minimal feature set

## Session Pipeline

The session pipeline supports all features of [**pgagroal**][pgagroal].

A `DISCARD ALL` query is run after each client session.

### Configuration

Select the session pipeline by:

```
pipeline = session
```

### Features

The session pipeline supports:
- Transport Layer Security (TLS)
- Failover functionality
- All configuration options
- Complete feature set

### Use Cases

The session pipeline is ideal for:
- Production environments requiring full features
- Applications needing TLS encryption
- Environments with failover requirements
- Complex connection pooling scenarios

## Transaction Pipeline

The transaction pipeline will release the connection back to the pool after each
transaction completes. This feature will support many more clients than there are
database connections.

### Configuration

Select the transaction pipeline by:

```
pipeline = transaction
```

### Features

- Connection released after each transaction
- Supports many more clients than database connections
- Automatic transaction boundary detection
- Rollback handling for failed transactions

### Use Cases

The transaction pipeline is ideal for:
- Applications with many short transactions
- Microservices architectures
- High-concurrency scenarios with brief database interactions
- Applications that can handle connection state loss between transactions

### Considerations

- Application must handle loss of connection state between transactions
- Prepared statements are not preserved across transactions
- Temporary tables and other session-specific objects are not available
- May require application code changes

## Pipeline Comparison

| Feature | Performance | Session | Transaction |
|---------|-------------|---------|-------------|
| Speed | Fastest | Fast | Moderate |
| TLS Support | No | Yes | Yes |
| Failover Support | No | Yes | Yes |
| Connection Reuse | Session-based | Session-based | Transaction-based |
| Client Capacity | Limited by pool size | Limited by pool size | High |
| State Preservation | Session | Session | None |
| Complexity | Low | Medium | High |

## Choosing the Right Pipeline

### Performance Pipeline
Choose when:
- Maximum performance is required
- TLS is not needed
- Simple connection pooling is sufficient
- Failover is handled externally

### Session Pipeline
Choose when:
- Full feature set is required
- TLS encryption is needed
- Failover support is required
- Standard connection pooling behavior is desired

### Transaction Pipeline
Choose when:
- High client concurrency is needed
- Connections are used for short transactions
- Application can handle stateless connections
- Database connection limits are a constraint

## Configuration Examples

### High-Performance Setup
```ini
[pgagroal]
pipeline = performance
max_connections = 100
validation = off
```

### Production Setup with TLS
```ini
[pgagroal]
pipeline = session
max_connections = 50
tls = on
tls_cert_file = /path/to/cert.pem
tls_key_file = /path/to/key.pem
failover = on
failover_script = /path/to/failover.sh
```

### High-Concurrency Setup
```ini
[pgagroal]
pipeline = transaction
max_connections = 20
# Support many more clients than connections
```