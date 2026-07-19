/* rawtxtest.c - minimal AF_PACKET raw-TX reproducer.
 *
 * Sends ONE Ethernet frame out an interface via a raw packet socket and
 * reports what the kernel thinks happened (send() return value and the
 * interface tx_packets counter before/after). A capture on the peer shows
 * whether the frame actually reached the wire.
 *
 * On the BeagleBone Black (kernel 4.9.100-bone-rt, TI cpsw driver) the
 * send() succeeds and tx_packets increments, but the frame never arrives
 * at the peer - this program makes that reproducible in isolation.
 *
 *   build:  gcc -O2 -Wall -o rawtxtest rawtxtest.c
 *   run:    sudo ./rawtxtest eth0 2a:07:e7:4f:50:d9 [frame-length]
 *
 * Frame sent: dst = argv[2], src = interface MAC, ethertype 0x88b5
 *             (IEEE Std 802 local experimental), payload "RAWTXBUG" + 0xA5
 *             fill up to frame-length (default 60, range 22..1514). Filter
 *             it on the peer with:
 *             tcpdump -i en0 -e -n ether proto 0x88b5
 *
 * The frame length matters on CPSW: the switch silently drops egress
 * frames of 60..63 octets when a port VLAN forces untagging (fixed
 * upstream by commit 9421c9015047, which raised the driver minimum to 64;
 * 4.9.y never got the fix). Compare length 60 vs 64 to test for that.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#define ETHERTYPE_TEST 0x88b5

static long read_tx_packets(const char *ifname)
{
	char path[128];
	snprintf(path, sizeof(path),
		 "/sys/class/net/%s/statistics/tx_packets", ifname);
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	long v = -1;
	if (fscanf(f, "%ld", &v) != 1)
		v = -1;
	fclose(f);
	return v;
}

static int parse_mac(const char *s, unsigned char mac[6])
{
	int b[6];
	if (sscanf(s, "%x:%x:%x:%x:%x:%x",
		   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
		return -1;
	for (int i = 0; i < 6; i++)
		mac[i] = (unsigned char) b[i];
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 3 || argc > 5) {
		fprintf(stderr,
			"usage: %s <interface> <dst-mac aa:bb:cc:dd:ee:ff> "
			"[frame-length] [src-mac]\n",
			argv[0]);
		return 2;
	}
	const char *ifname = argv[1];
	unsigned char dst[6];
	if (parse_mac(argv[2], dst) < 0) {
		fprintf(stderr, "bad destination MAC: %s\n", argv[2]);
		return 2;
	}
	size_t framelen = 60;
	if (argc == 4) {
		framelen = (size_t) atol(argv[3]);
		if (framelen < 22 || framelen > 1514) {
			fprintf(stderr, "frame-length must be 22..1514\n");
			return 2;
		}
	}

	int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (fd < 0) {
		perror("socket(AF_PACKET, SOCK_RAW)");
		return 1;
	}

	/* resolve interface index and source MAC */
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
		perror("SIOCGIFINDEX");
		return 1;
	}
	int ifindex = ifr.ifr_ifindex;
	if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
		perror("SIOCGIFHWADDR");
		return 1;
	}
	unsigned char src[6];
	memcpy(src, ifr.ifr_hwaddr.sa_data, 6);
	if (argc == 5 && parse_mac(argv[4], src) < 0) {
		fprintf(stderr, "bad source MAC: %s\n", argv[4]);
		return 2;
	}

	/* build the frame */
	unsigned char frame[1514];
	memset(frame, 0, sizeof(frame));
	memcpy(frame + 0, dst, 6);
	memcpy(frame + 6, src, 6);
	frame[12] = (ETHERTYPE_TEST >> 8) & 0xff;
	frame[13] = ETHERTYPE_TEST & 0xff;
	memcpy(frame + 14, "RAWTXBUG", 8);
	memset(frame + 22, 0xa5, framelen - 22);

	printf("interface : %s (ifindex %d)\n", ifname, ifindex);
	printf("src MAC   : %02x:%02x:%02x:%02x:%02x:%02x\n",
	       src[0], src[1], src[2], src[3], src[4], src[5]);
	printf("dst MAC   : %02x:%02x:%02x:%02x:%02x:%02x\n",
	       dst[0], dst[1], dst[2], dst[3], dst[4], dst[5]);
	printf("ethertype : 0x%04x, frame length %zu\n",
	       ETHERTYPE_TEST, framelen);

	long tx_before = read_tx_packets(ifname);

	struct sockaddr_ll sll;
	memset(&sll, 0, sizeof(sll));
	sll.sll_family = AF_PACKET;
	sll.sll_ifindex = ifindex;
	sll.sll_halen = 6;
	sll.sll_protocol = htons(ETHERTYPE_TEST);
	memcpy(sll.sll_addr, dst, 6);

	ssize_t n = sendto(fd, frame, framelen, 0,
			   (struct sockaddr *) &sll, sizeof(sll));
	int send_errno = errno;

	/* the counter update is not instantaneous */
	usleep(100000);
	long tx_after = read_tx_packets(ifname);

	printf("\n");
	if (n < 0)
		printf("sendto()  : FAILED (%s)\n", strerror(send_errno));
	else
		printf("sendto()  : returned %zd (of %zu requested)\n",
		       n, framelen);
	printf("tx_packets: %ld -> %ld  (delta %ld)\n",
	       tx_before, tx_after, tx_after - tx_before);
	printf("\nThe kernel %s the frame. Check the peer capture to see\n"
	       "whether it actually reached the wire.\n",
	       (n == (ssize_t) framelen) ? "accepted" : "rejected");

	close(fd);
	return (n == (ssize_t) framelen) ? 0 : 1;
}
