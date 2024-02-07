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
import os
import typing as tp

from PIL import Image

import simbricks.orchestration.experiments as exp
import simbricks.orchestration.nodeconfig as node
import simbricks.orchestration.simulators as sim
from simbricks.orchestration.nodeconfig import NodeConfig


class JpegDecoderWorkload(node.AppConfig):

    def __init__(
        self,
        pci_dev: str,
        img: str,
        dma_src_addr: int,
        dma_dst_addr: int,
        debug: bool
    ) -> None:
        super().__init__()
        self.pci_dev = pci_dev
        self.img = img
        self.dma_src_addr = dma_src_addr
        self.dma_dst_addr = dma_dst_addr
        self.debug = debug

    def prepare_pre_cp(self) -> tp.List[str]:
        return [
            'mount -t proc proc /proc',
            'mount -t sysfs sysfs /sys',
        ]

    def run_cmds(self, node: NodeConfig) -> tp.List[str]:
        with Image.open(self.img) as img:
            width, height = img.size
            f'echo image dump begin {width} {height}',
            (
                f'dd if=/dev/mem iflag=skip_bytes,count_bytes '
                f'skip={self.dma_dst_addr} count={width * height * 2} '
                'status=none | base64'
            ),
            'echo image dump end'
        cmds = [
            # enable vfio access to JPEG decoder
            'echo 1 >/sys/module/vfio/parameters/enable_unsafe_noiommu_mode',
            'echo "dead beef" >/sys/bus/pci/drivers/vfio-pci/new_id',
            # copy image into memory
            (
                f'dd if=/tmp/guest/{os.path.basename(self.img)} bs=4096 '
                f'of=/dev/mem seek={self.dma_src_addr} oflag=seek_bytes '
            ),
            # invoke workload driver
            (
                f'/tmp/guest/jpeg_decoder_workload_driver {self.pci_dev} '
                f'{self.dma_src_addr} {os.path.getsize(self.img)} '
                f'{self.dma_dst_addr}'
            ),
        ]

        if self.debug:
            # dump the image as base64 to stdout
            cmds.extend([
                f'echo image dump begin {width} {height}',
                (
                    f'dd if=/dev/mem iflag=skip_bytes,count_bytes bs=4096 '
                    f'skip={self.dma_dst_addr} count={width * height * 2} '
                    'status=none | base64'
                ),
                'echo image dump end'
            ])
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


experiments: tp.List[exp.Experiment] = []
for host_var in ['gem5', 'qemu_icount', 'qemu_kvm']:
    for jpeg_var in ['lpn', 'rtl']:
        e = exp.Experiment(f'jpeg_decoder-{host_var}-{jpeg_var}')
        e.checkpoint = host_var not in ['qemu_icount', 'qemu_kvm']

        node_cfg = node.NodeConfig()
        node_cfg.kcmd_append = 'memmap=512M!1G'
        dma_src = 1 * 1024**3
        dma_dst = dma_src + 10 * 1024**2
        node_cfg.memory = 2 * 1024
        node_cfg.app = JpegDecoderWorkload(
            '0000:00:00.0',
            '../sims/misc/jpeg_decoder/test_img/420/medium.jpg',
            dma_src,
            dma_dst,
            True
        )

        if host_var == 'gem5':
            host = sim.Gem5Host(node_cfg)
        elif host_var == 'qemu_icount':
            node_cfg.app.pci_dev = '0000:00:02.0'
            host = sim.QemuHost(node_cfg)
            host.sync = True
        elif host_var == 'qemu_kvm':
            node_cfg.app.pci_dev = '0000:00:02.0'
            host = sim.QemuHost(node_cfg)
        else:
            raise NameError(f'Variant {host_var} is unhandled')
        host.wait = True
        e.add_host(host)

        if jpeg_var == 'lpn':
            jpeg_dev = sim.JpegDecoderLpnBmDev()
        elif jpeg_var == 'rtl':
            jpeg_dev = sim.JpegDecoderDev()
        else:
            raise NameError(f'Variant {jpeg_var} is unhandled')
        host.add_pcidev(jpeg_dev)
        e.add_pcidev(jpeg_dev)

        # TODO set realistic PCIe latencies, this is 2000 ns to make simulation
        # fast for testing. On the physical board with no PCIe and just AXI, I
        # measured 110 ns.
        #
        # With more than 2000 ns, the the lower half of the decoded image is
        # somehow missing. The same effect can be observed when running with
        # QEMU KVM.
        host.pci_latency = host.sync_period = jpeg_dev.pci_latency = \
            jpeg_dev.sync_period = 1000

        experiments.append(e)
