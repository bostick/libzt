/**
 * libzt C API example
 *
 * Simple socket-based server application
 */

#define _GNU_SOURCE

#include "ZeroTierSockets.h"

#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h> // for uint64_t
#include <inttypes.h> // for PRIx64
#include <unistd.h> // for getpid
#include <pthread.h>

// #include "Inst.hpp"

int main(int argc, char** argv) {
    
    fprintf(stderr, "SERVER PROCESS ID: %d\n", getpid());

    // SETTHREADNAME("main");
    
    // INST("thread starting %s", "main");

    int nodelay = 0;
    int single  = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "nodelay") == 0) {
            nodelay = 1;
        } else if (strcmp(argv[i], "single") == 0) {
            single = 1;
        }
    }

    unsigned short port = 8080;

    int ztsFamily;
    struct zts_sockaddr_in addr;
    struct zts_sockaddr_in6 addr6;
    struct zts_sockaddr *addr_p;
    int addr_len;
    if (strcmp(argv[1], "4") == 0) {

        ztsFamily = ZTS_AF_INET;

        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);

        addr_p = (struct zts_sockaddr *)&addr;
        addr_len = sizeof(addr);

    } else if (strcmp(argv[1], "6") == 0) {

        //
        // rfc4193
        //

        ztsFamily = ZTS_AF_INET6;

        addr6.sin6_family = AF_INET6;

        addr6.sin6_addr = zts_in6addr_any;

        addr6.sin6_port = htons(port);

        addr_p = (struct zts_sockaddr *)&addr6;
        addr_len = sizeof(addr6);

    } else {
        fprintf(stderr, "Unable to start server. Exiting.\n");
        exit(1);
    }

    if (!nodelay) {
        sleep(15);
    }

    char* storagePath = "server_storage";

    uint64_t networdId = strtoull("9e1948db63f87e3e", NULL, 16);

    //
    // ZeroTier setup
    //

    fprintf(stderr, "init from storage...\n");
    if ((zts_init_from_storage(storagePath)) != ZTS_ERR_OK) {
        printf("Unable to start service. Exiting.\n");
        exit(1);
    }

    fprintf(stderr, "start\n");
    if ((zts_node_start()) != ZTS_ERR_OK) {
        printf("Unable to start service. Exiting.\n");
        exit(1);
    }

    fprintf(stderr, "Waiting for node to come online\n");
    while (! zts_node_is_online()) {
        zts_util_delay(50);
    }

    fprintf(stderr, "Node ID: %" PRIx64 "\n", zts_node_get_id());

    fprintf(stderr, "Joining network...\n");
    if (zts_net_join(networdId) != ZTS_ERR_OK) {
        printf("Unable to join network. Exiting.\n");
        exit(1);
    }

    fprintf(stderr, "Waiting for network...\n");
    while (! zts_net_transport_is_ready(networdId)) {
        zts_util_delay(50);
    }

    fprintf(stderr, "Joined\n");

    // IPv4
    
    char ipstr[ZTS_IP_MAX_STR_LEN] = { 0 };
    zts_addr_get_str(networdId, ZTS_AF_INET, ipstr, ZTS_IP_MAX_STR_LEN);
    fprintf(stderr, "IPv4 address on network %" PRIx64 " is %s\n", networdId, ipstr);

    // IPv6

    zts_addr_get_str(networdId, ZTS_AF_INET6, ipstr, ZTS_IP_MAX_STR_LEN);
    fprintf(stderr, "IPv6 address on network %" PRIx64 " is %s\n", networdId, ipstr);

    // Socket logic
    
    fprintf(stderr, "Starting server...\n");

    int err;
    int fd;

    if ((fd = zts_bsd_socket(ztsFamily, ZTS_SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "zts_bsd_socket error. Exiting.\n");
        exit(1);
    }

    if ((err = zts_bsd_bind(fd, addr_p, addr_len)) < 0) {
        printf("zts_bsd_bind error (fd=%d, err=%d, zts_errno=%d). Exiting.\n", fd, err, zts_errno);
        exit(1);
    }

    int backlog = 100;
    if (zts_bsd_listen(fd, backlog) < 0) {
        fprintf(stderr, "zts_bsd_listen error. Exiting.\n");
        exit(1);
    }

    for (;;) {

        int accfd;
        struct zts_sockaddr client_addr;
        zts_socklen_t client_addrlen;
        if ((accfd = zts_bsd_accept(fd, &client_addr, &client_addrlen)) < 0) {
            printf("zts_bsd_accept error (fd=%d, accfd=%d, zts_errno=%d). Exiting.\n", fd, accfd, zts_errno);
            exit(1);
        }

        fprintf(stderr, "accfd: %d\n", accfd);

        char recvBuf[128] = { 0 };

        ssize_t bytes;

        if ((bytes = zts_read(accfd, recvBuf, sizeof(recvBuf))) < 0) {
            printf("zts_read error. Exiting.\n");
            exit(1);
        }

        fprintf(stderr, "recv: %s\n", recvBuf);


        if ((bytes = zts_write(accfd, "Hello from C Server!", 20)) < 0) {
            printf("zts_write error. Exiting.\n");
            exit(1);
        }


        if (zts_close(accfd) != ZTS_ERR_OK) {
            printf("zts_close error. Exiting.\n");
            exit(1);
        }

        if (single) {
            break;
        }
    }

    if (zts_close(fd) != ZTS_ERR_OK) {
        printf("zts_close error. Exiting.\n");
        exit(1);
    }
}
