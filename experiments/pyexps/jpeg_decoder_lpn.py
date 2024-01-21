# Copyright 2023 Max Planck Institute for Software Systems, and
# National University of Singapore
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
import math
import os
import typing as tp

from PIL import Image

import simbricks.orchestration.experiments as exp
import simbricks.orchestration.nodeconfig as node
import simbricks.orchestration.simulators as sim
from simbricks.orchestration.nodeconfig import NodeConfig


class JpegDecoderWorkload(node.AppConfig):

    def __init__(self, img: str, dma_src_addr: int, dma_dst_addr: int) -> None:
        super().__init__()
        self.img = img
        self.dma_src_addr = dma_src_addr
        self.dma_dst_addr = dma_dst_addr

    def prepare_pre_cp(self) -> tp.List[str]:
        return [
            'mount -t proc proc /proc',
            'mount -t sysfs sysfs /sys',
        ]

    def run_cmds(self, node: NodeConfig) -> tp.List[str]:
        bs = 1024
        assert self.dma_src_addr % bs == 0
        assert self.dma_dst_addr % bs == 0

        with Image.open(self.img) as img:
            width, height = img.size

        cmds = [
            # enable vfio access to JPEG decoder
            'echo 1 >/sys/module/vfio/parameters/enable_unsafe_noiommu_mode',
            'echo "dead beef" >/sys/bus/pci/drivers/vfio-pci/new_id',
            # copy image into memory
            (
                f'dd if=/tmp/guest/{os.path.basename(self.img)} bs={bs} '
                f'of=/dev/mem seek={self.dma_src_addr // bs} status=progress'
            ),
            # invoke workload driver
            (
                '/tmp/guest/jpeg_decoder_workload_driver 0000:00:00.0 '
                f'{self.dma_src_addr} {os.path.getsize(self.img)} '
                f'{self.dma_dst_addr}'
            ),
            f'hexdump /dev/mem -n 1024 -s {self.dma_dst_addr}',
            # dump the image as base64 to stdout
            # (
            #     f'dd if=/dev/mem skip={self.dma_dst_addr // bs} bs={bs} '
            #     f'of=/img.jpg count={math.ceil(width * height * 3 / bs)} '
            #     'status=progress'
            # ),
            # 'base64 /img.jpg'
        ]
        return cmds

    def config_files(self) -> tp.Dict[str, tp.IO]:
        return {
            'jpeg_decoder_workload_driver':
                open(
                    '../sims/lpn/jpeg_decoder/jpeg_decoder_workload_driver',
                    'rb'
                ),
            os.path.basename(self.img):
                open(self.img, 'rb'),
        }


e = exp.Experiment('jpeg_decoder_lpn')
e.checkpoint = True

node_cfg = node.NodeConfig()
node_cfg.kcmd_append = 'memmap=512M!1G'
dma_src = 1 * 1024**3
dma_dst = dma_src + 10 * 1024**2
node_cfg.memory = 2 * 1024
node_cfg.app = JpegDecoderWorkload(
    '../sims/lpn/jpeg_decoder/test_data/test.jpg', dma_src, dma_dst
)
host = sim.Gem5Host(node_cfg)
host.wait = True
e.add_host(host)

jpeg_dev = sim.JpegDecoderLpnBmDev()
host.add_pcidev(jpeg_dev)
e.add_pcidev(jpeg_dev)

# TODO set realistic PCIe latencies, default is 500 ns
# host.pci_latency = host.sync_period = jpeg_dev.pci_latency = \
#     jpeg_dev.sync_period = 110

experiments = [e]
