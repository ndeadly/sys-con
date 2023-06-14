#include <switch.h>
#include "log.h"
#include <sys/stat.h>
#include <stratosphere.hpp>

namespace
{

    constexpr const char LogFilePath[] = "sdmc:" CONFIG_PATH "log.txt";
    ams::fs::FileHandle LogFile;

    ams::os::Mutex printMutex(false);

    ams::Result WriteToLogImpl(const char *fmt, std::va_list args)
    {
        ams::TimeSpan ts = ams::os::ConvertToTimeSpan(ams::os::GetSystemTick());

        // Check if log file exists and create it if not
        bool has_file;
        R_ABORT_UNLESS(ams::fs::HasFile(&has_file, LogFilePath));
        if (!has_file)
        {
            R_ABORT_UNLESS(ams::fs::EnsureDirectory("sdmc:" CONFIG_PATH));
            R_ABORT_UNLESS(ams::fs::CreateFile(LogFilePath, 0));
        }

        R_ABORT_UNLESS(ams::fs::OpenFile(&LogFile, LogFilePath, ams::fs::OpenMode_Write | ams::fs::OpenMode_AllowAppend));
        ON_SCOPE_EXIT { ams::fs::CloseFile(LogFile); };

        s64 logOffset;
        R_ABORT_UNLESS(ams::fs::GetFileSize(&logOffset, LogFile));

        char buff[0x200];

        // Print time
        int len = ams::util::TSNPrintf(buff, sizeof(buff), "%02lid %02li:%02li:%02li: ",
                                       ts.GetDays(),
                                       ts.GetHours() % 24,
                                       ts.GetMinutes() % 60,
                                       ts.GetSeconds() % 60);

        // Print the actual text
        len += ams::util::TVSNPrintf(&buff[len], sizeof(buff) - len, fmt, args);
        len += ams::util::TSNPrintf(&buff[len], sizeof(buff) - len, "\n");

        R_ABORT_UNLESS(ams::fs::WriteFile(LogFile, logOffset, buff, len, ams::fs::WriteOption::Flush));

        R_SUCCEED();
    }

} // namespace

ams::Result DiscardOldLogs()
{
    std::scoped_lock printLock(printMutex);

    s64 fileSize;
    {
        R_TRY(ams::fs::OpenFile(&LogFile, LogFilePath, ams::fs::OpenMode_Read));
        ON_SCOPE_EXIT { ams::fs::CloseFile(LogFile); };

        R_TRY(ams::fs::GetFileSize(&fileSize, LogFile));
    }

    if (fileSize >= 0x20'000)
    {
        R_TRY(ams::fs::DeleteFile(LogFilePath));
        WriteToLog("Deleted previous log file");
    }

    R_SUCCEED();
}

void WriteToLog(const char *fmt, ...)
{
    std::scoped_lock printLock(printMutex);

    std::va_list args;
    va_start(args, fmt);
    R_ABORT_UNLESS(WriteToLogImpl(fmt, args));
    va_end(args);
}

void LockedUpdateConsole()
{
    std::scoped_lock printLock(printMutex);
    consoleUpdate(NULL);
}