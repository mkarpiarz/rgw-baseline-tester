#include <stdio.h>
#include <stdlib.h>
#include "ceph_handler.h"

int ceph_connect(struct Connection *conn, const int argc, const char **argv, const short verbose)
{
    /* Declare the cluster handle and required arguments. */
    rados_t cluster;
    char cluster_name[] = "ceph";
    char user_name[] = "client.admin";
    uint64_t flags = 0;

    /* Initialize the cluster handle with the "ceph" cluster name and the "client.admin" user */
    int err;
    err = rados_create2(&cluster, cluster_name, user_name, flags);

    if (err < 0)
    {
        fprintf(stderr, "%s: Couldn't create the cluster handle! %s\n", argv[0], strerror(-err));
        exit(EXIT_FAILURE);
    }
    else
    {
        if (verbose)
            printf("\nCreated a cluster handle.\n");
    }


    /* Read a Ceph configuration file to configure the cluster handle. */
    err = rados_conf_read_file(cluster, "/etc/ceph/ceph.conf");
    if (err < 0)
    {
        fprintf(stderr, "%s: cannot read config file: %s\n", argv[0], strerror(-err));
        exit(EXIT_FAILURE);
    }
    else
    {
        if (verbose)
            printf("\nRead the config file.\n");
    }

    /* Read command line arguments */
    err = rados_conf_parse_argv(cluster, argc, argv);
    if (err < 0)
    {
        fprintf(stderr, "%s: cannot parse command line arguments: %s\n", argv[0], strerror(-err));
        exit(EXIT_FAILURE);
    }
    else
    {
        if (verbose)
            printf("\nRead the command line arguments.\n");
    }

    /* Connect to the cluster */
    err = rados_connect(cluster);
    if (err < 0)
    {
        fprintf(stderr, "%s: cannot connect to cluster: %s\n", argv[0], strerror(-err));
        exit(EXIT_FAILURE);
    }
    else
    {
        if (verbose)
            printf("\nConnected to the cluster.\n");
    }

    rados_ioctx_t io;
    char *poolname = ".rgw.root";

    err = rados_ioctx_create(cluster, poolname, &io);
    if (err < 0)
    {
        fprintf(stderr, "%s: cannot open rados pool %s: %s\n", argv[0], poolname, strerror(-err));
        rados_shutdown(cluster);
        exit(EXIT_FAILURE);
    }
    else
    {
        if (verbose)
            printf("\nCreated I/O context.\n");
    }

    conn->cluster = cluster;
    conn->io = io;

    if (!verbose)
        printf("INFO: Successfully connected to Ceph.\n");

    return 0;
}

int ceph_write_object(struct Connection *conn, const char *obj_name, const char *obj_content, const unsigned long obj_size, const short verbose)
{
    rados_t cluster = conn->cluster;
    rados_ioctx_t io = conn->io;
    int err;

    /* Write data to the cluster synchronously. */
    err = rados_write(io, obj_name, obj_content, obj_size, 0);
    if (err < 0)
    {
        fprintf(stderr, "ERROR: Cannot write object \"%s\": %s\n", obj_name, strerror(-err));
        rados_ioctx_destroy(io);
        rados_shutdown(cluster);
        exit(1);
    }
    else
    {
        if (verbose)
            printf("\nWrote \"%s\" to object \"%s\".\n", obj_content, obj_name);
    }

    return 0;
}

int ceph_remove_object(struct Connection *conn, const char *obj_name, const short verbose)
{
    rados_t cluster = conn->cluster;
    rados_ioctx_t io = conn->io;
    int err;

    err = rados_remove(io, obj_name);
    if (err < 0)
    {
        fprintf(stderr, "ERROR: Cannot remove object. %s\n", strerror(-err));
        rados_ioctx_destroy(io);
        rados_shutdown(cluster);
        exit(1);
    }
    else
    {
        if (verbose)
            printf("\nRemoved object \"%s\".\n", obj_name);
    }

    return 0;
}

int ceph_close(struct Connection *conn)
{
    rados_ioctx_destroy(conn->io);
    rados_shutdown(conn->cluster);

    return 0;
}
