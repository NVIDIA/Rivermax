# generic_receiver

## Description

`generic_receiver` is an RX demo application that demonstrates receiving _generic_ data using Rivermax API of the [Rivermax SDK](https://developer.nvidia.com/networking/rivermax).

>The code is provided "As Is" and any express or implied warranties, including,
but not limited to, the implied warranties of merchantability and fitness for a particular
purpose are disclaimed.

### Details

* Release date: 31-10-2022
* Update date: 31-10-2022
* Version: 1.20.10

### Tested on

* Rivermax: 1.20.10
* OFED: 5.8-1.0.1.1
* WinOF2: 3.10.50000
* CUDA: 11.8.0
* OS: 
  * Ubuntu 20.04
  * Windows 10
* Hardware: ConnectX DX 6

## How to Build

From the `generic_receiver` directory run the following commands:

```shell
$ cmake -B ./build
$ cmake --build ./build
```

The resulting binary can be found in directory `build`.

The application can be used to demonstrate GPUDirect. Use the following command line to build with [CUDA-Toolkit](https://docs.nvidia.com/cuda/) support:

```shell
$ cmake -DRIVERMAX_ENABLE_CUDA=ON -B ./build
$ cmake --build ./build
```

## How to Run / Examples

### Synopsis

```shell
generic_receiver --interface-ip <local IP> --multicast-dst <multicast group IP>  --port <multicast group UDP port#> --multicast-src <sender IP> [optional parameters]
```

To see all command-line options please invoke `generic_receiver --help`.

> `--checksum-header` command-line flag can be used to demonstrate simple GPU utilization. It can be utilized when incoming packets have a header containing a sequence number and a checksum of the data.

### Example #1: _Receiving a simple stream using a specific core affinity_

This example demonstrates receiving a simple stream sent from a sender with source port 192.168.1.3.
The stream is received via the NIC which has local IP 192.168.1.2. The multicast address and UDP ports
on which the stream is being received are 239.5.5.5:56789.
The application is set to run on cores #2, #3 and #4.

```shell
$ sudo ./generic_receiver --interface-ip 192.168.1.2 --multicast-dst 239.5.5.5 --multicast-src 192.168.1.3 --port 56789 --cpu-affinity 2,3,4
```

### Example #2: _Receiving a simple stream using defined data sizes_

This example demonstrates receiving a simple stream with the same flow parameters as in the previous example, and the incoming packets are of size 1460 bytes. The initial 40 byte are stripped from the payload as application header and placed in buffers allocated on the CPU. The remaining 1420 bytes are placed in dedicated payload buffers. In this case, the payload buffers are also allocated on the CPU. 

```shell
$ sudo ./generic_receiver --interface-ip 192.168.1.2 --multicast-dst 239.5.5.5 --multicast-src 192.168.1.3 --port 56789 --header-size 40 --data-size 1460
```

### Example #3: _Receiving generic data with GPUDirect on Windows_
This example demonstrates receiving a stream in buffers allocated on GPU memory using GPUDirect and CUDA. When using CUDA, the following environment variable must be set. The GPU selected to be used for this stream is GPU #0. 

```shell
$ set RIVERMAX_ENABLE_CUDA=1 
$ generic_receiver.exe --interface-ip 192.168.1.2 --multicast-dst 224.2.3.44 --multicast-src 192.168.1.3 --port 39608 --gpu 0
```

## Known Issues / Limitations

None identified so far 
