/* qunibusadapter.hpp: host-test stub

   Copyright (c) 2026, Hans Huebner
   hans@huebner.org
   MIT license, see webserver.hpp for the full text.

   webconfigs.cpp only names qunibusadapter_c in a dynamic_cast that classifies
   a device as infrastructure. The host test has no bus adapter, so a minimal
   polymorphic class is enough: the cast compiles and never matches a synthetic
   device.
*/
#ifndef _QUNIBUSADAPTER_HPP_
#define _QUNIBUSADAPTER_HPP_

class qunibusadapter_c {
public:
	virtual ~qunibusadapter_c() {}
};

#endif // _QUNIBUSADAPTER_HPP_
