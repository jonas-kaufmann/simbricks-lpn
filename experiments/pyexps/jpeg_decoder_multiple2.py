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
import typing as tp
from enum import Enum

import simbricks.orchestration.experiments as exp
import simbricks.orchestration.nodeconfig as node
import simbricks.orchestration.simulators as sim
from simbricks.orchestration.nodeconfig import NodeConfig


class ImgType(Enum):
    small = 'small_test_img.jpg'
    small_unopt = 'small_test_img_unoptimized.jpg'
    medium = 'medium_test_img.jpg'
    medium_unopt = 'medium_test_img_unoptimized.jpg'

    def input_size(self):
        if self == ImgType.small:
            return 303
        elif self == ImgType.small_unopt:
            return 660
        elif self == ImgType.medium:
            return 645813
        elif self == ImgType.medium_unopt:
            return 808139
        raise RuntimeError('unknown ImgType')

    def result_size(self):
        if self in [ImgType.small, ImgType.small_unopt]:
            return 16 * 16 * 2
        elif self in [ImgType.medium, ImgType.medium_unopt]:
            return 2560 * 1440 * 2
        raise RuntimeError('unknown ImgType')


class JpegDecoderWorkload(node.AppConfig):

    def __init__(
        self, img_type: ImgType, dma_src_addr: int, dma_dst_addr: int
    ) -> None:
        super().__init__()
        self.img_type = img_type
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
        cmds = [
            'echo 1 >/sys/module/vfio/parameters/enable_unsafe_noiommu_mode',
            'echo "dead beef" >/sys/bus/pci/drivers/vfio-pci/new_id',
            f'dd if=/tmp/guest/{self.img_type.value} bs={bs} of=/dev/mem '
            f'seek={self.dma_src_addr // bs} status=progress',
            '/tmp/guest/jpeg_decoder_workload 0000:00:00.0 '
            f'{self.dma_src_addr} {self.img_type.input_size()} {self.dma_dst_addr}'
        ]
        if self.img_type in [
            ImgType.small,
            ImgType.small_unopt,
            ImgType.medium,
            ImgType.medium_unopt
        ]:
            cmds.append(f'hexdump /dev/mem -n 1024 -s {self.dma_dst_addr}')
        return cmds

    def config_files(self) -> tp.Dict[str, tp.IO]:
        return {
            'jpeg_decoder_workload':
                open('../sims/misc/jpeg_decoder/jpeg_decoder_workload', 'rb'),
            ImgType.small.value:
                open(f'../sims/misc/jpeg_decoder/{ImgType.small.value}', 'rb'),
            ImgType.small_unopt.value:
                open(
                    f'../sims/misc/jpeg_decoder/{ImgType.small_unopt.value}',
                    'rb'
                ),
            ImgType.medium.value:
                open(f'../sims/misc/jpeg_decoder/{ImgType.medium.value}', 'rb'),
            ImgType.medium_unopt.value:
                open(
                    f'../sims/misc/jpeg_decoder/{ImgType.medium_unopt.value}',
                    'rb'
                ),
        }


experiments = []
img_type: ImgType
img_types = {
    'single': [ImgType.small, ImgType.medium],
    'multiple': [ImgType.small_unopt, ImgType.medium_unopt]
}

for variant in ['single', 'multiple']:
    for img_type in img_types[variant]:
        e = exp.Experiment(f'jpeg_decoder_{variant}_{img_type.name}')
        e.checkpoint = True

        node_cfg = node.NodeConfig()
        node_cfg.kcmd_append = 'memmap=512M!1G'
        dma_src = 1 * 1024**3
        dma_dst = dma_src + 10 * 1024**2
        node_cfg.memory = 2 * 1024
        node_cfg.app = JpegDecoderWorkload(img_type, dma_src, dma_dst)
        host = sim.Gem5Host(node_cfg)
        host.wait = True
        host.cpu_freq = '1200MHz'
        host.sys_clock = '1200MHz'
        e.add_host(host)

        jpeg_dev = sim.JpegDecoderDev()
        if variant == 'multiple':
            jpeg_dev.variant = 'jpeg_decoder_multiple2_verilator'
        host.add_pcidev(jpeg_dev)
        e.add_pcidev(jpeg_dev)

        # set realistic latencies for AXI
        host.pci_latency = host.sync_period = jpeg_dev.pci_latency = \
            jpeg_dev.sync_period = 110
        experiments.append(e)
