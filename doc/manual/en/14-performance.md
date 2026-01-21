\newpage

# Performance

Performance is an important goal for [**pgagroal**][pgagroal] and effort have been made
to make [**pgagroal**][pgagroal] scale and use a limited number of resources.

This chapter describes [**pgagroal**][pgagroal] performance characteristics and provides
benchmarking results compared to other [PostgreSQL][postgresql] connection pool implementations.

## Benchmarking Methodology

The [pgbench][pgbench] program was used in the performance runs. All pool
configurations were made with performance in mind.

The runs were performed on [RHEL][rhel] 7.7 /
[EPEL](https://access.redhat.com/solutions/3358) / [DevTools](https://developers.redhat.com/products/developertoolset/overview) 8
based machines on 10G network. All connection pools were the latest versions as of January 14, 2020. [**pgagroal**][pgagroal] was
using the `epoll` mode of [libev][libev].

## Performance Results

### Simple Protocol

This run uses:

```
pgbench -M simple
```

![pgbench simple](https://github.com/pgagroal/pgagroal/raw/master/doc/images/perf-simple.png "pgbench simple")

### Extended Protocol

This run uses:

```
pgbench -M extended
```

![pgbench extended](https://github.com/pgagroal/pgagroal/raw/master/doc/images/perf-extended.png "pgbench extended")

### Prepared Statements

This run uses:

```
pgbench -M prepared
```

![pgbench prepared](https://github.com/pgagroal/pgagroal/raw/master/doc/images/perf-prepared.png "pgbench prepared")

### Read-Only Workload

This run uses:

```
pgbench -S
```

![pgbench readonly](https://github.com/pgagroal/pgagroal/raw/master/doc/images/perf-readonly.png "pgbench readonly")

## Performance Tuning

### Pipeline Selection

Choose the appropriate pipeline for your workload:

- **Performance pipeline**: Fastest option for high-throughput scenarios
- **Session pipeline**: Balanced performance with full feature support
- **Transaction pipeline**: Best for applications with many short transactions

See [Pipelines](#pipelines) for detailed configuration.

### Connection Pool Sizing

Optimal pool sizing depends on your workload:

- **CPU-bound workloads**: Pool size approximately equals
number of CPU cores
- **I/O-bound workloads**: Pool size can be higher than CPU cores
- **Mixed workloads**: Start with 2x CPU cores and adjust based on monitoring

### System-Level Optimizations

#### Network Configuration
- Use dedicated network interfaces for database traffic
- Configure appropriate TCP buffer sizes
- Consider using 10G or higher network speeds for high-throughput scenarios

#### Memory Configuration
- Enable huge pages for better memory management
- Configure appropriate shared memory settings
- Monitor memory usage patterns

#### CPU Configuration
- Pin pgagroal processes to specific CPU cores if needed
- Configure CPU governor for performance
- Monitor CPU utilization patterns

### Monitoring Performance

Use the following metrics to monitor pgagroal performance:

- **Connection utilization**: Active vs. total connections
- **Response times**: Average and percentile response times
- **Throughput**: Transactions per second
- **Resource usage**: CPU, memory, and network utilization

See [Prometheus](#prometheus) for detailed monitoring setup.