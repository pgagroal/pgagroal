\newpage

# Prometheus

## pgagroal

Once [**pgagroal**][pgagroal] is running you can access the metrics with a browser at the pooler address, specifying the `metrics` port number and routing to the `/metrics` page. For example, point your web browser at:

```
http://localhost:2346/metrics
```

It is also possible to get an explaination of what is the meaning of each metric by pointing your web browser at:

```
http://localhost:2346/
```

### Metrics

**pgagroal_state**

The state of pgagroal

**pgagroal_pipeline_mode**

The mode of pipeline

**pgagroal_server_error**

The number of errors for servers

**pgagroal_logging_info**

The number of INFO logging statements

**pgagroal_logging_warn**

The number of WARN logging statements

**pgagroal_logging_error**

The number of ERROR logging statements

**pgagroal_logging_fatal**

The number of FATAL logging statements

**pgagroal_failed_servers**

The number of failed servers

**pgagroal_wait_time**

The waiting time of clients

**pgagroal_query_count**

The number of queries

**pgagroal_connection_query_count**

The number of queries per connection

**pgagroal_tx_count**

The number of transactions

**pgagroal_active_connections**

The number of active connections

**pgagroal_total_connections**

The total number of connections

**pgagroal_max_connections**

The maximum number of connections

**pgagroal_connection**

The connection information

**pgagroal_session_time_seconds**

The session times

**pgagroal_connection_error**

Number of connection errors

**pgagroal_connection_kill**

Number of connection kills

**pgagroal_connection_remove**

Number of connection removes

**pgagroal_connection_timeout**

Number of connection time outs

**pgagroal_connection_return**

Number of connection returns

**pgagroal_connection_invalid**

Number of connection invalids

**pgagroal_connection_get**

Number of connection gets

**pgagroal_connection_idletimeout**

Number of connection idle timeouts

**pgagroal_connection_max_connection_age**

Number of connection max age timeouts

**pgagroal_connection_flush**

Number of connection flushes

**pgagroal_connection_success**

Number of connection successes

**pgagroal_auth_user_success**

Number of successful user authentications

**pgagroal_auth_user_bad_password**

Number of bad passwords during user authentication

**pgagroal_auth_user_error**

Number of errors during user authentication

**pgagroal_client_wait**

Number of waiting clients

**pgagroal_client_active**

Number of active clients

**pgagroal_network_sent**

Bytes sent by clients

**pgagroal_network_received**

Bytes received from servers

**pgagroal_client_sockets**

Number of sockets the client used

**pgagroal_self_sockets**

Number of sockets used by pgagroal itself

**pgagroal_connection_awaiting**

Number of connection on-hold (awaiting)

**pgagroal_os_info**

Operating system version information

**pgagroal_certificates_total**

Total number of TLS certificates configured

**pgagroal_certificates_accessible**

Number of accessible TLS certificates

**pgagroal_certificates_valid**

Number of valid TLS certificates

**pgagroal_certificates_expired**

Number of expired TLS certificates

**pgagroal_certificates_expiring_soon**

Number of TLS certificates expiring within 30 days

**pgagroal_certificates_inaccessible**

Number of inaccessible TLS certificate files

**pgagroal_certificates_parse_errors**

Number of TLS certificates with parsing errors

**pgagroal_tls_certificate_status**

Certificate status (1=valid, 0=invalid/inaccessible)

**pgagroal_tls_certificate_expiration_seconds**

TLS certificate expiration time

**pgagroal_tls_certificate_key_size_bits**

TLS certificate key size in bits

**pgagroal_tls_certificate_is_ca**

Whether certificate is a CA certificate

**pgagroal_tls_certificate_key_type**

TLS certificate key type

**pgagroal_tls_certificate_signature_algorithm**

TLS certificate signature algorithm

**pgagroal_tls_certificate_info**

TLS certificate metadata

## pgagroal-vault

Once [**pgagroal-vault**][pgagroal] is running you can access the metrics with a browser at the pooler address, specifying the `metrics` port number and routing to the `/metrics` page. For example, point your web browser at:

```
http://localhost:2501/metrics
```

It is also possible to get an explaination of what is the meaning of each metric by pointing your web browser at:

```
http://localhost:2501/
```
