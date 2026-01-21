# pgagroal performance

Performance is an important goal for [**pgagroal**](https://github.com/pgagroal/pgagroal) and effort have been made
to make [**pgagroal**](https://github.com/pgagroal/pgagroal) scale and use a limited number of resources.

This report describe [**pgagroal**](https://github.com/pgagroal/pgagroal) in relationship to 3 other [PostgreSQL](https://www.postgresql.org)
connection pool implementations, which we will call `a`, `b` and `c`.

The [pgbench](https://www.postgresql.org/docs/11/pgbench.html) program was used in the runs. All pool
configurations were made with performance in mind.

All diagrams are using the same identifier for the connection pool in question, so `a` is `a` in all
diagrams and so on.

The runs were performed on [RHEL](https://www.redhat.com/en/technologies/linux-platforms/enterprise-linux) 7.7 /
[EPEL](https://access.redhat.com/solutions/3358) / [DevTools](https://developers.redhat.com/products/developertoolset/overview) 8
based machines on 10G network. All connection pools were the latest versions as of January 14, 2020. [**pgagroal**](https://github.com/pgagroal/pgagroal) was
using the `epoll` mode of [libev](http://software.schmorp.de/pkg/libev.html).

## Simple

This run uses

```
pgbench -M simple
```

![pgbench simple](https://github.com/pgagroal/pgagroal/raw/master/doc/images/perf-simple.png "pgbench simple")

## Extended

This run uses

```
pgbench -M extended
```

![pgbench extended](https://github.com/pgagroal/pgagroal/raw/master/doc/images/perf-extended.png "pgbench extended")

## Prepared

This run uses

```
pgbench -M prepared
```

![pgbench prepared](https://github.com/pgagroal/pgagroal/raw/master/doc/images/perf-prepared.png "pgbench prepared")

## ReadOnly

This run uses

```
pgbench -S -M prepared
```

![pgbench readonly](https://github.com/pgagroal/pgagroal/raw/master/doc/images/perf-readonly.png "pgbench readonly")

## Closing

**Please**, run your own benchmarks to see how [**pgagroal**](https://github.com/pgagroal/pgagroal) compare to your existing connection pool
deployment.
