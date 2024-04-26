#!/bin/bash
set -eux

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
    unzip


# install mxnet and clone the model
pip install mxnet
mkdir -p /mxnet/models
cd /mxnet/models
wget https://apache-mxnet.s3-accelerate.dualstack.amazonaws.com/gluon/models/resnet18_v1-a0666292.zip
unzip resnet18_v1-a0666292.zip
rm resnet18_v1-a0666292.zip
cd /


export TVM_HOME=/root/tvm
export PYTHONPATH=$TVM_HOME/python:$TVM_HOME/vta/python
export VTA_HW_PATH=$TVM_HOME/3rdparty/vta-hw


mkdir -p /root
git clone --recursive --branch lpn https://github.com/jonas-kaufmann/tvm-simbricks.git /root/tvm
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
