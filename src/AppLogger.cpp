#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <fx.h>

#include "AppLogger.h"

static const char* logFilename(AppLogCategory category)
{
    switch (category)
    {
    case APP_LOG_APPLICATION:
        return "application.log";
    case APP_LOG_UI:
        return "ui.log";
    case APP_LOG_FILE_OPERATIONS:
        return "file-operations.log";
    case APP_LOG_SETTINGS:
        return "settings.log";
    case APP_LOG_ERRORS:
        return "errors.log";
    }
    return "application.log";
}

static FXString logDirectory()
{
    const char* portableData = getenv("SWORDFISH_PORTABLE_DATA");
    if (portableData && *portableData)
    {
        return FXString(portableData) + "/logs";
    }

    const char* stateHome = getenv("XDG_STATE_HOME");
    if (stateHome && *stateHome)
    {
        return FXString(stateHome) + "/swordfish/logs";
    }

    const char* home = getenv("HOME");
    if (home && *home)
    {
        return FXString(home) + "/.local/state/swordfish/logs";
    }
    return "/tmp/swordfish-logs";
}

static bool ensureDirectory(const FXString& pathname)
{
    char* path = strdup(pathname.text());
    if (!path)
    {
        return false;
    }

    for (char* p = path + (path[0] == '/' ? 1 : 0); ; ++p)
    {
        if (*p != '/' && *p != '\0')
        {
            continue;
        }

        char separator = *p;
        *p = '\0';
        if (*path)
        {
            struct stat info;
            if (stat(path, &info) != 0)
            {
                if (errno != ENOENT || mkdir(path, 0700) != 0)
                {
                    free(path);
                    return false;
                }
            }
            else if (!S_ISDIR(info.st_mode))
            {
                free(path);
                return false;
            }
        }
        *p = separator;

        if (separator == '\0')
        {
            break;
        }
    }

    free(path);
    return true;
}

void appLog(AppLogCategory category, const FXString& event, const FXString& details)
{
    FXString directory = logDirectory();
    if (!ensureDirectory(directory))
    {
        fprintf(stderr, "Swordfish: cannot create log directory %s: %s\n", directory.text(), strerror(errno));
        return;
    }

    FXString pathname = directory + PATHSEPSTRING + logFilename(category);
    int descriptor = open(pathname.text(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
    if (descriptor < 0)
    {
        fprintf(stderr, "Swordfish: cannot open log file %s: %s\n", pathname.text(), strerror(errno));
        return;
    }

    time_t now = time(NULL);
    struct tm timestamp;
    char timeBuffer[32];
    if (localtime_r(&now, &timestamp))
    {
        strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%dT%H:%M:%S%z", &timestamp);
    }
    else
    {
        strcpy(timeBuffer, "unknown-time");
    }

    FXString safeEvent = event;
    FXString safeDetails = details;
    safeEvent.substitute('\n', ' ');
    safeEvent.substitute('\r', ' ');
    safeDetails.substitute('\n', ' ');
    safeDetails.substitute('\r', ' ');

    FXString line;
    line.format("%s pid=%ld event=%s %s\n", timeBuffer, static_cast<long>(getpid()), safeEvent.text(),
                safeDetails.text());
    const char* next = line.text();
    FXint remaining = line.length();
    while (remaining > 0)
    {
        ssize_t written = write(descriptor, next, static_cast<size_t>(remaining));
        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            fprintf(stderr, "Swordfish: cannot write log file %s: %s\n", pathname.text(), strerror(errno));
            break;
        }
        if (written == 0)
        {
            fprintf(stderr, "Swordfish: zero-length write to log file %s\n", pathname.text());
            break;
        }
        next += written;
        remaining -= static_cast<FXint>(written);
    }
    close(descriptor);
}
