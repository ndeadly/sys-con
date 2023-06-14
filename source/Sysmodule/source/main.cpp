#include <switch.h>
#include "log.h"
#include <stratosphere.hpp>

#include "usb_module.h"
#include "controller_handler.h"
#include "config_handler.h"
#include "psc_module.h"
#include "SwitchHDLHandler.h"

#define APP_VERSION "0.6.4"

namespace ams
{

    ncm::ProgramId CurrentProgramId = {0x690000000000000D};
    namespace result
    {
        bool CallFatalOnResultAssertion = true;
    }

    namespace
    {

        alignas(0x40) constinit u8 g_heap_memory[256_KB];
        constinit lmem::HeapHandle g_heap_handle;
        constinit bool g_heap_initialized;
        constinit os::SdkMutex g_heap_init_mutex;

        lmem::HeapHandle GetHeapHandle()
        {
            if (AMS_UNLIKELY(!g_heap_initialized))
            {
                std::scoped_lock lk(g_heap_init_mutex);

                if (AMS_LIKELY(!g_heap_initialized))
                {
                    g_heap_handle = lmem::CreateExpHeap(g_heap_memory, sizeof(g_heap_memory), lmem::CreateOption_ThreadSafe);
                    g_heap_initialized = true;
                }
            }

            return g_heap_handle;
        }

        void *Allocate(size_t size)
        {
            return lmem::AllocateFromExpHeap(GetHeapHandle(), size);
        }

        void *AllocateWithAlign(size_t sz, size_t align)
        {
            return lmem::AllocateFromExpHeap(GetHeapHandle(), sz, align);
        }

        void Deallocate(void *p, size_t size)
        {
            AMS_UNUSED(size);
            return lmem::FreeToExpHeap(GetHeapHandle(), p);
        }

        alignas(0x1000) constinit uint8_t g_tmem_buffer[0x1000];

    } // namespace

    namespace init
    {

        void InitializeSystemModule()
        {
            R_ABORT_UNLESS(sm::Initialize());

            fs::InitializeForSystem();
            fs::SetAllocator(ams::Allocate, ams::Deallocate);
            fs::SetEnabledAutoAbort(false);

            R_ABORT_UNLESS(pmdmntInitialize());
            R_ABORT_UNLESS(pminfoInitialize());
            R_ABORT_UNLESS(pscmInitialize());
            R_ABORT_UNLESS(usbHsInitialize());

            R_ABORT_UNLESS(hiddbgInitialize());
            if (hosversionAtLeast(7, 0, 0))
                R_ABORT_UNLESS(hiddbgAttachHdlsWorkBuffer(&SwitchHDLHandler::GetHdlsSessionId(), g_tmem_buffer, sizeof(g_tmem_buffer)));

            R_ABORT_UNLESS(fs::MountSdCard("sdmc"));
        }

        void FinalizeSystemModule()
        {
            hiddbgReleaseHdlsWorkBuffer(SwitchHDLHandler::GetHdlsSessionId());
            hiddbgExit();
            usbHsExit();
            pscmExit();
        }

        void Startup()
        { /* ... */
        }

    } // namespace init

    void Main()
    {
        WriteToLog("\n\nNew sysmodule session started on version " APP_VERSION);
        R_ABORT_UNLESS(syscon::config::Initialize());
        R_ABORT_UNLESS(syscon::usb::Initialize());
        R_ABORT_UNLESS(syscon::psc::Initialize());
        syscon::controllers::Initialize();

        while (true)
        {
            svcSleepThread(1e+8L);
        }

        syscon::controllers::Exit();
        syscon::psc::Exit();
        syscon::usb::Exit();
        syscon::config::Exit();
    }

} // namespace ams

void *operator new(size_t size)
{
    return ams::Allocate(size);
}

void *operator new(size_t size, const std::nothrow_t &)
{
    return ams::Allocate(size);
}

void operator delete(void *p)
{
    return ams::Deallocate(p, 0);
}

void operator delete(void *p, size_t size)
{
    return ams::Deallocate(p, size);
}

void *operator new[](size_t size)
{
    return ams::Allocate(size);
}

void *operator new[](size_t size, const std::nothrow_t &)
{
    return ams::Allocate(size);
}

void operator delete[](void *p)
{
    return ams::Deallocate(p, 0);
}

void operator delete[](void *p, size_t size)
{
    return ams::Deallocate(p, size);
}

void *operator new(size_t size, std::align_val_t align)
{
    // AMS_UNUSED(align);
    // return ams::Allocate(size);

    return ams::AllocateWithAlign(size, static_cast<size_t>(align));
}

void operator delete(void *p, std::align_val_t align)
{
    AMS_UNUSED(align);
    return ams::Deallocate(p, 0);
}