/* pru_backend.cpp: choosing how to reach the PRUs

 Copyright (c) 2026, Hans Huebner
 MIT license, see delqa.hpp for the full text.

 Which interface exists depends on the kernel, so the choice is made by
 asking rather than by configuring: each backend's open() reports whether it
 can run here, and the first that can is kept.

 remoteproc is tried first. A kernel new enough to offer it is one where
 uio_pruss has been removed, and a kernel old enough for uio_pruss has no PRU
 remoteproc devices, so in practice only one ever answers.
*/

#include "logsource.hpp"
#include "logger.hpp"
#include "pru_backend.hpp"

pru_backend_c *pru_backend = NULL;

bool pru_backend_select(void) {
	pru_backend_c *candidates[] = { pru_backend_remoteproc(), pru_backend_prussdrv() };

	for (unsigned i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
		if (candidates[i]->open()) {
			pru_backend = candidates[i];
			return true;
		}
	return false;
}
