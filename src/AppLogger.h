#ifndef APPLOGGER_H
#define APPLOGGER_H

#include <fx.h>

enum AppLogCategory
{
    APP_LOG_APPLICATION,
    APP_LOG_UI,
    APP_LOG_FILE_OPERATIONS,
    APP_LOG_SETTINGS,
    APP_LOG_ERRORS
};

void appLog(AppLogCategory category, const FXString& event, const FXString& details = FXString());

#endif
