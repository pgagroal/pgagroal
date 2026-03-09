\newpage

# Web console

The pgagroal web console is a lightweight HTTP UI for monitoring PostgreSQL
connection pooler metrics in real time. It displays metrics organized by category, 
with filtering options and multiple view modes to help you drill down into the data you need.

## Enable the console

Add a console port to `pgagroal.conf`:

```ini
[pgagroal]
host = 127.0.0.1
metrics = 5002
console = 5003
```

The console requires the metrics endpoint to be enabled. Start pgagroal:

```sh
pgagroal -c /etc/pgagroal/pgagroal.conf -a /etc/pgagroal/pgagroal_hba.conf
```

## Open the console

Navigate to your console endpoint:

```
http://localhost:5003/
```

## Console flow & pages

### 1. Home page (overview)

When you first load the console, you see the **home page** with:

- **Service header** showing pgagroal service status (Running or Unavailable)
- **Version** of pgagroal
- **Category selector** dropdown to choose which metric group to view
- **View selector** (Simple or Advanced mode)
- **Server filter** dropdown to choose which PostgreSQL servers to display
- **Metrics table** showing the selected category
- **Refresh** button to reload metrics and status
- **Theme toggle** button (Dark/Light)

### 2. Home page—simple view

The default simple view shows:
- **Metric name** (column 1)
- **Value** (column 2)
- **Label columns** (additional columns) — each label key appears as its own column

![Web console home page in simple view](images/console_home_simple.png)

### 3. Home page—advanced view

Toggle to advanced view to see:
- **Metric name** (column 1)
- **Type** (gauge, counter, histogram, etc.) — (column 2)
- **Value** (column 3)
- **Labels** (column 4) — one comma-separated column, e.g., `database=mydb, name=primary`

![Web console home page in advanced view](images/console_home_advanced.png)

### 4. Category organization

Metrics are automatically organized into **categories** based on shared prefixes:
- `pgagroal_state_*` → one category
- `pgagroal_server_*` → another category
- etc.

Use the **Category selector** to switch between categories. The page displays all
metrics in the selected category.

### 5. Server filter

The **Server filter** dropdown:
- Shows all configured PostgreSQL servers
- Allows multi-select (check/uncheck each server)
- The metrics table updates to show rows only from selected servers

![Web console home page server filter](images/console_home_server_filter.png)

### 6. Refresh button

Click **Refresh** in the header (next to the **Updated** timestamp) to reload
all metrics and service status.

## API endpoints

- `/` — Main console (home page)
- `/api` — JSON endpoint with all metrics (useful for scripting)

## Theme toggle

Click the theme button (moon/sun icon) in the top right to switch between:
- **Light mode** (default, white background)
- **Dark mode** (dark background)

Theme preference is saved in your browser's local storage.

## Service status values

The header shows:
- **Running** — pgagroal management service is reachable
- **Unavailable** — pgagroal management service is not reachable

## Troubleshooting

- **No metrics displayed?**
  - Ensure `metrics` port is enabled in `pgagroal.conf`
  - Verify the Prometheus endpoint is reachable on the configured host

- **Service shows "Unavailable"?**
  - Check that `unix_socket_dir` is writable
  - Confirm the directory path in `pgagroal.conf` is accessible to the pgagroal process

- **Unable to access the console?**
  - Verify the console port is not blocked by firewall
  - Check that the console host/port in `pgagroal.conf` matches your URL