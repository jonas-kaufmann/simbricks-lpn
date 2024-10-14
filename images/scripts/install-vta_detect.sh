#!/bin/bash
set -eux

apt-get update
apt-get -y install \
    python3-cffi \
    python3-opencv \
    python3-matplotlib

export MODEL_NAME=yolov3-tiny
export REPO_URL=https://github.com/dmlc/web-data/blob/main/darknet/
export DARKNET_DIR=/root/darknet
mkdir -p $DARKNET_DIR
cd $DARKNET_DIR
wget -O ${MODEL_NAME}.cfg https://github.com/pjreddie/darknet/blob/master/cfg/${MODEL_NAME}.cfg?raw=true
wget -O ${MODEL_NAME}.weights https://pjreddie.com/media/files/${MODEL_NAME}.weights?raw=true
wget -O libdarknet2.0.so ${REPO_URL}lib/libdarknet2.0.so?raw=true
wget -O coco.names ${REPO_URL}data/coco.names?raw=true
wget -O arial.ttf ${REPO_URL}data/arial.ttf?raw=true
wget -O dog.jpg ${REPO_URL}data/dog.jpg?raw=true
wget -O eagle.jpg ${REPO_URL}data/eagle.jpg?raw=true
wget -O giraffe.jpg ${REPO_URL}data/giraffe.jpg?raw=true
wget -O horses.jpg ${REPO_URL}data/horses.jpg?raw=true
wget -O kite.jpg ${REPO_URL}data/kite.jpg?raw=true
wget -O person.jpg ${REPO_URL}data/person.jpg?raw=true
wget -O scream.jpg ${REPO_URL}data/scream.jpg?raw=true

export PYTHONPATH=/root/tvm/python:/root/tvm/vta/python
python3 /root/tvm/vta/tutorials/frontend/deploy_detection-compile_lib.py $DARKNET_DIR