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
import glob
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
        images: tp.List[str],
        dma_src_addr: int,
        dma_dst_addr: int,
        debug: bool
    ) -> None:
        super().__init__()
        self.pci_dev = pci_dev
        self.images = images
        self.dma_src_addr = dma_src_addr
        self.dma_dst_addr = dma_dst_addr
        self.debug = debug

    def prepare_pre_cp(self) -> tp.List[str]:
        return [
            'mount -t proc proc /proc',
            'mount -t sysfs sysfs /sys',
            'echo 1 >/sys/module/vfio/parameters/enable_unsafe_noiommu_mode',
            'echo "dead beef" >/sys/bus/pci/drivers/vfio-pci/new_id',
        ]

    def run_cmds(self, node: NodeConfig) -> tp.List[str]:
        # enable vfio access to JPEG decoder
        cmds = []

        for img in self.images:
            with Image.open(img) as loaded_img:
                width, height = loaded_img.size

            cmds.extend([
                f'echo starting decode of image {os.path.basename(img)}',
                # copy image into memory
                (
                    f'dd if=/tmp/guest/{os.path.basename(img)} bs=4096 '
                    f'of=/dev/mem seek={self.dma_src_addr} oflag=seek_bytes '
                ),
                # invoke workload driver
                (
                    f'/tmp/guest/jpeg_decoder_workload_driver {self.pci_dev} '
                    f'{self.dma_src_addr} {os.path.getsize(img)} '
                    f'{self.dma_dst_addr}'
                ),
                f'echo finished decode of image {os.path.basename(img)}',
            ])

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
        files = {
            'jpeg_decoder_workload_driver':
                open(
                    '../sims/lpn/jpeg_decoder/jpeg_decoder_workload_driver',
                    'rb'
                )
        }

        for img in self.images:
            files[os.path.basename(img)] = open(img, 'rb')

        return files


experiments: tp.List[exp.Experiment] = []
for host_var in ['gem5_kvm', 'gem5_timing', 'qemu_icount', 'qemu_kvm']:
    for jpeg_var in ['lpn', 'rtl']:
        e = exp.Experiment(f'jpeg_decoder-{host_var}-{jpeg_var}')
        node_cfg = node.NodeConfig()
        node_cfg.kcmd_append = 'memmap=512M!1G'
        dma_src = 1 * 1024**3
        dma_dst = dma_src + 10 * 1024**2
        node_cfg.memory = 2 * 1024
        images = glob.glob('../sims/misc/jpeg_decoder/test_img/420/*.jpg')
        images.sort()
        # images = images[:len(images) // 2]  # only decode half of them
        node_cfg.app = JpegDecoderWorkload(
            '0000:00:00.0', images, dma_src, dma_dst, False
        )

        if host_var == 'gem5_kvm':
            host = sim.Gem5Host(node_cfg)
            host.cpu_type = 'X86KvmCPU'
        elif host_var == 'gem5_timing':
            e.checkpoint = True
            host = sim.Gem5Host(node_cfg)
            host.modify_checkpoint_tick = False
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

        host.pci_latency = host.sync_period = jpeg_dev.pci_latency = \
            jpeg_dev.sync_period = 400

        experiments.append(e)
