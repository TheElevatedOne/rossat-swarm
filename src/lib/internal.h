// Private definitions shared by the librossat translation units.
#pragma once

#include "rossat/rossat.h"

#include <hidapi/hidapi.h>

struct rossat_device {
    hid_device *hid = nullptr;
    rossat_device_info info{};
};
