#pragma once
#include <stratosphere.hpp>

namespace syscon::usb
{
    ams::Result Initialize();
    void Exit();

    ams::Result Enable();
    void Disable();

    ams::Result CreateUsbEvents();
    void DestroyUsbEvents();
} // namespace syscon::usb