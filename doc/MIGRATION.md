# Migration

## From 2.0.x to 2.1.x

### Vault Encryption

The vault encryption has been upgraded to **AES-256-GCM**, providing authenticated encryption for all sensitive data. Key derivation is implemented as a two-step process: **600,000 PBKDF2 iterations** are used with a unique per-installation salt (stored in `master.key`) to derive a cached Master Key from the user-supplied passphrase; then, for each entry, a cryptographically random 16-byte salt and a single PBKDF2 iteration are used together with the master key to derive the specific AES-256-GCM key and IV.

This is a **breaking change** for version 2.1.x. Existing vault files encrypted with the old method cannot be decrypted, and users are required to regenerate their vaults following the steps below.

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

### Management protocol encryption

The management wire protocol has been updated in 2.1.x to exclusively use authenticated encryption (GCM). Legacy CBC and CTR modes are no longer supported.

While this does not affect on-disk data, it is a **breaking change for mixed-version deployments**:
- A 2.0.x `pgagroal-cli` or `pgagroal-vault` cannot communicate with a 2.1.x server.
- A 2.1.x `pgagroal-cli` or `pgagroal-vault` cannot interoperate with a 2.0.x server.

### Encryption Format

The AES encryption format has been upgraded to a unified 28-byte header followed by data and a trailing Authentication Tag (AEAD). This ensures maximum security and cross-project consistency.

* **Header (28 bytes)**:
  * `Salt` (16 bytes): Per-file/entry random salt.
  * `IV` (12 bytes): Initialization Vector field (GCM native length).
* **Data**: Encrypted ciphertext.
* **Tag (16 bytes)**: GCM Authentication Tag appended at the **end of the entry/ciphertext blob**.

**Action required:**
- Upgrade the `pgagroal` server and all `pgagroal-cli`/`pgagroal-vault` tools to 2.1.x at the same time.
- After upgrading, restart the server and any long-running management clients to ensure they are all using the updated authenticated protocol.
