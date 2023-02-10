#!/bin/bash -eux

apt-get update
apt-get -y install \
    build-essential \
    g++ \
    gcc \
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
    libtinfo-dev \
    zlib1g-dev \
    build-essential \
    cmake \
    libedit-dev \
    libxml2-dev \
    llvm-dev \
    strace


export TVM_HOME=/root/tvm
export PYTHONPATH=$TVM_HOME/python:$TVM_HOME/vta/python:$PYTHONPATH
export VTA_HW_PATH=$TVM_HOME/3rdparty/vta-hw


mkdir -p /root
git clone --recursive --branch simbricks https://github.com/simbricks/tvm-simbricks /root/tvm
cd /root/tvm
cp 3rdparty/vta-hw/config/simbricks_pci_sample.json 3rdparty/vta-hw/config/vta_config.json
mkdir build
cp cmake/config.cmake build
cd build
cmake ..
make -j`nproc`
make -j`nproc` runtime vta
make clean # some hack because of broken build system
make -j`nproc`
make runtime vta -j`nproc`


cd /root
git clone https://github.com/tlc-pack/tophub.git
cd tophub
mkdir /root/.tvm
mv tophub /root/.tvm/

mkdir /root/.vta_cache
touch /root/.vta_cache/simbricks-pci-dummy.bit
