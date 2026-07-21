/* qbone-leds.c - QBone status indicator on three BeagleBone user LEDs.
 *
 * Shows how far the board is through bring-up on three of the four green user
 * LEDs - usr0, usr2, usr3 - and leaves usr1 to the kernel's mmc0 trigger so
 * SD-card activity is still visible. A growing blink marks progress; a
 * bouncing light means the emulator is up:
 *
 *     X00   booting        blink, 0.5 s
 *     XX0   configuring    blink, 0.5 s
 *     XXX   starting       blink, 0.5 s
 *     bounce (X00 0X0 00X 0X0 ...)   ready, ~150 ms sweep, forever
 *
 * The phase comes from systemd and the qbone-setup marker, polled once a
 * second. Nothing in the emulator is involved: the bounce simply means the
 * qbone service is active.
 *
 * The three LEDs are found under /sys/class/leds by the usrN suffix of their
 * name, so the label prefix does not matter. Each is taken over once with
 * trigger=none; its brightness file is then held open and written on change.
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NLED 3
static const char *want[NLED] = { "usr0", "usr2", "usr3" };
static int bri_fd[NLED] = { -1, -1, -1 };
static char on_val[16] = "1";

static int ends_with(const char *s, const char *suf)
{
	size_t ls = strlen(s), lf = strlen(suf);
	return ls >= lf && strcmp(s + ls - lf, suf) == 0;
}

static void write_path(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY);
	if (fd < 0)
		return;
	if (write(fd, val, strlen(val)) < 0) {
		/* nothing useful to do this early in boot */
	}
	close(fd);
}

/* find the three LEDs, take them over, keep their brightness files open */
static void setup_leds(void)
{
	const char *base = "/sys/class/leds";
	DIR *d = opendir(base);
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir(d))) {
		for (int i = 0; i < NLED; i++) {
			if (bri_fd[i] >= 0 || !ends_with(e->d_name, want[i]))
				continue;
			char p[512];
			snprintf(p, sizeof p, "%s/%s/trigger", base, e->d_name);
			write_path(p, "none");
			snprintf(p, sizeof p, "%s/%s/max_brightness", base, e->d_name);
			int fd = open(p, O_RDONLY);
			if (fd >= 0) {
				char b[16] = { 0 };
				if (read(fd, b, sizeof b - 1) > 0) {
					int v = atoi(b);
					if (v > 0)
						snprintf(on_val, sizeof on_val, "%d", v);
				}
				close(fd);
			}
			snprintf(p, sizeof p, "%s/%s/brightness", base, e->d_name);
			bri_fd[i] = open(p, O_WRONLY);
		}
	}
	closedir(d);
}

static void set_led(int i, int on)
{
	if (bri_fd[i] < 0)
		return;
	const char *v = on ? on_val : "0";
	lseek(bri_fd[i], 0, SEEK_SET);
	if (write(bri_fd[i], v, strlen(v)) < 0) {
		/* ignore: the next tick tries again */
	}
}

static void show(int a, int b, int c)
{
	set_led(0, a);
	set_led(1, b);
	set_led(2, c);
}

static int svc_active(const char *unit)
{
	char cmd[128];
	snprintf(cmd, sizeof cmd, "systemctl is-active --quiet %s", unit);
	return system(cmd) == 0;
}

/* 0 booting, 1 configuring, 2 starting, 3 ready */
static int detect_phase(void)
{
	if (svc_active("qbone.service"))
		return 3;
	if (access("/var/lib/qbone/.setup-done", F_OK) == 0)
		return 2;
	if (svc_active("qbone-setup.service"))
		return 1;
	return 0;
}

int main(void)
{
	setup_leds();

	/* 50 ms tick: blink toggles every 10 ticks (0.5 s), the ready sweep
	 * steps every 3 ticks (~150 ms), and the phase is re-read every 20
	 * ticks (~1 s). */
	const struct timespec tick = { 0, 50 * 1000 * 1000L };
	static const int sweep[4] = { 0, 1, 2, 1 };
	int phase = -1;
	for (long t = 0;; t++) {
		if (t % 20 == 0)
			phase = detect_phase();
		int on = (t / 10) & 1;
		switch (phase) {
		case 0:
			show(on, 0, 0);
			break;
		case 1:
			show(on, on, 0);
			break;
		case 2:
			show(on, on, on);
			break;
		default: {
			int pos = sweep[(t / 3) % 4];
			show(pos == 0, pos == 1, pos == 2);
			break;
		}
		}
		nanosleep(&tick, NULL);
	}
	return 0;
}
