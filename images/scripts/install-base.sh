#!/bin/bash -eux

set -eux

apt-get update
apt-get -y install \
    iperf \
    iputils-ping \
    lbzip2 \
    netperf \
    netcat-openbsd \
    ethtool \
    tcpdump \
    pciutils \
    busybox \
    numactl \
    sysbench \
    time \
    bsdextrautils \
    wget \
    build-essential


mkdir -p /root
pushd /root
wget https://oc.cs.uni-saarland.de/index.php/s/CSFRMcpKTprZQXj/download/npb.tar.gz
tar -xvzf npb.tar.gz
rm npb.tar.gz
pushd npb
make suite
popd
popd

pushd /tmp/input
mv guestinit.sh /home/ubuntu/guestinit.sh
mv bzImage /boot/vmlinuz-6.11.0
mv config-6.11.0 /boot/
mv m5 /sbin/m5

GRUB_CFG_FILE=/etc/default/grub.d/50-cloudimg-settings.cfg
echo 'GRUB_DISABLE_OS_PROBER=true' >> $GRUB_CFG_FILE
echo 'GRUB_HIDDEN_TIMEOUT=0' >> $GRUB_CFG_FILE
echo 'GRUB_TIMEOUT=0' >> $GRUB_CFG_FILE
update-grub

# with stupid ubuntu22 /lib is a symlink at which point just untaring to / will
# replace that symlink with a directory, so first extract and then carefully
# copy... -.-
mkdir kheaders
cd kheaders
tar xf /tmp/input/kheaders.tar.bz2
cp -a lib/modules/* /lib/modules/
cp -a usr/* /usr/

# cleanup
popd
rm -rf /tmp/input

apt-get -y install \
    gcc-multilib \
    build-essential \
    libssl-dev \
    llvm \
    lld \
    libelf-dev \
    clang \
    meson \
    cmake \
    pkg-config \
    libsystemd-dev

apt-get -y install \
    build-essential \
    g++ \
    gcc \
    g++-9 \
    gcc-9 \
    git \
    make \
    python-is-python3 \
    python3 \
    python3-cloudpickle \
    python3-decorator \
    python3-dev \
    python3-numpy \
    python3-psutil \
    python3-pytest \
    python3-scipy \
    python3-setuptools \
    python3-typing-extensions \
    python3-tornado \
    python3-willow \
    python3-pip \
    libtinfo-dev \
    zlib1g-dev \
    build-essential \
    cmake \
    libedit-dev \
    libxml2-dev \
    llvm-dev \
    strace \
    libquadmath0 \
    unzip \
    libcapstone-dev \
    libbpf-dev

# install mxnet and clone the model
# pip install mxnet
# mkdir -p /mxnet/models
# cd /mxnet/models
# wget https://apache-mxnet.s3-accelerate.dualstack.amazonaws.com/gluon/models/resnet18_v1-a0666292.zip
# unzip resnet18_v1-a0666292.zip
# rm resnet18_v1-a0666292.zip
cd /


export TVM_HOME=/root/tvm
export PYTHONPATH=$TVM_HOME/python:$TVM_HOME/vta/python
export VTA_HW_PATH=$TVM_HOME/3rdparty/vta-hw



mkdir -p /root
git clone --recursive https://github.com/MJChku/tvm-simbricks /root/tvm
cd /root/tvm
cp 3rdparty/vta-hw/config/simbricks_pci_sample.json 3rdparty/vta-hw/config/vta_config.json
mkdir build
cp cmake/config.cmake build
cd build
cmake ..
make -j`nproc`
make -j`nproc` runtime vta

cd /root
git clone https://github.com/tlc-pack/tophub.git
cd tophub
mkdir /root/.tvm
mv tophub /root/.tvm/

mkdir /root/.vta_cache
touch /root/.vta_cache/simbricks-pci-dummy.bit
