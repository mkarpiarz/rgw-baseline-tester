#ifndef CEPH_HANDLER_H
#define CEPH_HANDLER_H
#include <rados/librados.h>

/* A structure holding rados objects required for connection to librados */
struct Connection {
    rados_t cluster;
    rados_ioctx_t io;
};

int ceph_connect(struct Connection*, const int, const char**, const short);
int ceph_write_object(struct Connection*, const char*, const char*, unsigned long, const short);
int ceph_remove_object(struct Connection*, const char*, const short);
int ceph_close(struct Connection*);
#endif
