=================
pgagroal_hba.conf
=================

-------------------------------------------------
Host based access configuration file for pgagroal
-------------------------------------------------

:Manual section: 5

DESCRIPTION
===========

pgagroal_hba.conf specifies the host based access pattern for pgagroal.

FORMAT
======

TYPE
  Specifies the access method for clients. Only host supported

DATABASE
  Specifies the database for the rule. Either specific name or all for all databases

USER
  Specifies the user for the rule. Either specific name or all for all users
  
ADDRESS
  Specifies the network for the rule. all for all networks, or IPv4 address with a mask (0.0.0.0/0) or IPv6 address with a mask (::0/0)

METHOD
  Specifies the authentication mode for the user. all for all methods, otherwise trust, reject, password, md5 or scram-sha-256

EXAMPLE
=======

::
   
  #
  # TYPE  DATABASE USER  ADDRESS  METHOD
  #
  host    all      all   all      all


REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/pgagroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal.conf(5), pgagroal_databases.conf(5), pgagroal_vault.conf(5), pgagroal(1), pgagroal-cli(1), pgagroal-admin(1), pgagroal-vault(1)
