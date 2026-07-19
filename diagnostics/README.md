# diagnostics

Standalone diagnostic programs, independent of the QUniBone build.

## rawtxtest.c — AF_PACKET raw-transmit reproducer

Sends one Ethernet frame out an interface through a raw packet socket and
reports the kernel's view of the send (the `sendto()` return value and the
interface `tx_packets` counter before/after). A capture on the peer shows
whether the frame actually reached the wire.

On the BeagleBone Black used for QUniBone (kernel 4.9.100-bone-rt, TI CPSW
driver) the `send()` succeeds and `tx_packets` increments, but the frame
never arrives at the peer. This isolates that defect from the DELQA
emulation: raw-socket transmit does not egress the physical port, so the
emulated Ethernet controller can only reach services on the BeagleBone
itself (e.g. a local `mopd`).

Build and run on the BeagleBone:

    gcc -std=gnu99 -O2 -Wall -o rawtxtest rawtxtest.c
    sudo ./rawtxtest eth0 <peer-mac>          # e.g. 2a:07:e7:4f:50:d9

Watch on the peer for the test ethertype (0x88b5):

    tcpdump -i <iface> -e -n 'ether proto 0x88b5'

Positive control — a kernel-stack ping from the same interface with the
same source MAC IS captured by the peer, proving the link and the capture
work; only the raw-socket path fails.

Use this to check whether a fix works: run it against a USB-Ethernet
adapter's interface or on an upgraded kernel and see whether the frame
appears on the peer.
