import math

import simbricks.orchestration.experiments as exp
import simbricks.orchestration.simulators as sim
import simbricks.orchestration.nodeconfig as node
import itertools

experiments = []

# Experiment parameters
host_variants = ["qk", "qt", "gt", "simics"]
inference_device_opts = [
    node.TvmDeviceType.VTA,
    node.TvmDeviceType.CPU,
    node.TvmDeviceType.CPU_AVX512,
]
vta_clk_freq_opts = [100, 200, 400, 800]
vta_batch_opts = [1, 2, 4]
vta_block_opts = [16, 32]
model_name_opts = [
    "resnet18_v1",
    "resnet34_v1",
    "resnet50_v1",
]
core_opts = [1, 4, 8, 16]


class TvmClassifyLocal(node.AppConfig):
    """Runs inference for detection model locally, either on VTA or the CPU."""

    def __init__(self):
        super().__init__()
        self.pci_vta_id = 0
        self.device = node.TvmDeviceType.VTA
        self.repetitions = 1
        self.batch_size = 4
        self.vta_batch = 1
        self.vta_block = 16
        self.model_name = "resnet18_v1"
        self.debug = True

    def config_files(self):
        # mount TVM inference script in simulated server under /tmp/guest
        return {
            "deploy_classification-infer.py":
                open(
                    "/local/jkaufman/tvm-simbricks/vta/tutorials/frontend/deploy_classification-infer.py",
                    "rb",
                ),
            "cat.jpg":
                open("/local/jkaufman/cat.jpg", "rb"),
        }

    def prepare_pre_cp(self) -> list[str]:
        cmds = super().prepare_pre_cp()
        cmds.extend([
            'echo \'{"TARGET" : "simbricks-pci", "HW_VER" : "0.0.2",'
            ' "LOG_INP_WIDTH" : 3, "LOG_WGT_WIDTH" : 3,'
            ' "LOG_ACC_WIDTH" : 5, "LOG_BATCH" :'
            f' {int(math.log2(self.vta_batch))}, "LOG_BLOCK" :'
            f' {int(math.log2(self.vta_block))}, "LOG_UOP_BUFF_SIZE" :'
            ' 15, "LOG_INP_BUFF_SIZE" : 15, "LOG_WGT_BUFF_SIZE" : 18,'
            ' "LOG_ACC_BUFF_SIZE" : 17 }\' >'
            " /root/tvm/3rdparty/vta-hw/config/vta_config.json"
        ])
        return cmds

    def run_cmds(self, node):
        # define commands to run on simulated server
        cmds = [
            # start RPC server
            f"VTA_DEVICE=0000:00:{(self.pci_vta_id):02d}.0 python3 -m"
            " vta.exec.rpc_server &"
            # wait for RPC server to start
            "sleep 6",
            f"export VTA_RPC_HOST=127.0.0.1",
            f"export VTA_RPC_PORT=9091",
            # run inference
            (
                "python3 /tmp/guest/deploy_classification-infer.py /root/mxnet"
                f" {self.device.value} {self.model_name} /tmp/guest/cat.jpg"
                f" {self.batch_size} {self.repetitions} {int(self.debug)}"
            ),
        ]
        return cmds


class VtaNode(node.NodeConfig):

    def __init__(self) -> None:
        super().__init__()
        # Use locally built disk image
        self.disk_image = "vta_classification"
        # Bump amount of system memory
        self.memory = 4 * 1024
        # Reserve physical range of memory for the VTA user-space driver
        self.kcmd_append = " memmap=512M!1G"

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


# Build experiment for all combinations of parameters
for (
    host_var,
    inference_device,
    vta_clk_freq,
    vta_batch,
    vta_block,
    model_name,
    cores
) in itertools.product(
    host_variants,
    inference_device_opts,
    vta_clk_freq_opts,
    vta_batch_opts,
    vta_block_opts,
    model_name_opts,
    core_opts
):
    experiment = exp.Experiment(
        f"classify_simple-{model_name}-{inference_device.value}-{host_var}-{cores}-{vta_clk_freq}-{vta_batch}x{vta_block}"
    )
    pci_vta_id = 2
    sync = False
    if host_var == "qk":
        HostClass = sim.QemuHost
    elif host_var == "qt":
        HostClass = sim.QemuIcountHost
        sync = True
    elif host_var == "gt":
        pci_vta_id = 0
        HostClass = sim.Gem5Host
        experiment.checkpoint = True
        sync = True
    elif host_var == "simics":
        HostClass = sim.SimicsHost

    # Instantiate server
    server_cfg = VtaNode()
    server_cfg.nockp = True
    server_cfg.cores = cores
    if host_var == "simics":
        server_cfg.disk_image += "-simics"
        server_cfg.kcmd_append = ""
    server_cfg.app = TvmClassifyLocal()
    server_cfg.app.device = inference_device
    server_cfg.app.vta_batch = vta_batch
    server_cfg.app.vta_block = vta_block
    server_cfg.app.model_name = model_name
    server_cfg.app.pci_vta_id = pci_vta_id
    server = HostClass(server_cfg)
    # Whether to synchronize VTA and server
    server.sync = sync
    # Wait until server exits
    server.wait = True

    # Instantiate and connect VTA PCIe-based accelerator to server
    if inference_device == node.TvmDeviceType.VTA:
        vta = sim.VTADev()
        vta.clock_freq = vta_clk_freq
        vta.batch = vta_batch
        vta.block = vta_block
        server.add_pcidev(vta)
        if host_var == "simics":
            server.debug_messages = False
            server.start_ts = vta.start_tick = int(60 * 10**12)

    server.pci_latency = server.sync_period = vta.pci_latency = (
        vta.sync_period
    ) = 500

    # Add both simulators to experiment
    experiment.add_host(server)
    experiment.add_pcidev(vta)

    experiments.append(experiment)
