\newpage

# Remote administration

**Enable remote management**

On the pooler machine, you need to enable the remote management. In order to do so,
add the `management` setting to the main `pgagroal.conf` configuration file.
The value of setting is the number of a free TCP/IP port to which the remote
management will connect to.

With your editor of choice, edit the `/etc/pgagroal/pgagroal.conf` file and add the
`management` option likely the following:

```
management = 2347
```

under the `[pgagroal]` section, so that the configuration file looks like:

```
[pgagroal]
...
management = 2347
```

See [the pgagroal configuration settings](https://github.com/pgagroal/pgagroal/blob/master/doc/CONFIGURATION.md#pgagroal) for more details.

**Add remote admin user**

Remote management is done via a specific admin user, that has to be created within the pooler vault.
As the [**pgagroal**][pgagroal] operating system user, run the following command:

```
cd /etc/pgagroal
pgagroal-admin -f /etc/pgagroal/pgagroal_admins.conf -U admin -P admin1234 add-user
```

The above will create the `admin` username with the `admin1234` password.

**We strongly encourage you to choose non trivial usernames and passwords!**


**Restart pgagroal**

In order to make the changes available, and therefore activate the remote management, you have to restart [**pgagroal**][pgagroal], for example by issuing the following commands from the [**pgagroal**][pgagroal] operatng system user:

```
pgagroal-cli shutdown
pgagroal -d
```

**Connect via remote administration interface**

In order to connect remotely, you need to specify at least the `-h` and `-p` flags on the `pgagroal-cli` command line. Such flags will tell `pgagroal-cli` to connect to a remote host. You can also specify the username you want to connect with by specifying the `-U` flag.
So, to get the status of the pool remotely, you can issue:

```
pgagroal-cli -h localhost -p 2347 -U admin status
```

and type the password `admin1234` when asked for it.

If you don't specify the `-U` flag on the command line, you will be asked for a username too.

Please note that the above example uses `localhost` as the remote host, but clearly you can specify any *real* remote host you want to manage.
