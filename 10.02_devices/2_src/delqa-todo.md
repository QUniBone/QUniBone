# DELQA emulation — open items

Test machine: PDP-11/73 (KDJ11-B).

## MOP network boot (BOOT XH0)

State: the boot ROM load and citizenship test pass, the DELQA firmware
self-test suite completes (intermittently, see below), and the full MOP
load dialogue has been exercised against a `mopd` running on the
BeagleBone itself: RPR → ASV → unicast RPR → MLD block 0 → RML →
MLD block 1 → MLT. The remaining work:

- [x] **Self-test intermittency** — fixed. The BeagleBone is
      single-core, so every SCHED_RR device worker round-robins on one
      CPU and the firmware's ~33ms reflection window was missed about
      half the time. Two changes settled it: pin the cpufreq governor to
      performance (1GHz, was ondemand at 720MHz) at startup, and raise
      device workers to SCHED_RR priority 60 (above the web server's
      civetweb threads at 50, which were sharing the RT band). Six
      consecutive BOOT XH0 runs then passed the self-test. (Earlier
      latency work also contributed: worker wakeup on enqueue,
      asynchronous logger, RT throttling disabled, pre-mutex unicast
      filtering.)
- [x] **Setup filter handling of the firmware's final 220-byte setup**
      — fixed. The parse was correct: the firmware really programs
      `7f:00:2b:24:8d:47` in all 14 slots, deliberately setting the
      multicast bit in every entry so the table names no physical
      address. Per EK-DELQA-UG-002 section 3.6.2.4, normal mode keeps
      one physical address active for datagram reception — the first
      table entry with a clear multicast bit, or the module's PROM
      station address when there is none. `bridge_accept()` now models
      this, so station-addressed MOP replies pass on the first
      RPR→ASV round (previously each exchange lost ~104s waiting for
      the retry after a software reset restored the fallback filter).
- [ ] **mopd state machine fragility** — `mopNextLoad` answers a
      repeated RML for the current block by reading at the advanced file
      position, which yields 0 bytes and escalates to MLT/slot-free
      instead of retransmitting the block. Harmless when no responses
      are lost; fix in netbsd-mopd for robustness.

## Host Ethernet access (BeagleBone)

- [x] **AF_PACKET transmit reaches the wire** — verified after a clean
      power cycle: `diagnostics/rawtxtest` frames (60 and 64 bytes,
      unicast/broadcast, native or spoofed source MAC, plain or
      DELQA-style bound+promisc socket, also under SCHED_RR 60) all
      arrive at a peer capture, and the CPSW wire-port hardware counter
      (`TxMcast` at 0x4a10093c with `STAT_PORT_EN`=P1) confirms egress
      for the DELQA's own MOP frames. The 2026-07-17 total egress loss
      was a degraded CPSW runtime state cleared by the power cycle;
      promisc cycling, macvtap add/del, demo+DELQA operation, and eth0
      down/up do not re-trigger it. Working-state register baseline:
      CPSW_SS_CTRL(0x4a100004)=0x2, ALE_CONTROL(0x4a100d08)=0x80000004,
      ALE_PORTCTL0/1=0x3. If egress loss recurs, diff these registers
      before rebooting.
- [ ] **DEC multicast is dropped by the house LAN switch** — the DELQA's
      MOP frames to `ab:00:00:01:00:00`/`ab:00:00:02:00:00` egress the
      BeagleBone (hardware counters increment, `STAT_PORT_EN`=P1) but
      never arrive at other hosts, while broadcast, unicast, and
      `33:33:*` IPv6 multicast pass. The switch forwards snooped IP
      multicast and drops unknown non-IP multicast; neither
      `allmulticast on` nor `ip maddr add` on the BeagleBone changes
      that (the drop is downstream). A LAN `mopd` cannot hear MOP
      REQ_PROGRAM until the switch forwards DEC multicast (static
      multicast entry or disabled multicast filtering). Until then
      `mopd` must run on the BeagleBone.
- [ ] `mopd` runs on the BeagleBone with local patches in the
      `netbsd-mopd` checkout: transport sockets listen with `ETH_P_ALL`
      (locally transmitted frames are only looped to `ETH_P_ALL`
      listeners) with per-socket ethertype filtering, the event loop
      drops duplicate loop copies, and the artificial `usleep` response
      pacing is removed (the KDJ11-B firmware expects responses within
      its ~110ms poll window). Review, de-instrument (timestamp stamps
      in `process.c`/`loop-linux2.c`), and commit that repo.

## Functionality

- [x] **Boot/diagnostic ROM load (`CSR<BD>`)** — implemented with the
      SIMH DELQA boot ROM image; delivery sets bits 15/14 in both
      receive status words per the KDJ11-B citizenship test.
- [ ] **MOP System ID** — periodic (~4 min) multicast to
      `AB-00-00-02-00-00` and solicited reply to Request ID. Makes the
      node visible to `NCP SHOW KNOWN CIRCUITS` / `mopprobe`. Not needed
      for client-initiated MOP boot (the loaded firmware answers SID
      requests itself while waiting).
- [ ] **MOP remote console BOOT message** — lets a load host force a
      reboot (`NCP TRIGGER`). Needs a way to yank the bus (power-cycle
      via BDCOK, like the sanity timer).
- [ ] **Ethernet loopback protocol (90-00) forward responder** — answers
      `NCP LOOP CIRCUIT` tests directed at the node while the OS is not
      listening itself. (The loaded MOP firmware implements this on its
      own while waiting for a load.)
- [ ] **Sanity timer expiry action** — period select and enable are
      implemented, expiry only logs. Real hardware asserts BDCOK to force
      a reboot.

## Hardware test (11/73)

- [ ] 2.11BSD: `qe0` at 174440, vector 120 — `ifconfig`, ping the
      BeagleBone over the shared interface, ping an external host
      (external hosts need the AF_PACKET transmit fix above)
- [ ] DECnet on RT-11
- [ ] RSX
- [ ] RSTS/E with `deqna_lock=1`
- [ ] MOP boot from `mopd` end to end (XH0 → running RAM test image)
