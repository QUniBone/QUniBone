/* event_latency.hpp: how long the PRU waits for the ARM

 Copyright (c) 2026, Hans Huebner
 MIT license, see delqa.hpp for the full text.

 The PRU leaves RPLY asserted from raising a device register event until the
 ARM acknowledges it, so the QBUS cycle is stretched for exactly as long as
 Linux takes to schedule the bus worker. That interval is the one place where
 the emulation depends on the kernel rather than on its own code, and the
 only thing a realtime kernel would improve.

 A passing diagnostic on an idle board says nothing about it: what matters is
 the tail, which only appears under load and over hours. So the numbers kept
 here are a maximum and a histogram, not an average - an average hides
 precisely the outlier that would break a bus cycle.

 The PRU samples its own cycle counter as it signals; the ARM reads the same
 counter on waking. One clock domain, no cross-domain correction. It runs at
 200 MHz, wrapping every 21 seconds, and unsigned subtraction is right across
 a wrap.
*/
#ifndef _EVENT_LATENCY_HPP_
#define _EVENT_LATENCY_HPP_

#include <stdint.h>
#include <string.h>

// Buckets double each time: 0-1us, 1-2, 2-4, ... up to "16 seconds or more".
// Log scale because the interesting range spans microseconds to whatever a
// pathological scheduling delay turns out to be.
#define EVENT_LATENCY_BUCKETS 24

class event_latency_c {
public:
	// PRU1's cycle counter, or null when the backend cannot reach it, in
	// which case nothing is recorded and every reader sees zero samples.
	volatile uint32_t *counter;

	uint64_t count;			// samples taken
	uint64_t sum_cycles;		// for the mean, which is context for the max
	uint32_t max_cycles;
	uint64_t bucket[EVENT_LATENCY_BUCKETS];

	event_latency_c(): counter(NULL) {
		reset();
	}

	void reset(void) {
		count = 0;
		sum_cycles = 0;
		max_cycles = 0;
		memset(bucket, 0, sizeof(bucket));
	}

	// 200 MHz: 5ns a tick, so microseconds are cycles/200.
	static inline uint32_t cycles_to_us(uint32_t cycles) {
		return cycles / 200;
	}

	// Called from the bus worker on every device register event, so it stays
	// a handful of instructions: one register read and some arithmetic.
	inline void sample(uint32_t signal_cycle) {
		if (counter == NULL)
			return;
		uint32_t elapsed = *counter - signal_cycle;	// wraps correctly

		count++;
		sum_cycles += elapsed;
		if (elapsed > max_cycles)
			max_cycles = elapsed;

		// bucket by the power of two of the microsecond count
		unsigned us = elapsed / 200;
		unsigned b = 0;
		while (us > 0 && b < EVENT_LATENCY_BUCKETS - 1) {
			us >>= 1;
			b++;
		}
		bucket[b]++;
	}

	uint32_t mean_cycles(void) const {
		return count ? (uint32_t) (sum_cycles / count) : 0;
	}

	// Lower edge of a bucket in microseconds: bucket 0 is under 1us, bucket n
	// starts at 2^(n-1).
	static unsigned bucket_floor_us(unsigned b) {
		return b == 0 ? 0 : (1u << (b - 1));
	}
};

#endif
