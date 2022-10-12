# dperf

dperf: A DPDK-based network performance benchmark tool for bandwidth and latency measurement.

---

## Dependencies:
* [DPDK stable 20.11.1](https://fast.dpdk.org/rel/dpdk-20.11.3.tar.xz)
* ConnectX-5 NIC
  - [MLNX_OFED_LINUX-4.7-1.0.0.1-ubuntu18.04-x86_64.iso](https://www.mellanox.com/products/infiniband-drivers/linux/mlnx_ofed)
* OS
  - Ubuntu 18.04 LTS
  - 4.15.0-20-generic

## Description
dperf supports three modes: TCP, UDP and RTT. ...

## Installation
* Run the following commands to install dperf
  ```
    cd dperf
    make
    sudo make install
  ```

## Getting started
We use two end-hosts for experiments, one serves as the client (`192.168.0.1`), the other serves as the server (`192.168.0.3`). Here are some examples:
* TCP Bandwidth test
  * at the server-side: `sudo ./build/dperf -B 192.168.1.7 -P 4 -s`
  * at the client-side: `sudo ./build/dperf -B 192.168.1.1 -c 192.168.1.7 -P 4`

* UDP Bandwidth test
  * TEST 1: Generate UDP traffic using 4 threads from 192.168.1.1 to 192.168.1.7 for 15s
    * `sudo ./build/dperf -B 192.168.1.1 -c 192.168.1.7 -P 4 -t 15 -u`
  * TEST 2: Generate 10GBytes(per thread) UDP traffic using 4 threads from 192.168.1.1 to 192.168.1.7
    * `sudo ./build/dperf -B 192.168.1.1 -c 192.168.1.7 -P 4 -n 10G -u`

* Latency test
  * at the server-side: `sudo ./build/dperf -B 192.168.1.7 -P 2 -s --rtt`
  * at the client-side: `sudo ./build/dperf -B 192.168.1.1 -c 192.168.1.7 -P 2 --rtt`

* For more options
  ```
  [INFO] Usage: ./build/dperf [-B host|-N nic] [-s|-c host] [options]
  [INFO]        ./build/dperf [-h|--help]
  [INFO]
  [INFO] Client/Server:
  [INFO]     -i, --interval  #[s|ms]        seconds between periodic bandwidth reports
  [INFO]     -p, --port      #              server port to listen on/connect to
  [INFO]     -B, --bind      <host>         bind to <host>, an interface or multicast address
  [INFO]     -N, --nic       <nic>          bind to <nic>, a network interface
  [INFO]     -P, --parallel  #              number of threads to run
  [INFO]         --rtt       #              run ./build/dperf client for latency test in ping pong mode
  [INFO]
  [INFO] Server specific:
  [INFO]     -s, --server                   run in server mode
  [INFO]
  [INFO] Client specific:
  [INFO]     -c, --client    <host>         run in client mode, connecting to <host>
  [INFO]     -w, --window    #              maximum TCP sliding window size (<= 512)
  [INFO]         --bufsize   #[KMG]         lengof of buffer size to read (default=1G)
  [INFO]     -l, --len       #              the size of packet to be sent (Defaults: 1500 Bytes)
  [INFO]     -t, --time      #              time in seconds to transmit for (default 10 secs)
  [INFO]     -n, --num       #[KMG]         number of bytes to transmit (instead of -t)
  [INFO]         --rttnum                   number of packets to transmit in rtt test (Defaults: 10000)
  [INFO]     -u, --udp                      use UDP rather than TCP
  ```


