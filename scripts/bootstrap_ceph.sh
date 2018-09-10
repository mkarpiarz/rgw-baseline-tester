#!/bin/bash

curl https://get.docker.com/ | sh
sudo usermod -aG docker vagrant

# Reload user to use new group assignment
sudo su -l vagrant

# Launch containers
docker run -d --name=ceph-mon -h ceph-mon --net=host --restart=always -v /etc/ceph:/etc/ceph -v /var/lib/ceph/:/var/lib/ceph/ -e MON_IP=192.168.42.100 -e CEPH_PUBLIC_NETWORK=192.168.42.0/24 ceph/daemon mon
docker run -d --name=ceph-mgr -h ceph-mgr --net=host --restart=always -v /etc/ceph:/etc/ceph -v /var/lib/ceph/:/var/lib/ceph/ ceph/daemon mgr
docker run -d --name=ceph-osd0 -h ceph-osd0 --net=host --pid=host --privileged=true --restart=always -v /etc/ceph:/etc/ceph -v /var/lib/ceph/:/var/lib/ceph/ -v /dev/:/dev/ -e OSD_DEVICE=/dev/sdb ceph/daemon osd
docker run -d --name=ceph-osd1 -h ceph-osd1 --net=host --pid=host --privileged=true --restart=always -v /etc/ceph:/etc/ceph -v /var/lib/ceph/:/var/lib/ceph/ -v /dev/:/dev/ -e OSD_DEVICE=/dev/sdc ceph/daemon osd
docker run -d --name=ceph-osd2 -h ceph-osd2 --net=host --pid=host --privileged=true --restart=always -v /etc/ceph:/etc/ceph -v /var/lib/ceph/:/var/lib/ceph/ -v /dev/:/dev/ -e OSD_DEVICE=/dev/sdd ceph/daemon osd

sudo apt-get install jq -y
# Wait for OSD to get up and make sure their hosts are all in the default root
while [ $(docker exec ceph-mon ceph -f json osd stat | jq -r '.num_up_osds') -ne 3 ]
do
    echo "Waiting for OSDs to become ready..."
    sleep 1
done
docker exec ceph-mon ceph osd crush move ceph-osd0 root=default
docker exec ceph-mon ceph osd crush move ceph-osd1 root=default
docker exec ceph-mon ceph osd crush move ceph-osd2 root=default

# At this point RGW should have everything it needs
docker run -d --name=ceph-rgw -h ceph-rgw --net=host --restart=always -v /etc/ceph:/etc/ceph -v /var/lib/ceph/:/var/lib/ceph/ ceph/daemon rgw

# Get Ceph config and admin's keyring
while [ ! -f /etc/ceph/ceph.client.admin.keyring ]
do
    sleep 1
done
cp /etc/ceph/ceph.conf /etc/ceph/ceph.client.admin.keyring /vagrant/
