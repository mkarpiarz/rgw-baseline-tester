/*
 * The initial single threaded synchronous TCP-only server implementation
 * was taken from:
 * https://banu.com/blog/2/how-to-use-epoll-a-complete-example-in-c/epoll-example.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/resource.h>
#include <limits.h>
#include "ceph_handler.h"

#define KiB 1024
#define MiB 1024*KiB

#define MAXEVENTS 64
#define MAX_CONTENT_SIZE 1*MiB
#define READ_BUFFER_SIZE 512 //B

struct FDstruct
{
    int efd;    // event fd
    int sfd;    // socket fd
    struct Connection *conn;
    bool enable_ceph;
    bool enable_http;
    short int verbose;
    struct EventData *edata;
};

struct EventData
{
    int fd;
    bool headers_received;
    unsigned long total_bytes; // total bytes received
    unsigned long n_bytes; // number of bytes in body
    char *content;
};

static int make_socket_non_blocking(int sfd)
{
    int flags, s;

    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1)
    {
        perror("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl(sfd, F_SETFL, flags);
    if (s == -1)
    {
        perror("fcntl");
        return -1;
    }

    return 0;
}

static int create_and_bind(const char *port)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;     // Return IPv4 and IPv6 choices
    hints.ai_socktype = SOCK_STREAM; // We want a TCP socket
    hints.ai_flags = AI_PASSIVE;     // All interfaces

    s = getaddrinfo(NULL, port, &hints, &result);
    if (s != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;

        s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
        if (s == 0)
        {
            // We managed to bind successfully!
            break;
        }

        close(sfd);
    }

    if (rp == NULL)
    {
        fprintf(stderr, "Could not bind\n");
        return -1;
    }

    freeaddrinfo(result);

    return sfd;
}

void *read_in_thread(void *fds)
{
    int s;

    struct FDstruct *my_fds = (struct FDstruct*)fds;
    int socketfd = my_fds->sfd;
    int eventfd = my_fds->efd;
    short int verbose = my_fds->verbose;
    if (verbose)
        printf("fds pointer (thread): %p; sfd=%d, efd=%d\n", fds, my_fds->sfd, my_fds->efd );
    struct Connection *conn = my_fds->conn;
    bool enable_ceph = my_fds->enable_ceph;
    bool enable_http = my_fds->enable_http;
    struct EventData *edata = (struct EventData*)my_fds->edata;
    bool headers_received = edata->headers_received;
    unsigned long total_bytes = edata->total_bytes;
    unsigned long n_bytes = edata->n_bytes;
    char *content = edata->content;

    // This pointer was dynamically allocated in the main thread, so free it here
    free(fds);

    int done = 0;
    // Controls where to insert data taken from buffer into the content array
    unsigned long content_index = total_bytes;

    while (1)
    {
        ssize_t count;
        char buf[READ_BUFFER_SIZE];

        count = read(socketfd, buf, sizeof(buf));
        if (verbose)
            printf("[sfd %d] read %ldB, ", socketfd, count);
        if (enable_http && count != -1)
        {
            total_bytes += count;
            if (verbose)
                printf("total bytes: %lu\n", total_bytes);
        }

        if (count == -1)
        {
            /*
             * If errno == EAGAIN, that means we have read all
             * data. So go back to the main loop.
             */
            if (errno != EAGAIN)
            {
                perror("read");
                done = 1;
            }

            if (enable_http)
            {
                if (!headers_received)
                {
                    // Look for the Content-Length field
                    char *saveptr;
                    char *pch = strtok_r(buf, " :\r\n", &saveptr);
                    while (pch != NULL)
                    {
                        if ( pch != NULL && !strcmp(pch, "Content-Length") )
                            break;
                        if (verbose)
                            printf("[sfd %d] %s\n", socketfd, pch);
                        pch = strtok_r(NULL, " :\r\n", &saveptr);
                    }
                    pch = strtok_r(NULL, " :\r\n", &saveptr);
                    if (pch != NULL)
                    {
                        n_bytes = strtol(pch, NULL, 10);
                        if (verbose)
                            printf("[sfd %d] content length (from headers): %lu\n", socketfd, n_bytes);
                    }
                    else
                    {
                        // Exit if Content-Length is missing
                        // TODO: Handle this more gracefully.
                        fprintf(stderr, "[sfd %d] ERROR: Couldn't get the value of Content-Length!\n", socketfd);
                        exit(1);
                    }
                    // Reset current count of bytes received as we only care about body size
                    total_bytes = 0;

                    // Send 100 Continue
                    const char* resp = "HTTP/1.1 100 Continue\r\n\r\n";
                    if (verbose)
                        printf("\n[sfd %d] INFO: Sending '%s'\n", socketfd, resp);
                    if (send(socketfd, resp, strlen(resp), 0) == -1)
                        perror("send");
                    headers_received = true;

                    // TODO: Handle case where data comes without headers
                }
                else
                {
                    if (total_bytes == n_bytes)
                    {
                        printf("\n[sfd %d] INFO: Read all %lu bytes of the message.\n", socketfd, n_bytes);
                        // Send 200 OK
                        // "HTTP/1.1 200 OK\r\nHeader1: Value1\r\nHeader2: Value2\r\n\r\nBODY"
                        const char *resp = "HTTP/1.1 200 OK\r\nETag: blahblahblahblahblahblahblahblah\r\nContent-Length: 0\r\n\r\n";
                        if (verbose)
                            printf("\n[sfd %d] INFO: Sending '%s'\n", socketfd, resp);
                        if (send(socketfd, resp, strlen(resp), 0) == -1)
                            perror("send");

                        // We now have the whole object, so send it
                        char obj_name[256] = "";
                        if (enable_ceph)
                        {
                            // Use this thread ID for object name
                            sprintf(obj_name, "%lu", pthread_self());
                            // Write object content to Ceph
                            ceph_write_object(conn, obj_name, content, total_bytes, verbose);
                        }
                        if (enable_ceph)
                        {
                            ceph_remove_object(conn, obj_name, verbose);
                            //TODO: ceph_remove_object is not thread-safe
                        }

                        // Reset values for threads working on the same fd
                        headers_received = false;
                        total_bytes = 0;
                    }
                }
                if (verbose)
                    printf("[sfd %d] headers received (thread)? %d\n", socketfd, headers_received);
            }

            // Re-arm the socket, so we get notifications again
            struct epoll_event event;
            edata->fd = socketfd;
            edata->headers_received = headers_received;
            edata->total_bytes = total_bytes;
            if (headers_received)
                edata->n_bytes = n_bytes;
            event.data.ptr = edata;
            event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
            s = epoll_ctl(eventfd, EPOLL_CTL_MOD, socketfd, &event);
            if (s == -1)
            {
                perror("epoll_ctl");
                abort();
            }
            // Exit to the main loop
            pthread_exit(0);
        }
        else if (count == 0)
        {
            // End of file. The remote has closed the connection.
            done = 1;
            break;
        }

        // Append buffer content to the variable storing object content
        if (enable_ceph)
        {
            if (headers_received)
            {
                memcpy( content+content_index, buf, count );
                if (verbose)
                {
                    printf("[sfd %d] content index: %lu\n", socketfd, content_index);
                    printf("[sfd %d] content size: %lu\n", socketfd, strlen(content));
                }
                content_index = total_bytes;
            }
        }

    }

    if (done)
    {
        free(edata);
        free(content);

        // Restore original value for a thread using the same fd
        if (enable_http)
        {
            headers_received = false;
            total_bytes = 0;
        }

        printf("Closed connection on descriptor %d\n", socketfd);

        /*
         * Closing the descriptor will make epoll remove it
         * from the set of descriptors which are monitored.
         */
        close(socketfd);
    }

}

void print_usage(const char **argv)
{
        fprintf(stderr, "Usage: %s [-c] [-w] [-v] [-h] port\n", argv[0]);
        fprintf(stderr, "\t-c: enables integration with Ceph\n");
        fprintf(stderr, "\t-w: enables HTTP web server\n");
        fprintf(stderr, "\t-v: turns on verbosity\n");
        fprintf(stderr, "\t-h: prints this help\n");
}

void print_stack_size(void)
{
    struct rlimit rl;
    getrlimit(RLIMIT_STACK, &rl);
    fprintf(stderr, "INFO: Stack size limit: %lu [KB]. Run `ulimit -s new-value` to change.\n", rl.rlim_cur/KiB);
}

void print_datastructure_sizes(void)
{
    fprintf(stderr, "INFO: Maximum object size is: %d [B].\n", MAX_CONTENT_SIZE);
    fprintf(stderr, "INFO: Read buffer size is: %d [B].\n", READ_BUFFER_SIZE);
}

int main(int argc, const char *argv[])
{
    int sfd, s;
    int efd;
    struct epoll_event event;
    struct epoll_event *events;
    bool *headers;

    bool enable_ceph = false;
    bool enable_http = false;
    short verbose = 0;
    const char *port = "-1";
    size_t i;

    // Parse arguments
    for (i = 1; i < argc; i++)
    {
        char const *option = argv[i];
        if (option[0] == '-')
        {
            switch (option[1])
            {
                case 'c':
                    fprintf(stderr, "INFO: Ceph integration enabled\n");
                    enable_ceph = true;
                    break;
                case 'w':
                    fprintf(stderr, "INFO: HTTP web server enabled\n");
                    enable_http = true;
                    break;
                case 'v':
                    fprintf(stderr, "INFO: Verbose output turned on\n");
                    verbose = 1;
                    break;
                case 'h':
                    print_usage(argv);
                    exit(EXIT_SUCCESS);
                    break;
                default:
                    fprintf(stderr, "flag not recognised %s\n", option);
                    exit(EXIT_FAILURE);
                    break;
            }
        }
        else
        {
            port = argv[i];
        }
    }

    if ( argc < 2 || !strcmp(port, "-1") )
    {
        print_usage(argv);
        exit(EXIT_FAILURE);
    }

    print_stack_size();
    print_datastructure_sizes();

    // Initialise Ceph
    struct Connection conn;
    if (enable_ceph)
        ceph_connect(&conn, argc, argv, verbose);

    sfd = create_and_bind(port);
    if (sfd == -1)
        abort();

    s = make_socket_non_blocking(sfd);
    if (s == -1)
        abort();

    s = listen(sfd, SOMAXCONN);
    if (s == -1)
    {
        perror("listen");
        abort();
    }

    efd = epoll_create1(0);
    if (efd == -1)
    {
        perror("epoll_create");
        abort();
    }

    struct EventData *edata = malloc( sizeof(struct EventData) );
    edata->fd = sfd;
    edata->headers_received = false;
    edata->total_bytes = 0;
    edata->n_bytes = ULONG_MAX;
    char *content = calloc(MAX_CONTENT_SIZE, sizeof(char));
    edata->content = content;
    event.data.ptr = edata;
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event);
    if (s == -1)
    {
        perror("epoll_ctl");
        abort();
    }

    // Buffer where events are returned
    events = calloc(MAXEVENTS, sizeof(event));

    // The event loop
    while (1)
    {
        int n, i;

        n = epoll_wait(efd, events, MAXEVENTS, -1);
        for (i = 0; i < n; i++)
        {
            if ((events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN)))
            {
                free(edata);
                free(content);
                /*
                 * An error has occured on this fd, or the socket is not
                 * ready for reading (why were we notified then?)
                 */
                fprintf(stderr, "epoll error\n");
                close(((struct EventData*) events[i].data.ptr)->fd);
                /*
                 * With HTTP server enabled this can happen if server sent a 200 OK
                 * and didn't manage to pull all data before client has closed the socket.
                 */
                ((struct EventData*) events[i].data.ptr)->headers_received = false;
                continue;
            }

            else if (sfd == ((struct EventData*) events[i].data.ptr)->fd)
            {
                /*
                 * We have a notification on the listening socket, which
                 * means one or more incoming connections.
                 */
                while (1)
                {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof(in_addr);
                    infd = accept(sfd, &in_addr, &in_len);
                    if (infd == -1)
                    {
                        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
                        {
                            // We have processed all incoming connections.
                            break;
                        }
                        else
                        {
                            perror("accept");
                            break;
                        }
                    }

                    s = getnameinfo(&in_addr, in_len,
                                    hbuf, sizeof(hbuf),
                                    sbuf, sizeof(sbuf),
                                    NI_NUMERICHOST | NI_NUMERICSERV);
                    if (s == 0)
                    {
                        printf("Accepted connection on descriptor %d "
                               "(host=%s, port=%s)\n", infd, hbuf, sbuf);
                    }

                    /*
                     * Make the incoming socket non-blocking and add it to the
                     * list of fds to monitor.
                     */
                    s = make_socket_non_blocking(infd);
                    if (s == -1)
                        abort();

                    struct EventData *edata = malloc( sizeof(struct EventData) );
                    edata->fd = infd;
                    edata->headers_received = false;
                    edata->total_bytes = 0;
                    edata->n_bytes = ULONG_MAX;
                    char *content = calloc(MAX_CONTENT_SIZE, sizeof(char));
                    edata->content = content;
                    event.data.ptr = edata;
                    // Make the socket a one shot so only one thread picks it up
                    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    s = epoll_ctl(efd, EPOLL_CTL_ADD, infd, &event);
                    if (s == -1)
                    {
                        perror("epoll_ctl");
                        abort();
                    }
                }
                continue;
            }
            else
            {
                /*
                 * We have data on the fd waiting to be read. Read and
                 * display it. We must read whatever data is available
                 * completely, as we are running in edge-triggered mode
                 * and won't get a notification again for the same data.
                 */

                // Holds event and socket descriptors passed to threads
                struct FDstruct *fds;
                fds = malloc(sizeof(struct FDstruct));
                fds->efd = efd;
                fds->sfd = ((struct EventData*) events[i].data.ptr)->fd;
                fds->conn = &conn;
                fds->enable_ceph = enable_ceph;
                fds->enable_http = enable_http;
                fds->verbose = verbose;
                fds->edata = events[i].data.ptr;
                if (verbose)
                    printf("[sfd %d] headers received? %d\n", fds->sfd, ((struct EventData*) events[i].data.ptr)->headers_received);
                pthread_t read_thread;
                if (verbose)
                    printf("(sfd,efd): (%d,%d)\n", fds->sfd, fds->efd);
                if(pthread_create(&read_thread, NULL, read_in_thread, (void *) fds))
                {
                    fprintf(stderr, "Error creating thread\n");
                    return 1;
                }
                if(pthread_detach(read_thread))
                {
                    fprintf(stderr, "Error detaching thread\n");
                    return 2;
                }
            }
        }
    }

    free(events);

    close(sfd);

    if (enable_ceph)
        ceph_close(&conn);

    return EXIT_SUCCESS;
}

