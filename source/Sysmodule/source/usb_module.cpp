#include "switch.h"
#include "usb_module.h"
#include "controller_handler.h"
#include "config_handler.h"

#include <stratosphere.hpp>

#include "SwitchUSBDevice.h"
#include "ControllerHelpers.h"
#include "log.h"
#include <string.h>

namespace syscon::usb
{
    namespace
    {
        constexpr u8 CatchAllEventIndex = 2;
        constexpr u8 SonyEventIndex = 0;

        constexpr size_t MaxUsbHsInterfacesSize = 16;

        ams::os::Mutex usbMutex(false);

        //Thread that waits on generic usb event
        void UsbEventThreadFunc(void *arg);
        //Thread that waits on sony vendor usb event
        void UsbSonyEventThreadFunc(void *arg);
        //Thread that waits on any disconnected usb devices
        void UsbInterfaceChangeThreadFunc(void *arg);

        alignas(ams::os::ThreadStackAlignment) u8 usb_event_thread_stack[0x2000];
        alignas(ams::os::ThreadStackAlignment) u8 sony_event_thread_stack[0x2000];
        alignas(ams::os::ThreadStackAlignment) u8 usb_interface_change_thread_stack[0x2000];

        Thread g_usb_event_thread;
        Thread g_sony_event_thread;
        Thread g_usb_interface_change_thread;

        bool is_usb_event_thread_running = false;
        bool is_usb_interface_change_thread_running = false;

        Event g_usbCatchAllEvent{};
        Event g_usbSonyEvent{};
        UsbHsInterface interfaces[MaxUsbHsInterfacesSize];

        HardwareId ps3_hardware_ids[] = 
        {
            {0x054c, 0x0268},   // DS3
            {0x0f0d, 0x0022},   // Hori Co., Ltd
            {0x0f0d, 0x0088},   // hori mini Arcade Stick
            {0x1c1a, 0x0100},   // datel Arcade Stick
            {0x0079, 0x181a},   // venom
            {0x1532, 0x0402},   // Razer Panthera
            {0x0f0d, 0x0085},   // Hori Fighting Commander v4
        };

        // hori fighting command in pc mode (0x0f0d, 0x0086)

        // got many from real hardware. And a lot from here:
        // https://github.com/Raphfriend/USB2DB15/blob/master/RFUSB_to_DB15/drivers.h#L39
        HardwareId ps4_hardware_ids[] = 
        {
            {0x054c, 0x05c4},   // DS4 v1
            {0x054c, 0x09cc},   // DS4 v2
            {0x054c, 0x0ba0},   // PS4 wireless Adapter
            {0x0c12, 0x0c30},   // Brooks Universal Fighting Board (PS4 mode)
            {0x0c12, 0x0ef1},   // Brooks PS2 -> PS4 Adapter
            {0x0c12, 0x1cf2},   // Brooks PS3 -> PS4 Adapter
            {0x0c12, 0x0e31},   // Brooks PS4 Audio Board
            {0x0c12, 0x0ef7},   // Brooks tiny square PS4 Board
            {0x0c12, 0x0ef8},   // Brooks Fighting Board
            {0x0f0d, 0x0087},   // Hori Mini Arcade Stick
            {0x0f0d, 0x0084},   // Hori Fighting Commander v4
            {0x0f0d, 0x00ae},   // Hori RAP Pro N Hayabusa
            {0x0f0d, 0x008a},   // HORI RAP V Hayabusa
            {0x0f0d, 0x00ee},   // HORI ワイヤードコントローラライト for PS4-102
            {0x0f0d, 0x006f},   // HORI RAP Pro VLX
            {0x1f4f, 0x1002},   // Xrd PS4 Pad
            {0x0079, 0x181b},   // Venom Arcade Stick
            {0x1532, 0x0401},   // Razer Panthera
            {0x1532, 0x1008},   // Razer Panthera EVO
            {0x1532, 0x1004},   // Razer Raiju Ultimate
            {0x2c22, 0x2000},   // Qanba Drone
            {0x2c22, 0x2200},   // Qanba Crystal
            {0x2c22, 0x2300},   // Qanba Obsidian
            {0x0738, 0x8180},   // Mad Catz Fight Stick Alpha
            {0x0738, 0x8481},   // Mad Catz SFV Arcade FightStick TE2+
            {0x0738, 0x8384},   // Mad Catz SFV Arcade FightStick TES+
            {0x0738, 0x8250},   // Mad Catz FightPad PRO PS4
            {0x146b, 0x0d09},   // Nacon Daija
        };

        ControllerType IdentifyControllerType(const UsbHsInterface *iface) {
            WriteToLog("Checking controller -> vid: 0x%04x, pid: 0c%04x", iface->device_desc.idVendor, iface->device_desc.idProduct);

            for (auto id : ps3_hardware_ids) {
                if ((id.vendor_id == iface->device_desc.idVendor) && (id.product_id == iface->device_desc.idProduct)) {
                    return CONTROLLER_DUALSHOCK3;
                }
            }

            for (auto id : ps4_hardware_ids) {
                if ((id.vendor_id == iface->device_desc.idVendor) && (id.product_id == iface->device_desc.idProduct)) {
                    return CONTROLLER_DUALSHOCK4;
                }
            }

            return CONTROLLER_UNDEFINED;
        }

        s32 QueryInterfaces(u8 iclass, u8 isubclass, u8 iprotocol);
        s32 QueryVendorProduct(uint16_t vendor_id, uint16_t product_id);

        void UsbEventThreadFunc(void *arg)
        {
            do
            {
                if (R_SUCCEEDED(eventWait(&g_usbCatchAllEvent, UINT64_MAX)))
                {
                    WriteToLog("Catch-all event went off");

                    /*
                    Result rc;

                    UsbHsInterfaceFilter filter;
                    filter.Flags = UsbHsInterfaceFilterFlags_bInterfaceClass | UsbHsInterfaceFilterFlags_bcdDevice_Min;
                    filter.bInterfaceClass = USB_CLASS_HID;
                    filter.bcdDevice_Min = 0;
                    */

                    std::scoped_lock usbLock(usbMutex);
                    if (!controllers::IsAtControllerLimit())
                    {
                        s32 total_entries = 0;

                        /*
                        rc = usbHsQueryAvailableInterfaces(&filter, interfaces, sizeof(interfaces), &total_entries);
                        WriteToLog("%d interfaces found", total_entries);
                        if (R_SUCCEEDED(rc) && (total_entries > 0)) {
                            for (int i = 0; i < total_entries; ++i) {
                                switch(IdentifyControllerType(&interfaces[i])) {
                                    case CONTROLLER_DUALSHOCK3:
                                        WriteToLog("Initializing Dualshock 3 controller: 0x%x", controllers::Insert(std::make_unique<Dualshock3Controller>(std::make_unique<SwitchUSBDevice>(&interfaces[i], 1))));
                                        break;
                                    case CONTROLLER_DUALSHOCK4:
                                        WriteToLog("Initializing Dualshock 4 controller: 0x%x", controllers::Insert(std::make_unique<Dualshock4Controller>(std::make_unique<SwitchUSBDevice>(&interfaces[i], 1))));
                                        break;
                                    default:
                                        break;
                                }
                            }
                        } 
                        else {
                            WriteToLog("rc: %d", rc);
                        }
                        */

                        if ((total_entries = QueryInterfaces(USB_CLASS_VENDOR_SPEC, 93, 1)) != 0)
                            WriteToLog("Initializing Xbox 360 controller: 0x%x", controllers::Insert(std::make_unique<Xbox360Controller>(std::make_unique<SwitchUSBDevice>(interfaces, total_entries))));

                        if ((total_entries = QueryInterfaces(USB_CLASS_VENDOR_SPEC, 93, 129)) != 0)
                            for (int i = 0; i != total_entries; ++i)
                                WriteToLog("Initializing Xbox 360 wireless controller: 0x%x", controllers::Insert(std::make_unique<Xbox360WirelessController>(std::make_unique<SwitchUSBDevice>(interfaces + i, 1))));

                        if ((total_entries = QueryInterfaces(0x58, 0x42, 0x00)) != 0)
                            WriteToLog("Initializing Xbox Original controller: 0x%x", controllers::Insert(std::make_unique<XboxController>(std::make_unique<SwitchUSBDevice>(interfaces, total_entries))));

                        if ((total_entries = QueryInterfaces(USB_CLASS_VENDOR_SPEC, 71, 208)) != 0)
                            WriteToLog("Initializing Xbox One controller: 0x%x", controllers::Insert(std::make_unique<XboxOneController>(std::make_unique<SwitchUSBDevice>(interfaces, total_entries))));
                    }
                }
            } while (is_usb_event_thread_running);
        }

        void UsbSonyEventThreadFunc(void *arg)
        {
            Result rc;

            UsbHsInterfaceFilter filter;
            filter.Flags = UsbHsInterfaceFilterFlags_bInterfaceClass | UsbHsInterfaceFilterFlags_bcdDevice_Min;
            filter.bInterfaceClass = USB_CLASS_HID;
            filter.bcdDevice_Min = 0;

            do
            {
                if (R_SUCCEEDED(eventWait(&g_usbSonyEvent, UINT64_MAX)))
                {
                    WriteToLog("Sony event went off");

                    std::scoped_lock usbLock(usbMutex);
                    if (!controllers::IsAtControllerLimit())
                    {
                        s32 total_entries = 0;
                        rc = usbHsQueryAvailableInterfaces(&filter, interfaces, sizeof(interfaces), &total_entries);
                        if (R_SUCCEEDED(rc) && (total_entries > 0)) {
                            for (int i = 0; i < total_entries; ++i) {
                                switch(IdentifyControllerType(&interfaces[i])) {
                                    case CONTROLLER_DUALSHOCK3:
                                        WriteToLog("Initializing Dualshock 3 controller: 0x%x", controllers::Insert(std::make_unique<Dualshock3Controller>(std::make_unique<SwitchUSBDevice>(&interfaces[i], 1))));
                                        break;
                                    case CONTROLLER_DUALSHOCK4:
                                        WriteToLog("Initializing Dualshock 4 controller: 0x%x", controllers::Insert(std::make_unique<Dualshock4Controller>(std::make_unique<SwitchUSBDevice>(&interfaces[i], 1))));
                                        break;
                                    default:
                                        break;
                                }
                            }
                        }
                        
                        /*
                        if ((QueryVendorProduct(VENDOR_SONY, PRODUCT_DUALSHOCK3) != 0) && (total_entries = QueryInterfaces(USB_CLASS_HID, 0, 0)) != 0)
                            WriteToLog("Initializing Dualshock 3 controller: 0x%x", controllers::Insert(std::make_unique<Dualshock3Controller>(std::make_unique<SwitchUSBDevice>(interfaces, total_entries))));

                        else if ((QueryVendorProduct(VENDOR_SONY, PRODUCT_DUALSHOCK4_1X) != 0 || QueryVendorProduct(VENDOR_SONY, PRODUCT_DUALSHOCK4_2X) != 0) && (total_entries = QueryInterfaces(USB_CLASS_HID, 0, 0)) != 0)
                            WriteToLog("Initializing Dualshock 4 controller: 0x%x", controllers::Insert(std::make_unique<Dualshock4Controller>(std::make_unique<SwitchUSBDevice>(interfaces, total_entries))));
                        */
                    }
                }
            } while (is_usb_event_thread_running);
        }

        void UsbInterfaceChangeThreadFunc(void *arg)
        {
            do
            {
                if (R_SUCCEEDED(eventWait(usbHsGetInterfaceStateChangeEvent(), UINT64_MAX)))
                {
                    s32 total_entries;
                    WriteToLog("Interface state was changed");

                    std::scoped_lock usbLock(usbMutex);
                    std::scoped_lock controllersLock(controllers::GetScopedLock());

                    eventClear(usbHsGetInterfaceStateChangeEvent());
                    memset(interfaces, 0, sizeof(interfaces));
                    if (R_SUCCEEDED(usbHsQueryAcquiredInterfaces(interfaces, sizeof(interfaces), &total_entries)))
                    {
                        for (auto it = controllers::Get().begin(); it != controllers::Get().end(); ++it)
                        {
                            bool found_flag = false;

                            for (auto &&ptr : (*it)->GetController()->GetDevice()->GetInterfaces())
                            {
                                //We check if a device was removed by comparing the controller's interfaces and the currently acquired interfaces
                                //If we didn't find a single matching interface ID, we consider a controller removed
                                for (int i = 0; i != total_entries; ++i)
                                {
                                    if (interfaces[i].inf.ID == static_cast<SwitchUSBInterface *>(ptr.get())->GetID())
                                    {
                                        found_flag = true;
                                        break;
                                    }
                                }
                            }

                            if (!found_flag)
                            {
                                WriteToLog("Erasing controller");
                                controllers::Get().erase(it--);
                                WriteToLog("Controller erased!");
                            }
                        }
                    }
                }
            } while (is_usb_interface_change_thread_running);
        }

        s32 QueryInterfaces(u8 iclass, u8 isubclass, u8 iprotocol)
        {
            UsbHsInterfaceFilter filter{
                .Flags = UsbHsInterfaceFilterFlags_bInterfaceClass | UsbHsInterfaceFilterFlags_bInterfaceSubClass | UsbHsInterfaceFilterFlags_bInterfaceProtocol,
                .bInterfaceClass = iclass,
                .bInterfaceSubClass = isubclass,
                .bInterfaceProtocol = iprotocol,
            };
            s32 out_entries = 0;
            memset(interfaces, 0, sizeof(interfaces));
            usbHsQueryAvailableInterfaces(&filter, interfaces, sizeof(interfaces), &out_entries);
            return out_entries;
        }

        s32 QueryVendorProduct(uint16_t vendor_id, uint16_t product_id)
        {
            UsbHsInterfaceFilter filter{
                .Flags = UsbHsInterfaceFilterFlags_idVendor | UsbHsInterfaceFilterFlags_idProduct,
                .idVendor = vendor_id,
                .idProduct = product_id,
            };
            s32 out_entries = 0;
            usbHsQueryAvailableInterfaces(&filter, interfaces, sizeof(interfaces), &out_entries);
            return out_entries;
        }

        inline Result CreateCatchAllAvailableEvent()
        {
            constexpr UsbHsInterfaceFilter filter{
                .Flags = UsbHsInterfaceFilterFlags_bcdDevice_Min,
                .bcdDevice_Min = 0,
            };
            return usbHsCreateInterfaceAvailableEvent(&g_usbCatchAllEvent, true, CatchAllEventIndex, &filter);
        }

        inline Result CreateSonyAvailableEvent()
        {
            constexpr UsbHsInterfaceFilter filter{
                //.Flags = UsbHsInterfaceFilterFlags_idVendor,
                //.idVendor = VENDOR_SONY,
                .Flags = UsbHsInterfaceFilterFlags_bInterfaceClass | UsbHsInterfaceFilterFlags_bcdDevice_Min,
                .bcdDevice_Min = 0,
                .bInterfaceClass = USB_CLASS_HID,
            };
            return usbHsCreateInterfaceAvailableEvent(&g_usbSonyEvent, true, SonyEventIndex, &filter);
        }

    } // namespace

    void Initialize()
    {
        R_ABORT_UNLESS(Enable());
    }

    void Exit()
    {
        Disable();
    }

    Result Enable()
    {
        R_TRY(CreateUsbEvents());

        is_usb_event_thread_running = true;
        is_usb_interface_change_thread_running = true;

        R_ABORT_UNLESS(threadCreate(&g_usb_event_thread, &UsbEventThreadFunc, nullptr, usb_event_thread_stack, sizeof(usb_event_thread_stack), 0x3A, -2));
        R_ABORT_UNLESS(threadCreate(&g_sony_event_thread, &UsbSonyEventThreadFunc, nullptr, sony_event_thread_stack, sizeof(sony_event_thread_stack), 0x3B, -2));
        R_ABORT_UNLESS(threadCreate(&g_usb_interface_change_thread, &UsbInterfaceChangeThreadFunc, nullptr, usb_interface_change_thread_stack, sizeof(usb_interface_change_thread_stack), 0x2C, -2));

        R_ABORT_UNLESS(threadStart(&g_usb_event_thread));
        R_ABORT_UNLESS(threadStart(&g_sony_event_thread));
        R_ABORT_UNLESS(threadStart(&g_usb_interface_change_thread));
        return 0;
    }

    void Disable()
    {
        is_usb_event_thread_running = false;
        is_usb_interface_change_thread_running = false;

        svcCancelSynchronization(g_usb_event_thread.handle);
        threadWaitForExit(&g_usb_event_thread);
        threadClose(&g_usb_event_thread);

        svcCancelSynchronization(g_sony_event_thread.handle);
        threadWaitForExit(&g_sony_event_thread);
        threadClose(&g_sony_event_thread);

        svcCancelSynchronization(g_usb_interface_change_thread.handle);
        threadWaitForExit(&g_usb_interface_change_thread);
        threadClose(&g_usb_interface_change_thread);

        DestroyUsbEvents();
        controllers::Reset();
    }

    Result CreateUsbEvents()
    {
        if (g_usbCatchAllEvent.revent != INVALID_HANDLE)
            return 0x99;
        R_TRY(CreateCatchAllAvailableEvent());
        R_TRY(CreateSonyAvailableEvent());
        return 0;
    }

    void DestroyUsbEvents()
    {
        usbHsDestroyInterfaceAvailableEvent(&g_usbCatchAllEvent, CatchAllEventIndex);
        usbHsDestroyInterfaceAvailableEvent(&g_usbSonyEvent, SonyEventIndex);
    }
} // namespace syscon::usb
