#ifndef APPLOGGER_H
#define APPLOGGER_H

#include <fx.h>

enum AppLogCategory
{
    APP_LOG_APPLICATION,
    APP_LOG_UI,
    APP_LOG_FILE_OPERATIONS,
    APP_LOG_SETTINGS,
    APP_LOG_ERRORS,
    APP_LOG_LISTING_DIAGNOSTIC
};

// Stored as OPTIONS/log_level; higher levels include everything below them
enum AppLogLevel
{
    APP_LOG_LEVEL_OFF = 0,
    APP_LOG_LEVEL_ERRORS,
    APP_LOG_LEVEL_NORMAL,
    APP_LOG_LEVEL_DETAILED,
    APP_LOG_LEVEL_DEBUG
};

void appLog(AppLogCategory category, const FXString& event, const FXString& details = FXString());
FXuint appLogLevel();
void appLogSetLevel(FXuint level);
FXString appLogDirectory();

#endif
