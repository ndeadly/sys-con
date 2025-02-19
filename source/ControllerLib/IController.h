#pragma once
#include "IUSBDevice.h"
#include "ControllerTypes.h"
#include "ControllerConfig.h"

struct NormalizedButtonData
{
    bool buttons[MAX_CONTROLLER_BUTTONS];
    float triggers[2];
    NormalizedStick sticks[2];
};

class IController
{
protected:
    std::unique_ptr<IUSBDevice> m_device;

public:
    IController(std::unique_ptr<IUSBDevice> &&interface) : m_device(std::move(interface))
    {
    }
    virtual ~IController() = default;

    virtual ams::Result Initialize() = 0;

    // Since Exit is used to clean up resources, no Result report should be needed
    virtual void Exit() = 0;

    virtual ams::Result GetInput() { R_RETURN(1); }

    virtual NormalizedButtonData GetNormalizedButtonData() { return NormalizedButtonData(); }

    inline IUSBDevice *GetDevice() { return m_device.get(); }
    virtual ControllerType GetType() = 0;
    // virtual ams::Result SetRumble(uint8_t strong_magnitude, uint8_t weak_magnitude) { R_RETURN(1); }
    virtual bool IsControllerActive() { return true; }

    virtual ams::Result OutputBuffer() { R_RETURN(1); };

    virtual ControllerConfig *GetConfig() { return nullptr; }
};