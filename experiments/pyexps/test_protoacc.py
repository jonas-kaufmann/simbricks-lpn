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
host_sim_choices = ["gem5_o3", "gem5_kvm", "qemu_kvm"]
which_bench = ["bench0","bench1","bench2","bench3","bench4","bench5", "b1s"]
rtl_mode = ['rtl', 'lpn']

class CustomGem5(sim.Gem5Host):

    def __init__(self, node_config: sim.NodeConfig) -> None:
        super().__init__(node_config)
        self.cpu_type = 'O3CPU'
        self.cpu_freq = '3GHz'
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

class VtaNode(node.NodeConfig):

    def __init__(self) -> None:
        super().__init__()
        # Use locally built disk image
        self.disk_image = "vta_classification"
        # Bump amount of system memory
        self.memory = 4 * 1024
        # Reserve physical range of memory for the VTA user-space driver
        # 1G is the start 1G - 1G+512M 
        self.kcmd_append = "memmap=512M$1G iomem=relaxed"

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
            'echo 1 >/sys/module/vfio/parameters/enable_unsafe_noiommu_mode',
            'echo "dead beef" >/sys/bus/pci/drivers/vfio-pci/new_id',
            'cat /sys/module/vfio/parameters/enable_unsafe_noiommu_mode',
        ])

        
        return cmds

class ProtoaccBenchmark(node.AppConfig):

    def __init__(self, pci_device: str, which_bench:str) -> None:
        super().__init__()
        self.pci_device = pci_device
        self.which_bench = which_bench

    def config_files(self):
        # mount TVM inference script in simulated server under /tmp/guest
        return {
            
            "benchmark.x86":
                open(
                    f"/home/jiacma/chipyard-protoacc-ae/generators/protoacc/firesim-workloads/hyperproto/HyperZurvanGem5/{self.which_bench}-ser/benchmark.x86",
                    # f"/home/jiacma/chipyard-protoacc-ae/generators/protoacc/firesim-workloads/hyperproto/HyperZurvanNoAccel/{self.which_bench}-ser/benchmark.x86",
                    # f"/home/jiacma/chipyard-protoacc-ae/generators/protoacc/firesim-workloads/hyperproto/HyperZurvan/{self.which_bench}-ser/b_pure_cpu_gem5.x86",
                    # f"/home/jiacma/chipyard-protoacc-ae/generators/protoacc/firesim-workloads/hyperproto/HyperZurvan/{self.which_bench}-ser/vfio_with_pac.x86",
                    # f"/home/jiacma/chipyard-protoacc-ae/generators/protoacc/firesim-workloads/hyperproto/HyperZurvan/bench1-ser/benchmark.x86",
                    "rb",
                )
        }

    def prepare_pre_cp(self):
        cmds = super().prepare_pre_cp()
        cmds.extend([
            f'export PROTOACC_DEVICE={self.pci_device}',
            # "cd /tmp/guest && ./benchmark.x86"
            ]
        )
        return cmds
    
    def run_cmds(self, node) -> List[str]:
        cmds = ["cd /tmp/guest && ./benchmark.x86"]
        cmds.append("./benchmark.x86")
        # cmds.append("./benchmark.x86")
        cmds.append("ls")
        # cmds.append("taskset -c 1 ./benchmark.x86")
        # cmds.append("taskset -c 1 ./benchmark.x86")
        # cmds.append("taskset -c 1 ./benchmark.x86")
        # cmds.append("taskset -c 1 ./benchmark.x86")
        # cmds.append("taskset -c 1 ./benchmark.x86")
        # cmds.append("./benchmark.x86")
        return cmds

for acc_mode in rtl_mode: 
    for host_sim in host_sim_choices:
        for this_bench in which_bench:
            e = exp.Experiment(f"protoacc_benchmark_t99-{acc_mode}-{host_sim}-{this_bench}")
            # e = exp.Experiment(f"pac_benchmark_busywait-{acc_mode}-{host_sim}-{this_bench}")

            e.checkpoint = True

            node_config = node.LinuxVTANode()
            # node_config = VtaNode()
            # node_config.nockp = not e.checkpoint
            # node_config.memory = 3072
            node_config.cores = 1
            
            node_config.app = ProtoaccBenchmark('0000:00:02.0', this_bench)

            if host_sim == "gem5_kvm":
                node_config.app.pci_device = '0000:00:00.0'
                e.checkpoint = False
                host = sim.Gem5Host(node_config)
                host.cpu_type = 'X86KvmCPU'
                host.name = 'host0'
                host.sync = True
                host.wait = True
            elif host_sim == "gem5_o3":
                host = CustomGem5(node_config)
                # host = sim.Gem5Host(node_config)
                node_config.app.pci_device = '0000:00:00.0'
                e.checkpoint = True
                host.cpu_type = 'O3CPU'
                host.variant = 'fast'
                host.cpu_freq = '3GHz'
                host.name = 'host0'
                host.sync = True
                host.wait = True
            elif host_sim == "qemu_kvm":
                host = sim.QemuHost(node_config)
            
            if acc_mode == "rtl":
                pac = sim.ProtoaccDev()
            elif acc_mode == "lpn":
                pac = sim.ProtoaccLpnBmDev()

            pac.name = 'pac0'
            pac.clock_freq = 2000
            host.add_pcidev(pac)

            if acc_mode == "lpn" and host_sim == "gem5_o3":
                host.mem_sidechannels.append(pac)

            pac.pci_latency = pac.sync_period = host.pci_latency = \
                host.sync_period = host.pci_latency = host.sync_period = 4 # 4ns
            e.add_pcidev(pac)

            e.add_host(host)

            experiments.append(e)
