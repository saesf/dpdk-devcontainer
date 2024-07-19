A simple [devcontainer](https://code.visualstudio.com/docs/devcontainers/containers) for [dpdk](https://www.dpdk.org/) that runs l2fwd example.
# bind interfaces to vfio driver:
cd /opt/trex-core/scripts/
python3 /opt/dpdk-23.11/usertools/dpdk-devbind.py --bind vfio-pci 01:00.0 01:00.1
# runnging l2fwd:
```bash
/workspaces/build/tunnel -l 10 -n 1 -- -q 1 -p 1 --portmap="(0,0)"
```

