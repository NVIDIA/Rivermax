# doca_rmax_rx_perf

[NVIDIA DOCA](https://developer.nvidia.com/networking/doca) is a Software
Framework to accelerate application development for the NVIDIA BlueField DPU.

`doca_rmax_rx_perf` is an RX performance measurement application to be used with the
[NVIDIA DOCA RMAX](https://docs.nvidia.com/doca/sdk/rmax-programming-guide/index.html)
library. The code is provided "As Is" and any express or implied warranties, including,
but not limited to, the implied warranties of merchantability and fitness for a particular
purpose are disclaimed.

* Release date: 14-Aug-2024
* Update date: 14-Aug-2024
* Version: 1.3

### Tested on

* Rivermax: 1.51.6
* OFED: 24.07-0.6.1.0
* DOCA: 2.8.0
* OS: Ubuntu 22.04
* Hardware:
  * BlueField-2

## Documentation

* [NVIDIA DOCA SDK documentation](https://docs.nvidia.com/doca/sdk/)
* [NVIDIA DOCA RMAX Programming Guide](https://docs.nvidia.com/doca/sdk/rmax-programming-guide/index.html)
* [NVIDIA DOCA RMAX Samples](https://docs.nvidia.com/doca/sdk/rmax-samples/index.html)

## Prerequisites

* [NVIDIA DOCA SDK](https://developer.nvidia.com/networking/doca) 2.7.0036.
  See [Installation guide](https://docs.nvidia.com/doca/sdk/nvidia+doca+installation+guide+for+linux/index.html).
* [Rivermax](https://developer.nvidia.com/networking/rivermax) library.
* NVIDIA DOCA RMAX library. See section "Installing Rivermax Libraries from DOCA" in DOCA Installation guide.
* [CMake](https://cmake.org) 3.16 or later.

## Command-line parameters

```
Usage: doca_rmax_rx_perf [DOCA Flags] [Program Flags]

DOCA Flags:
  -h, --help                        Print a help synopsis
  -v, --version                     Print program version information
  -l, --log-level                   Set the (numeric) log level for the program <10=DISABLE, 20=CRITICAL, 30=ERROR, 40=WARNING, 50=INFO, 60=DEBUG, 70=TRACE>
  --sdk-log-level                   Set the SDK (numeric) log level for the program <10=DISABLE, 20=CRITICAL, 30=ERROR, 40=WARNING, 50=INFO, 60=DEBUG, 70=TRACE>
  -j, --json <path>                 Parse all command flags from an input json file

Program Flags:
  --list                            List available devices
  --scatter-type                    Scattering type: RAW (default), ULP or payload
  --tstamp-format                   Timestamp format: raw (default), free-running or synced
  -i, --interface-ip                IP of the local interface to receive data
  -m, --multicast-dst               Multicast address to bind to
  -s, --multicast-src               Source address to read from
  -p, --port                        Destination port to read from
  -r, --header-size                 Packet's application header size (default 0)
  -d, --data-size                   Packet's data size (default 1500)
  -k, --packets                     Number of packets to allocate memory for (default 1024)
  -a, --cpu-affinity                Comma separated list of CPU affinity cores for the application main thread
  --sleep                           Amount of microseconds to sleep between requests (default 0)
  --min                             Block until at least this number of packets are received (default 0)
  --max                             Maximum number of packets to return in one completion
  --dump                            Dump packet content
```

Examples:
* List available devices: `doca_rmax_rx_perf --list`
* Receive a stream: `doca_rmax_rx_perf --interface-ip 1.1.64.67 --multicast-dst 1.1.64.67 --multicast-src 1.1.63.5 --port 7000`
* Receive a stream (header-data split mode): `doca_rmax_rx_perf --interface-ip 1.1.64.67 --multicast-dst 1.1.64.67 --multicast-src 1.1.63.5 --port 7000 --header-size 20 --data-size 1200`

## How to build

From the `doca_rmax_rx_perf` directory run the following commands:

```shell
$ cmake -B ./build
$ cmake --build ./build
```

Enjoy the `doca_rmax_rx_perf` binary in directory `build`.

## Testing

Was tested in NVIDIA lab:
* Card: (MT41692) Nvidia BlueField-3 BF3210 P-Series DPU 100GbE/EDR VPI dual-port QSFP112; PCIe Gen5.0 x16 FHHL with x16 PCIe extension option; Crypto Enabled; 32GB on-board DDR; integrated BMC; DK
* FW: 32.99.8163
* DOCA 2.5.0:
    * Rivermax: 1.40.1
    * OFED (on BF): MLNX_OFED_LINUX-23.07-0.1.3.0
    * DOCA: 2.5.0050-1
    * BFB: DOCA_2.2.0_BSP_4.2.0_Ubuntu_22.04-1.20230622.dev.bfb

## Limitations

* Supported only on aarch64 platform and Ubuntu 20.04.
* `--scatter-type payload` is not yet supported by Rivermax for Generic and ST 2110 streams.
