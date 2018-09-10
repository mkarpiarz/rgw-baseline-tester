#!/bin/bash
apt-get install librados-dev -y
mkdir /etc/ceph
cp ceph.conf ceph.client.admin.keyring /etc/ceph/
