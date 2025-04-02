# Copyright 2021 Max Planck Institute for Software Systems, and
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

import os

import simbricks.orchestration.experiments as exp
import simbricks.orchestration.nodeconfig as node
import simbricks.orchestration.simulators as sim


class MemTest(node.AppConfig):

    def run_cmds(self, _: node.NodeConfig) -> tp.List[str]:
        dma_base = 1024**3
        return [
            f'busybox devmem {dma_base} 64',
            f'busybox devmem {dma_base} 64 0x1234567890123456',
            f'busybox devmem {dma_base} 64',
            'sleep 2'
        ]


class CustomGem5(sim.Gem5Host):

    def __init__(self, node_config: sim.NodeConfig) -> None:
        super().__init__(node_config)
        self.cpu_type = 'O3CPU'
        self.cpu_freq = '3GHz'
        self.mem_sidechannels = []
        self.variant = 'opt'

    def run_cmd(self, env: sim.ExpEnv) -> str:
        cmd = super().run_cmd(env)
        cmd += ' '

        for mem_sidechannel in self.mem_sidechannels:
            cmd += (
                '--simbricks-mem_sidechannel=listen'
                f':{env.dev_mem_path(mem_sidechannel)}'
                f':{env.dev_shm_path(mem_sidechannel)}'
            )
            cmd += ' '

        return cmd

    def sockets_wait(self, env: sim.ExpEnv) -> tp.List[str]:
        for mem_sidechannel in self.mem_sidechannels:
            return [env.dev_mem_path(mem_sidechannel)]


class MemSidechannelTester(sim.Simulator):

    def __init__(self) -> None:
        super().__init__()
        self.name = "mem_sidechannel.tester"
        self.deps = []

    def run_cmd(self, env: sim.ExpEnv) -> str:
        binary = os.path.join(env.repodir, "sims/misc/mem_sidechannel/tester")
        socket_path = env.dev_mem_path(self)
        return f"{binary} {socket_path}"

    def full_name(self) -> str:
        return self.name

    def dependencies(self) -> tp.List[exp.Simulator]:
        return self.deps


experiment = exp.Experiment("mem_sidechannel_test")
experiment.checkpoint = True

mem_tester = MemSidechannelTester()
# mem_tester = sim.ProtoaccLpnBmDev()
# experiment.add_pcidev(mem_tester)
experiment.add_memdev(mem_tester)

node_config = node.NodeConfig()
node_config.memory = 2048
node_config.kcmd_append = "memmap=512M$1G iomem=relaxed"
node_config.app = MemTest()
host = CustomGem5(node_config)
host.sync = True
host.mem_sidechannels.append(mem_tester)
# mem_tester.pci_latency = mem_tester.sync_period = host.pci_latency = \
#             host.sync_period = host.pci_latency = host.sync_period = 20 #1 us
host.wait = True
experiment.add_host(host)

mem_tester.deps.append(host)

experiments = [experiment]