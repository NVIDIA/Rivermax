# doca_rmax_rx_perf

[NVIDIA DOCA](https://developer.nvidia.com/networking/doca) is a Software
Framework to accelerate application development for the NVIDIA BlueField DPU.

`doca_rmax_rx_perf` is an RX performance measurement application to be used with the
[NVIDIA DOCA RMAX](https://docs.nvidia.com/doca/sdk/rmax-programming-guide/index.html)
library. The code is provided "As Is" and any express or implied warranties, including,
but not limited to, the implied warranties of merchantability and fitness for a particular
purpose are disclaimed.

* Release date: 27-Oct-2022
* Update date: 30-Feb-2023
* Version: 1.0
* Compatibility:
  * Rivermax: 1.21.10
  * OFED: 5.9-0.5.6.0
  * DOCA: 1.5, 2.0
  * OS: Ubuntu 20.04
  * Hardware: BlueField 2

## Documentation

* [NVIDIA DOCA SDK documentation](https://docs.nvidia.com/doca/sdk/)
* [NVIDIA DOCA RMAX Programming Guide](https://docs.nvidia.com/doca/sdk/rmax-programming-guide/index.html)
* [NVIDIA DOCA RMAX Samples](https://docs.nvidia.com/doca/sdk/rmax-samples/index.html)

## Prerequisites

* [NVIDIA DOCA SDK](https://developer.nvidia.com/networking/doca) 1.5 or 2.0.
  See [Installation guide](https://docs.nvidia.com/doca/sdk/installation-guide/index.html).
* NVIDIA DOCA RMAX library. Can be installed by SDK Manager (see DOCA SDK
  installation guide) or manually by installing `doca-rmax-libs` and
  `libdoca-rmax-libs-dev`.
* [CMake](https://cmake.org) 3.6 or later.

## Command-line parameters

```
Usage: doca_doca_rmax_perf [DOCA Flags] [Program Flags]

DOCA Flags:
  -h, --help                        Print a help synopsis
  -v, --version                     Print program version information
  -l, --log-level                   Set the log level for the program <CRITICAL=20, ERROR=30, WARNING=40, INFO=50, DEBUG=60>

Program Flags:
  --list                            List available devices
  -t, --stream-type                 Stream type: generic (default) or RTP-2110
  --scatter-type                    Scattering type: RAW (default), ULP or payload
  --tstamp-format                   Timestamp format: counter (default), nano or synced
  -i, --interface-ip                IP of the local interface to receive data
  -m, --multicast-dst               Multicast address to bind to
  -s, --multicast-src               Source address to read from
  -p, --port                        Destination port to read from
  -r, --header-size                 Header size (default 0)
  -d, --data-size                   Data size (default 1500)
  -k, --packets                     Number of packets to allocate memory for (default 1024)
  -a, --cpu-affinity                Comma separated list of CPU affinity cores for the application main thread
  --sleep                           Amount of microseconds to sleep between requests (default 0)
  --min                             Block until at least this number of packets are received (default 0)
  --max                             Maximum number of packets to return in one completion
  --dump                            Dump packet content
```

Examples:
* List available devices: `doca_doca_rmax_perf --list`
* Receive a stream: `doca_doca_rmax_perf --interface-ip 1.1.64.67 --multicast-dst 1.1.64.67 --multicast-src 1.1.63.5 --port 7000`
* Receive a stream (header-data split mode): `doca_doca_rmax_perf --interface-ip 1.1.64.67 --multicast-dst 1.1.64.67 --multicast-src 1.1.63.5 --port 7000 --header-size 20 --data-size 1200`

## How to build

From the `doca_doca_rmax_perf` directory run the following commands:

```shell
$ cmake -B ./build
$ cmake --build ./build
```

Enjoy the `doca_rmax_rx_perf` binary in directory `build`.

## Testing

Was tested in NVIDIA lab:
* Card: (MT41686 - MBF2M516A-CENOT) BlueField-2 DPU 100GbE Dual-Port QSFP56, Crypto Disabled, 16GB on-board DDR, 1GbE OOB management, Tall Bracket
* FW: 24.35.0314
* DOCA 1.5:
    * Rivermax 1.20.9
    * OFED (on BF): MLNX_OFED_LINUX-5.8-0.3.3.0
    * DOCA: 1.5.0041-1
    * BFB: DOCA_1.5.0_BSP_3.9.3_Ubuntu_20.04-5.20221003
* DOCA 2.0:
    * Rivermax: 1.21.10
    * OFED (on BF): MLNX_OFED_LINUX-5.9-0.5.6.0
    * DOCA: 2.0
    * BFB: DOCA_2.0.0_BSP_4.0.0_Ubuntu_20.04-1.20230209

## Limitations

* Supported only on aarch64 platform and Ubuntu 20.04.
* `--scatter-type payload` is not yet supported by Rivermax for Generic and ST 2110 streams.
