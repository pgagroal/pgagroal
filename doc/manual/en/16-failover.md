\newpage

# Failover

[**pgagroal**][pgagroal] can failover a PostgreSQL instance if clients can't write to it.

## Configuration

In `pgagroal.conf` define:

```
failover = on
failover_script = /path/to/myscript.sh
```

The script will be run as the same user as the pgagroal process so proper
permissions (access and execution) must be in place.

## Failover Script

The following information is passed to the script as parameters:

1. Old primary host
2. Old primary port
3. New primary host
4. New primary port

### Example Script

A basic failover script could look like:

```sh
#!/bin/bash

OLD_PRIMARY_HOST=$1
OLD_PRIMARY_PORT=$2
NEW_PRIMARY_HOST=$3
NEW_PRIMARY_PORT=$4

# Promote the new primary
ssh -tt -o StrictHostKeyChecking=no postgres@${NEW_PRIMARY_HOST} pg_ctl promote -D /mnt/pgdata

if [ $? -ne 0 ]; then
  exit 1
fi

exit 0
```

### Script Requirements

- The script is assumed successful if it has an exit code of 0
- Otherwise both servers will be recorded as failed
- The script should handle promotion of the new primary server
- Consider implementing proper error handling and logging

## Advanced Failover Scenarios

### Multiple Replica Configuration

When multiple replicas are available, the failover script can implement logic to:

1. Check replica lag to select the best candidate
2. Ensure proper promotion sequence
3. Update DNS or load balancer configuration
4. Notify monitoring systems

### Automatic Failback

Consider implementing automatic failback when the original primary becomes available:

```sh
#!/bin/bash

# Check if original primary is healthy
if pg_isready -h $OLD_PRIMARY_HOST -p $OLD_PRIMARY_PORT; then
    # Implement failback logic
    echo "Original primary is healthy, considering failback"
fi
```

## Monitoring Failover

Monitor failover events through:

- **Log files**: Check pgagroal logs for failover events
- **Prometheus metrics**: Monitor server status changes
- **External monitoring**: Implement alerts for failover events

## Best Practices

1. **Test failover scripts** regularly in non-production environments
2. **Monitor replica lag** to ensure replicas are suitable for promotion
3. **Implement proper logging** in failover scripts for troubleshooting
4. **Consider network partitions** and split-brain scenarios
5. **Document failover procedures** for operational teams
6. **Use configuration management** to ensure consistent failover scripts across environments