A simple [devcontainer](https://code.visualstudio.com/docs/devcontainers/containers) for [dpdk](https://www.dpdk.org/). this program frowards packets from one port to another and print total number of flows per source ip.
# bind interfaces to vfio driver:
```bash
cd /opt/trex-core/scripts/
python3 /opt/dpdk-23.11/usertools/dpdk-devbind.py --bind vfio-pci 01:00.0 01:00.1
```
# runnging simplefwd:
```bash
/workspaces/build/simplefwd -l 10-11 -n 2 -- -q 1 -p 3 --portmap="(0,1)"
```