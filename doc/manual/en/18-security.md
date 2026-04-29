\newpage

# Security

This chapter provides comprehensive security guidance for [**pgagroal**][pgagroal] deployments.

## Security Models

AES-GCM (Galois/Counter Mode) is the recommended encryption mode in `pgagroal`. It provides both confidentiality (encryption) and integrity/authenticity (verification), ensuring that encrypted data has not been tampered with. Legacy modes (AES-CBC and AES-CTR) have been removed to ensure the highest security standards and to leverage modern CPU optimizations.

### Why AES-GCM?

GCM is an Authenticated Encryption with Associated Data (AEAD) mode. Unlike CBC or CTR, which only provide confidentiality, GCM includes a built-in authentication tag. This protects against:
* **Integrity Protection**: GCM ensures that any unauthorized modification to the ciphertext is detected immediately during the decryption process, preventing the system from processing tampered vault entries.
* **Performance**: GCM is highly parallelizable and leverages **AES-NI** instructions on modern CPUs for incredible speeds (up to 6GB/s per core).

### Performance Benchmarks

You can verify your system's encryption performance using OpenSSL speed tests:

```sh
# Verify AES-NI support
cat /proc/cpuinfo | grep aes

# Test single-core performance
openssl speed -elapsed -evp aes-256-gcm

# Test multi-core performance (e.g., 8 cores)
openssl speed -multi 8 -elapsed -evp aes-256-gcm
```

Modern high-performance systems typically see throughput exceed **5GB/s** per core for AES-256-GCM. In `pgagroal`, the most significant performance gain comes from the **two-step key derivation**, which reduces the per-entry PBKDF2 overhead by **600,000x** (from 600,000 iterations to 1, once the master key is derived and cached). This ensures that even with hundreds of vault entries, operations remain instantaneous.

### Key Derivation and Caching

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

### Management protocol encryption

The management wire protocol has been updated in 2.1.x to exclusively use authenticated encryption (GCM). Legacy CBC and CTR modes are no longer supported.

While this does not affect on-disk data, it is a **breaking change for mixed-version deployments**:
- A 2.0.x `pgagroal-cli` or `pgagroal-vault` cannot communicate with a 2.1.x server.
- A 2.1.x `pgagroal-cli` or `pgagroal-vault` cannot interoperate with a 2.0.x server.

**Action required:**
- Upgrade the `pgagroal` server and all `pgagroal-cli`/`pgagroal-vault` tools to 2.1.x at the same time.
- After upgrading, restart the server and any long-running management clients to ensure they are all using the updated authenticated protocol.

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

pgagroal uses **AES-256-GCM** encryption for storing user credentials in vault files. This provides authenticated encryption, ensuring both confidentiality and integrity for all sensitive data.

Key derivation is implemented as a high-performance **two-step process** based on `PKCS5_PBKDF2_HMAC` with a SHA-256 hash:
1. **Master Key**: The user-supplied passphrase is processed with **600,000 PBKDF2 iterations** and a unique per-installation salt (stored in `master.key`) to derive a cached master key.
2. **Session Key**: For each ciphertext entry, a cryptographically random 16-byte salt and a single PBKDF2 iteration are used together with the master key to derive the specific AES-256-GCM key and IV.

Each encrypted entry uses a standardized 28-byte header followed by the ciphertext and a trailing 16-byte authentication tag:
* **Header (28 bytes)**:
  * `Salt` (16 bytes): Random salt used for session key derivation.
  * `IV` (12 bytes): Initialization Vector (GCM nonce).
* **Data**: Encrypted ciphertext.
* **Tag (16 bytes)**: GCM Authentication Tag appended at the **end** of the entry.

This architecture provides the security of the full iteration count for the main passphrase while allowing sub-second performance for bulk vault operations. Unlike older versions that supported multiple AES modes, pgagroal now exclusively uses GCM to provide built-in integrity protection (via an authentication tag). This ensures that any tampering with the vault file or the use of an incorrect master key will be immediately detected.

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
