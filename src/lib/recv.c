/*
 * Copyright (C) 2013 - David Goulet <dgoulet@ev0ke.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2 only, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <assert.h>
#include <stdlib.h>

#include <common/log.h>

#include "torsocks.h"

/*
 * Torsocks call for recvmsg(2)
 *
 * We only hijack this call to handle the FD passing between process on Unix
 * socket. If an INET/INET6 socket is recevied, we stop everything because at
 * that point we can't guarantee traffic going through Tor.
 */
LIBC_RECVMSG_RET_TYPE tsocks_recvmsg(LIBC_RECVMSG_SIG)
{
	int fd;
	ssize_t ret = 0;
	char dummy, recv_fd[CMSG_SPACE(sizeof(fd))];
	struct iovec iov[1];
	struct cmsghdr *cmsg;
	struct msghdr msg;

	memset(&msg, 0, sizeof(msg));

	/* Prepare to receive the structures */
	iov[0].iov_base = &dummy;
	iov[0].iov_len = 1;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = recv_fd;
	msg.msg_controllen = sizeof(recv_fd);

	do {
		/* Just peek the data to inspect the payload for fd. */
		ret = tsocks_libc_recvmsg(__sockfd, &msg, MSG_PEEK);
	} while (ret < 0 && errno == EINTR);

	cmsg = CMSG_FIRSTHDR(&msg);
	if (!cmsg) {
		goto end;
	}

	/*
	 * Detecting FD passing, the next snippet of code will check if we get a
	 * inet/inet6 socket. If so, everything stops immediately before going
	 * further.
	 */
	if (cmsg->cmsg_type == SCM_RIGHTS || cmsg->cmsg_level == SOL_SOCKET) {
		socklen_t optlen;
		int sock_dom;

		memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));

		/* Get socket domain. */
		optlen = sizeof(sock_dom);
		ret = getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &sock_dom, &optlen);
		if (ret < 0) {
			/* Use the getsockopt() errno value. */
			goto end;
		}

		if (sock_dom == AF_INET || sock_dom == AF_INET6) {
			ERR("[recvmsg] Inet socket passing detected. Aborting everything! "
					"A non Tor socket could be used thus leaking information.");
			exit(EXIT_FAILURE);
		}
	}

end:
	return tsocks_libc_recvmsg(LIBC_RECVMSG_ARGS);
}

/*
 * Libc hijacked symbol recvmsg(2).
 */
LIBC_RECVMSG_DECL
{
	/* Find symbol if not already set. Exit if not found. */
	tsocks_libc_recvmsg = tsocks_find_libc_symbol(LIBC_RECVMSG_NAME_STR,
			TSOCKS_SYM_EXIT_NOT_FOUND);
	return tsocks_recvmsg(LIBC_RECVMSG_ARGS);
}
