/**
 * libzt C API example
 *
 * Simple socket-based client application
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

    fprintf(stderr, "CLIENT PROCESS ID: %d\n", getpid());

    // SETTHREADNAME("main");

    // INST("thread starting %s", "main");

    int nodelay = 0;
    int single = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "nodelay") == 0) {
            nodelay = 1;
        } else if (strcmp(argv[i], "single") == 0) {
            single = 1;
        }
    }

    char* storagePath = "client_storage";

    unsigned short port = 8080;

    int ztsFamily;
    char *remoteAddr;
    struct zts_sockaddr_in serv_addr;
    struct zts_sockaddr_in6 serv_addr6;
    struct zts_sockaddr *serv_addr_p;
    int serv_addr_len;
    if (strcmp(argv[1], "4") == 0) {

#if defined(__APPLE__)
        remoteAddr = "172.30.75.127";
#elif defined(__linux__)
        remoteAddr = "172.30.71.15";
#endif

        ztsFamily = ZTS_AF_INET;

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);

        if (zts_inet_pton(AF_INET, remoteAddr, &serv_addr.sin_addr) <= 0) {
            fprintf(stderr, "Invalid address/ Address not supported \n");
            exit(1);
        }

        serv_addr_p = (struct zts_sockaddr *)&serv_addr;
        serv_addr_len = sizeof(serv_addr);

    } else if (strcmp(argv[1], "6") == 0) {

        //
        // rfc4193
        //

#if defined(__APPLE__)
        remoteAddr = "fd9e:1948:db63:f87e:3e99:933b:a302:6272";
#elif defined(__linux__)
        remoteAddr = "fd9e:1948:db63:f87e:3e99:938c:9ecd:6aa1";
#endif

        ztsFamily = ZTS_AF_INET6;

        serv_addr6.sin6_family = AF_INET6;
        serv_addr6.sin6_port = htons(port);

        if (zts_inet_pton(AF_INET6, remoteAddr, &serv_addr6.sin6_addr) <= 0) {
            fprintf(stderr, "Invalid address/ Address not supported \n");
            exit(1);
        }
        
        serv_addr_p = (struct zts_sockaddr *)&serv_addr6;
        serv_addr_len = sizeof(serv_addr6);

    } else {
        fprintf(stderr, "Unable to start client. Exiting.\n");
        exit(1);
    }

    if (!nodelay) {
        sleep(15);
    }

    uint64_t networdId = strtoull("9e1948db63f87e3e", NULL, 16);

    //
    // ZeroTier setup
    //

    // Initialize node

    int err;

    if ((err = zts_init_from_storage(storagePath)) != ZTS_ERR_OK) {
        fprintf(stderr, "Unable to start service, error = %d. Exiting.\n", err);
        exit(1);
    }

    fprintf(stderr, "start\n");
    if ((err = zts_node_start()) != ZTS_ERR_OK) {
        fprintf(stderr, "Unable to start service, error = %d. Exiting.\n", err);
        exit(1);
    }
    
    fprintf(stderr, "Waiting for node to come online\n");
    while (! zts_node_is_online()) {
        zts_util_delay(50);
    }

    fprintf(stderr, "Node ID: %" PRIx64 "\n", zts_node_get_id());

    fprintf(stderr, "Joining network...\n");
    if (zts_net_join(networdId) != ZTS_ERR_OK) {
        fprintf(stderr, "Unable to join network. Exiting.\n");
        exit(1);
    }
    fprintf(stderr, "Joined\n");

    fprintf(stderr, "delaying until transport ready...\n");
    while (! zts_net_transport_is_ready(networdId)) {
        zts_util_delay(50);
    }
    fprintf(stderr, "done delaying until transport ready\n");

    // Socket logic

    fprintf(stderr, "Starting client...\n");

    for (;;) {

        // INST("main loop");

        int fd;

        if ((fd = zts_bsd_socket(ztsFamily, ZTS_SOCK_STREAM, 0)) < 0) {
            fprintf(stderr, "zts_bsd_socket error. Exiting.\n");
            exit(1);
        }

        fprintf(stderr, "fd: %d\n", fd);

        if ((err = zts_bsd_connect(fd, serv_addr_p, serv_addr_len)) != ZTS_ERR_OK) {
            fprintf(stderr, "zts_bsd_connect error: err: %d zts_errno: %d\n", err, zts_errno);
            exit(1);
        }

        ssize_t bytes;

        if ((bytes = zts_write(fd, "Hello from C Client!", 20)) < 0) {
            fprintf(stderr, "zts_write error. Exiting.\n");
            exit(1);
        }


        char recvBuf[128] = { 0 };

        if ((bytes = zts_read(fd, recvBuf, sizeof(recvBuf))) < 0) {
            fprintf(stderr, "zts_read error. Exiting.\n");
            exit(1);
        }
        
        fprintf(stderr, "recv: %s\n", recvBuf);


        if ((err = zts_close(fd)) != ZTS_ERR_OK) {
            fprintf(stderr, "zts_close error. Exiting.\n");
            exit(1);
        }

        if (single) {
            break;
        }

        zts_util_delay(50);
    }
}
