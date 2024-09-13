#!/bin/bash
set -eux

export TVM_HOME=/root/tvm
export PYTHONPATH=$TVM_HOME/python:$TVM_HOME/vta/python
export VTA_HW_PATH=$TVM_HOME/3rdparty/vta-hw

apt-get update
apt-get -y install --no-install-recommends \
    build-essential \
    git \
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
    llvm-dev

# build tvm
mkdir -p /root
git clone --depth 10 --recursive --branch lpn https://github.com/jonas-kaufmann/tvm-simbricks.git /root/tvm
cd /root/tvm
cp 3rdparty/vta-hw/config/simbricks_pci_sample.json 3rdparty/vta-hw/config/vta_config.json
mkdir build
cp cmake/config.cmake build
cd build
cmake ..
make -j`nproc`

# add pre-tuned autotvm configurations
mkdir /root/.tvm
cd /root/.tvm
git clone https://github.com/tlc-pack/tophub.git tophub
