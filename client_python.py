#!/usr/bin/env python
import socket
import threading
import sys

def sender(host, port, n_bytes):
    data = '*'*n_bytes

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host,port))

    s.send(data.encode())
    s.close()

def cast_to_int(value):
    result = None
    try:
        result = int(value)
    except Exception as e:
        sys.stderr.write("ERROR: {}\n".format(e))
        exit(1)
    return result


def main(argv):
    if len(argv) < 6:
        sys.stderr.write("Usage: %s <host> <port> <num_threads> <num_bytes> <num_objects>\n" % (argv[0],))
        sys.stderr.write("\t<num_threads> - number of worker threads (not including the main thread)\n")
        sys.stderr.write("\t\tnum_threads set to 0 disables parallelism\n")
        sys.stderr.write("\t<num_bytes> - number of bytes to send\n")
        sys.stderr.write("\t<num_objects> - number of objects to send\n")
        return 1
    host = argv[1]
    port = cast_to_int(argv[2])
    n_threads = cast_to_int(argv[3])
    n_bytes = cast_to_int(argv[4])
    n_objects = cast_to_int(argv[5])

    if n_threads == 0:
        sys.stderr.write("INFO: Parallelism disabled\n")
        for _ in range(n_objects):
            sender(host, port, n_bytes)
    else:
        n_objects_sent = 0
        while n_objects_sent < n_objects:
            # check number of active threads
            threads_active = threading.active_count()
            print(threads_active)
            # +1 to account for the main thread which always runs
            if threads_active < n_threads+1:
                n_objects_sent += 1
                # make sure there is always X threads running
                t = threading.Thread(target=sender, args=(host, port, n_bytes))
                t.start()

if __name__ == '__main__':
    sys.exit(main(sys.argv))
