import os
import sys

import simbricks.orchestration.experiments as exp
import simbricks.orchestration.simulators as sim
import simbricks.orchestration.nodeconfig as node
import itertools
import os


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
    
experiments = []

# Experiment parameters
host_variants = ["qemu_kvm", "qemu_icount", "gem5_o3"]
vta_ops = ["rtl", "lpn"]
inference_device_opts = [node.TvmDeviceType.CPU, node.TvmDeviceType.VTA]
vta_clk_freq_opts = [100, 160, 200, 400, 2000]

# Build experiment for all combinations of parameters
for host_var, inference_device, vta_clk_freq, vta_op in itertools.product(
    host_variants, inference_device_opts, vta_clk_freq_opts, vta_ops
):
    experiment = exp.Experiment(
        f"detect_t-{inference_device.value}-{host_var}-{vta_op}-{vta_clk_freq}"
    )
    pci_vta_id = 2
    sync = False
    if host_var == "qemu_kvm":
        HostClass = sim.QemuHost
    elif host_var == "qemu_icount":
        HostClass = sim.QemuIcountHost
        sync = True
    elif host_var == "gem5_o3":
        HostClass = CustomGem5
        sync = True
        experiment.checkpoint = True
        pci_vta_id = 0

    #######################################################
    # Define application
    # -----------------------------------------------------

    class TvmDetectLocal(node.AppConfig):
        """Runs inference for detection model locally, either on VTA or the CPU."""

        def __init__(self):
            super().__init__()
            self.pci_device_id = f"0000:00:{(pci_vta_id):02d}.0"
            self.device = inference_device
            self.test_img = "person.jpg"
            self.repetitions = 1
            self.batch_size = 1
            self.debug = False
            """Whether to dump inference result."""

        def config_files(self):
            # mount TVM inference script in simulated server under /tmp/guest
            return {
                "deploy_detection-infer.py":
                    open(
                        "/home/jiacma/simbricks-lpn/tvm/vta/tutorials/frontend/deploy_detection-infer.py",
                        "rb",
                    )
            }

        def run_cmds(self, node):
            # define commands to run on simulated server
            cmds = [
                f"export TVM_NUM_THREADS=1 ",
                # start RPC server
                f"VTA_DEVICE={self.pci_device_id} python3 -m"
                " vta.exec.rpc_server &"
                # wait for RPC server to start
                "sleep 6",
                f"export VTA_RPC_HOST=127.0.0.1",
                f"export VTA_RPC_PORT=9091"
            ]
            if self.gem5_cp:
                cmds.append("export GEM5_CP=1")

            cmds.append(
                # run inference
                (
                    "python3 /tmp/guest/deploy_detection-infer.py "
                    "/root/darknet"
                    f" {self.device.value} {self.test_img} {self.batch_size} "
                    f"{self.repetitions} {int(self.debug)} 0"
                )
            )

            print(cmds)
            # dump image with detection boxes as base64 to allow later inspection
            if self.debug:
                cmds.extend([
                    "echo dump deploy_detection-infer-result.png START",
                    "base64 deploy_detection-infer-result.png",
                    "echo dump deploy_detection-infer-result.png END",
                ])
            return cmds

    #######################################################
    # Define server configuration
    # -----------------------------------------------------

    class VtaNode(node.NodeConfig):

        def __init__(self) -> None:
            super().__init__()
            # Use locally built disk image
            self.disk_image = "vta_detect"
            # Bump amount of system memory
            self.memory = 3 * 1024
            # Reserve physical range of memory for the VTA user-space driver
            self.kcmd_append = " memmap=512M!1G"
            self.nockp = True

        def prepare_pre_cp(self):
            # Define commands to run before application to configure the server
            cmds = super().prepare_pre_cp()
            cmds.extend([
                "mount -t proc proc /proc",
                "mount -t sysfs sysfs /sys",
                # Make TVM's Python framework available
                "export PYTHONPATH=/root/tvm/python:${PYTHONPATH}",
                "export PYTHONPATH=/root/tvm/vta/python:${PYTHONPATH}",
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

    #######################################################
    # Define and connect all simulators
    # -----------------------------------------------------
    # Instantiate server
    server_cfg = VtaNode()
    server_cfg.cores = 4
    server_cfg.app = TvmDetectLocal()
    server_cfg.app.gem5_cp = host_var in ["gem5_o3"]
    server = HostClass(server_cfg)
    # Whether to synchronize VTA and server
    server.sync = sync
    # Wait until server exits
    server.wait = True



    # Instantiate and connect VTA PCIe-based accelerator to server
    if vta_op == "rtl":
        vta = sim.VTADev()
    else:
        vta = sim.VTALpnBmDev()
        if host_var == "gem5_o3":
            server.mem_sidechannels.append(vta)

    vta.clock_freq = vta_clk_freq
    server.add_pcidev(vta)

    server.pci_latency = server.sync_period = vta.pci_latency = (
        vta.sync_period
    ) = 400

    # Add both simulators to experiment
    experiment.add_host(server)
    experiment.add_pcidev(vta)

    experiments.append(experiment) 

