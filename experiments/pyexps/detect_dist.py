import simbricks.orchestration.experiments as exp
import simbricks.orchestration.nodeconfig as node
import simbricks.orchestration.simulators as sim
import simbricks.orchestration.simulator_utils as sim_utils
import itertools

experiments = []

host_variants = ["qemu_k", "gem5", "qemu_i"]
num_clients_opts = [1, 4, 12]
num_servers_opts = [1, 2, 4, 6]
inference_device_opts = [node.TvmDeviceType.CPU, node.TvmDeviceType.VTA]
vta_clk_freq_opts = [100, 400]

for host_var, num_clients, num_servers, inference_device, vta_clk_freq in itertools.product(
    host_variants, num_clients_opts, num_servers_opts, inference_device_opts, vta_clk_freq_opts
):
    experiment = exp.Experiment(
        f"detect_dist-{inference_device.value}-{host_var}-{num_servers}s-{num_clients}c-{vta_clk_freq}"
    )
    pci_vta_id_start = 1
    sync = False
    if host_var == "qemu_k":
        HostClass = sim.QemuHost
        pci_vta_id_start = 3
    elif host_var == "qemu_i":
        HostClass = sim.QemuIcountHost
        pci_vta_id_start = 3
        sync = True
    elif host_var == "gem5":
        HostClass = sim.Gem5Host
        sync = True

    # instantiate network
    switch = sim.SwitchNet()
    switch.name = "switch0"
    experiment.add_network(switch)

    # instantiate load balancer
    tracker = sim_utils.create_basic_hosts(
        experiment,
        1,
        "tvm_tracker",
        switch,
        sim.I40eNIC,
        HostClass,
        node.i40eLinuxTvmNode,
        node.TvmTracker,
    )[0]

    # instantiate & configure inference servers
    servers = sim_utils.create_basic_hosts(
        experiment,
        num_servers,
        "vta_server",
        switch,
        sim.I40eNIC,
        HostClass,
        node.i40eLinuxVtaNode,
        object,
        2,
    )
    for i in range(len(servers)):
        app = node.VtaRpcServerWTracker()
        app.tracker_host = tracker.node_config.ip
        app.pci_device_id = f"0000:00:{(pci_vta_id_start):02d}.0"
        servers[i].node_config.app = app

        vta = sim.VTADev()
        vta.name = f"vta{i}"
        vta.clock_freq = vta_clk_freq
        servers[i].add_pcidev(vta)
        experiment.add_pcidev(vta)

    # instantiate & configure clients using inference service
    clients = sim_utils.create_basic_hosts(
        experiment,
        num_clients,
        "tvm_client",
        switch,
        sim.I40eNIC,
        HostClass,
        node.i40eLinuxTvmDetectNode,
        object,
        2 + num_servers,
    )
    for client in clients:
        app = node.TvmDetectWTracker()
        app.tracker_host = tracker.node_config.ip
        app.device = inference_device
        client.node_config.app = app
        client.wait = True

    # either globally synchronize
    for dev in experiment.pcidevs:
        dev.sync_mode = 1 if sync else 0
    for host in experiment.hosts:
        host.node_config.nockp = not experiment.checkpoint
    switch.sync = sync

    experiments.append(experiment)