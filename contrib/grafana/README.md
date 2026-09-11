# pgagroal Grafana Dashboards

This directory contains Grafana dashboards for monitoring `pgagroal` connection pooler using Prometheus.

## Dashboards

- **pgagroal Overview** (`pgagroal-overview.json`):
  - **System Status**: Global pgagroal service state (UP/DOWN/GRACEFUL), pipeline mode, and failed servers.
  - **Connection Statistics**: Maximum, total, and active connections with utilization gauge and historical trends.
  - **Client Activity**: Waiting clients, active clients, and wait time metrics with alert thresholds.
  - **Error Trends**: Visualized error rates over time to spot anomalies.
  - **Logging**: Log level distribution (rate per second) with critical/warning thresholds.

- **pgagroal Pool Details** (`pgagroal-pool-details.json`):
  - **Authentication**: Success vs failure rates and total counts.
  - **Connection Events**: 
    - **Totals**: Historical counts for Gets, Success, Kills, and Errors.
    - **Rates**: Real-time operations per second (ops/s) for lifecycle events.
  - **Timeouts & Issues**: 
    - **Alerts**: Rate-based graphs with Warning (Yellow) and Critical (Red) thresholds for timeouts and errors.
    - **Totals**: Cumulative counts for all timeout types.
  - **Connection Leaks**: Risk indicator showing unreturned connection ratios.
  - **Server Errors**: Total error counts and real-time error rates per server.

- **pgagroal Performance** (`pgagroal-performance.json`):
  - **Query Performance**: Total queries, transactions, and rates over time.
  - **Database Breakdown**: Query rate per database.
  - **Network Traffic**: Send/receive bytes and rates.
  - **Socket Usage**: Client, server, and self socket counts.

## Grafana Version Compatibility

These dashboards are compatible with **Grafana 12 or higher** (schemaVersion 39). They include:
- Templated Prometheus data source selection
- Modern panel configurations with gradients and opacity fills
- Interactive tooltips with multi-series support
- Responsive layouts

## Setup

1. **Configure pgagroal**: Ensure `metrics` is enabled in `pgagroal.conf`.
   - See [GETTING_DATA.md](GETTING_DATA.md) for detailed instructions on enabling metrics
2. **Import into Grafana**:
   - Go to Dashboards -> New -> Import.
   - Upload the `.json` files.
   - Select your Prometheus data source when prompted.
   - Or use the docker-compose setup for automatic provisioning (see below)

## Local Testing with Docker

A `docker-compose.yml` is provided to quickly spin up Grafana 12 and Prometheus for testing.

1. **Run the stack**:
   ```bash
   docker compose up -d
   ```

2. **Access Grafana**:
   - Open http://localhost:3000
   - Login with `admin`/`admin`
   - The dashboards are provisioned automatically (if mapped) or can be imported manually.

3. **Prometheus**:
   - Accessible at http://localhost:9090
   - Configured in `prometheus.yml`. You may need to adjust the `targets` to point to your running `pgagroal` instance (e.g., `host.docker.internal:5001`).

## Available Metrics

The dashboards visualize these pgagroal Prometheus metrics:

### General
- `pgagroal_state` - Pool state (0=DOWN, 1=UP, 2=GRACEFUL)
- `pgagroal_pipeline_mode` - Pipeline mode (0=Performance, 1=Session, 2=Transaction)
- `pgagroal_failed_servers` - Number of failed servers
- `pgagroal_os_info` - Operating system information

### Connections
- `pgagroal_active_connections` - Currently active connections
- `pgagroal_total_connections` - Total connections in pool
- `pgagroal_max_connections` - Maximum configured connections
- `pgagroal_connection` - Per-connection details (database, user, state, server)
- `pgagroal_connection_*` - Connection events (error, kill, remove, timeout, return, get, etc.)

### Authentication
- `pgagroal_auth_user_success` - Successful authentications
- `pgagroal_auth_user_bad_password` - Bad password attempts
- `pgagroal_auth_user_error` - Authentication errors

### Clients
- `pgagroal_client_wait` - Number of waiting clients
- `pgagroal_client_active` - Number of active clients
- `pgagroal_wait_time` - Client wait time
- `pgagroal_max_wait_time` - Maximum client wait time
- `pgagroal_total_wait_time` - Total accumulated client wait time

### Performance
- `pgagroal_query_count` - Total query count
- `pgagroal_tx_count` - Total transaction count
- `pgagroal_connection_query_count` - Per-connection query count

### Network
- `pgagroal_network_sent` - Total bytes sent
- `pgagroal_network_received` - Total bytes received
- `pgagroal_client_sockets` - Client socket count
- `pgagroal_self_sockets` - Self socket count

### Session
- `pgagroal_session_time_seconds_bucket` - Session time histogram

### Server
- `pgagroal_server_error` - Server error counts

### Logging
- `pgagroal_logging_info/warn/error/fatal` - Log level counts