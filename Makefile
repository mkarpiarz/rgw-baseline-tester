all: baseliner client_s3

baseliner:
	gcc -g -std=gnu11 -o baseliner baseliner.c ceph_handler.c -pthread -lrados

client_s3:
	gcc -g -std=gnu11 -o client_s3 client_s3.c -lcrypto

clean:
	rm -f baseliner client_s3
