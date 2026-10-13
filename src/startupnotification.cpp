#include "config.h"
#include "i18n.h"

#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <map>

#include <fx.h>

#include "xfedefs.h"
#include "xfeutils.h"
#include "startupnotification.h"
#include "AppLogger.h"
#include "MessageBox.h"

struct LaunchedCommand
{
    FXString command;
    FXString errorFile;
};

static std::map<pid_t, LaunchedCommand> launchedCommands;

struct LaunchFileCleanup
{
    ~LaunchFileCleanup()
    {
        for (std::map<pid_t, LaunchedCommand>::const_iterator entry = launchedCommands.begin();
             entry != launchedCommands.end(); ++entry)
        {
            unlink(entry->second.errorFile.text());
        }
    }
};

static LaunchFileCleanup launchFileCleanup;

static int launchCommand(const FXString& command
#ifdef STARTUP_NOTIFICATION
                         , SnLauncherContext* context = NULL
#endif
                         )
{
    char errorFile[] = "/tmp/swordfish-launch-XXXXXX";
    int fd = mkstemp(errorFile);
    if (fd < 0)
    {
        perror("Cannot create launch error log");
        return -1;
    }
    pid_t pid = fork();
    if (pid == 0)
    {
        if (dup2(fd, STDERR_FILENO) < 0)
        {
            perror("Cannot redirect launch errors");
            _exit(127);
        }
        close(fd);
        xf_restorehostenv();
#ifdef STARTUP_NOTIFICATION
        if (context)
        {
            sn_launcher_context_setup_child_process(context);
        }
#endif
        execl("/bin/sh", "sh", "-c", command.text(), (char*)NULL);
        perror("Cannot start command");
        _exit(127);
    }
    close(fd);
    if (pid < 0)
    {
        unlink(errorFile);
        perror("Cannot fork command");
        return -1;
    }
    LaunchedCommand launched;
    launched.command = command;
    launched.errorFile = errorFile;
    launchedCommands[pid] = launched;
    return 0;
}

void handleCommandExit(FXWindow* owner, pid_t pid, int status)
{
    std::map<pid_t, LaunchedCommand>::iterator entry = launchedCommands.find(pid);
    if (entry == launchedCommands.end())
    {
        return;
    }
    LaunchedCommand launched = entry->second;
    launchedCommands.erase(entry);
    FXString details;
    FILE* errors = fopen(launched.errorFile.text(), "r");
    if (errors)
    {
        char buffer[1024];
        while (details.length() < 8192 && fgets(buffer, sizeof(buffer), errors))
        {
            details += buffer;
        }
        fclose(errors);
    }
    unlink(launched.errorFile.text());
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        return;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 126 &&
        FXPath::name(xf_commandprogram(launched.command)) == "pkexec")
    {
        appLog(APP_LOG_UI, "administrator-launch-cancelled", launched.command);
        return;
    }
    FXString message;
    FXString reason;
    if (WIFEXITED(status))
    {
        reason.format(_("Exit status: %d"), WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status))
    {
        reason.format(_("Terminated by signal: %d"), WTERMSIG(status));
    }
    message.format(_("Application failed to run:\n%s\n\n%s\n%s"),
                   launched.command.text(), reason.text(), details.text());
    appLog(APP_LOG_ERRORS, "application-launch-failed", message);
    MessageBox::error(owner, BOX_OK, _("Application error"), "%s", message.text());
}


#ifdef STARTUP_NOTIFICATION  // Use startup notification


// Indicate that launchee has completed startup
void startup_completed(void)
{
    SnLauncheeContext* launchee;
    Display* xdisplay;
    SnDisplay* display;

    // Open display
    xdisplay = XOpenDisplay(NULL);
    if (xdisplay != NULL)
    {
        // Create startup notification context
        display = sn_display_new(xdisplay, NULL, NULL);
        launchee = sn_launchee_context_new_from_environment(display, DefaultScreen(xdisplay));

        // Indicate startup has completed and free resources
        if (launchee)
        {
            sn_launchee_context_complete(launchee);
            sn_launchee_context_unref(launchee);
        }
        sn_display_unref(display);
        XCloseDisplay(xdisplay);
    }
}


// Hack to obtain a timestamp for startup notification
// Create a fake window and set up a property change event
// Code snippet borrowed from the Internet
Time gettimestamp(Display* display)
{
    Window window;
    XEvent event;
    Atom atom_name, atom_type;

    window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, InputOnly, 0, 0, NULL);
    XSelectInput(display, window, PropertyChangeMask);
    atom_name = XInternAtom(display, "_NET_WM_USER_TIME_WINDOW", False);
    atom_type = XInternAtom(display, "WINDOW", true);
    XChangeProperty(display, window, atom_name, atom_type, 8, PropModeReplace, (const FXuchar*)&window, 1);
    XNextEvent(display, &event);
    assert(event.type == PropertyNotify);

    return ((XPropertyEvent*)&event)->time;
}


// Launch a command and initiate a startup notification
int runcmd(FXString cmd, FXString cmdname, FXString dir, FXString startdir, FXbool usesn = true,
           FXString snexcepts = "")
{
    // Change to current directory
    int ret = chdir(dir.text());
    if (ret < 0)
    {
        int errcode = errno;
        if (errcode)
        {
            fprintf(stderr, _("Error: Can't enter folder %s: %s"), dir.text(), strerror(errcode));
        }
        else
        {
            fprintf(stderr, _("Error: Can't enter folder %s"), dir.text());
        }

        return -1;
    }

    // Get rid of possible command options
    cmdname = cmdname.before(' ');

    // Check if command is in the startup notification exception list
    FXbool startup_notify = true;
    if (snexcepts != "")
    {
        FXString entry;
        for (int i = 0; ; i++)
        {
            entry = snexcepts.section(':', i);
            if (xf_strequal(entry.text(), ""))
            {
                break;
            }
            if (xf_strequal(entry.text(), cmdname.text()))
            {
                startup_notify = false;
                break;
            }
        }
    }

    // Run command with startup notification
    if (usesn && startup_notify)
    {
        Display* xdisplay;
        SnDisplay* display;
        SnLauncherContext* context;
        Time timestamp;

        // Open display
        xdisplay = XOpenDisplay(NULL);
        if (xdisplay == NULL)
        {
            fprintf(stderr, _("Error: Can't open display\n"));
            int ret = chdir(startdir.text());
            if (ret < 0)
            {
                int errcode = errno;
                if (errcode)
                {
                    fprintf(stderr, _("Error: Can't enter folder %s: %s"), startdir.text(), strerror(errcode));
                }
                else
                {
                    fprintf(stderr, _("Error: Can't enter folder %s"), startdir.text());
                }
            }
            return -1;
        }

        // Message displayed in the task bar (if any)
        FXString message;
        message.format(_("Start of %s"), cmdname.text());

        // Initiate launcher context
        display = sn_display_new(xdisplay, NULL, NULL);
        context = sn_launcher_context_new(display, DefaultScreen(xdisplay));
        sn_launcher_context_set_name(context, message.text());
        sn_launcher_context_set_binary_name(context, cmdname.text());
        sn_launcher_context_set_description(context, message.text());
        sn_launcher_context_set_icon_name(context, cmdname.text());
        timestamp = gettimestamp(xdisplay);
        sn_launcher_context_initiate(context, "SwordFish", cmd.text(), timestamp);

        ret = launchCommand(cmd, context);
        sn_launcher_context_unref(context);
        sn_display_unref(display);
        XCloseDisplay(xdisplay);
    }
    // Run command without startup notification
    else
    {
        ret = launchCommand(cmd);
    }

    int launchResult = ret;
    // Go back to startup directory
    ret = chdir(startdir.text());
    if (ret < 0)
    {
        int errcode = errno;
        if (errcode)
        {
            fprintf(stderr, _("Error: Can't enter folder %s: %s"), startdir.text(), strerror(errcode));
        }
        else
        {
            fprintf(stderr, _("Error: Can't enter folder %s"), startdir.text());
        }

        return -1;
    }

    return launchResult;
}


#else  // Don't use startup notification

// Run a command and simulate a startup time
int runcmd(FXString cmd, FXString dir, FXString startdir)
{
    // Change to current directory
    int ret = chdir(dir.text());
    if (ret < 0)
    {
        int errcode = errno;
        if (errcode)
        {
            fprintf(stderr, _("Error: Can't enter folder %s: %s"), dir.text(), strerror(errcode));
        }
        else
        {
            fprintf(stderr, _("Error: Can't enter folder %s"), dir.text());
        }

        return -1;
    }

    int launchResult = launchCommand(cmd);

    // Go back to startup directory
    ret = chdir(startdir.text());
    if (ret < 0)
    {
        int errcode = errno;
        if (errcode)
        {
            fprintf(stderr, _("Error: Can't enter folder %s: %s"), startdir.text(), strerror(errcode));
        }
        else
        {
            fprintf(stderr, _("Error: Can't enter folder %s"), startdir.text());
        }

        return -1;
    }

    return launchResult;
}

#endif
