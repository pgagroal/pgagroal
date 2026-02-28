# Migration

## From 2.0.x to 2.1.0

### Vault Encryption

The key derivation for vault file encryption has been upgraded to
`PKCS5_PBKDF2_HMAC` (SHA-256, random 16-byte salt, 600,000 iterations).

This is a **breaking change**. Existing vault files encrypted with the
old method cannot be decrypted by version 2.1.0.

**Action required:**

1. Stop pgagroal
2. Delete the existing user files:
   - `pgagroal_users.conf`
   - `pgagroal_frontend_users.conf`.
   - `pgagroal_admins.conf`
   - `pgagroal_superuser.conf`
   - Vault users file (if applicable)
3. Delete the existing master key:
   ```
   rm ~/.pgagroal/master.key
   ```
4. Regenerate the master key:
   ```
   pgagroal-admin master-key
   ```
5. Re-add all users:
   ```
   pgagroal-admin user add -f <users_file>
   ```
6. Restart pgagroal
