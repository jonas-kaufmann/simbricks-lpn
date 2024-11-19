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
host_sim_choices = ["gem5_o3"]
binaries = ["bt", "cg", "ep", "ft", "is", "lu", "mg", "sp"]
classes = ["S", "W", "A", "B", "C", "D"]
threads = ["1", "2", "4", "8"]


class VtaNode(node.NodeConfig):

    def __init__(self) -> None:
        super().__init__()
        # Use locally built disk image
        self.disk_image = "npb"
        # Bump amount of system memory
        self.memory = 4 * 1024
        # Reserve physical range of memory for the VTA user-space driver
        # 1G is the start 1G - 1G+512M 
        # self.kcmd_append = "memmap=512M$1G iomem=relaxed"
        self.kcmd_append = "memmap=512M@1G iomem=relaxed"
        # self.kcmd_append = "iomem=relaxed"

    def prepare_pre_cp(self):
        # Define commands to run before application to configure the server
        cmds = super().prepare_pre_cp()
        cmds.extend([
            "mount -t proc proc /proc",
            "mount -t sysfs sysfs /sys",
            # Make TVM's Python framework available
            "export PYTHONPATH=/root/tvm/python:${PYTHONPATH}",
            "export PYTHONPATH=/root/tvm/vta/python:${PYTHONPATH}",
            "export MXNET_HOME=/root/mxnet",
            # Set up loopback interface so the TVM inference script can
            # connect to the RPC server
            "ip link set lo up",
            "ip addr add 127.0.0.1/8 dev lo",
            # Make VTA device available for control from user-space via
            # VFIO
            (
                "echo 1"
                " >/sys/module/vfio/parameters/enable_unsafe_noiommu_mode"
            ),
            'echo "dead beef" >/sys/bus/pci/drivers/vfio-pci/new_id',
        ])
        return cmds

class NpbBenchmark(node.AppConfig):

    def __init__(self, binary, class_, threads) -> None:
        super().__init__()
        self.binary = binary
        self.class_ = class_
        self.threads = threads

    def prepare_pre_cp(self):
        cmds = super().prepare_pre_cp()
        # cmds.append(
        #     "g++ -o /tmp/guest/test /tmp/guest/test.c"
        # )
        return cmds
    
    def run_cmds(self, node) -> List[str]:
        # cmds = ["cd /tmp/guest && ./test"]
        cmds = ["cd /root/npb-new/bin"]
        num_thread = int(self.threads)
        cmds.extend([
            f"OMP_NUM_THREADS={self.threads} taskset -c 0-{num_thread-1} ./{self.binary}.{self.class_}",
            f"OMP_NUM_THREADS={self.threads} taskset -c 0-{num_thread-1} ./{self.binary}.{self.class_}"
        ])
        # avoid the output are eaten by the shell
        cmds.extend(["ls -l /root/npb-new/bin"])
        # binaries = [
        #     "bt.W", "cg.W", "ep.W", "ft.W", "is.W", "lu.W", "mg.W", "sp.W"
        # ]
        return cmds

for thread in threads:
    for binary in binaries:
        for class_ in classes:
            for host_sim in host_sim_choices:
                e = exp.Experiment(f"npb_benchmark-{host_sim}-{binary}-{class_}-{thread}")
                e.checkpoint = True

                node_config = VtaNode()
                node_config.nockp = not e.checkpoint
                node_config.memory = 3072
                node_config.cores = 4

                node_config.app = NpbBenchmark(binary, class_, thread)

                if host_sim == "gem5_kvm":
                    host = sim.Gem5Host(node_config)
                    host.cpu_type = 'X86KvmCPU'
                    host.name = 'host0'
                    host.sync = False
                    host.wait = True
                elif host_sim == "gem5_o3":
                    host = sim.Gem5Host(node_config)
                    host.cpu_type = 'O3CPU'
                    host.variant = 'fast'
                    host.cpu_freq = '3GHz'
                    host.name = 'host0'
                    host.sync = False
                    host.wait = True

                # This is just a dummy to have a simulator to synchronize with. We need this
                # to evaluate the overhead for scheduling an event for synchronization every
                # x ns. You need to uncomment the two lines below to enable injecting these
                # synchronization events.
                # vta = sim.VTADev()
                # vta.name = 'vta0'
                # host.add_pcidev(vta)

                # vta.pci_latency = vta.sync_period = host.pci_latency = \
                    # host.sync_period = host.pci_latency = host.sync_period = 100000000  #100 us

                e.add_host(host)
                # e.add_pcidev(vta)

                experiments.append(e)
