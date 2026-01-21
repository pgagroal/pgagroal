==============
pgagroal-vault
==============

--------------------------------------------------------------------------------
Simple vault that hosts an HTTP server to handle user frontend password requests
--------------------------------------------------------------------------------

:Manual section: 1

SYNOPSIS
========

pgagroal-vault [ -c CONFIG_FILE ] [ -u USERS_FILE ]

DESCRIPTION
===========

**pgagroal-vault** is a basic HTTP server designed to handle special HTTP GET requests for retrieving pgagroal user passwords. When a client sends an HTTP GET request to ``http://<host_name>:<port>/users/<frontend_user>``, the vault extracts ``<frontend_user>`` from the URL. It then connects to the pgagroal main process to fetch the current ``<frontend_password>`` corresponding to the ``<frontend_user>``.

If the vault successfully fetches the ``<frontend_password>``, it responds with an HTTP status code 200 and includes ``<frontend_password>`` in the response body. Otherwise, the server responds with an HTTP 404 error indicating that the password for the specified user could not be found.

**Note:** For pgagroal-vault to operate correctly, the management port of the pgagroal server must be open and functional.

OPTIONS
=======

-c, --config CONFIG_FILE
  Set the path to the pgagroal_vault.conf file

-u, --users USERS_FILE
  Set the path to the pgagroal_vault_users.conf file

-d, --daemon
  Run as a daemon

-?, --help
  Display help

REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/pgagroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal.conf(5), pgagroal_hba.conf(5), pgagroal_databases.conf(5), pgagroal_vault.conf(5), pgagroal-cli(1), pgagroal-admin(1), pgagroal(1)
