/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra_utils.h"
#include "demux.h"

struct fwd_hash {
    int in;
    int out;

    char buf[HYD_TMPBUF_SIZE];
    int buf_offset;
    int buf_count;

    struct fwd_hash *next;
};

HYD_status HYDU_sock_listen(int *listen_fd, char *port_range, uint16_t * port)
{
    struct sockaddr_in sa;
    int one = 1;
    uint16_t low_port, high_port;
    char *port_str;
    uint16_t i;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    low_port = 0;
    high_port = 0;
    if (port_range) {
        /* If port range is set, we always pick from there */
        *port = 0;

        port_str = strtok(port_range, ":");
        if (port_str == NULL)
            HYDU_ERR_SETANDJUMP(status, HYD_INTERNAL_ERROR, "error parsing port range\n");
        low_port = atoi(port_str);

        port_str = strtok(NULL, ":");
        if (port_str == NULL)
            HYDU_ERR_SETANDJUMP(status, HYD_INTERNAL_ERROR, "error parsing port range\n");
        high_port = atoi(port_str);

        if (high_port < low_port)
            HYDU_ERR_SETANDJUMP(status, HYD_INTERNAL_ERROR, "high port < low port\n");
    }
    else {
        /* If port range is NULL, if a port is already provided, we
         * pick that. Otherwise, we search for an available port. */
        low_port = *port;
        high_port = *port;
    }

    *listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (*listen_fd < 0)
        HYDU_ERR_SETANDJUMP1(status, HYD_SOCK_ERROR, "cannot open socket (%s)\n",
                             HYDU_strerror(errno));

    if (setsockopt(*listen_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(int)) < 0)
        HYDU_ERR_SETANDJUMP(status, HYD_SOCK_ERROR, "cannot set TCP_NODELAY\n");

    /* The sockets standard does not guarantee that a successful
     * return here means that this is set. However, REUSEADDR not
     * being set is not a fatal error, so we ignore that
     * case. However, we do check for error cases, which means that
     * something bad has happened. */
    if (setsockopt(*listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
        HYDU_ERR_SETANDJUMP(status, HYD_SOCK_ERROR, "cannot set SO_REUSEADDR\n");

    for (i = low_port; i <= high_port; i++) {
        memset((void *) &sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(i);
        sa.sin_addr.s_addr = INADDR_ANY;

        if (bind(*listen_fd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
            /* If the address is in use, we should try the next
             * port. Otherwise, it's an error. */
            if (errno != EADDRINUSE)
                HYDU_ERR_SETANDJUMP1(status, HYD_SOCK_ERROR, "bind error (%s)\n",
                                     HYDU_strerror(errno));
        }
        else    /* We got a port */
            break;
    }

    *port = i;
    if (*port > high_port)
        HYDU_ERR_SETANDJUMP(status, HYD_SOCK_ERROR, "no port to bind\n");

    if (listen(*listen_fd, -1) < 0)
        HYDU_ERR_SETANDJUMP1(status, HYD_SOCK_ERROR, "listen error (%s)\n",
                             HYDU_strerror(errno));

    /* We asked for any port, so we need to find out which port we
     * actually got. */
    if (*port == 0) {
        socklen_t sinlen = sizeof(sa);

        if (getsockname(*listen_fd, (struct sockaddr *) &sa, &sinlen) < 0)
            HYDU_ERR_SETANDJUMP1(status, HYD_SOCK_ERROR, "getsockname error (%s)\n",
                                 HYDU_strerror(errno));
        *port = ntohs(sa.sin_port);
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_status HYDU_sock_connect(const char *host, uint16_t port, int *fd)
{
    struct hostent *ht;
    struct sockaddr_in sa;
    int one = 1;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    memset((char *) &sa, 0, sizeof(struct sockaddr_in));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    /* Get the remote host's IP address. Note that this is not
     * thread-safe. Since we don't use threads right now, we don't
     * worry about locking it. */
    ht = gethostbyname(host);
    if (ht == NULL)
        HYDU_ERR_SETANDJUMP1(status, HYD_INVALID_PARAM,
                             "unable to get host address (%s)\n", HYDU_strerror(errno));
    memcpy(&sa.sin_addr, ht->h_addr_list[0], ht->h_length);

    /* Create a socket and set the required options */
    *fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*fd < 0)
        HYDU_ERR_SETANDJUMP1(status, HYD_SOCK_ERROR, "cannot open socket (%s)\n",
                             HYDU_strerror(errno));

    /* Not being able to connect is not an error in all cases. So we
     * return an error, but only print a warning message. The upper
     * layer can decide what to do with the return status. */
    if (connect(*fd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
        HYDU_error_printf("connect error (%s)\n", HYDU_strerror(errno));
        status = HYD_SOCK_ERROR;
        goto fn_fail;
    }

    /* Disable nagle */
    if (setsockopt(*fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(int)) < 0)
        HYDU_ERR_SETANDJUMP(status, HYD_SOCK_ERROR, "cannot set TCP_NODELAY\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_status HYDU_sock_accept(int listen_fd, int *fd)
{
    int one = 1;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    *fd = accept(listen_fd, 0, 0);
    if (*fd < 0)
        HYDU_ERR_SETANDJUMP1(status, HYD_SOCK_ERROR, "accept error (%s)\n",
                             HYDU_strerror(errno));

    /* Disable nagle */
    if (setsockopt(*fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(int)) < 0)
        HYDU_ERR_SETANDJUMP(status, HYD_SOCK_ERROR, "cannot set TCP_NODELAY\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


/* HYD_sock_readline: Return the next newline-terminated string of
 * maximum length maxlen.  This is a buffered version, and reads from
 * fd as necessary. */
HYD_status HYDU_sock_readline(int fd, char *buf, int maxlen, int *linelen)
{
    int n;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    *linelen = 0;
    while (1) {
        n = read(fd, buf + *linelen, maxlen - *linelen - 1);
        if (n == 0) {   /* No more data to read */
            break;
        }
        else if (n < 0) {
            if (errno == EINTR)
                continue;
            HYDU_ERR_SETANDJUMP1(status, HYD_SOCK_ERROR, "read error (%s)\n",
                                 HYDU_strerror(errno));
        }

        *linelen += n;
        break;
    }

    /* Done reading; pad the last byte with a NULL */
    buf[*linelen - 1] = 0;

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

HYD_status HYDU_sock_read(int fd, void *buf, int maxlen, int *count,
                          enum HYDU_sock_comm_flag flag)
{
    int tmp;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    HYDU_ASSERT(maxlen, status);

    *count = 0;
    while (1) {
        do {
            tmp = read(fd, (char *) buf + *count, maxlen - *count);
        } while (tmp < 0 && errno == EINTR);

        if (tmp < 0) {
            *count = tmp;
            break;
        }
        *count += tmp;

        if (flag != HYDU_SOCK_COMM_MSGWAIT || *count == maxlen)
            break;
        else if (0 == tmp)
            HYDU_ERR_SETANDJUMP(status, HYD_INTERNAL_ERROR, "MSGWAIT requested but EOF encountered\n");
    };

    if (*count < 0)
        HYDU_ERR_SETANDJUMP1(status, HYD_SOCK_ERROR, "read errno (%s)\n",
                             HYDU_strerror(errno));

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_status HYDU_sock_write(int fd, const void *buf, int maxsize)
{
    int n;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    do {
        n = write(fd, buf, maxsize);
    } while (n < 0 && errno == EINTR);

    if (n < maxsize)
        HYDU_ERR_SETANDJUMP1(status, HYD_SOCK_ERROR, "write error (%s)\n",
                             HYDU_strerror(errno));

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_status HYDU_sock_trywrite(int fd, const void *buf, int maxsize)
{
    int n;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    do {
        n = write(fd, buf, maxsize);
    } while (n < 0 && errno == EINTR);

    if (n < maxsize) {
        HYDU_warn_printf("write error (%s)\n", HYDU_strerror(errno));
        status = HYD_SOCK_ERROR;
        goto fn_fail;
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status set_nonblock(int fd)
{
    int flags;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    if ((flags = fcntl(fd, F_GETFL, 0)) == -1)
        flags = 0;
#if defined O_NONBLOCK
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        HYDU_ERR_SETANDJUMP1(status, HYD_SOCK_ERROR, "fcntl failed on %d\n", fd);
#endif /* O_NONBLOCK */

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status set_block(int fd)
{
    int flags;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    if ((flags = fcntl(fd, F_GETFL, 0)) == -1)
        flags = 0;
#if defined O_NONBLOCK
    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0)
        HYDU_ERR_SETANDJUMP1(status, HYD_SOCK_ERROR, "fcntl failed on %d\n", fd);
#endif /* O_NONBLOCK */

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status alloc_fwd_hash(struct fwd_hash **fwd_hash, int in, int out)
{
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    HYDU_MALLOC(*fwd_hash, struct fwd_hash *, sizeof(struct fwd_hash), status);
    (*fwd_hash)->in = in;
    (*fwd_hash)->out = out;

    (*fwd_hash)->buf_offset = 0;
    (*fwd_hash)->buf_count = 0;

    (*fwd_hash)->next = NULL;

    status = set_block(in);
    HYDU_ERR_POP(status, "unable to set out-socket to non-blocking\n");

    status = set_nonblock(out);
    HYDU_ERR_POP(status, "unable to set out-socket to non-blocking\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

/* This function does not provide any flow control. We just read from
 * the incoming socket as much as we can and push out to the outgoing
 * socket as much as we can. This can result in the process calling it
 * polling continuously waiting for events, but that's a rare case for
 * stdio (which is what this function is meant to provide
 * functionality for). */
HYD_status HYDU_sock_forward_stdio(int in, int out, int *closed)
{
    static struct fwd_hash *fwd_hash_list = NULL;
    struct fwd_hash *fwd_hash, *tmp;
    int count;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    /* find the fwd hash */
    for (tmp = fwd_hash_list; tmp; tmp = tmp->next)
        if (out == tmp->out)
            break;

    if (tmp == NULL) {  /* No hash found; create one */
        alloc_fwd_hash(&fwd_hash, in, out);
        if (fwd_hash_list == NULL)
            fwd_hash_list = fwd_hash;
        else {
            for (tmp = fwd_hash_list; tmp->next; tmp = tmp->next);
            tmp->next = fwd_hash;
        }
    }
    else {
        fwd_hash = tmp;
    }

    *closed = 0;
    if (fwd_hash->buf_count == 0) {
        /* there is no data in the buffer, read something into it */
        count = read(in, fwd_hash->buf, HYD_TMPBUF_SIZE);
        if (count <= 0) {
            *closed = 1;
        }
        else {
            fwd_hash->buf_offset = 0;
            fwd_hash->buf_count += count;
        }
    }

    if (fwd_hash->buf_count) {
        /* there is data in the buffer, send it out first */
        count = write(out, fwd_hash->buf + fwd_hash->buf_offset, fwd_hash->buf_count);
        if (count < 0) {
            if (errno == EPIPE)
                *closed = 1;
            else if (errno != EAGAIN)
                HYDU_ERR_SETANDJUMP2(status, HYD_INTERNAL_ERROR,
                                     "write error on %d; errno: %d\n", out, errno);
        }
        else if (count) {
            fwd_hash->buf_offset += count;
            fwd_hash->buf_count -= count;
        }
    }

    while (*closed && fwd_hash->buf_count) {
        count = write(out, fwd_hash->buf + fwd_hash->buf_offset, fwd_hash->buf_count);
        if (count < 0) {
            if (errno == EPIPE)
                *closed = 1;
            else if (errno != EAGAIN)
                HYDU_ERR_SETANDJUMP2(status, HYD_INTERNAL_ERROR,
                                     "write error on %d; errno: %d\n", out, errno);
        }
        else if (count) {
            fwd_hash->buf_offset += count;
            fwd_hash->buf_count -= count;
        }
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

HYD_status HYDU_sock_get_iface_ip(char *iface, char **ip)
{
    HYD_status status = HYD_SUCCESS;

#if defined(HAVE_GETIFADDRS)
    struct ifaddrs *ifaddr, *ifa;
    char buf[MAX_HOSTNAME_LEN];
    struct sockaddr_in *sa;

    /* Got the interface name; let's query for the IP address */
    if (getifaddrs(&ifaddr) == -1)
        HYDU_ERR_SETANDJUMP(status, HYD_INTERNAL_ERROR, "getifaddrs failed\n");

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next)
        if (!strcmp(ifa->ifa_name, iface) && (ifa->ifa_addr->sa_family == AF_INET))
            break;

    if (!ifa)
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR, "unable to find interface %s\n",
                             iface);

    sa = (struct sockaddr_in *) ifa->ifa_addr;
    (*ip) = HYDU_strdup((char *)
                        inet_ntop(AF_INET, (void *) &(sa->sin_addr), buf, MAX_HOSTNAME_LEN));
    if (!*ip)
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find IP for interface %s\n", iface);

    freeifaddrs(ifaddr);
#else
    /* For now just disable interface selection when getifaddrs isn't
     * available, such as on AIX.  In the future we can implement in MPL
     * something along the lines of MPIDI_GetIPInterface from tcp_getip.c in
     * nemesis. */
    HYDU_ERR_SETANDJUMP(status, HYD_INTERNAL_ERROR,
                        "interface selection not supported on this platform\n");
#endif

  fn_exit:
    return status;

  fn_fail:
    goto fn_exit;
}

HYD_status
HYDU_sock_create_and_listen_portstr(char *iface, char *port_range, char **port_str,
                                    HYD_status(*callback) (int fd, HYD_event_t events,
                                                           void *userp), void *userp)
{
    int listenfd;
    char *sport, *real_port_range, *ip = NULL;
    uint16_t port;
    HYD_status status = HYD_SUCCESS;

    /* Listen on a port in the port range */
    port = 0;
    real_port_range = port_range ? HYDU_strdup(port_range) : NULL;
    status = HYDU_sock_listen(&listenfd, real_port_range, &port);
    HYDU_ERR_POP(status, "unable to listen on port\n");

    /* Register the listening socket with the demux engine */
    status = HYDT_dmx_register_fd(1, &listenfd, HYD_POLLIN, userp, callback);
    HYDU_ERR_POP(status, "unable to register fd\n");

    /* Create a port string for MPI processes to use to connect to */
    if (iface) {
        status = HYDU_sock_get_iface_ip(iface, &ip);
        HYDU_ERR_POP(status, "unable to get network interface IP\n");
    }
    else {
        HYDU_MALLOC(ip, char *, MAX_HOSTNAME_LEN, status);
        status = HYDU_gethostname(ip);
        HYDU_ERR_POP(status, "unable to get local hostname\n");
    }

    sport = HYDU_int_to_str(port);
    HYDU_MALLOC(*port_str, char *, strlen(ip) + 1 + strlen(sport) + 1, status);
    HYDU_snprintf(*port_str, strlen(ip) + 1 + strlen(sport) + 1,
                  "%s:%s", ip, sport);
    HYDU_FREE(sport);

  fn_exit:
    if (ip)
        HYDU_FREE(ip);
    return status;

  fn_fail:
    goto fn_exit;
}
