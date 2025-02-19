#include "SwitchHDLHandler.h"
#include "ControllerHelpers.h"
#include <cmath>

static HiddbgHdlsSessionId g_hdlsSessionId;

SwitchHDLHandler::SwitchHDLHandler(std::unique_ptr<IController> &&controller)
    : SwitchVirtualGamepadHandler(std::move(controller))
{
}

SwitchHDLHandler::~SwitchHDLHandler()
{
    Exit();
}

ams::Result SwitchHDLHandler::Initialize()
{

    R_TRY(m_controller->Initialize());

    if (DoesControllerSupport(m_controller->GetType(), SUPPORTS_NOTHING))
        R_SUCCEED();

    R_TRY(InitHdlState());

    if (DoesControllerSupport(m_controller->GetType(), SUPPORTS_PAIRING))
    {
        R_TRY(InitOutputThread());
    }

    R_TRY(InitInputThread());

    R_SUCCEED();
}

void SwitchHDLHandler::Exit()
{
    if (DoesControllerSupport(m_controller->GetType(), SUPPORTS_NOTHING))
    {
        m_controller->Exit();
        return;
    }

    ExitInputThread();
    ExitOutputThread();
    m_controller->Exit();
    ExitHdlState();
}

ams::Result SwitchHDLHandler::InitHdlState()
{
    m_hdlHandle = {0};
    m_deviceInfo = {0};
    m_hdlState = {0};

    // Set the controller type to Pro-Controller, and set the npadInterfaceType.
    m_deviceInfo.deviceType = HidDeviceType_FullKey15;
    m_deviceInfo.npadInterfaceType = HidNpadInterfaceType_USB;
    // Set the controller colors. The grip colors are for Pro-Controller on [9.0.0+].
    ControllerConfig *config = m_controller->GetConfig();
    m_deviceInfo.singleColorBody = config->bodyColor.rgbaValue;
    m_deviceInfo.singleColorButtons = config->buttonsColor.rgbaValue;
    m_deviceInfo.colorLeftGrip = config->leftGripColor.rgbaValue;
    m_deviceInfo.colorRightGrip = config->rightGripColor.rgbaValue;

    m_hdlState.battery_level = 4; // Set battery charge to full.
    m_hdlState.analog_stick_l.x = 0x1234;
    m_hdlState.analog_stick_l.y = -0x1234;
    m_hdlState.analog_stick_r.x = 0x5678;
    m_hdlState.analog_stick_r.y = -0x5678;

    if (m_controller->IsControllerActive())
        R_TRY(hiddbgAttachHdlsVirtualDevice(&m_hdlHandle, &m_deviceInfo));

    R_SUCCEED();
}

ams::Result SwitchHDLHandler::ExitHdlState()
{
    R_RETURN(hiddbgDetachHdlsVirtualDevice(m_hdlHandle));
}

// Sets the state of the class's HDL controller to the state stored in class's hdl.state
ams::Result SwitchHDLHandler::UpdateHdlState()
{
    Result rc = hiddbgSetHdlsState(m_hdlHandle, &m_hdlState);
    if (rc == 0x1c24ca)
    {
        // Re-attach virtual gamepad and set state
        R_TRY(hiddbgAttachHdlsVirtualDevice(&m_hdlHandle, &m_deviceInfo));
        R_TRY(hiddbgSetHdlsState(m_hdlHandle, &m_hdlState));
    }

    R_SUCCEED();
}

void SwitchHDLHandler::FillHdlState(const NormalizedButtonData &data)
{
    // we convert the input packet into switch-specific button states
    m_hdlState.buttons = 0;

    if (data.buttons[0])
        m_hdlState.buttons |= HidNpadButton_X;
    if (data.buttons[1])
        m_hdlState.buttons |= HidNpadButton_A;
    if (data.buttons[2])
        m_hdlState.buttons |= HidNpadButton_B;
    if (data.buttons[3])
        m_hdlState.buttons |= HidNpadButton_Y;
    if (data.buttons[4])
        m_hdlState.buttons |= HidNpadButton_StickL;
    if (data.buttons[5])
        m_hdlState.buttons |= HidNpadButton_StickR;
    if (data.buttons[6])
        m_hdlState.buttons |= HidNpadButton_L;
    if (data.buttons[7])
        m_hdlState.buttons |= HidNpadButton_R;
    if (data.buttons[8])
        m_hdlState.buttons |= HidNpadButton_ZL;
    if (data.buttons[9])
        m_hdlState.buttons |= HidNpadButton_ZR;
    if (data.buttons[10])
        m_hdlState.buttons |= HidNpadButton_Minus;
    if (data.buttons[11])
        m_hdlState.buttons |= HidNpadButton_Plus;

    ControllerConfig *config = m_controller->GetConfig();

    if (config && config->swapDPADandLSTICK)
    {
        if (data.sticks[0].axis_y > 0.5f)
            m_hdlState.buttons |= HidNpadButton_Up;
        if (data.sticks[0].axis_x > 0.5f)
            m_hdlState.buttons |= HidNpadButton_Right;
        if (data.sticks[0].axis_y < -0.5f)
            m_hdlState.buttons |= HidNpadButton_Down;
        if (data.sticks[0].axis_x < -0.5f)
            m_hdlState.buttons |= HidNpadButton_Left;

        float daxis_x{}, daxis_y{};

        daxis_y += data.buttons[12] ? 1.0f : 0.0f;  // DUP
        daxis_x += data.buttons[13] ? 1.0f : 0.0f;  // DRIGHT
        daxis_y += data.buttons[14] ? -1.0f : 0.0f; // DDOWN
        daxis_x += data.buttons[15] ? -1.0f : 0.0f; // DLEFT

        // clamp lefstick values to their acceptable range of values
        float real_magnitude = std::sqrt(daxis_x * daxis_x + daxis_y * daxis_y);
        float clipped_magnitude = std::min(1.0f, real_magnitude);
        float ratio = clipped_magnitude / real_magnitude;

        daxis_x *= ratio;
        daxis_y *= ratio;

        ConvertAxisToSwitchAxis(daxis_x, daxis_y, 0, &m_hdlState.analog_stick_l.x, &m_hdlState.analog_stick_l.y);
    }
    else
    {
        if (data.buttons[12])
            m_hdlState.buttons |= HidNpadButton_Up;
        if (data.buttons[13])
            m_hdlState.buttons |= HidNpadButton_Right;
        if (data.buttons[14])
            m_hdlState.buttons |= HidNpadButton_Down;
        if (data.buttons[15])
            m_hdlState.buttons |= HidNpadButton_Left;

        ConvertAxisToSwitchAxis(data.sticks[0].axis_x, data.sticks[0].axis_y, 0, &m_hdlState.analog_stick_l.x, &m_hdlState.analog_stick_l.y);
    }

    ConvertAxisToSwitchAxis(data.sticks[1].axis_x, data.sticks[1].axis_y, 0, &m_hdlState.analog_stick_r.x, &m_hdlState.analog_stick_r.y);

    if (data.buttons[16])
        m_hdlState.buttons |= HiddbgNpadButton_Capture;
    if (data.buttons[17])
        m_hdlState.buttons |= HiddbgNpadButton_Home;
}

void SwitchHDLHandler::UpdateInput()
{
    // We process any input packets here. If it fails, return and try again
    if (R_FAILED(m_controller->GetInput()))
        return;

    // This is a check for controllers that can prompt themselves to go inactive - e.g. wireless Xbox 360 controllers
    if (!m_controller->IsControllerActive())
    {
        hiddbgDetachHdlsVirtualDevice(m_hdlHandle);
    }
    else
    {
        // We get the button inputs from the input packet and update the state of our controller
        FillHdlState(m_controller->GetNormalizedButtonData());
        if (R_FAILED(UpdateHdlState()))
            return;
    }
}

void SwitchHDLHandler::UpdateOutput()
{
    // Process a single output packet from a buffer
    if (R_SUCCEEDED(m_controller->OutputBuffer()))
        return;

    // Process rumble values if supported
    // if (DoesControllerSupport(m_controller->GetType(), SUPPORTS_RUMBLE))
    // {
    //     Result rc;
    //     HidVibrationValue value;
    //     rc = hidGetActualVibrationValue(m_vibrationDeviceHandle, &value);
    //     if (R_SUCCEEDED(rc))
    //         m_controller->SetRumble(static_cast<uint8_t>(value.amp_high * 255.0f), static_cast<uint8_t>(value.amp_low * 255.0f));
    // }

    svcSleepThread(1e+7L);
}

HiddbgHdlsSessionId &SwitchHDLHandler::GetHdlsSessionId()
{
    return g_hdlsSessionId;
}