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

import simbricks.orchestration.experiments as exp
import simbricks.orchestration.nodeconfig as node
import simbricks.orchestration.simulators as sim

experiments = []
class CustomGem5(sim.Gem5Host):

    def __init__(self, node_config: sim.NodeConfig) -> None:
        super().__init__(node_config)
        self.cpu_type = 'O3CPU'
        self.cpu_freq = '4.2GHz'
        self.mem_sidechannels = []
        self.variant = 'fast'

    def run_cmd(self, env: sim.ExpEnv) -> str:
        cmd = super().run_cmd(env)
        cmd += ' '

        for mem_sidechannel in self.mem_sidechannels:
            cmd += (
                '--simbricks-mem_sidechannel=connect'
                f':{env.dev_mem_path(mem_sidechannel)}'
            )
            cmd += ' '
        return cmd

for h in ['qk', 'qt', 'gk', 'gt']:
    for vta_var in ['lpn', 'rtl']:
        print("running")
        e = exp.Experiment('vtatest_200-' + h + '-' + vta_var)
        e.checkpoint = False

        node_config = node.LinuxVTANode()
        node_config.app = node.VTAMatMul('0000:00:02.0')

        if h == 'gk':
            node_config.app.pci_device = '0000:00:00.0'
            host = sim.Gem5Host(node_config)
            host.cpu_type = 'X86KvmCPU'
        elif h == 'gt':
            e.checkpoint = True
            node_config.app.gem5_cp = True
            node_config.app.pci_device = '0000:00:00.0'
            host = CustomGem5(node_config)
            host.sync = True
            host.cpu_type = 'O3CPU'
            host.variant = 'fast'
        elif h == 'qk':
            host = sim.QemuHost(node_config)
        elif h == 'qt':
            host = sim.QemuHost(node_config)
            host.sync = True
        host.name = 'host.0'
        e.add_host(host)
        host.wait = True
        if vta_var == 'lpn':
            vta = sim.VTALpnBmDev()
            if h == 'gt':
                host.mem_sidechannels.append(vta)

        elif vta_var == 'rtl':
            vta = sim.VTADev()
        vta.name = 'vta0'
        vta.clock_freq = 200 # in Mhz
        e.add_pcidev(vta)

        host.add_pcidev(vta)

        vta.pci_latency = vta.sync_period = host.pci_latency = \
            host.sync_period = 500

        experiments.append(e)
