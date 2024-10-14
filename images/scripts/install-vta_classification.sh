#!/bin/bash
set -eux

apt-get update
apt-get -y install --no-install-recommends \
    python3-cffi \
    python3-opencv \
    python3-pil \
    python3-pip \
    unzip

pip install mxnet

export MXNET_HOME=/root/mxnet
mkdir -p $MXNET_HOME
cd $MXNET_HOME

wget https://github.com/uwsampl/web-data/raw/main/vta/models/synset.txt

cd /

export PYTHONPATH=/root/tvm/python:/root/tvm/vta/python
export VTA_CFG=/root/tvm/3rdparty/vta-hw/config/vta_config.json

# compile libraries
cp /root/tvm/3rdparty/vta-hw/config/simbricks_pci_sample.json $VTA_CFG
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet18_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet34_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet50_v1 $MXNET_HOME &
wait

python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py cpu resnet18_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py cpu resnet34_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py cpu resnet50_v1 $MXNET_HOME &
wait
