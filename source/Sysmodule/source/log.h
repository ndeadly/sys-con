#pragma once
#include "config_handler.h"

// #define LOG_PATH CONFIG_PATH "log.txt"

ams::Result DiscardOldLogs();

void WriteToLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void LockedUpdateConsole();
