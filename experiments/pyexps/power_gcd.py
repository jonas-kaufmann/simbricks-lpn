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

import simbricks.orchestration.experiments as exp
import simbricks.orchestration.nodeconfig as node
import simbricks.orchestration.simulators as sim


class GcdWorkload(node.AppConfig):

    def prepare_pre_cp(self) -> tp.List[str]:
        return [
            'mount -t proc proc /proc',
            'mount -t sysfs sysfs /sys',
        ]

    def config_files(self) -> tp.Dict[str, tp.IO]:
        return {
            'gcd_sw_workload': open('../sims/misc/gcd/gcd_sw_workload', 'rb'),
            'gcd_hw_workload': open('../sims/misc/gcd/gcd_hw_workload', 'rb')
        }


class GcdSwWorkload(GcdWorkload):

    def run_cmds(self, _: node.NodeConfig) -> tp.List[str]:
        return [
            f'm5 dumpstats 0 {10 * 10 ** 6}',  # dump every 10 milliseconds
            f'time -v /tmp/guest/gcd_sw_workload 46368 28657 {10 ** 8}',
            'm5 dumpstats 0 0'
        ]


class GcdHwWorkload(GcdWorkload):

    def prepare_post_cp(self) -> tp.List[str]:
        return [
            'echo 1 >/sys/module/vfio/parameters/enable_unsafe_noiommu_mode',
            'echo "dead beef" >/sys/bus/pci/drivers/vfio-pci/new_id'
        ]

    def run_cmds(self, _: node.NodeConfig) -> tp.List[str]:
        return [
            f'm5 dumpstats 0 {10 * 10 ** 6}',  # dump every 10 milliseconds
            'time -v /tmp/guest/gcd_hw_workload 0000:00:00.0 46368 28657 '
            f'{10 ** 8}',
            'm5 dumpstats 0 0'
        ]


experiments = []
for workload_type in ['sw', 'hw']:
    e = exp.Experiment(f'power_gcd_{workload_type}')
    e.checkpoint = True

    node_cfg = node.NodeConfig()
    node_cfg.app = GcdSwWorkload() if workload_type == 'sw' else GcdHwWorkload()
    host = sim.Gem5Host(node_cfg)
    host.wait = True
    host.variant = 'fast'
    host.cpu_freq = '250MHz'
    host.sys_clock = '250MHz'
    e.add_host(host)

    gcd_dev = sim.GcdDev()
    gcd_dev.clock_freq = 1000
    if workload_type == 'hw':
        host.add_pcidev(gcd_dev)
        e.add_pcidev(gcd_dev)

    # 30 ns RTT
    host.pci_latency = host.sync_period = gcd_dev.pci_latency = \
        gcd_dev.sync_period = 15
    experiments.append(e)
