# Using the latest epitech image
FROM ubuntu:22.04

RUN apt-get -y update  && apt-get -y install build-essential wget cmake git libnuma-dev python3-pyelftools meson ninja-build iproute2 python3 pkg-config clangd pciutils
RUN cd /opt && wget https://fast.dpdk.org/rel/dpdk-23.11.tar.xz && tar xJf dpdk-23.11.tar.xz
# cd /opt && git clone http://dpdk.org/git/dpdk-kmods && cd dpdk-kmods/linux/igb_uio/ && make && modprobe uio && insmod igb_uio.ko &&
RUN  cd /opt/dpdk-23.11 && meson setup -Dexamples=all build && cd build && ninja && meson install && ldconfig 
# Configure our workspace
WORKDIR /workspaces