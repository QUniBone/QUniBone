# diagnostics

Standalone diagnostic programs, independent of the QUniBone build.

## rawtxtest.c — AF_PACKET raw-transmit reproducer

Sends one Ethernet frame out an interface through a raw packet socket and
reports the kernel's view of the send (the `sendto()` return value and the
interface `tx_packets` counter before/after). A capture on the peer shows
whether the frame actually reached the wire.

QBone's Ethernet path now works fully, including reaching external hosts on
the LAN. This tool remains a quick isolation check for the raw-socket TX
path if a networking problem is ever suspected.

Historical note: on the BeagleBone Black (kernel 4.9.100-bone-rt, TI CPSW
driver) a raw-TX loss was seen on 2026-07-17 where `send()` succeeded and
`tx_packets` incremented but the frame never reached the peer; it turned out
to be a degraded runtime state that cleared on a clean boot, not a static
defect.

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
