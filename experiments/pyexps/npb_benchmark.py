# Copyright 2024 Max Planck Institute for Software Systems, and
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

from typing import List
import simbricks.orchestration.experiments as exp
import simbricks.orchestration.nodeconfig as node
import simbricks.orchestration.simulators as sim

experiments = []
host_sim_choices = ["gem5_kvm"]

class NpbBenchmark(node.AppConfig):

    def run_cmds(self, node) -> List[str]:
        cmds = super().run_cmds(node)
        binaries = [
            "bt.W", "cg.W", "ep.W", "ft.W", "is.W", "lu.W", "mg.W", "sp.W"
        ]
        cmds.extend([
            "cd /root/npb/bin", *[f"./{binary}" for binary in binaries]
        ])
        return cmds

for host_sim in host_sim_choices:

    e = exp.Experiment(f"npb_benchmark-{host_sim}")
    e.checkpoint = False

    node_config = node.LinuxNode()
    node_config.nockp = not e.checkpoint
    node_config.memory = 3072
    node_config.cores = 4

    node_config.app = NpbBenchmark()

    if host_sim == "gem5_kvm":
        host = sim.Gem5Host(node_config)
        host.cpu_type = 'X86KvmCPU'
        host.name = 'host0'
        host.sync = True
        host.wait = True

    # This is just a dummy to have a simulator to synchronize with. We need this
    # to evaluate the overhead for scheduling an event for synchronization every
    # x ns. You need to uncomment the two lines below to enable injecting these
    # synchronization events.
    vta = sim.VTALpnBmDev()
    vta.name = 'vta0'
    # host.add_pcidev(vta)

    vta.pci_latency = vta.sync_period = host.pci_latency = \
        host.sync_period = host.pci_latency = host.sync_period = 1000  #1 us

    e.add_host(host)
    # e.add_pcidev(vta)

    experiments.append(e)
