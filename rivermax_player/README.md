# rivermax_player

## Description

`rivermax_player` is a TX application that demonstrates sending video, audio and ancillary streams using [Rivermax](https://developer.nvidia.com/networking/rivermax) API. These streams can be sent separately or in parallel (NOTE: sending an ancillary stream requires that a video stream be sent at the same time).

>The code is provided "As Is" and any express or implied warranties, including,
but not limited to, the implied warranties of merchantability and fitness for a particular
purpose are disclaimed.

### Details

* Release date: 31-Oct-2022
* Update date: 31-Oct-2022
* Version: 1.20.10

### Tested on

* Rivermax: 1.20.10
* OFED: 5.8-0.2.3.0
* CUDA: 11.8.0
* WinOF2: 3.10.50000
* FFmpeg: 5.1.2
* OS: 
  * Ubuntu 20.04
  * Windows 10
* Hardware: ConnectX DX 6

## How to Build

From the `rivermax_player` directory run the following commands:

```shell
$ cmake -B ./build
$ cmake --build ./build
```

> If no FFmpeg is found on the system, it will be downloaded automatically into the `build` directory, the temporary directory with the build results

The resulting binary can be found in directory `build`.

## How to Run / Examples

### Synopsis

```shell
rivermax_player --sdp-files <SDP files> --media-files <media files> [optional parameters]
```

For the full set of the command-line parameters one shall invoke `rivermax_player --help`.

> Option `-v 4` is only supported in the following cases: when using BlueField-2 with time and 
> scheduling services and on Linux bare metal with ConnectX-6 Dx (and higher) 
> when configured to use PTP Hardware Clock.

> The Rivermax `media_receiver` demo application (provided with the SDK bundle)
> can be used to view the video stream sent by this application. For this, the
> `media_receiver` must be built with the `--enable-viewer` build option.

### SDP Example

The `rivermax_player` application requires an SDP file to configure the streams it must send.
For example, the following SDP can be used to configure the `rimvermax_player` to send one video stream and one
audio stream:

```config
v=0
s=SMPTE stream example
i=Includes a video stream and an audio stream
t=0 0
m=video 7000 RTP/AVP 96
c=IN IP4 230.156.10.25/64
a=source-filter:incl IN IP4 230.156.10.25 1.1.63.9
a=rtpmap:96 raw/90000
a=fmtp:96 sampling=YCbCr-4:2:2; width=1920; height=1080; exactframerate=25; depth=10; TCS=SDR; colorimetry=BT709; PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN;
a=mediaclk:direct=0
a=ts-refclk:localmac=40-a3-6b-a0-2b-d2
m=audio 7010 RTP/AVP 97
c=IN IP4 230.156.10.26/64
a=source-filter:incl IN IP4 230.156.10.26 1.1.63.9
a=rtpmap:97 L24/48000/6
a=mediaclk:direct=0 rate=48000
a=ptime:1
a=ts-refclk:localmac=40-a3-6b-a0-2b-d2
```

The stream is transmitted from the NIC with the local IPv4 11.1.63.17. The destination multicast address and UDP ports
being used are 230.156.10.25:7000.

### Example #1: _Sending a simple stream using a specific core affinity_

This example demonstrates transmitting all streams encoded in a media file (video, audio and ancillary). 
The command-line configures the application to transmit the streams according to the specified SDP file. 
It also allocates four CPU cores: one per each stream (cores #1, #2 and #3), and one core for the internal 
Rivermax thread (core #4).

```shell
$ sudo ./rivermax_player --media-files ~/videos/video_1080p_25fps.mp4 -r 4 -t 1,2,3 -s ~/sdps/sdp_1080p_25fps.txt
```

### Example #2: _Sending only video and audio streams in a loop_

This example demonstrates transmitting of video and audio streams only.
The command-line configures the application to transmit the streams according to the specified SDP file. 

```shell
$ sudo ./rivermax_player --media-files ~/videos/video_1080p_25fps.mp4 -s ~/sdps/sdp_1080p_25fps.txt -p av --loop
```

## Known Issues / Limitations

