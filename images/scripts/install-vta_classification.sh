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

# 1x16
echo '{"TARGET" : "simbricks-pci", "HW_VER" : "0.0.2", "LOG_INP_WIDTH" : 3, "LOG_WGT_WIDTH" : 3, "LOG_ACC_WIDTH" : 5, "LOG_BATCH" : 0, "LOG_BLOCK" : 4, "LOG_UOP_BUFF_SIZE" : 15, "LOG_INP_BUFF_SIZE" : 15, "LOG_WGT_BUFF_SIZE" : 18, "LOG_ACC_BUFF_SIZE" : 17 }' > $VTA_CFG
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet18_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet34_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet50_v1 $MXNET_HOME &
wait

# 2x16
echo '{"TARGET" : "simbricks-pci", "HW_VER" : "0.0.2", "LOG_INP_WIDTH" : 3, "LOG_WGT_WIDTH" : 3, "LOG_ACC_WIDTH" : 5, "LOG_BATCH" : 1, "LOG_BLOCK" : 4, "LOG_UOP_BUFF_SIZE" : 15, "LOG_INP_BUFF_SIZE" : 15, "LOG_WGT_BUFF_SIZE" : 18, "LOG_ACC_BUFF_SIZE" : 17 }' > $VTA_CFG
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet18_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet34_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet50_v1 $MXNET_HOME &
wait

# 4x16
echo '{"TARGET" : "simbricks-pci", "HW_VER" : "0.0.2", "LOG_INP_WIDTH" : 3, "LOG_WGT_WIDTH" : 3, "LOG_ACC_WIDTH" : 5, "LOG_BATCH" : 2, "LOG_BLOCK" : 4, "LOG_UOP_BUFF_SIZE" : 15, "LOG_INP_BUFF_SIZE" : 15, "LOG_WGT_BUFF_SIZE" : 18, "LOG_ACC_BUFF_SIZE" : 17 }' > $VTA_CFG
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet18_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet34_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet50_v1 $MXNET_HOME &
wait

# 1x32
echo '{"TARGET" : "simbricks-pci", "HW_VER" : "0.0.2", "LOG_INP_WIDTH" : 3, "LOG_WGT_WIDTH" : 3, "LOG_ACC_WIDTH" : 5, "LOG_BATCH" : 0, "LOG_BLOCK" : 5, "LOG_UOP_BUFF_SIZE" : 15, "LOG_INP_BUFF_SIZE" : 15, "LOG_WGT_BUFF_SIZE" : 18, "LOG_ACC_BUFF_SIZE" : 17 }' > $VTA_CFG
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet18_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet34_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet50_v1 $MXNET_HOME &
wait

# 2x32
echo '{"TARGET" : "simbricks-pci", "HW_VER" : "0.0.2", "LOG_INP_WIDTH" : 3, "LOG_WGT_WIDTH" : 3, "LOG_ACC_WIDTH" : 5, "LOG_BATCH" : 1, "LOG_BLOCK" : 5, "LOG_UOP_BUFF_SIZE" : 15, "LOG_INP_BUFF_SIZE" : 15, "LOG_WGT_BUFF_SIZE" : 18, "LOG_ACC_BUFF_SIZE" : 17 }' > $VTA_CFG
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet18_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet34_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet50_v1 $MXNET_HOME &
wait

# 4x32
echo '{"TARGET" : "simbricks-pci", "HW_VER" : "0.0.2", "LOG_INP_WIDTH" : 3, "LOG_WGT_WIDTH" : 3, "LOG_ACC_WIDTH" : 5, "LOG_BATCH" : 2, "LOG_BLOCK" : 5, "LOG_UOP_BUFF_SIZE" : 15, "LOG_INP_BUFF_SIZE" : 15, "LOG_WGT_BUFF_SIZE" : 18, "LOG_ACC_BUFF_SIZE" : 17 }' > $VTA_CFG
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet18_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet34_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py vta resnet50_v1 $MXNET_HOME &
wait

# restore default config
cp /root/tvm/3rdparty/vta-hw/config/simbricks_pci_sample.json $VTA_CFG

python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py cpu resnet18_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py cpu resnet34_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py cpu resnet50_v1 $MXNET_HOME &
wait

python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py cpu_avx512 resnet18_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py cpu_avx512 resnet34_v1 $MXNET_HOME &
python3 /root/tvm/vta/tutorials/frontend/deploy_classification-compile_lib.py cpu_avx512 resnet50_v1 $MXNET_HOME &
wait
