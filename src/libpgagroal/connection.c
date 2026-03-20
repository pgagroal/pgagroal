/*
 * Copyright (C) 2026 The pgagroal community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* pgagroal */
#include <pgagroal.h>
#include <connection.h>
#include <logging.h>
#include <memory.h>
#include <network.h>
#include <utils.h>

/* system */
#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#ifdef __NetBSD__
typedef unsigned char u_char;
#endif

static int read_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_socket(int socket, void* buf, size_t size);
static int write_ssl(SSL* ssl, void* buf, size_t size);

int
pgagroal_connection_get(int* client_fd)
{
   int fd;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   *client_fd = -1;

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, TRANSFER_UDS, &fd))
   {
      pgagroal_log_warn("pgagroal_management_transfer_connection: get connect: %d", fd);
      errno = 0;
      goto error;
   }

   *client_fd = fd;

   return 0;

error:

   return 1;
}

int
pgagroal_connection_get_pid(pid_t pid, int* client_fd)
{
   char* f = NULL;
   int fd;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   *client_fd = -1;

   f = pgagroal_append(f, ".s.pgagroal.");
   f = pgagroal_append_int(f, (int)pid);

   if (pgagroal_connect_unix_socket(config->unix_socket_dir, f, &fd))
   {
      pgagroal_log_warn("pgagroal_management_transfer_connection: get_pid connect = %d, pid = %d, f = [%s]", fd, pid, f);
      errno = 0;
      goto error;
   }

   *client_fd = fd;

   free(f);

   return 0;

error:

   free(f);

   return 1;
}

int
pgagroal_connection_id_write(int client_fd, int id)
{
   char buf4[4];

   memset(&buf4[0], 0, sizeof(buf4));
   pgagroal_write_int32(&buf4, id);

   if (write_complete(NULL, client_fd, &buf4, sizeof(buf4)))
   {
      pgagroal_log_warn("pgagroal_connection_id_write: %d %s", client_fd, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_connection_id_read(int client_fd, int* id)
{
   char buf4[4];

   *id = -1;

   memset(&buf4[0], 0, sizeof(buf4));
   if (read_complete(NULL, client_fd, &buf4, sizeof(buf4)))
   {
      pgagroal_log_warn("pgagroal_connection_id_read: %d %s", client_fd, strerror(errno));
      errno = 0;
      goto error;
   }

   *id = pgagroal_read_int32(&buf4);

   return 0;

error:

   return 1;
}

int
pgagroal_connection_transfer_write(int client_fd, int32_t slot)
{
   struct cmsghdr* cmptr = NULL;
   struct iovec iov[1];
   struct msghdr msg;
   char buf2[2];
   char buf4[4];
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   memset(&buf4[0], 0, sizeof(buf4));
   pgagroal_write_int32(&buf4, slot);

   if (write_complete(NULL, client_fd, &buf4, sizeof(buf4)))
   {
      pgagroal_log_warn("pgagroal_management_transfer_connection: write: %d %s", client_fd, strerror(errno));
      errno = 0;
      goto error;
   }

   /* Write file descriptor */
   memset(&buf2[0], 0, sizeof(buf2));

   iov[0].iov_base = &buf2[0];
   iov[0].iov_len = sizeof(buf2);

   cmptr = malloc(CMSG_SPACE(sizeof(int)));
   memset(cmptr, 0, CMSG_SPACE(sizeof(int)));
   cmptr->cmsg_level = SOL_SOCKET;
   cmptr->cmsg_type = SCM_RIGHTS;
   cmptr->cmsg_len = CMSG_LEN(sizeof(int));

   msg.msg_name = NULL;
   msg.msg_namelen = 0;
   msg.msg_iov = iov;
   msg.msg_iovlen = 1;
   msg.msg_control = cmptr;
   msg.msg_controllen = CMSG_SPACE(sizeof(int));
   msg.msg_flags = 0;
   *(int*)CMSG_DATA(cmptr) = config->connections[slot].fd;

   if (sendmsg(client_fd, &msg, 0) != 2)
   {
      goto error;
   }

   free(cmptr);

   return 0;

error:
   if (cmptr)
   {
      free(cmptr);
   }

   return 1;
}

int
pgagroal_connection_transfer_read(int client_fd, int* slot, int* fd)
{
   int nr;
   char buf2[2];
   char buf4[4];
   struct cmsghdr* cmptr = NULL;
   struct iovec iov[1];
   struct msghdr msg;

   *slot = -1;
   *fd = -1;

   memset(&buf4[0], 0, sizeof(buf4));
   if (read_complete(NULL, client_fd, &buf4, sizeof(buf4)))
   {
      pgagroal_log_warn("pgagroal_connection_transfer_read: %d %s", client_fd, strerror(errno));
      errno = 0;
      goto error;
   }

   *slot = pgagroal_read_int32(&buf4);

   memset(&buf2[0], 0, sizeof(buf2));

   iov[0].iov_base = &buf2[0];
   iov[0].iov_len = sizeof(buf2);

   cmptr = (struct cmsghdr*)calloc(1, CMSG_SPACE(sizeof(int)));
   if (cmptr == NULL)
   {
      goto error;
   }
   cmptr->cmsg_len = CMSG_LEN(sizeof(int));
   cmptr->cmsg_level = SOL_SOCKET;
   cmptr->cmsg_type = SCM_RIGHTS;

   msg.msg_name = NULL;
   msg.msg_namelen = 0;
   msg.msg_iov = iov;
   msg.msg_iovlen = 1;
   msg.msg_control = cmptr;
   msg.msg_controllen = CMSG_SPACE(sizeof(int));
   msg.msg_flags = 0;

   if ((nr = recvmsg(client_fd, &msg, 0)) < 0)
   {
      goto error;
   }
   else if (nr == 0)
   {
      goto error;
   }

   *fd = *(int*)CMSG_DATA(cmptr);

   free(cmptr);

   return 0;

error:

   if (cmptr != NULL)
   {
      free(cmptr);
   }

   return 1;
}

int
pgagroal_connection_slot_write(int client_fd, int32_t slot)
{
   char buf4[4];

   memset(&buf4[0], 0, sizeof(buf4));
   pgagroal_write_int32(&buf4, slot);

   if (write_complete(NULL, client_fd, &buf4, sizeof(buf4)))
   {
      pgagroal_log_warn("pgagroal_connection_slot_write: %d %s", client_fd, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_connection_slot_read(int client_fd, int32_t* slot)
{
   char buf4[4];

   *slot = -1;

   memset(&buf4[0], 0, sizeof(buf4));
   if (read_complete(NULL, client_fd, &buf4, sizeof(buf4)))
   {
      pgagroal_log_warn("pgagroal_connection_id_read: %d %s", client_fd, strerror(errno));
      errno = 0;
      goto error;
   }

   *slot = (int32_t)pgagroal_read_int32(&buf4);

   return 0;

error:

   return 1;
}

int
pgagroal_connection_socket_write(int client_fd, int socket)
{
   char buf4[4];

   memset(&buf4[0], 0, sizeof(buf4));
   pgagroal_write_int32(&buf4, socket);

   if (write_complete(NULL, client_fd, &buf4, sizeof(buf4)))
   {
      pgagroal_log_warn("pgagroal_connection_socket_write: %d %s", client_fd, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_connection_socket_read(int client_fd, int* socket)
{
   char buf4[4];

   *socket = -1;

   memset(&buf4[0], 0, sizeof(buf4));
   if (read_complete(NULL, client_fd, &buf4, sizeof(buf4)))
   {
      pgagroal_log_warn("pgagroal_connection_id_read: %d %s", client_fd, strerror(errno));
      errno = 0;
      goto error;
   }

   *socket = pgagroal_read_int32(&buf4);

   return 0;

error:

   return 1;
}

int
pgagroal_connection_pid_write(int client_fd, pid_t pid)
{
   char buf4[4];

   memset(&buf4[0], 0, sizeof(buf4));
   pgagroal_write_int32(&buf4, (int)pid);

   if (write_complete(NULL, client_fd, &buf4, sizeof(buf4)))
   {
      pgagroal_log_warn("pgagroal_connection_pid_write: %d %s", client_fd, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_connection_pid_read(int client_fd, pid_t* pid)
{
   char buf4[4];

   *pid = -1;

   memset(&buf4[0], 0, sizeof(buf4));
   if (read_complete(NULL, client_fd, &buf4, sizeof(buf4)))
   {
      pgagroal_log_warn("pgagroal_connection_id_read: %d %s", client_fd, strerror(errno));
      errno = 0;
      goto error;
   }

   *pid = (pid_t)pgagroal_read_int32(&buf4);

   return 0;

error:

   return 1;
}

static int
read_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   ssize_t r;
   size_t offset;
   size_t needs;
   int retries;

   offset = 0;
   needs = size;
   retries = 0;

read:

   if (ssl == NULL)
   {
      r = read(socket, buf + offset, needs);
   }
   else
   {
      r = SSL_read(ssl, buf + offset, needs);
   }

   if (r == -1)
   {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
         errno = 0;
         goto read;
      }

      goto error;
   }
   else if (r < (ssize_t)needs)
   {
      /* Sleep for 10ms */
      SLEEP(10000000L);

      if (retries < 100)
      {
         offset += r;
         needs -= r;
         retries++;
         goto read;
      }
      else
      {
         errno = EINVAL;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static int
write_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   if (ssl == NULL)
   {
      return write_socket(socket, buf, size);
   }

   return write_ssl(ssl, buf, size);
}

static int
write_socket(int socket, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = write(socket, buf + offset, remaining);

      if (likely(numbytes == size))
      {
         return 0;
      }
      else if (numbytes != -1)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == size)
         {
            return 0;
         }

         pgagroal_log_trace("Write %d - %zd/%zd vs %zd", socket, numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         switch (errno)
         {
            case EAGAIN:
               keep_write = true;
               errno = 0;
               break;
            default:
               keep_write = false;
               break;
         }
      }
   }
   while (keep_write);

   return 1;
}

static int
write_ssl(SSL* ssl, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = SSL_write(ssl, buf + offset, remaining);

      if (likely(numbytes == size))
      {
         return 0;
      }
      else if (numbytes > 0)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == size)
         {
            return 0;
         }

         pgagroal_log_trace("SSL/Write %d - %zd/%zd vs %zd", SSL_get_fd(ssl), numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         int err = SSL_get_error(ssl, numbytes);

         switch (err)
         {
            case SSL_ERROR_ZERO_RETURN:
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
               errno = 0;
               keep_write = true;
               break;
            case SSL_ERROR_SYSCALL:
               pgagroal_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
            case SSL_ERROR_SSL:
               pgagroal_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
         }
         ERR_clear_error();

         if (!keep_write)
         {
            return 1;
         }
      }
   }
   while (keep_write);

   return 1;
}

/* int */
/* pgagroal_management_client_done(pid_t pid) */
/* { */
/*    char buf[4]; */
/*    int fd; */
/*    struct main_configuration* config; */

/*    config = (struct main_configuration*)shmem; */

/*    if (pgagroal_connect_unix_socket(config->unix_socket_dir, TRANSFER_UDS, &fd)) */
/*    { */
/*       pgagroal_log_warn("pgagroal_management_client_done: connect: %d", fd); */
/*       errno = 0; */
/*       goto error; */
/*    } */

/*    if (write_header(NULL, fd, MANAGEMENT_CLIENT_DONE, -1)) */
/*    { */
/*       pgagroal_log_warn("pgagroal_management_client_done: write: %d", fd); */
/*       errno = 0; */
/*       goto error; */
/*    } */

/*    memset(&buf, 0, sizeof(buf)); */
/*    pgagroal_write_int32(buf, pid); */

/*    if (write_complete(NULL, fd, &buf, sizeof(buf))) */
/*    { */
/*       pgagroal_log_warn("pgagroal_management_client_done: write: %d %s", fd, strerror(errno)); */
/*       errno = 0; */
/*       goto error; */
/*    } */

/*    pgagroal_disconnect(fd); */

/*    return 0; */

/* error: */
/*    pgagroal_disconnect(fd); */

/*    return 1; */
/* } */

/* int */
/* pgagroal_management_client_fd(int32_t slot, pid_t pid) */
/* { */
/*    char p[MISC_LENGTH]; */
/*    int fd; */
/*    struct main_configuration* config; */
/*    struct cmsghdr* cmptr = NULL; */
/*    struct iovec iov[1]; */
/*    struct msghdr msg; */
/*    char buf[2]; /\* send_fd()/recv_fd() 2-byte protocol *\/ */

/*    config = (struct main_configuration*)shmem; */

/*    memset(&p, 0, sizeof(p)); */
/*    snprintf(&p[0], sizeof(p), ".s.%d", pid); */

/*    if (pgagroal_connect_unix_socket(config->unix_socket_dir, &p[0], &fd)) */
/*    { */
/*       pgagroal_log_debug("pgagroal_management_client_fd: connect: %d", fd); */
/*       errno = 0; */
/*       goto unavailable; */
/*    } */

/*    if (write_header(NULL, fd, MANAGEMENT_CLIENT_FD, slot)) */
/*    { */
/*       pgagroal_log_warn("pgagroal_management_client_fd: write: %d", fd); */
/*       errno = 0; */
/*       goto error; */
/*    } */

/*    /\* Write file descriptor *\/ */
/*    iov[0].iov_base = buf; */
/*    iov[0].iov_len = 2; */
/*    msg.msg_iov = iov; */
/*    msg.msg_iovlen = 1; */
/*    msg.msg_name = NULL; */
/*    msg.msg_namelen = 0; */

/*    cmptr = malloc(CMSG_LEN(sizeof(int))); */
/*    cmptr->cmsg_level = SOL_SOCKET; */
/*    cmptr->cmsg_type = SCM_RIGHTS; */
/*    cmptr->cmsg_len = CMSG_LEN(sizeof(int)); */
/*    msg.msg_control = cmptr; */
/*    msg.msg_controllen = CMSG_LEN(sizeof(int)); */
/*    *(int*)CMSG_DATA(cmptr) = config->connections[slot].fd; */
/*    buf[1] = 0; /\* zero status means OK *\/ */
/*    buf[0] = 0; /\* null byte flag to recv_fd() *\/ */

/*    if (sendmsg(fd, &msg, 0) != 2) */
/*    { */
/*       goto error; */
/*    } */

/*    free(cmptr); */
/*    pgagroal_disconnect(fd); */

/*    return 0; */

/* unavailable: */
/*    free(cmptr); */
/*    pgagroal_disconnect(fd); */

/*    return 1; */

/* error: */
/*    free(cmptr); */
/*    pgagroal_disconnect(fd); */
/*    pgagroal_kill_connection(slot, NULL); */

/*    return 1; */
/* } */

/* int */
/* pgagroal_management_remove_fd(int32_t slot, int socket, pid_t pid) */
/* { */
/*    char p[MISC_LENGTH]; */
/*    int fd; */
/*    char buf[4]; */
/*    struct main_configuration* config; */

/*    config = (struct main_configuration*)shmem; */

/*    if (atomic_load(&config->states[slot]) == STATE_NOTINIT) */
/*    { */
/*       return 0; */
/*    } */

/*    memset(&p, 0, sizeof(p)); */
/*    snprintf(&p[0], sizeof(p), ".s.%d", pid); */

/*    if (pgagroal_connect_unix_socket(config->unix_socket_dir, &p[0], &fd)) */
/*    { */
/*       pgagroal_log_debug("pgagroal_management_remove_fd: slot %d state %d database %s user %s socket %d pid %d connect: %d", */
/*                          slot, atomic_load(&config->states[slot]), */
/*                          config->connections[slot].database, config->connections[slot].username, socket, pid, fd); */
/*       errno = 0; */
/*       goto error; */
/*    } */

/*    if (write_header(NULL, fd, MANAGEMENT_REMOVE_FD, slot)) */
/*    { */
/*       pgagroal_log_warn("pgagroal_management_remove_fd: write: %d", fd); */
/*       errno = 0; */
/*       goto error; */
/*    } */

/*    pgagroal_write_int32(&buf, socket); */
/*    if (write_complete(NULL, fd, &buf, sizeof(buf))) */
/*    { */
/*       pgagroal_log_warn("pgagroal_management_remove_fd: write: %d %s", fd, strerror(errno)); */
/*       errno = 0; */
/*       goto error; */
/*    } */

/*    pgagroal_disconnect(fd); */

/*    return 0; */

/* error: */
/*    pgagroal_disconnect(fd); */

/*    return 1; */
/* } */

/* /\* */
/*  * Utility function to convert PGAGROAL_VERSION into a number. */
/*  * The major version is represented by a single digit. */
/*  * For minor and patch, a leading 0 is added if they are single digits. */
/*  *\/ */
/* static int */
/* pgagroal_executable_version_number(char* version, size_t version_size) */
/* { */
/*    int major; */
/*    int minor; */
/*    int patch; */

/*    long val; */

/*    char* del = "."; */
/*    char* endptr; */

/*    if (version == NULL) */
/*    { */
/*       version = PGAGROAL_VERSION; */
/*       version_size = sizeof(version); */
/*    } */

/*    char buf[version_size]; */

/*    memcpy(buf, version, sizeof(buf)); */

/*    char* token = strtok(buf, del); */
/*    val = strtol(token, &endptr, 10); */
/*    if (errno == ERANGE || val <= LONG_MIN || val >= LONG_MAX) */
/*    { */
/*       goto error; */
/*    } */
/*    major = (int)val; */

/*    token = strtok(NULL, del); */
/*    val = strtol(token, &endptr, 10); */
/*    if (errno == ERANGE || val <= LONG_MIN || val >= LONG_MAX) */
/*    { */
/*       goto error; */
/*    } */
/*    minor = (int)val; */

/*    token = strtok(NULL, del); */
/*    val = strtol(token, &endptr, 10); */
/*    if (errno == ERANGE || val <= LONG_MIN || val >= LONG_MAX) */
/*    { */
/*       goto error; */
/*    } */
/*    patch = (int)val; */

/*    int version_number = (major % 10) * 10000 + (minor / 10) * 1000 + (minor % 10) * 100 + patch; */

/*    if (version_number < INT_MIN || version_number > INT_MAX || version_number < 10700) */
/*    { */
/*       goto error; */
/*    } */

/*    return version_number; */

/* error: */
/*    pgagroal_log_debug("pgagroal_get_executable_number got overflowed or suspicious value: %s %s", version, strerror(errno)); */
/*    errno = 0; */
/*    return 1; */
/* } */

/* /\* */
/*  * Utility function to convert back version_number into a string. */
/*  *\/ */
/* static int */
/* pgagroal_executable_version_string(char** version_string, int version_number) */
/* { */
/*    char* v = NULL; */
/*    int major = version_number / 10000; */
/*    int minor = (version_number / 100) % 100; */
/*    int patch = version_number % 100; */

/*    v = pgagroal_append_int(v, major); */
/*    v = pgagroal_append(v, "."); */
/*    v = pgagroal_append_int(v, minor); */
/*    v = pgagroal_append(v, "."); */
/*    v = pgagroal_append_int(v, patch); */

/*    *version_string = v; */

/*    return 0; */
/* } */

/* /\* */
/*  * Utility function to convert command into a string. */
/*  *\/ */
/* static char* */
/* pgagroal_executable_name(int command) */
/* { */
/*    switch (command) */
/*    { */
/*       case PGAGROAL_EXECUTABLE: */
/*          return "pgagroal"; */
/*       case PGAGROAL_EXECUTABLE_CLI: */
/*          return "pgagroal-cli"; */
/*       case PGAGROAL_EXECUTABLE_VAULT: */
/*          return "pgagroal-vault"; */
/*       default: */
/*          pgagroal_log_debug("pgagroal_get_command_name got unexpected value: %d", command); */
/*          return NULL; */
/*    } */
/* } */
