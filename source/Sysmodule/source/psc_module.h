#pragma once
#include <switch.h>
#include <stratosphere.hpp>

namespace syscon::psc
{
    ams::Result Initialize();
    void Exit();
};