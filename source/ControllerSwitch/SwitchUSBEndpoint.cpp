#include "SwitchUSBEndpoint.h"
#include <cstring>
#include <malloc.h>

SwitchUSBEndpoint::SwitchUSBEndpoint(UsbHsClientIfSession &if_session, usb_endpoint_descriptor &desc)
    : m_ifSession(&if_session),
      m_descriptor(&desc)
{
}

SwitchUSBEndpoint::~SwitchUSBEndpoint()
{
    if (m_buffer != nullptr)
    {
        free(m_buffer);
        m_buffer = nullptr;
    }
}

ams::Result SwitchUSBEndpoint::Open(int maxPacketSize)
{
    maxPacketSize = maxPacketSize != 0 ? maxPacketSize : m_descriptor->wMaxPacketSize;

    R_TRY(usbHsIfOpenUsbEp(m_ifSession, &m_epSession, 1, maxPacketSize, m_descriptor));

    if (m_buffer != nullptr)
        free(m_buffer);
    m_buffer = memalign(0x1000, maxPacketSize);
    if (m_buffer == nullptr)
        return -1;

    R_SUCCEED();
}

void SwitchUSBEndpoint::Close()
{
    usbHsEpClose(&m_epSession);
}

ams::Result SwitchUSBEndpoint::Write(const void *inBuffer, size_t bufferSize)
{
    if (m_buffer == nullptr)
        return -1;
    u32 transferredSize = 0;

    memcpy(m_buffer, inBuffer, bufferSize);

    R_TRY(usbHsEpPostBuffer(&m_epSession, m_buffer, bufferSize, &transferredSize));

    svcSleepThread(m_descriptor->bInterval * 1e+6L);

    R_SUCCEED();
}

ams::Result SwitchUSBEndpoint::Read(void *outBuffer, size_t bufferSize)
{
    if (m_buffer == nullptr)
        return -1;

    u32 transferredSize;

    R_TRY(usbHsEpPostBuffer(&m_epSession, m_buffer, bufferSize, &transferredSize));

    memcpy(outBuffer, m_buffer, transferredSize);

    R_SUCCEED();
}

IUSBEndpoint::Direction SwitchUSBEndpoint::GetDirection()
{
    return ((m_descriptor->bEndpointAddress & USB_ENDPOINT_IN) ? USB_ENDPOINT_IN : USB_ENDPOINT_OUT);
}

IUSBEndpoint::EndpointDescriptor *SwitchUSBEndpoint::GetDescriptor()
{
    return reinterpret_cast<IUSBEndpoint::EndpointDescriptor *>(m_descriptor);
}