FROM debian:9
LABEL maintainer="Mariusz Karpiarz <mariusz.karpiarz@bbc.co.uk>"

RUN apt-get update
RUN apt-get install -y \
    build-essential \
    librados-dev \
    netcat \
    curl \
    libssl-dev \
    python-pip \
    python-dev
RUN pip install awscli
RUN mkdir ~/.aws/

# Drop Ceph config and admin keyring
RUN mkdir /etc/ceph
COPY ceph.conf ceph.client.admin.keyring /etc/ceph/

# Copy and compile source code
COPY . /src/
WORKDIR /src
RUN make clean && make all
