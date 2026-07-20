/* main_demo.cpp: entry point of the interactive test application

 Copyright (c) 2026, Hans Huebner
 hans@huebner.org
 MIT license.

 "demo" drives the hardware from a menu on a terminal: bus latches, master/slave
 tests, interrupts, the device exercisers and the emulated device set. It expects
 an operator on stdin. A QBone that only serves the web interface runs
 qbone-web instead, which has no menu and no terminal.
 */

#include "kbhit.h"
#include "application.hpp"

int main(int argc, char *argv[])
{
	// flush stuff on stdin. (Eclipse remote debugging)
	while (os_kbhit())
		;

	qunibone_factory();
	return app->run(argc, argv);
}
