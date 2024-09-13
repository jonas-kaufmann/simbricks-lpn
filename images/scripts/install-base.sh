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

pushd /root
git clone https://github.com/sched-ext/scx.git
pushd scx
meson setup build --prefix ~ -Denable_rust=false
meson compile -C build
# meson install -C build
popd
popd

