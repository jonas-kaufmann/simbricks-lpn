#!/bin/bash -eux

set -eux

GRUB_CFG_FILE=/etc/default/grub.d/50-cloudimg-settings.cfg
echo "GRUB_CMDLINE_LINUX_DEFAULT=\"console=tty1 console=ttyS0 init=/home/ubuntu/guestinit.sh memmap=512M!1G rw\"" >> $GRUB_CFG_FILE
update-grub
