/* pru_backend.hpp: how the ARM reaches the PRUs

 Copyright (c) 2026, Hans Huebner
 MIT license, see delqa.hpp for the full text.

 The kernel offers two ways to drive the PRUs, and which one is available
 depends on the kernel: uio_pruss, reached through libprussdrv, was removed
 from mainline in 6.10 in favour of remoteproc. Everything version specific
 lives behind this interface so the rest of the emulator does not know which
 is in use.

 The jobs are the ones libprussdrv performs: load and start firmware, map
 the PRU data memories that carry the mailbox and the device registers, map
 the host memory the PRUs use as the emulated machine's, and wait for the
 event a PRU raises when it wants attention.

 The mailbox itself is not part of this. ARM and PRU hand work to each other
 through a word in shared memory, so once the memory is mapped that protocol
 is the same either way.
*/
#ifndef _PRU_BACKEND_HPP_
#define _PRU_BACKEND_HPP_

#include <stdint.h>
#include <stddef.h>

// The PRU data memories, named without reference to either driver's
// constants.
enum pru_ram_e {
	PRU_RAM_PRU0_DATA = 0, // PRU0's own 8K, holding the device registers
	PRU_RAM_PRU1_DATA = 1, // PRU1's own 8K
	PRU_RAM_SHARED = 2     // the 12K both PRUs see, holding the mailbox
};

class pru_backend_c {
public:
	virtual ~pru_backend_c() {}

	// A name for logs, "prussdrv" or "remoteproc".
	virtual const char *name(void) = 0;

	// Claim the PRU subsystem. False if this backend cannot run here,
	// which is how the caller chooses between them.
	virtual bool open(void) = 0;
	virtual void close(void) = 0;

	// Map one of the PRU data memories into this process. Null on failure.
	virtual void *map_ram(enum pru_ram_e ram) = 0;

	// Map the host memory the PRUs use as the emulated machine's memory,
	// and report where it lies for the PRUs, which address it physically.
	virtual bool map_host_memory(void **virt, size_t *len, uint32_t *phys) = 0;

	// Load firmware into one PRU and start it. The image is whatever this
	// backend loads: instruction words for one, an ELF file for the other,
	// which is why the caller hands over both and lets the backend choose.
	virtual bool load_and_start(unsigned pru_num, const uint32_t *code,
			size_t code_bytes, const uint8_t *elf, size_t elf_bytes,
			uint32_t entry) = 0;

	// Halt one PRU. Harmless if it is not running.
	virtual void halt(unsigned pru_num) = 0;

	// Wait for a PRU to raise its event. Returns the number of events, 0 on
	// timeout, negative on error.
	virtual int wait_event(unsigned timeout_us) = 0;
	virtual void clear_event(void) = 0;
};

// The backends. Each returns a singleton; open() decides whether it can run
// on this kernel.
pru_backend_c *pru_backend_prussdrv(void);
pru_backend_c *pru_backend_remoteproc(void);

// The backend in use, chosen once at startup by pru_backend_select().
extern pru_backend_c *pru_backend;

// Try each backend in turn and keep the first that opens. False if none
// does, which means the kernel offers neither interface.
bool pru_backend_select(void);

#endif
