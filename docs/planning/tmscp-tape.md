# TMSCP tape controller

**Status:** Gathering

A TMSCP tape controller so a guest OS can mount, read, and write magnetic tape,
backed by SimH-compatible `.tap` container files on the board.

## Current state

QBone emulates the disk half of the DEC Mass Storage Control Protocol: the UDA50
port (`uda_c`, also identifying as RQDX3) drives an `mscp_server` and
`mscp_drive` units backed by SimH-compatible block-stream images through
`storageimage`. There is no tape controller. TMSCP — the tape sibling of the
same protocol, as fronted by controllers such as the TQK50 and TU81 — is not
implemented, and nothing reads or writes the SimH `.tap` tape container format
(variable-length records with 4-byte length words and tape marks), which is a
different layout from the disk block stream the disk images use.

## Requirements

### The controller

- A TMSCP tape controller emulation that a QBus guest sees as a real DEC tape
  subsystem, presenting the TMSCP command set (mount, read, write, write tape
  mark, rewind, space forward/reverse, unload, get-unit-status) over the same
  MSCP port/initialisation mechanism the disk controller already uses.
- The controller identifies as a specific DEC tape controller the target guests
  have drivers for; which one is an open question below.

### SimH-compatible tape files

- Tape media is a SimH `.tap` container file: data records framed by 4-byte
  little-endian length words (a trailing copy after the record), tape marks as a
  zero-length record, and end-of-medium handling per the SimH tape format.
- Files written by QBone are readable by SimH and other emulators using the
  format, and QBone reads `.tap` files those tools produce.
- Read, write, and write-protected mounting are all supported.

### Integration

- Tape images live alongside disk images in `/var/lib/bone/images` and are
  managed through the same image and device APIs, so a tape unit is attached,
  detached, and swapped like a disk drive.

### Implementation standard

- The controller follows the
  [device implementation standard](device-implementation-standard.md): built
  from the relevant DEC manual (kept in the repo, OCR'd if needed) and validated
  by its XXDP diagnostic.

## Open questions

### Which controller and drives

- Which TMSCP controller does QBone emulate — TQK50 (TK50 cartridge, common on
  MicroVAX/PDP-11 Q-bus), TU81/TK50 via a UDA-style port, or another — and which
  gives the widest driver coverage across the guest OSes in use (2.11BSD, RSX,
  RT-11)?
- How many tape units does the controller expose, and does the disk MSCP server
  code generalise to serve tape units or does the tape side need its own server?

### SimH format specifics

- Which SimH `.tap` conventions must be matched exactly — the error/half-gap bit
  encodings in the record length word, extended/8-byte metadata records, and how
  end-of-medium versus logical end-of-tape are reported to the guest?
- What record and tape-mark semantics do the target guests rely on (for example
  `tar`/`dump` block sizes, multi-file tapes separated by tape marks)?

### Reuse versus new code

- How much of `mscp_server` (message rings, credits, initialisation, endcodes)
  is shared with TMSCP, and what is the cleanest split between a common MSCP core
  and disk-versus-tape command handlers?
- Does `storageimage` extend to a `.tap` record stream, or does tape need a
  separate image class since it is record-structured rather than a flat block
  stream?

### Validation

- Which XXDP diagnostic exercises this controller, and does it run under QBone
  the way the disk diagnostics do?
- What is the guest-level acceptance test — writing a tape under one OS and
  reading it back, or exchanging a `.tap` file with SimH?
