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

#include <pgagroal.h>
#include <connection.h>
#include <network.h>
#include <utils.h>

#include <tsclient.h>
#include <mctf.h>

#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Send a slot and a descriptor like pgagroal_connection_transfer_write(), which
 * can only send a valid slot's descriptor */
static int
send_transfer(int client_fd, int32_t slot, int fd)
{
   char buf4[4];
   char buf2[2] = {0, 0};
   char control[CMSG_SPACE(sizeof(int))];
   struct iovec iov[1];
   struct msghdr msg;
   struct cmsghdr* cmptr;

   pgagroal_write_int32(&buf4, slot);
   if (write(client_fd, &buf4, sizeof(buf4)) != sizeof(buf4))
   {
      return 1;
   }

   memset(&control, 0, sizeof(control));
   memset(&msg, 0, sizeof(msg));
   iov[0].iov_base = &buf2[0];
   iov[0].iov_len = sizeof(buf2);
   msg.msg_iov = iov;
   msg.msg_iovlen = 1;
   msg.msg_control = control;
   msg.msg_controllen = sizeof(control);

   cmptr = CMSG_FIRSTHDR(&msg);
   cmptr->cmsg_level = SOL_SOCKET;
   cmptr->cmsg_type = SCM_RIGHTS;
   cmptr->cmsg_len = CMSG_LEN(sizeof(int));
   memcpy(CMSG_DATA(cmptr), &fd, sizeof(int));

   return sendmsg(client_fd, &msg, 0) != sizeof(buf2);
}

/* 1 if the other side closed fd within 10 seconds without sending anything */
static int
closed_by_peer(int fd)
{
   struct pollfd pfd;
   char c;

   pfd.fd = fd;
   pfd.events = POLLIN;
   pfd.revents = 0;

   if (poll(&pfd, 1, 10000) != 1)
   {
      return 0;
   }

   return read(fd, &c, 1) == 0;
}

/* Main must ignore a transfer message with a slot outside the pool, and close
 * the descriptor that came with it */
MCTF_TEST_NEGATIVE(test_pgagroal_transfer_invalid_slot)
{
   int ret = 0;
   int client_fd = -1;
   int sv[2] = {-1, -1};
   int32_t slot = -1;
   struct main_configuration* config = (struct main_configuration*)shmem;

   MCTF_ASSERT(config != NULL, cleanup, "no configuration");
   slot = config->max_connections;

   /* CONNECTION_TRANSFER: main receives sv[1], so sv[0] sees EOF only when main closed it */
   MCTF_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, cleanup, "socketpair failed");
   MCTF_ASSERT(pgagroal_connection_get(&client_fd) == 0, cleanup, "transfer connect failed");
   MCTF_ASSERT(pgagroal_connection_id_write(client_fd, CONNECTION_TRANSFER) == 0, cleanup, "transfer id write failed");
   MCTF_ASSERT(send_transfer(client_fd, slot, sv[1]) == 0, cleanup, "transfer write failed");
   close(sv[1]);
   sv[1] = -1;
   MCTF_ASSERT(closed_by_peer(client_fd), cleanup, "transfer: main kept the request open");
   MCTF_ASSERT(closed_by_peer(sv[0]), cleanup, "transfer: main kept the descriptor");
   pgagroal_disconnect(client_fd);
   client_fd = -1;

   /* CONNECTION_KILL */
   MCTF_ASSERT(pgagroal_connection_get(&client_fd) == 0, cleanup, "kill connect failed");
   MCTF_ASSERT(pgagroal_connection_id_write(client_fd, CONNECTION_KILL) == 0, cleanup, "kill id write failed");
   MCTF_ASSERT(pgagroal_connection_slot_write(client_fd, slot) == 0, cleanup, "kill slot write failed");
   MCTF_ASSERT(pgagroal_connection_generation_write(client_fd, 1) == 0, cleanup, "kill generation write failed");
   MCTF_ASSERT(closed_by_peer(client_fd), cleanup, "kill: main kept the request open");
   pgagroal_disconnect(client_fd);
   client_fd = -1;

   /* CONNECTION_GET_FD: without the check main reads past config->connections */
   MCTF_ASSERT(pgagroal_connection_get(&client_fd) == 0, cleanup, "get fd connect failed");
   MCTF_ASSERT(pgagroal_connection_id_write(client_fd, CONNECTION_GET_FD) == 0, cleanup, "get fd id write failed");
   MCTF_ASSERT(pgagroal_connection_slot_write(client_fd, slot) == 0, cleanup, "get fd slot write failed");
   MCTF_ASSERT(closed_by_peer(client_fd), cleanup, "get fd: main answered for an invalid slot");
   pgagroal_disconnect(client_fd);
   client_fd = -1;

   /* Main still serves clients */
   ret = pgagroal_tsclient_execute_pgbench(user, database, true, 0, 0, 0);
   MCTF_ASSERT(ret == 0, cleanup, "pgbench failed after the invalid slots");

cleanup:
   if (client_fd != -1)
   {
      pgagroal_disconnect(client_fd);
   }
   if (sv[0] != -1)
   {
      close(sv[0]);
   }
   if (sv[1] != -1)
   {
      close(sv[1]);
   }
   MCTF_FINISH();
}
