\newpage

# Security

This chapter provides comprehensive security guidance for [**pgagroal**][pgagroal] deployments.

## Security Models

[**pgagroal**][pgagroal] supports multiple security models to meet different deployment requirements.

### Pass-through Security

[**pgagroal**][pgagroal] uses pass-through security by default.

This means that [**pgagroal**][pgagroal] delegates to PostgreSQL to determine if the credentials used are valid.

Once a connection is obtained [**pgagroal**][pgagroal] will replay the previous communication sequence to verify
the new client. This only works for connections using `trust`, `password` or `md5` authentication
methods, so `scram-sha-256` based connections are not cached.

**Security Considerations:**
- This can lead to replay attacks against `md5` based connections since the hash doesn't change
- Make sure that [**pgagroal**][pgagroal] is deployed on a private trusted network
- Consider using either a user vault or authentication query instead

### User Vault

A user vault is a vault which defines the known users and their password.

The vault is static, and is managed through the `pgagroal-admin` tool.

The user vault is specified using the `-u` or `--users` command line parameter.

#### Frontend Users

The `-F` or `--frontend` command line parameter allows users to be defined for the client to
[**pgagroal**][pgagroal] authentication. This allows the setup to use different passwords for the [**pgagroal**][pgagroal] to
PostgreSQL authentication.

All users defined in the frontend authentication must be defined in the user vault (`-u`).

Frontend users (`-F`) requires a user vault (`-u`) to be defined.

#### Vault Encryption

pgagroal uses AES-256-CBC encryption for storing user credentials in vault files. Key derivation uses PKCS5_PBKDF2_HMAC with a SHA-256 hash, a cryptographically random 16-byte salt, and a high iteration count to protect against brute-force attacks.

### Authentication Query

Authentication query will use the below defined function to query the database
for the user password:

```sql
CREATE FUNCTION public.pgagroal_get_password(
  IN  p_user     name,
  OUT p_password text
) RETURNS text
LANGUAGE sql SECURITY DEFINER SET search_path = pg_catalog AS
$SELECT passwd FROM pg_shadow WHERE usename = p_user$;
```

This function needs to be installed in each database.

## Network Security

### Host-Based Authentication

Configure `pgagroal_hba.conf` to restrict access:

```
# TYPE  DATABASE USER  ADDRESS  METHOD
host    mydb     myuser 192.168.1.0/24  scram-sha-256
host    mydb     myuser 10.0.0.0/8      scram-sha-256
```

### TLS Configuration

For complete TLS setup, see [Transport Level Security (TLS)](#transport-level-security-tls).

Key security considerations:
- Use strong cipher suites
- Regularly update certificates
- Implement proper certificate validation
- Consider mutual TLS authentication

## Access Control

### User Management

Use `pgagroal-admin` to manage users securely:

```sh
# Create master key
pgagroal-admin -g master-key

# Add users with strong passwords
pgagroal-admin -f pgagroal_users.conf user add
```

### Database Access Control

Configure database-specific access in `pgagroal_databases.conf`:

```
# DATABASE USER    MAX_SIZE INITIAL_SIZE MIN_SIZE
production myuser  10       5            2
test       testuser 5       2            1
```

### Administrative Access

Secure administrative access:

```sh
# Create admin users with strong credentials
pgagroal-admin -f pgagroal_admins.conf -U admin user add
```

## Hardening Guidelines

### System-Level Security

1. **Run as dedicated user**: Never run pgagroal as root
2. **File permissions**: Ensure configuration files have appropriate permissions
3. **Network isolation**: Deploy on private networks when possible
4. **Firewall rules**: Restrict access to pgagroal ports
5. **Log monitoring**: Monitor logs for suspicious activity

### Configuration Security

1. **Strong passwords**: Use complex passwords for all users
2. **Regular rotation**: Implement password rotation policies
3. **Minimal privileges**: Grant only necessary database permissions
4. **Connection limits**: Set appropriate connection limits per user/database

### Monitoring and Auditing

1. **Enable connection logging**:
   ```ini
   log_connections = on
   log_disconnections = on
   ```

2. **Monitor failed authentication attempts**
3. **Set up alerts for unusual connection patterns**
4. **Regular security audits of user accounts and permissions**

## Security Best Practices

### Production Deployment

1. **Use TLS encryption** for all connections
2. **Implement proper certificate management**
3. **Regular security updates** of pgagroal and dependencies
4. **Network segmentation** to isolate database traffic
5. **Backup and disaster recovery** procedures

### Development and Testing

1. **Separate environments** for development, testing, and production
2. **Test security configurations** before production deployment
3. **Use different credentials** for each environment
4. **Regular penetration testing** of the complete stack

### Compliance Considerations

For environments requiring compliance (PCI DSS, HIPAA, etc.):

1. **Encryption at rest and in transit**
2. **Audit logging** of all database access
3. **Access control documentation**
4. **Regular security assessments**
5. **Incident response procedures**

## Security Troubleshooting

### Common Security Issues

**Authentication failures:**
- Check user vault configuration
- Verify password hashes
- Review HBA configuration

**TLS connection issues:**
- Verify certificate validity
- Check cipher suite compatibility
- Review TLS configuration

**Access denied errors:**
- Check HBA rules
- Verify user permissions
- Review database access configuration

### Security Monitoring

Monitor these security-related metrics:
- Failed authentication attempts
- Unusual connection patterns
- Certificate expiration dates
- User account activity
- Administrative actions