# rmax_ipo_receiver

## Description

`rmax_ipo_receiver` is an RX demo application that demonstrates receiving data
using [Rivermax](https://developer.nvidia.com/networking/rivermax) **Inline
Packet Ordering** (IPO) feature.

>The code is provided "As Is" and any express or implied warranties, including,
but not limited to, the implied warranties of merchantability and fitness for a particular
purpose are disclaimed.

### Details

* Release date: 31-01-2023
* Update date: 16-05-2023
* Version: 1.30.16

### Tested on

* Rivermax: 1.30.16
* OFED: 23.04-0.5.3.3
* WinOF2: 23.4.26054
* OS:
  * Ubuntu 20.04
  * Ubuntu 22.04
  * Windows 10
* Hardware:
  * ConnectX-6 DX
  * ConnectX-6 LX
  * ConnectX-7
  * BlueField-2

## How to Build

At first you need to download the latest Rivermax package from
[Rivermax Downloads](https://developer.nvidia.com/networking/rivermax-getting-started)
page. Extract directory `rmax_apps_lib` from this package somewhere (in our
example the archive is extracted to user home directory).

From the `rmax_ipo_receiver` directory run the following commands:

```shell
$ cmake -DRMAX_APPS_LIB=/home/user/rmax_apps_lib -B ./build
$ cmake --build ./build
```

The resulting binary can be found in directory `build`.

## How to Run / Examples

### Synopsis

```shell
sudo ./rmax_ipo_receiver --local-ips 1.2.3.4 --src-ips 6.7.8.9 --dst-ips 1.2.3.4 -p 50020 [optional parameters]
```

To see all command-line options please invoke `rmax_ipo_receiver --help`.

The application supports receiving the same stream from multiple redundant
paths. Each path is a combination of a source IP address, a destination IP
address, a destination port, and a local IP address of the receiver device.
Single path receive demonstrates packet reordering within the NIC, multi-path
receive also adds recovery of missing packets from other streams. The `--max-pd`
command-line option sets the maximum number of microseconds that receiver waits
for the same packet to arrive from a different stream.

When the application is configured to receive multiple streams, destination
addresses' last octet will be incremented by the number of redundant paths for
every stream. This algorithm for address allocation is designed to produce the
increasing sequence of addresses starting with an initial list of
consecutive destination addresses (e.g. 239.4.4.4, 239.4.4.5).

The RTP sequence number is used by the hardware to determine the location of
arriving packets in the receive buffer. The application supports two sequence
number parsing modes: 16-bit RTP sequence number (default) and 32-bit extended
sequence number, consisting of 16 low order RTP sequence number bits and 16
high order bits from the start of RTP payload (enabled by `--ext-seq-num`
flag).

The `--register-memory` command-line flag reduces the number of memory keys in
use by registering all the memory in a single pass on application side. Can be
used only together with non-zero `-e` or `--app-hdr-size` parameter.

### Memory allocation

The application supports memory allocator selection via command-line key `-A`
or `--allocator-type`. The following types are available:
* auto - Automatic selection (default).
* gpu - Allocate GPU memory. Currently not supported by this application.
* malloc - malloc memory allocation using system-default page size.
* hugepage - Huge Page allocation using system-default huge page size.
* hugepage-2m - Allocate 2 MB huge pages.
* hugepage-512m - Allocate 512 MB huge pages.
* hugepage-1g - Allocate 1024 MB huge pages.

### Example #1: _Receiving a single stream_

This example demonstrates receiving a single stream sent from a sender with
source address 192.168.1.3. The stream is received via the NIC which has local
IP 192.168.1.2. The multicast address and UDP ports on which the stream is
being received are 239.4.4.5:56789. The application is set to run on cores #2
and #3.

```shell
sudo ./rmax_ipo_receiver --local-ips 192.168.1.2 --src-ips 192.168.1.3 --dst-ips 239.4.4.5 -p 56789 -i 2 -a 3
```

### Example #2: _Receiving a redundant stream from two different paths_

This example demonstrates receiving a redundant stream sent from a sender with
source addresses 192.168.1.3 and 192.168.2.4. The stream is received via NICs
which have local IPs 192.168.1.2 and 192.168.2.3. The multicast addresses and
UDP ports on which the stream is being received are 239.4.4.4:45678 and
239.4.4.5:56789. The application is set to run on cores #2 and #3.

```shell
sudo ./rmax_ipo_receiver --local-ips 192.168.1.2,192.168.2.3 --src-ips 192.168.1.3,192.168.2.4 --dst-ips 239.4.4.4,239.4.4.5 -p 45678,56789 -i 2 -a 3
```

### Example #3: _Receiving a redundant stream using header/data split feature_

This example demonstrates receiving a redundant stream with the same flow
parameters as in the previous example, and the incoming packets are of size
1460 bytes. The initial 40 bytes are stripped from the payload as an
application header and placed in buffers allocated in RAM. The remaining 1420
bytes are placed in dedicated payload buffers. In this case, the payload
buffers are also allocated in RAM.

```shell
sudo ./rmax_ipo_receiver --local-ips 192.168.1.2,192.168.2.3 --src-ips 192.168.1.3,192.168.2.4 --dst-ips 239.4.4.4,239.4.4.5 -p 45678,56789 --app-hdr-size 40 --payload-size 1420
```

### Example #4: _Receiving multiple streams_

This example demonstrates how to receive multiple redundant streams using the
same application process. Streams:
* Stream 0:
    * source 192.168.1.3, destination 239.4.4.4:45678, device 192.168.1.2
    * source 192.168.2.4, destination 239.4.4.5:56789, device 192.168.2.3
* Stream 1:
    * source 192.168.1.3, destination 239.4.4.6:45678, device 192.168.1.2
    * source 192.168.2.4, destination 239.4.4.7:56789, device 192.168.2.3
* Stream 2:
    * source 192.168.1.3, destination 239.4.4.8:45678, device 192.168.1.2
    * source 192.168.2.4, destination 239.4.4.9:56789, device 192.168.2.3

```shell
sudo ./rmax_ipo_receiver --local-ips 192.168.1.2,192.168.2.3 --src-ips 192.168.1.3,192.168.2.4 --dst-ips 239.4.4.4,239.4.4.5 -p 45678,56789 --streams 3
```

## Known Issues / Limitations

None identified so far.
