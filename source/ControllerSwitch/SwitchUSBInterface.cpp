#include "SwitchUSBInterface.h"
#include "SwitchUSBEndpoint.h"
#include <malloc.h>
#include <cstring>

SwitchUSBInterface::SwitchUSBInterface(UsbHsInterface &interface)
    : m_interface(interface)
{
}

SwitchUSBInterface::~SwitchUSBInterface()
{
}

ams::Result SwitchUSBInterface::Open()
{
    UsbHsClientIfSession temp;
    R_TRY(usbHsAcquireUsbIf(&temp, &m_interface));

    m_session = temp;

    for (int i = 0; i != 15; ++i)
    {
        usb_endpoint_descriptor &epdesc = m_session.inf.inf.input_endpoint_descs[i];
        if (epdesc.bLength != 0)
        {
            m_inEndpoints[i] = std::make_unique<SwitchUSBEndpoint>(m_session, epdesc);
        }
    }

    for (int i = 0; i != 15; ++i)
    {
        usb_endpoint_descriptor &epdesc = m_session.inf.inf.output_endpoint_descs[i];
        if (epdesc.bLength != 0)
        {
            m_outEndpoints[i] = std::make_unique<SwitchUSBEndpoint>(m_session, epdesc);
        }
    }

    R_SUCCEED();
}
void SwitchUSBInterface::Close()
{
    for (auto &&endpoint : m_inEndpoints)
    {
        if (endpoint)
        {
            endpoint->Close();
        }
    }
    for (auto &&endpoint : m_outEndpoints)
    {
        if (endpoint)
        {
            endpoint->Close();
        }
    }
    usbHsIfClose(&m_session);
}

ams::Result SwitchUSBInterface::ControlTransfer(u8 bmRequestType, u8 bmRequest, u16 wValue, u16 wIndex, u16 wLength, void *buffer)
{
    void *temp_buffer = memalign(0x1000, wLength);
    ON_SCOPE_EXIT { free(temp_buffer); };

    if (temp_buffer == nullptr)
        return -1;

    u32 transferredSize;

    memcpy(temp_buffer, buffer, wLength);

    R_TRY(usbHsIfCtrlXfer(&m_session, bmRequestType, bmRequest, wValue, wIndex, wLength, temp_buffer, &transferredSize));

    memcpy(buffer, temp_buffer, transferredSize);

    R_SUCCEED();
}

ams::Result SwitchUSBInterface::ControlTransfer(u8 bmRequestType, u8 bmRequest, u16 wValue, u16 wIndex, u16 wLength, const void *buffer)
{
    void *temp_buffer = memalign(0x1000, wLength);
    ON_SCOPE_EXIT { free(temp_buffer); };

    if (temp_buffer == nullptr)
        return -1;

    u32 transferredSize;

    memcpy(temp_buffer, buffer, wLength);

    R_RETURN(usbHsIfCtrlXfer(&m_session, bmRequestType, bmRequest, wValue, wIndex, wLength, temp_buffer, &transferredSize));
}

IUSBEndpoint *SwitchUSBInterface::GetEndpoint(IUSBEndpoint::Direction direction, uint8_t index)
{
    if (direction == IUSBEndpoint::USB_ENDPOINT_IN)
        return m_inEndpoints[index].get();
    else
        return m_outEndpoints[index].get();
}

ams::Result SwitchUSBInterface::Reset()
{
    R_RETURN(usbHsIfResetDevice(&m_session));
}