// File management class with progress dialog

#include "config.h"
#include "AppLogger.h"
#include "i18n.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <utime.h>
#if defined(linux)
#include <sys/statfs.h>
#endif

// For Sun compatibility
#ifdef __sun
#include <alloca.h>
#endif

#include <glib.h>
#include <memory>
#include <vector>


#include <fx.h>
#include <fxkeys.h>
#include <FXPNGIcon.h>
#include <FXUTF8Codec.h>

#include "xfedefs.h"
#include "icons.h"
#include "xfeutils.h"
#include "XFileExplorer.h"
#include "OverwriteBox.h"
#include "MessageBox.h"
#include "CommandWindow.h"
#include "File.h"
#include "TabButtons.h"
#include "Transfer.h"
#include "InputDialog.h"


// Delay before the progress bar should be shown (ms)
#define SHOW_PROGRESSBAR_DELAY      1000

// Progress dialog width
#define PROGRESSDIALOG_WIDTH        300

// Source size refresh delay  (ms)
#define SOURCESIZE_REFRESH_DELAY    500

// Copy speed compute delay  (ms)
#define COPYSPEED_DELAY    1000


// Global variables
extern FXMainWindow* mainWindow;

namespace
{
TransferStarter transferStarter = NULL;
}

void xf_setTransferStarter(TransferStarter starter)
{
    transferStarter = starter;
}

FXbool xf_hasTransferStarter()
{
    return transferStarter != NULL;
}

FXbool xf_startTransfer(FXWindow* owner, FXuint operation, const std::vector<TransferItem>& items)
{
    return transferStarter && transferStarter(owner, operation, items);
}

FXbool xf_startDroppedTransfer(FXWindow* owner, FXuint operation, const FXString& uris,
                               const FXString& directory, FXbool confirm)
{
    std::vector<FXString> sources;
    for (FXint start = 0; start < uris.length();)
    {
        FXint end = uris.find('\n', start);
        if (end < 0) end = uris.length();
        FXString uri = uris.mid(start, end - start);
        if (uri.right(1) == "\r") uri.trunc(uri.length() - 1);
        if (!uri.empty() && uri[0] != '#')
        {
            FXString path = FXURL::decode(FXURL::fileFromURL(uri));
            if (path.empty())
            {
                MessageBox::error(owner, BOX_OK, _("Error"), _("The drop contains an invalid file URL."));
                return false;
            }
            sources.push_back(path);
        }
        start = end + 1;
    }
    if (sources.empty())
    {
        MessageBox::error(owner, BOX_OK, _("Error"), _("The drop contains no files."));
        return false;
    }
    FXString target = directory;
    if (confirm)
    {
        FXString title = operation == COPY ? _("Copy") : _("Move");
        FXString message;
        if (sources.size() == 1)
            message = title + sources.front();
        else
            message.format(operation == COPY ? _("Copy %s files/folders from: %s") :
                                               _("Move %s files/folders from: %s"),
                           FXStringVal(sources.size()).text(), FXPath::directory(sources.front()).text());
        InputDialog dialog(owner, target, message, title, _("To:"), operation == COPY ? bigcopyicon : bigmoveicon);
        dialog.CursorEnd();
        if (!dialog.execute()) return false;
        target = dialog.getText();
        if (target.empty())
        {
            MessageBox::warning(owner, BOX_OK, _("Warning"), _("File name is empty, operation cancelled"));
            return false;
        }
        target = xf_filepath(target);
    }
    std::vector<TransferItem> items;
    for (const FXString& source : sources) items.push_back(TransferItem(source, target));
    return xf_startTransfer(owner, operation, items);
}


// Compute source size recursively (same algorithm as pathsize() function)
FXulong computeSourceSize(File* f, FXApp* app, const FXString& path, FXuint* nbfiles, FXuint* nbsubdirs,
                          FXulong* totalsize, FXButton* cancelButton, FXbool* cancelled)
{
    struct stat statbuf;
    struct dirent* dirp;
    DIR* dp;
    FXulong dsize;
    int ret;

    ret = xf_lstat(path.text(), &statbuf);
    if (ret < 0)
    {
        return 0;
    }

    dsize = (FXulong)statbuf.st_size;
    (*totalsize) += dsize;
    (*nbfiles)++;

    // Give cancel button an opportunity to be clicked and repaint, a few times per second
    if (f->uiUpdateDue())
    {
        f->setTotalSourceSize(*totalsize);
        if (cancelButton)
        {
            f->processProgressEvents();
        }
        if (app != NULL)
        {
            app->repaint();
        }
    }

    if (*cancelled)
    {
        return 0;
    }

    // Not a directory
    if (!S_ISDIR(statbuf.st_mode))
    {
        return dsize;
    }

    // Directory
    (*nbsubdirs)++;

    if ((dp = opendir(path.text())) == NULL)
    {
        return 0;
    }

    while ((dirp = readdir(dp)))
    {
        if (xf_strequal(dirp->d_name, ".") || xf_strequal(dirp->d_name, ".."))
        {
            continue;
        }
        // Recursive call
        FXString child = path;
        if (child.right(1) != PATHSEPSTRING) child += PATHSEPSTRING;
        child += dirp->d_name;
        dsize += computeSourceSize(f, app, child, nbfiles, nbsubdirs, totalsize, cancelButton, cancelled);
        if (*cancelled) break;
    }

    if (closedir(dp) < 0)
    {
        fprintf(stderr, _("Error: Can't close folder %s\n"), path.text());
    }

    f->setTotalSourceSize(*totalsize);
    f->rememberSourceSize(path, dsize);

    return dsize;
}


// Message Map
FXDEFMAP(File) FileMap[] =
{
    FXMAPFUNC(SEL_COMMAND, File::ID_CANCEL_BUTTON, File::onCmdCancel),
    FXMAPFUNC(SEL_COMMAND, DialogBox::ID_CANCEL, File::onDialogCancel),
    FXMAPFUNC(SEL_COMMAND, File::ID_PAUSE_BUTTON, File::onCmdPause),
    FXMAPFUNC(SEL_TIMEOUT, File::ID_PROGRESSBAR, File::onTimeout),
    FXMAPFUNC(SEL_TIMEOUT, File::ID_SOURCESIZE, File::onSourceSizeRefresh),
    FXMAPFUNC(SEL_TIMEOUT, File::ID_COPYSPEED, File::onCopySpeed),
    FXMAPFUNC(SEL_TIMEOUT, File::ID_COMPLETE, File::onComplete),
};


// Object implementation
FXIMPLEMENT(File, DialogBox, FileMap, ARRAYNUMBER(FileMap))

class TransferPathLabel : public FXLabel
{
private:
    FXString value;

    void fit()
    {
        if (!id()) return;
        FXString shown = value;
        shown.substitute('\n', ' ');
        shown.substitute('\r', ' ');
        FXint available = FXMAX(0, getWidth() - getPadLeft() - getPadRight());
        FXFont* font = getFont();
        if (font->getTextWidth(shown) > available)
        {
            FXint count = shown.count(), low = 0, high = count;
            while (low < high)
            {
                FXint keep = (low + high + 1) / 2;
                FXint left = (keep + 1) / 2, right = keep / 2;
                FXString candidate = shown.left(shown.offset(left)) + "..." +
                                     shown.right(shown.length() - shown.offset(count - right));
                if (font->getTextWidth(candidate) <= available) low = keep;
                else high = keep - 1;
            }
            FXint left = (low + 1) / 2, right = low / 2;
            shown = shown.left(shown.offset(left)) + "..." +
                    shown.right(shown.length() - shown.offset(count - right));
        }
        FXLabel::setText(shown);
    }

public:
    TransferPathLabel(FXComposite* parent, const FXString& text) :
        FXLabel(parent, text, NULL, JUSTIFY_LEFT | LAYOUT_FILL_X), value(text) {}

    virtual FXint getDefaultWidth() { return 1; }
    virtual void position(FXint x, FXint y, FXint width, FXint height)
    {
        FXLabel::position(x, y, width, height);
        fit();
    }
    virtual void resize(FXint width, FXint height)
    {
        FXLabel::resize(width, height);
        fit();
    }
    void setValue(const FXString& text)
    {
        value = text;
        setTipText(text);
        fit();
    }
};


// Construct object
File::File(FXWindow* owner, FXString title, const FXuint operation, const FXuint num) :
DialogBox(owner, title, DECOR_TITLE | DECOR_BORDER | DECOR_STRETCHABLE)
{
    initialize(owner, operation, num);
}


File::File(FXApp* app, FXString title, const FXuint operation, const FXuint num) :
DialogBox(app, title, DECOR_TITLE | DECOR_BORDER | DECOR_STRETCHABLE | DECOR_CLOSE), independent(true)
{
    initialize(this, operation, num);
}


void File::initialize(FXWindow* owner, const FXuint operation, const FXuint num)
{
    // Progress window
    FXint buttonPadding = independent ? 12 : PROGRESSDIALOG_WIDTH;
    FXPacker* buttons = new FXPacker(this, LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X, 0, 0, 10, 10,
                                     buttonPadding, buttonPadding, 5, 5);
    FXVerticalFrame* contents = new FXVerticalFrame(this, LAYOUT_SIDE_TOP | FRAME_NONE | LAYOUT_FILL_X | LAYOUT_FILL_Y);

    // Cancel Button
#if defined(linux)
    if (operation != MOUNT && operation != UNMOUNT)
#endif
    {
        FXComposite* buttonParent = buttons;
        if (independent)
        {
            buttonParent = new FXHorizontalFrame(buttons, LAYOUT_CENTER_X | PACK_UNIFORM_WIDTH);
            pauseButton = new FXButton(buttonParent, _("&Pause"), NULL, this, File::ID_PAUSE_BUTTON,
                                       FRAME_GROOVE, 0, 0, 0, 0, 20, 20);
        }
        cancelButton = new FXButton(buttonParent, _("&Cancel"), NULL, this, File::ID_CANCEL_BUTTON, FRAME_GROOVE |
                                    LAYOUT_CENTER_X, 0, 0, 0, 0, 20, 20);
        cancelButton->setFocus();
        cancelButton->addHotKey(KEY_Escape);        
    }
    cancelled = false;

    // Progress bar
    progressbar = NULL;

    // Progress bar colors (foreground and background)
    FXuint r, g, b, l;
    FXColor textcolor, textaltcolor;
    FXColor fgcolor = getApp()->reg().readColorEntry("SETTINGS", "pbarcolor", FXRGB(10, 36, 106));
    FXColor bgcolor = getApp()->reg().readColorEntry("SETTINGS", "backcolor", FXRGB(255, 255, 255));

    // Text color is white or black depending on the background luminance
    r = FXREDVAL(bgcolor);
    g = FXGREENVAL(bgcolor);
    b = FXBLUEVAL(bgcolor);
    l = (FXuint)(0.3 * r + 0.59 * g + 0.11 * b);
    if (l < 150)
    {
        textcolor = FXRGB(255, 255, 255);
    }
    else
    {
        textcolor = FXRGB(0, 0, 0);
    }

    // Alternate text color is white or black depending on the foreground luminance
    r = FXREDVAL(fgcolor);
    g = FXGREENVAL(fgcolor);
    b = FXBLUEVAL(fgcolor);
    l = (FXuint)(0.3 * r + 0.59 * g + 0.11 * b);
    if (l < 150)
    {
        textaltcolor = FXRGB(255, 255, 255);
    }
    else
    {
        textaltcolor = FXRGB(0, 0, 0);
    }

    // Copy suffix
    copysuffix = getApp()->reg().readStringEntry("OPTIONS", "copysuffix", DEFAULT_COPY_SUFFIX);
    copysuffix_pos = getApp()->reg().readUnsignedEntry("OPTIONS", "copysuffix_pos", 0);

    if (independent)
    {
        activityStatus = _("Preparing...");
        statusLabel = new FXLabel(contents, activityStatus, NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    }

    // Progress dialog depends on the file operation
    switch (operation)
    {
    case COPY:
        // Labels and progress bar
        uplabel = independent ? new TransferPathLabel(contents, _("Source:")) :
                               new FXLabel(contents, _("Source:"), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        downlabel = independent ? new TransferPathLabel(contents, _("Target:")) :
                                 new FXLabel(contents, _("Target:"), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        progressbar = new FXProgressBar(contents, NULL, 0, LAYOUT_FILL_X | TEXTFIELD_NORMAL | PROGRESSBAR_PERCENTAGE,
                                        0, 0, 0, 0, 0, 0, 0, 0); // 0 top and bottom pads
        progressbar->setBarColor(fgcolor);
        progressbar->setTextColor(textcolor);
        progressbar->setTextAltColor(textaltcolor);
        datatext = _("Copied data:");
        datalabel = independent ? new TransferPathLabel(contents, datatext) :
                                  new FXLabel(contents, datatext, NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        timetext = _("Remaining time:");
        timelabel = independent ? new TransferPathLabel(contents, timetext) :
                                  new FXLabel(contents, timetext, NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);

        // Timer on
        getApp()->addTimeout(this, File::ID_PROGRESSBAR, SHOW_PROGRESSBAR_DELAY);
        break;

    case MOVE:
        // Labels and progress bar
        uplabel = independent ? new TransferPathLabel(contents, _("Source:")) :
                               new FXLabel(contents, _("Source:"), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        downlabel = independent ? new TransferPathLabel(contents, _("Target:")) :
                                 new FXLabel(contents, _("Target:"), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        progressbar = new FXProgressBar(contents, NULL, 0, LAYOUT_FILL_X | TEXTFIELD_NORMAL | PROGRESSBAR_PERCENTAGE,
                                        0, 0, 0, 0, 0, 0, 0, 0); // 0 top and bottom pads
        progressbar->setBarColor(fgcolor);
        progressbar->setTextColor(textcolor);
        progressbar->setTextAltColor(textaltcolor);
        datatext = _("Moved data:");
        datalabel = independent ? new TransferPathLabel(contents, datatext) :
                                  new FXLabel(contents, datatext, NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        timetext = _("Remaining time:");
        timelabel = independent ? new TransferPathLabel(contents, timetext) :
                                  new FXLabel(contents, timetext, NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);

        // Timer on
        getApp()->addTimeout(this, File::ID_PROGRESSBAR, SHOW_PROGRESSBAR_DELAY);
        break;

    case DELETE:
        // Labels
        uplabel = new FXLabel(contents, _("Delete:"), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        downlabel = new FXLabel(contents, _("From:"), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        datalabel = NULL;
        timelabel = NULL;

        // Timer on
        getApp()->addTimeout(this, File::ID_PROGRESSBAR, SHOW_PROGRESSBAR_DELAY);
        break;

    case CHMOD:
        // Labels
        uplabel = new FXLabel(contents, _("Changing permissions..."), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        downlabel = new FXLabel(contents, _("File: "), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        datalabel = NULL;
        timelabel = NULL;
        break;

    case CHOWN:
        // Labels
        uplabel = new FXLabel(contents, _("Changing owner..."), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        downlabel = new FXLabel(contents, _("File: "), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        datalabel = NULL;
        timelabel = NULL;

        // Timer on
        getApp()->addTimeout(this, File::ID_PROGRESSBAR, SHOW_PROGRESSBAR_DELAY);
        break;

#if defined(linux)
    case MOUNT:
        // Labels
        uplabel = new FXLabel(contents, _("Mount file system..."), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        downlabel = new FXLabel(contents, _("Mounting folder:"), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        datalabel = NULL;
        timelabel = NULL;

        // Timer on
        getApp()->addTimeout(this, File::ID_PROGRESSBAR, SHOW_PROGRESSBAR_DELAY);
        break;

    case UNMOUNT:
        // Labels
        uplabel = new FXLabel(contents, _("Unmount file system..."), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        downlabel = new FXLabel(contents, _("Unmounting folder:"), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        datalabel = NULL;
        timelabel = NULL;

        // Timer on
        getApp()->addTimeout(this, File::ID_PROGRESSBAR, SHOW_PROGRESSBAR_DELAY);
        break;
#endif

    default: // Other : RENAME, SYMLINK, ARCHIVE, EXTRACT, PKG_INSTALL, PKG_UNINSTALL
        // Progress dialog not used
        uplabel = NULL;
        downlabel = NULL;
        datalabel = NULL;
        timelabel = NULL;
    }

    confirmOverwrite = getApp()->reg().readUnsignedEntry("OPTIONS", "confirm_overwrite", true);

    // Total read data
    totaldataread = 0;
    prevTotaldataread = 0;
    
    // Vector of copy speeds
    for (FXuint i = 0; i < NMAX_COPY_SPEED; i++)
    {
        vecCopyspeed[i] = 0.0;
    }
    numcopyspeed = 0;
    
    // Owner window
    ownerwin = owner;

    // Operation
    op = operation;

    // Number of selected items
    numsel = num;

    // Error message box
    mbox = new MessageBox(ownerwin, _("Error"), "", bigerroricon, BOX_OK_CANCEL | DECOR_TITLE | DECOR_BORDER);

    // Total data size
    totalsourcesize = 0;

    // Refresh source size label
    if (!independent && (op == COPY || op == MOVE))
    {
        getApp()->addTimeout(this, File::ID_SOURCESIZE, SOURCESIZE_REFRESH_DELAY);
    }
}


// Destructor
File::~File()
{
    if (op == COPY || op == MOVE)
    {
        getApp()->removeTimeout(this, File::ID_SOURCESIZE);
    }
    getApp()->removeTimeout(this, File::ID_PROGRESSBAR);
    getApp()->removeTimeout(this, File::ID_COPYSPEED);
    getApp()->removeTimeout(this, File::ID_COMPLETE);

    delete progressbar;
    delete mbox;
}


// Create and initialize
void File::create()
{
    DialogBox::create();
}


void File::startIndependent()
{
    getApp()->removeTimeout(this, File::ID_PROGRESSBAR);
    const char* const statuses[] =
    {
        N_("Preparing..."), N_("Waiting for another transfer..."), N_("Copying..."), N_("Moving..."),
        N_("Paused"), N_("Cancelling..."),
        N_("Copy complete"), N_("Move complete"), N_("Copy cancelled"), N_("Move cancelled"),
        N_("Copy finished with skipped items"), N_("Move finished with skipped items"),
        N_("Copy finished with errors"), N_("Move finished with errors")
    };
    FXint statusWidth = 0;
    for (const char* status : statuses)
    {
        statusLabel->setText(_(status));
        statusWidth = FXMAX(statusWidth, statusLabel->getDefaultWidth());
    }
    statusLabel->setText(_("Preparing..."));
    statusLabel->setLayoutHints(statusLabel->getLayoutHints() | LAYOUT_FIX_WIDTH);
    statusLabel->setWidth(statusWidth);
    setInfoLabel(datalabel, datatext + " " + xf_humansize("0"));
    setInfoLabel(timelabel, timetext + " --");
    FXint width = FXMIN(FXMAX(1, getRoot()->getWidth() - 20), FXMAX(500, getDefaultWidth() + 40));
    FXint height = FXMIN(FXMAX(1, getRoot()->getHeight() - 40), FXMAX(240, getDefaultHeight() + 20));
    resize(width, height);
    show(PLACEMENT_SCREEN);
    getApp()->forceRefresh();
    getApp()->repaint();
    getApp()->flush();
}


void File::beginTransfer()
{
    getApp()->removeTimeout(this, File::ID_SOURCESIZE);
    if (datalabel)
        setInfoLabel(datalabel, datatext + " " + xf_humansize("0") + " / " +
                                xf_humansize(FXStringVal(totalsourcesize).text()));
    setIndependentStatus(op == COPY ? _("Copying...") : _("Moving..."));
}


void File::setIndependentStatus(const FXString& text)
{
    FXbool changed = activityStatus != text;
    activityStatus = text;
    if (changed && statusLabel && !paused && !cancelled && !finished)
    {
        statusLabel->setText(text);
        getApp()->repaint();
    }
}


void File::setIndependentPaths(const FXString& source, const FXString& target)
{
    setPathLabel(uplabel, _("Source: ") + source);
    setPathLabel(downlabel, _("Target: ") + target);
}


void File::finishIndependent(FXbool errors)
{
    getApp()->removeTimeout(this, File::ID_SOURCESIZE);
    getApp()->removeTimeout(this, File::ID_COPYSPEED);
    finished = true;
    paused = false;
    FXString text;
    if (cancelled)
        text = op == COPY ? _("Copy cancelled") : _("Move cancelled");
    else if (errors || warned)
        text = op == COPY ? _("Copy finished with errors") : _("Move finished with errors");
    else if (skipped)
        text = op == COPY ? _("Copy finished with skipped items") : _("Move finished with skipped items");
    else
    {
        text = op == COPY ? _("Copy complete") : _("Move complete");
        if (progressbar) progressbar->setProgress(100);
        if (datalabel)
            setInfoLabel(datalabel, datatext + " " + xf_humansize(FXStringVal(totaldataread).text()) +
                                     " / " + xf_humansize(FXStringVal(totalsourcesize).text()));
        if (timelabel) setInfoLabel(timelabel, timetext + " " + xf_secondstotimestring(0));
    }
    if (cancelled || errors || warned || skipped)
    {
        if (datalabel)
            setInfoLabel(datalabel, datatext + " " + xf_humansize(FXStringVal(totaldataread).text()));
        if (timelabel) setInfoLabel(timelabel, timetext + " --");
    }
    if (statusLabel) statusLabel->setText(text);
    if (pauseButton) pauseButton->hide();
    if (cancelButton)
    {
        cancelButton->setText(_("&Close"));
        cancelButton->enable();
    }
    if (!errors && !warned) getApp()->addTimeout(this, File::ID_COMPLETE, 3000);
    getApp()->runModalWhileShown(this);
}


void File::processProgressEvents()
{
    if (!cancelButton) return;
    FXWindow* modal = independent ? static_cast<FXWindow*>(this) : cancelButton;
    getApp()->runModalWhileEvents(modal);
    if (independent) getApp()->repaint();
    while (independent && paused && !cancelled) getApp()->runOneEvent(true);
}


void File::notifyChange(const FXString& pathname)
{
    if (changeCallback) changeCallback(changeData, pathname);
}

void File::setPathLabel(FXLabel* label, const FXString& text)
{
    if (independent) static_cast<TransferPathLabel*>(label)->setValue(text);
    else label->setText(xf_truncline(text, MAX_MESSAGE_LENGTH));
}

void File::setInfoLabel(FXLabel* label, const FXString& text)
{
    if (independent) static_cast<TransferPathLabel*>(label)->setValue(text);
    else label->setText(text);
}

FXbool File::runHostCopy(const FXString& command, FXString& output)
{
    output.clear();
    int channel[2];
    if (pipe(channel) != 0)
    {
        output = strerror(errno);
        return false;
    }
    FXString hostCommand = "exec " + xf_hostcommand(command);
    pid_t child = fork();
    if (child == 0)
    {
        ::close(channel[0]);
        if (setpgid(0, 0) != 0 || dup2(channel[1], STDOUT_FILENO) < 0 ||
            dup2(channel[1], STDERR_FILENO) < 0)
            _exit(126);
        ::close(channel[1]);
        execl("/bin/sh", "sh", "-c", hostCommand.text(), static_cast<char*>(NULL));
        _exit(127);
    }
    int failure = errno;
    ::close(channel[1]);
    if (child < 0)
    {
        ::close(channel[0]);
        output = strerror(failure);
        return false;
    }
    setpgid(child, child);
    int flags = fcntl(channel[0], F_GETFL);
    if (flags < 0 || fcntl(channel[0], F_SETFL, flags | O_NONBLOCK) != 0)
    {
        failure = errno;
        kill(-child, SIGKILL);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
        ::close(channel[0]);
        output = strerror(failure);
        return false;
    }
    activeChild = child;
    int status = 0;
    FXbool exited = false;
    FXbool truncated = false;
    FXbool readFailed = false;
    FXlong cancelTime = 0;
    auto readOutput = [&]()
    {
        char buffer[4096];
        ssize_t count = 0;
        size_t drained = 0;
        while (drained < 256 * 1024 && (count = ::read(channel[0], buffer, sizeof(buffer))) > 0)
        {
            drained += static_cast<size_t>(count);
            FXint remaining = 65536 - output.length();
            if (remaining > 0)
                output += FXString(buffer, FXMIN(count, static_cast<ssize_t>(remaining)));
            if (count > remaining) truncated = true;
        }
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && !readFailed)
        {
            output += FXString("\n") + strerror(errno);
            readFailed = true;
            kill(-child, SIGKILL);
        }
    };
    while (!exited)
    {
        readOutput();
        // Keep the group leader's PID reserved until any cancelled descendants are stopped.
        siginfo_t childInfo = {};
        int result = waitid(P_PID, child, &childInfo, WEXITED | WNOHANG | WNOWAIT);
        if (result == 0 && childInfo.si_pid == child)
        {
            if (cancelled && kill(-child, SIGKILL) != 0 && errno != ESRCH)
                appLog(APP_LOG_ERRORS, "transfer-cancel-failed", strerror(errno));
            pid_t reaped;
            do
            {
                reaped = waitpid(child, &status, 0);
            } while (reaped < 0 && errno == EINTR);
            if (reaped < 0)
            {
                output += FXString("\n") + strerror(errno);
                status = -1;
            }
            exited = true;
        }
        else if (result < 0 && errno != EINTR)
        {
            output += FXString("\n") + strerror(errno);
            status = -1;
            exited = true;
        }
        if (!exited)
        {
            processProgressEvents();
            if (cancelled)
            {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                FXlong time = static_cast<FXlong>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
                if (!cancelTime) cancelTime = time;
                else if (time - cancelTime >= 1000) kill(-child, SIGKILL);
            }
            struct pollfd descriptor = {channel[0], POLLIN, 0};
            if (poll(&descriptor, 1, 50) < 0 && errno != EINTR && !readFailed)
            {
                output += FXString("\n") + strerror(errno);
                readFailed = true;
                kill(-child, SIGKILL);
            }
        }
    }
    readOutput();
    activeChild = -1;
    ::close(channel[0]);
    output.trim();
    if (truncated) output += _("\n[Command output truncated]");
    if (!readFailed && status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
    if (status >= 0 && WIFEXITED(status))
        output += FXString("\n") + _("Command exited with status ") + FXStringVal(WEXITSTATUS(status));
    else if (status >= 0 && WIFSIGNALED(status))
        output += FXString("\n") + _("Command terminated by signal ") + FXStringVal(WTERMSIG(status));
    return false;
}


// Progress labels, the cancel button and repaints cost X server round trips; doing them for
// every small file made copies several times slower, so refresh them about ten times a second
FXbool File::uiUpdateDue(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    FXlong ms = (FXlong)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    if (lastUiUpdate != 0 && ms - lastUiUpdate < 100)
    {
        return false;
    }
    lastUiUpdate = ms;
    return true;
}


// Force timeout for progress dialog (used before opening confirmation or error dialogs)
void File::forceTimeout(void)
{
    if (independent) return;
    getApp()->removeTimeout(this, File::ID_PROGRESSBAR);
    hide();
    getApp()->forceRefresh();
    getApp()->flush();
}


// Restart timeout for progress dialog  (used after closing confirmation or error dialogs)
void File::restartTimeout(void)
{
    if (!independent) getApp()->addTimeout(this, File::ID_PROGRESSBAR, SHOW_PROGRESSBAR_DELAY);
}


// Read bytes
FXlong File::fullread(int fd, FXuchar* ptr, FXlong len)
{
    FXlong nread;

#ifdef EINTR
    do
    {
        nread = read(fd, ptr, len);
    }
    while (nread < 0 && errno == EINTR);
#else
    nread = read(fd, ptr, len);
#endif
    return nread;
}


// Write bytes
FXlong File::fullwrite(int fd, const FXuchar* ptr, FXlong len)
{
    FXlong nwritten, ntotalwritten = 0;

    while (len > 0)
    {
        nwritten = write(fd, ptr, len);
        if (nwritten < 0)
        {
#ifdef EINTR
            if (errno == EINTR)
            {
                continue;
            }
#endif
            return -1;
        }
        if (nwritten == 0)
        {
            errno = EIO;
            return -1;
        }
        ntotalwritten += nwritten;
        ptr += nwritten;
        len -= nwritten;
    }
    return ntotalwritten;
}


static FXuint conflictKind(mode_t mode)
{
    return S_ISDIR(mode) ? 0 : S_ISREG(mode) ? 1 : S_ISLNK(mode) ? 2 : 3;
}


static FXString numberedTarget(const FXString& target, FXbool directory)
{
    FXString name = FXPath::name(target), extension;
    FXint dot = directory ? -1 : name.rfind('.');
    if (dot > 0)
    {
        extension = name.mid(dot, name.length() - dot);
        name = name.left(dot);
        if (name.length() > 4 && name.right(4).lower() == ".tar")
        {
            extension = name.right(4) + extension;
            name = name.left(name.length() - 4);
        }
    }
    FXString result;
    FXuint number = 0;
    do
    {
        result = FXPath::directory(target) + PATHSEPSTRING + name +
                 " (" + FXStringVal(++number) + ")" + extension;
    }
    while (xf_existfile(result));
    return result;
}


int File::resolveConflict(const FXString& source, FXString& destination, FXbool merge,
                          FXbool restart_timeout, FXbool confirm)
{
    struct stat srcinfo, dstinfo;
    if (xf_lstat(source.text(), &srcinfo) != 0)
    {
        int errcode = errno;
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Can't read source %s: %s"),
                          source.text(), strerror(errcode));
        return -1;
    }
    if (xf_lstat(destination.text(), &dstinfo) != 0)
    {
        int errcode = errno;
        if (errcode == ENOENT) return OVWBOX_CLICKED_OVERWRITE;
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Can't read destination %s: %s"),
                          destination.text(), strerror(errcode));
        return -1;
    }
    if (srcinfo.st_dev == dstinfo.st_dev && srcinfo.st_ino == dstinfo.st_ino)
    {
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Destination %s is identical to source"),
                          destination.text());
        return -1;
    }

    // Cache decisions by both item types: merging folders never permits replacing their files.
    FXuint kind = conflictKind(srcinfo.st_mode) * 4 + conflictKind(dstinfo.st_mode);
    FXuint answer = conflictAnswers[kind];
    if (!confirmOverwrite || !confirm)
    {
        answer = OVWBOX_CLICKED_OVERWRITE;
    }
    FXString renamed;
    if (answer == OVWBOX_CLICKED_CANCEL)
    {
        renamed = numberedTarget(destination, S_ISDIR(srcinfo.st_mode));
        forceTimeout();
        OverwriteBox* dialog = new OverwriteBox(ownerwin, source, destination, srcinfo, dstinfo, renamed,
                                                merge && S_ISDIR(srcinfo.st_mode) && S_ISDIR(dstinfo.st_mode));
        answer = dialog->execute(PLACEMENT_OWNER);
        delete dialog;
        if (restart_timeout) restartTimeout();
        switch (answer)
        {
        case OVWBOX_CLICKED_OVERWRITE_ALL:
            answer = conflictAnswers[kind] = OVWBOX_CLICKED_OVERWRITE;
            break;
        case OVWBOX_CLICKED_SKIP_ALL:
            answer = conflictAnswers[kind] = OVWBOX_CLICKED_SKIP;
            break;
        case OVWBOX_CLICKED_RENAME_ALL:
            answer = conflictAnswers[kind] = OVWBOX_CLICKED_RENAME;
            break;
        }
    }
    if (answer == OVWBOX_CLICKED_RENAME)
    {
        // Recheck after the modal dialog, which may have processed other file operations.
        destination = numberedTarget(destination, S_ISDIR(srcinfo.st_mode));
        return OVWBOX_CLICKED_OVERWRITE;
    }
    if (answer == OVWBOX_CLICKED_CANCEL)
    {
        cancelled = true;
    }
    if (answer == OVWBOX_CLICKED_SKIP) skipped = true;
    return answer;
}


// Compute the MD5 digest of a file, reading it from storage rather than the page cache when possible
// Return true on success, false if cancelled, -2 on read error (errcode is set)
int File::md5file(const FXString& path, FXString& digest, int& errcode)
{
    errcode = 0;
    int fd = ::open(path.text(), O_RDONLY);
    if (fd < 0)
    {
        errcode = errno;
        return -2;
    }
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);

    std::unique_ptr<GChecksum, decltype(&g_checksum_free)> sum(g_checksum_new(G_CHECKSUM_MD5), g_checksum_free);
    std::vector<FXuchar> buffer(BUFFER_COPY_SIZE);
    while (1)
    {
        errno = 0;
        FXlong nread = File::fullread(fd, buffer.data(), buffer.size());
        if (nread < 0)
        {
            errcode = errno ? errno : EIO;
            ::close(fd);
            return -2;
        }
        if (nread == 0)
        {
            break;
        }
        g_checksum_update(sum.get(), buffer.data(), nread);

        if (uiUpdateDue())
        {
            if (cancelButton)
            {
                processProgressEvents();
            }
            if (uplabel)
            {
                setPathLabel(uplabel, _("Verifying: ") + path);
            }
            getApp()->repaint();
        }
        if (cancelled)
        {
            ::close(fd);
            return false;
        }
    }
    ::close(fd);
    digest = g_checksum_get_string(sum.get());
    return true;
}


static FXuint showFileError(MessageBox* box, FXbool independent, const FXString& text)
{
    box->setText(text);
    if (independent)
    {
        // The worker creates this reusable dialog before its error text is known.
        FXint width = FXMIN(box->getDefaultWidth(), FXMAX(1, box->getRoot()->getWidth() - 20));
        FXint height = FXMIN(box->getDefaultHeight(), FXMAX(1, box->getRoot()->getHeight() - 40));
        box->resize(width, height);
    }
    return box->execute(PLACEMENT_OWNER);
}


// Compare a copied file with its source. sourcedigest may be empty, in which case the source is read again.
// A copy that does not match is removed so it cannot be mistaken for a good one.
// Return true when identical, false if cancelled, -2 when verification failed
int File::verifycopy(const FXString& source, const FXString& target, const FXString& sourcedigest)
{
    FXString srcdigest = sourcedigest, dstdigest;
    int errcode = 0;
    int ret = true;
    FXString failedpath;

    if (srcdigest.empty())
    {
        ret = md5file(source, srcdigest, errcode);
        failedpath = source;
    }
    if (ret == true)
    {
        ret = md5file(target, dstdigest, errcode);
        failedpath = target;
    }
    if (ret == false)
    {
        return false;
    }

    FXString str;
    if (ret == -2)
    {
        str.format(_("Can't verify file %s: %s"), failedpath.text(), strerror(errcode));
    }
    else if (srcdigest != dstdigest)
    {
        str.format(_("Verification failed: %s does not match %s\nThe damaged copy has been removed."),
                   target.text(), source.text());
        ret = -2;
    }
    else
    {
        return true;
    }

    appLog(APP_LOG_ERRORS, "copy-verification-failed", source + " -> " + target + ": " + str);
    warned = true;
    if (srcdigest != dstdigest && !dstdigest.empty())
    {
        ::unlink(target.text());
        notifyChange(target);
    }
    forceTimeout();
    FXuint answer = showFileError(mbox, independent, str);
    restartTimeout();
    if (answer == BOX_CLICKED_CANCEL)
    {
        cancelled = true;
        return false;
    }
    return -2;
}


// Copy ordinary file
// Return  0 to allow displaying an error message in the calling function
// Return -1 to prevent displaying an error message in the calling function
// Return -2 when an error has occurred during the copy
int File::copyfile(const FXString& source, const FXString& target, const FXString& hsourcesize,
                   const FXulong sourcesize, const FXulong tstart, const FXbool preserve_date)
{
    FXString destfile;
    FXuchar buffer[BUFFER_COPY_SIZE];
    struct stat info;
    struct utimbuf timbuf;
    FXlong nread, nwritten;
    int src, dst;
    int ok = false;
    
    FXbool warn = getApp()->reg().readUnsignedEntry("OPTIONS", "preserve_date_warn", true);
    FXbool verify = getApp()->reg().readUnsignedEntry("OPTIONS", "verify_copy", false);

    // Needed by onCopySpeed()
    copyspeedSourcesize = sourcesize;
    copyspeedHsourcesize = hsourcesize;

    if ((src = ::open(source.text(), O_RDONLY)) >= 0)
    {
        if (xf_stat(source.text(), &info) == 0)
        {
            // If destination is a directory
            if (xf_isdirectory(target))
            {
                destfile = target + PATHSEPSTRING + FXPath::name(source);
            }
            else
            {
                destfile = target;
            }

            // File is on an MTP mount
            if (destfile.contains("mtp:host="))
            {
                // Force timeout checking for progress dialog
                checkProgressBarTimeout();

                FXulong mtpSize = 0;
                if (xf_lstat(source.text(), &info) == 0)
                {
                    mtpSize = (FXulong)info.st_size;
                    if (!independent) totaldataread += mtpSize;
                }

                // Start copy speed timeout if not already done
                if (!getApp()->hasTimeout(this, File::ID_COPYSPEED))
                {
                    getApp()->addTimeout(this, File::ID_COPYSPEED, COPYSPEED_DELAY);
                }

                if (progressbar)
                {
                    // Percentage
                    int pct = (sourcesize == 0 ? 0 : (100.0 * totaldataread) / sourcesize);
                    progressbar->setProgress(pct);
                }
                if (independent && datalabel && !numcopyspeed)
                    setInfoLabel(datalabel, datatext + " " +
                                 xf_humansize(FXStringVal(totaldataread).text()) + " / " + hsourcesize);

                // Give cancel button an opportunity to be clicked
                if (cancelButton)
                {
                    processProgressEvents();
                }

                // Set labels for progress dialog
                FXString label = _("Source: ") + source;
                if (uplabel)
                {
                    setPathLabel(uplabel, label);
                }
                label = _("Target: ") + target;
                if (downlabel)
                {
                    setPathLabel(downlabel, label);
                }
                getApp()->repaint();

                // If cancel button was clicked, close files and return
                if (cancelled)
                {
                    ::close(src);

                    return false;
                }

                // Copy using gio command (regular copy does not work on MTP mounts)
                notifyChange(destfile);
                FXString cmd = "gio copy " + xf_quote(source) + " " + xf_quote(destfile);
                FXString ret;
                FXbool copied;
                if (independent)
                    copied = runHostCopy(cmd, ret);
                else
                {
                    ret = xf_getcommandoutput(cmd);
                    copied = ret.empty();
                }
                notifyChange(destfile);

                // An error has occurred
                if (cancelled)
                {
                    ::close(src);
                    return false;
                }
                if (!copied)
                {
                    forceTimeout();
                    FXString str;
                    str.format(_("Can't copy file %s: %s"), target.text(), ret.text());
                    FXuint answer = showFileError(mbox, independent, str);
                    if (answer == BOX_CLICKED_CANCEL)
                    {
                        cancelled = true;
                        ::close(src);
                        
                        return false;
                    }
                    else
                    {
                        ok = -2; // An error has occurred
                    }
                }
                else
                {
                    if (independent)
                    {
                        totaldataread += mtpSize;
                        if (progressbar)
                            progressbar->setProgress(sourcesize == 0 ? 0 :
                                                     FXMIN(100.0, (100.0 * totaldataread) / sourcesize));
                    }
                    ok = verify ? verifycopy(source, destfile, "") : true;
                }
            }
    
            // File is on a regular file system
            else
            {
                // Start copy speed timeout if not already done
                if (!getApp()->hasTimeout(this, File::ID_COPYSPEED))
                {
                    getApp()->addTimeout(this, File::ID_COPYSPEED, COPYSPEED_DELAY);
                }

                // Copy file block by block
                if ((dst = ::open(destfile.text(), O_WRONLY | O_CREAT | O_TRUNC, info.st_mode)) >= 0)
                {
                    notifyChange(destfile);
                    int error = false;
                    std::unique_ptr<GChecksum, decltype(&g_checksum_free)> srcsum(
                        verify ? g_checksum_new(G_CHECKSUM_MD5) : NULL, g_checksum_free);

                    while (1)
                    {
                        errno = 0;
                        nread = File::fullread(src, buffer, sizeof(buffer));

                        int errcode = errno;
                        if (nread < 0)
                        {
                            forceTimeout();

                            FXString str;
                            if (errcode)
                            {
                                str.format(_("Can't copy file %s: %s"), target.text(), strerror(errcode));
                            }
                            else
                            {
                                str.format(_("Can't copy file %s"), target.text());
                            }
                            FXuint answer = showFileError(mbox, independent, str);

                            restartTimeout();
                            if (answer == BOX_CLICKED_CANCEL)
                            {
                                ::close(dst);
                                ::close(src);
                                notifyChange(destfile);
                                cancelled = true;

                                return false;
                            }
                            else
                            {
                                error = true; // An error has occurred
                                break;
                            }
                        }
                        if (nread == 0)
                        {
                            break;
                        }

                        if (srcsum)
                        {
                            g_checksum_update(srcsum.get(), buffer, nread);
                        }

                        // Set percentage value for progress dialog
                        totaldataread += nread;
                        FXbool update = uiUpdateDue();
                        if (update)
                        {
                            if (progressbar)
                            {
                                // Percentage
                                int pct = (sourcesize == 0 ? 0 : (100.0 * totaldataread) / sourcesize);
                                progressbar->setProgress(pct);
                            }
                            if (independent && datalabel && !numcopyspeed)
                                setInfoLabel(datalabel, datatext + " " +
                                             xf_humansize(FXStringVal(totaldataread).text()) + " / " + hsourcesize);

                            // Give cancel button an opportunity to be clicked
                            if (cancelButton)
                            {
                                processProgressEvents();
                            }

                            // Set labels for progress dialog
                            FXString label = _("Source: ") + source;
                            if (uplabel)
                            {
                                setPathLabel(uplabel, label);
                            }
                            label = _("Target: ") + target;
                            if (downlabel)
                            {
                                setPathLabel(downlabel, label);
                            }
                            getApp()->repaint();
                        }

                        // If cancel button was clicked, close files and return
                        if (cancelled)
                        {
                            ::close(dst);
                            ::close(src);
                            notifyChange(destfile);

                            return false;
                        }
                        errno = 0;
                        nwritten = File::fullwrite(dst, buffer, nread);
                        errcode = errno;
                        if (nwritten < 0)
                        {
                            forceTimeout();

                            FXString str;
                            if (errcode)
                            {
                                str.format(_("Can't copy file %s: %s"), target.text(), strerror(errcode));
                            }
                            else
                            {
                                str.format(_("Can't copy file %s"), target.text());
                            }
                            FXuint answer = showFileError(mbox, independent, str);

                            restartTimeout();
                            if (answer == BOX_CLICKED_CANCEL)
                            {
                                ::close(dst);
                                ::close(src);
                                notifyChange(destfile);
                                cancelled = true;
                                
                                return false;
                            }
                            else
                            {
                                error = true; // An error has occurred
                                break;
                            }
                        }
                        if (update) notifyChange(destfile);

                    }

                    // An error has occurred
                    if (error)
                    {
                        ok = -2;
                    }
                    else
                    {
                        ok = true;
                    }

                    if (ok == true && srcsum && fdatasync(dst) != 0)
                    {
                        int errcode = errno;
                        ::close(dst);
                        ::close(src);
                        notifyChange(destfile);
                        forceTimeout();
                        FXString str;
                        str.format(_("Can't copy file %s: %s"), target.text(), strerror(errcode));
                        FXuint answer = showFileError(mbox, independent, str);
                        restartTimeout();
                        if (answer == BOX_CLICKED_CANCEL)
                        {
                            cancelled = true;
                            return false;
                        }
                        return -2;
                    }
                    ::close(dst);
                    notifyChange(destfile);

                    FXbool verifyfailed = false;
                    if (ok == true && srcsum)
                    {
                        ok = verifycopy(source, destfile, g_checksum_get_string(srcsum.get()));
                        if (ok == false)
                        {
                            ::close(src);
                            return false;
                        }
                        verifyfailed = (ok != true);
                    }

                    // Keep original date if asked
                    if (preserve_date && !verifyfailed)
                    {
                        timbuf.actime = info.st_atime;
                        timbuf.modtime = info.st_mtime;
                        errno = 0;
                        int rc = utime(destfile.text(), &timbuf);
                        int errcode = errno;
                        if (warn && rc)
                        {
                            warned = true;
                            forceTimeout();

                            FXString str;
                            if (errcode)
                            {
                                str.format(_("Can't preserve date when copying file %s : %s"), target.text(),
                                           strerror(errcode));
                            }
                            else
                            {
                                str.format(_("Can't preserve date when copying file %s"), target.text());
                            }
                            FXuint answer = showFileError(mbox, independent, str);

                            restartTimeout();
                            if (answer == BOX_CLICKED_CANCEL)
                            {
                                ::close(src);
                                cancelled = true;
                                
                                return false;
                            }
                        }
                    }
                }

#if defined(linux)
                // If source file is on a ISO9660 file system (CD or DVD, thus read-only)
                // then add to the target the write permission for the user
                if (ok)
                {
                    struct statfs fs;
                    if ((statfs(source.text(), &fs) == 0) && (fs.f_type == 0x9660))
                    {
                        ::chmod(target.text(), info.st_mode | S_IWUSR);
                    }
                }
#endif
            }
        }
        ::close(src);
    }
    // File cannot be opened
    else
    {
        forceTimeout();
        int errcode = errno;

        FXString str;
        if (errcode)
        {
            str.format(_("Can't copy file %s: %s"), target.text(), strerror(errcode));
        }
        else
        {
            str.format(_("Can't copy file %s"), target.text());
        }
        FXuint answer = showFileError(mbox, independent, str);

        restartTimeout();
        if (answer == BOX_CLICKED_CANCEL)
        {
            cancelled = true;

            return false;
        }
        ok = -1;        // Prevent displaying an error message
                        // in the calling function
    }

    return ok;
}


// Copy directory
int File::copydir(const FXString& source, const FXString& target, const FXString& hsourcesize, const FXulong sourcesize,
                  const FXulong tstart, struct stat& parentinfo, inodelist* inodes, const FXbool preserve_date,
                  const FXbool moving)
{
    DIR* dirp;
    struct dirent* dp;
    struct stat linfo;
    struct utimbuf timbuf;
    inodelist* in, inode;
    FXString destfile, oldchild, newchild;

    FXbool warn = getApp()->reg().readUnsignedEntry("OPTIONS", "preserve_date_warn", true);

    // Destination file
    destfile = target;

    // See if visited this inode already
    for (in = inodes; in; in = in->next)
    {
        if (in->st_ino == parentinfo.st_ino)
        {
            return true;
        }
    }

    // Try make directory, if none exists yet
    int made = ::mkdir(destfile.text(), parentinfo.st_mode | S_IWUSR);
    if (made != 0 && errno != EEXIST)
    {
        return false;
    }
    if (made == 0) notifyChange(destfile);

    // Can we stat it
    if ((xf_lstat(destfile.text(), &linfo) != 0) || !S_ISDIR(linfo.st_mode))
    {
        return false;
    }

    // Try open directory to copy
    dirp = opendir(source.text());
    if (!dirp)
    {
        return false;
    }

    // Add this to the list
    inode.st_ino = linfo.st_ino;
    inode.next = inodes;

    // Copy stuff
    while ((dp = readdir(dirp)) != NULL)
    {
        if ((dp->d_name[0] != '.') || ((dp->d_name[1] != '\0') && ((dp->d_name[1] != '.') || (dp->d_name[2] != '\0'))))
        {
            oldchild = source;
            if (!ISPATHSEP(oldchild[oldchild.length() - 1]))
            {
                oldchild.append(PATHSEP);
            }
            oldchild.append(dp->d_name);
            newchild = destfile;
            if (!ISPATHSEP(newchild[newchild.length() - 1]))
            {
                newchild.append(PATHSEP);
            }
            newchild.append(dp->d_name);
            int result = moving ? fmove(oldchild, newchild, hsourcesize, sourcesize, tstart, true) :
                                  copyrec(oldchild, newchild, hsourcesize, sourcesize, tstart, &inode, preserve_date);
            if (result != true)
            {
                if (result < 0)
                {
                    closedir(dirp);
                    return result;
                }
                // If the cancel button was pressed
                if (cancelled)
                {
                    closedir(dirp);
                    return false;
                }
                // Or a permission problem occured
                else
                {
                    warned = true;
                    FXString str;
                    if (xf_isdirectory(oldchild))
                    {
                        str.format(moving ? _("Can't move folder %s : Permission denied") :
                                           _("Can't copy folder %s : Permission denied"), oldchild.text());
                    }
                    else
                    {
                        str.format(moving ? _("Can't move file %s : Permission denied") :
                                           _("Can't copy file %s : Permission denied"), oldchild.text());
                    }
                    forceTimeout();
                    FXuint answer = showFileError(mbox, independent, str);

                    restartTimeout();
                    if (answer == BOX_CLICKED_CANCEL)
                    {
                        closedir(dirp);
                        cancelled = true;
                        return false;
                    }
                }
            }
        }
    }

    // Close directory
    closedir(dirp);

    // Keep original date if asked
    if (preserve_date && !target.contains("mtp:host="))  // Don't report error if MTP mount
    {
        if (moving || xf_lstat(source.text(), &linfo) == 0)
        {
            timbuf.actime = moving ? parentinfo.st_atime : linfo.st_atime;
            timbuf.modtime = moving ? parentinfo.st_mtime : linfo.st_mtime;
            errno = 0;
            int rc = utime(destfile.text(), &timbuf);
            int errcode = errno;
            if (warn && rc)
            {
                warned = true;
                forceTimeout();
                FXString str;
                if (errcode)
                {
                    str.format(_("Can't preserve date when copying folder %s: %s"), target.text(), strerror(errcode));
                }
                else
                {
                    str.format(_("Can't preserve date when copying folder %s"), target.text());
                }
                FXuint answer = showFileError(mbox, independent, str);

                restartTimeout();
                if (answer == BOX_CLICKED_CANCEL)
                {
                    cancelled = true;
                    return false;
                }
            }
        }
    }

    if (moving)
    {
        // Skipped children remain at the source. Never recursively delete a merged source folder.
        if (::rmdir(source.text()) == 0)
        {
            notifyChange(source);
            xf_cleanuptrashinfo(source);
        }
        else if (errno == ENOTEMPTY || errno == EEXIST)
        {
            if (independent) skipped = true;
        }
        else
        {
            int errcode = errno;
            forceTimeout();
            MessageBox::error(this, BOX_OK, _("Error"), _("Can't remove source folder %s: %s"),
                              source.text(), strerror(errcode));
            return -1;
        }
    }

    // Success
    return true;
}


// Recursive copy
int File::copyrec(const FXString& source, const FXString& target, const FXString& hsourcesize, const FXulong sourcesize,
                  const FXulong tstart, inodelist* inodes, const FXbool preserve_date, const FXbool confirm)
{
    struct stat linfo1, linfo2;
    FXString destination = target;

    if (cancelled) return false;
    int answer = resolveConflict(source, destination, true, true, confirm);
    if (answer == OVWBOX_CLICKED_SKIP) return true;
    if (answer != OVWBOX_CLICKED_OVERWRITE) return answer;

    // Source file or directory does not exist
    if (xf_lstat(source.text(), &linfo1) != 0)
    {
        return false;
    }

    // Only two real directories merge; links are replaced without following them.
    if (xf_lstat(destination.text(), &linfo2) == 0)
    {
        if (!(S_ISDIR(linfo1.st_mode) && S_ISDIR(linfo2.st_mode)))
        {
            int result = File::remove(destination);
            if (result != true)
            {
                return result;
            }
        }
    }

    // Source is directory: copy recursively
    if (S_ISDIR(linfo1.st_mode))
    {
        return File::copydir(source, destination, hsourcesize, sourcesize, tstart, linfo1, inodes, preserve_date);
    }

    // Source is regular file: copy block by block
    if (S_ISREG(linfo1.st_mode))
    {
        return File::copyfile(source, destination, hsourcesize, sourcesize, tstart, preserve_date);
    }

    // Source is fifo: make a new one
    if (S_ISFIFO(linfo1.st_mode))
    {
        if (mkfifo(destination.text(), linfo1.st_mode) != 0) return false;
        notifyChange(destination);
        return true;
    }

    // Source is device: make a new one
    if (S_ISBLK(linfo1.st_mode) || S_ISCHR(linfo1.st_mode) || S_ISSOCK(linfo1.st_mode))
    {
        if (mknod(destination.text(), linfo1.st_mode, linfo1.st_rdev) != 0) return false;
        notifyChange(destination);
        return true;
    }

    // Source is symbolic link: make a new one
    if (S_ISLNK(linfo1.st_mode))
    {
        FXString lnkfile = xf_readlink(source);
        if (::symlink(lnkfile.text(), destination.text()) != 0) return false;
        notifyChange(destination);
        return true;
    }

    // This shouldn't happen
    return false;
}


// Copy file (with progress dialog)
// Return  0 to allow displaying an error message in the calling function
// Return -1 to prevent displaying an error message in the calling function
int File::copy(const FXString& source, const FXString& target, const FXString& hsourcesize, const FXulong sourcesize,
               const FXulong tstart, const FXbool confirm_dialog, const FXbool preserve_date)
{
    FXString targetfile;

    // Source doesn't exist
    if (!xf_existfile(source))
    {
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Source %s doesn't exist"), source.text());
        return -1;
    }

    // Source and target are identical
    if (xf_isidentical(target, source))
    {
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Destination %s is identical to source"), target.text());
        return -1;
    }

    // Source path is included into target path
    FXString str = source + PATHSEPSTRING;
    if (target.left(str.length()) == str)
    {
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Target %s is a sub-folder of source"), target.text());
        return -1;
    }

    // Target is an existing directory
    if (xf_isdirectory(target))
    {
        targetfile = target + PATHSEPSTRING + FXPath::name(source);
    }
    else
    {
        targetfile = target;
    }

    // Source and target are identical => add a suffix to the name
    if (xf_isidentical(source, targetfile))
    {
        FXString pathname = xf_cleanpath(targetfile);
        targetfile = xf_buildcopyname(pathname, xf_isdirectory(pathname), copysuffix, copysuffix_pos); // Remove trailing / if any
    }
    // Source and target file are identical
    if (xf_isidentical(targetfile, source))
    {
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Destination %s is identical to source"), targetfile.text());
        return -1;
    }

    // Copy file or directory
    return File::copyrec(source, targetfile, hsourcesize, sourcesize, tstart, NULL, preserve_date, confirm_dialog);
}


// Remove file or directory (with progress dialog)
// Return  0 to allow displaying an error message in the calling function
// Return -1 to prevent displaying an error message in the calling function
int File::remove(const FXString& file)
{
    FXString dirname;
    struct stat linfo;
    static FXbool ISDIR = false; // Caution! ISDIR is common to all File instances, is that we want?

    if (xf_lstat(file.text(), &linfo) == 0)
    {
        // It is a directory
        if (S_ISDIR(linfo.st_mode))
        {
            DIR* dirp = opendir(file.text());
            if (dirp)
            {
                struct dirent* dp;
                FXString child;

                // Used to display only one progress dialog when deleting a directory
                ISDIR = true;

                // Force timeout checking for progress dialog
                checkProgressBarTimeout();

                if (uiUpdateDue())
                {
                    // Give cancel button an opportunity to be clicked
                    if (cancelButton)
                    {
                        processProgressEvents();
                    }

                    // Set labels for progress dialog
                    FXString label = _("Delete folder: ") + file;
                    if (uplabel)
                    {
                        setPathLabel(uplabel, label);
                    }
                    dirname = FXPath::directory(FXPath::absolute(file));
                    label = _("From: ") + dirname;
                    if (downlabel)
                    {
                        setPathLabel(downlabel, label);
                    }
                    getApp()->repaint();
                }

                // If cancel button was clicked, return
                if (cancelled)
                {
                    closedir(dirp);
                    return false;
                }

                while ((dp = readdir(dirp)) != NULL)
                {
                    if ((dp->d_name[0] != '.') ||
                        ((dp->d_name[1] != '\0') && ((dp->d_name[1] != '.') || (dp->d_name[2] != '\0'))))
                    {
                        child = file;
                        if (!ISPATHSEP(child[child.length() - 1]))
                        {
                            child.append(PATHSEP);
                        }
                        child.append(dp->d_name);
                        if (!File::remove(child))
                        {
                            closedir(dirp);
                            return false;
                        }
                    }
                }
                closedir(dirp);
            }
            if (rmdir(file.text()) == -1)
            {
                int errcode = errno;
                forceTimeout();

                FXString str;
                if (errcode)
                {
                    str.format(_("Can't delete folder %s: %s"), file.text(), strerror(errcode));
                }
                else
                {
                    str.format(_("Can't delete folder %s"), file.text());
                }
                FXuint answer = showFileError(mbox, independent, str);

                restartTimeout();
                if (answer == BOX_CLICKED_CANCEL)
                {
                    cancelled = true;
                    return false;
                }
                return -1; // To prevent displaying an error message
                // in the calling function
            }
            else
            {
                notifyChange(file);
                xf_cleanuptrashinfo(file);
                return true;
            }
        }
        else
        {
            // If it was not a directory
            if (!ISDIR)
            {
                // Force timeout checking for progress dialog
                checkProgressBarTimeout();

                if (uiUpdateDue())
                {
                    // Give cancel button an opportunity to be clicked
                    if (cancelButton)
                    {
                        processProgressEvents();
                    }

                    // Set labels for progress dialog
                    FXString label = _("Delete:") + file;
                    if (uplabel)
                    {
                        setPathLabel(uplabel, label);
                    }
                    dirname = FXPath::directory(FXPath::absolute(file));
                    label = _("From: ") + dirname;
                    if (downlabel)
                    {
                        setPathLabel(downlabel, label);
                    }
                    getApp()->repaint();
                }

                // If cancel button was clicked, return
                if (cancelled)
                {
                    return false;
                }
            }
            if (unlink(file.text()) == -1)
            {
                int errcode = errno;
                forceTimeout();

                FXString str;
                if (errcode)
                {
                    str.format(_("Can't delete file %s: %s"), file.text(), strerror(errcode));
                }
                else
                {
                    str.format(_("Can't delete file %s"), file.text());
                }
                FXuint answer = showFileError(mbox, independent, str);

                restartTimeout();
                if (answer == BOX_CLICKED_CANCEL)
                {
                    cancelled = true;
                    return false;
                }
                return -1;      // To prevent displaying an error message
                                // in the calling function
            }
            else
            {
                notifyChange(file);
                xf_cleanuptrashinfo(file);
                return true;
            }
        }
    }
    return -1;
}


// Rename a file or a directory (no progress dialog)
// Return  0 to allow displaying an error message in the calling function
// Return -1 to prevent displaying an error message in the calling function
int File::rename(const FXString& source, const FXString& target)
{
    FXString destination = target;
    // Source doesn't exist
    if (!xf_existfile(source))
    {
        MessageBox::error(this, BOX_OK, _("Error"), _("Source %s doesn't exist"), source.text());
        return -1;
    }

    // Source and target are identical
    if (xf_isidentical(target, source))
    {
        MessageBox::error(this, BOX_OK, _("Error"), _("Destination %s is identical to source"), target.text());
        return -1;
    }

    // Target already exists => only allow overwriting destination if both source and target are files
    if (xf_existfile(target))
    {
        // Source or target are a directory
        if (xf_isdirectory(source) || xf_isdirectory(target))
        {
            MessageBox::error(this, BOX_OK, _("Error"), _("Destination %s already exists"), target.text());
            return -1;
        }
        // Source and target are files
        else
        {
            int answer = resolveConflict(source, destination, false, false);
            if (answer == OVWBOX_CLICKED_SKIP) return true;
            if (answer != OVWBOX_CLICKED_OVERWRITE) return answer;
        }
    }

    // File is on an MTP mount
    if (source.contains("mtp:host="))
    {       
        // Rename using gio command (regular rename does not work on MTP mounts)
        FXString cmd = "cd " + xf_quote(FXPath::directory(source)) +  "; gio rename " 
                       + xf_quote(FXPath::name(source)) + " " + xf_quote(FXPath::name(destination));
        FXString ret = xf_getcommandoutput(cmd);

        // An error has occurred
        if (ret != "")
        {
            forceTimeout();
            MessageBox::error(this, BOX_OK, _("Error"), _("Can't rename to target %s: %s"), destination.text(),
                              ret.text());

            return -1;
        }
        else
        {
            return true;
        }
    }    
    
    // File is on a regular file system
    else
    {
        // Rename file using the standard C function
        // This should only work for files that are on the same file system
        if (::rename(source.text(), destination.text()) == 0)
        {
            xf_cleanuptrashinfo(source);
            return true;
        }

        int errcode = errno;
        if ((errcode != EXDEV) && (errcode != ENOTEMPTY))
        {
            forceTimeout();
            MessageBox::error(this, BOX_OK, _("Error"), _("Can't rename to target %s: %s"), destination.text(),
                              strerror(errcode));
            return -1;
        }
    }

    return -1;
}


// Move files
// Return  0 to allow displaying an error message in the calling function
// Return -1 to prevent displaying an error message in the calling function
int File::fmove(const FXString& source, const FXString& target, const FXString& hsourcesize, const FXulong sourcesize,
                const FXulong tstart, const FXbool restore)
{
    // Source doesn't exist
    if (!xf_existfile(source))
    {
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Source %s doesn't exist"), source.text());
        return -1;
    }

    // Source and target are identical
    if (xf_isidentical(target, source))
    {
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Destination %s is identical to source"), target.text());
        return -1;
    }

    // Source path is included into target path
    FXString str = source + PATHSEPSTRING;
    if (target.left(str.length()) == str)
    {
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Target %s is a sub-folder of source"), target.text());
        return -1;
    }

    // Target is an existing directory (don't do this in the restore case)
    FXString targetfile;
    if (!restore && xf_isdirectory(target))
    {
        targetfile = target + PATHSEPSTRING + FXPath::name(source);
    }
    else
    {
        targetfile = target;
    }

    // Source and target file are identical
    if (xf_isidentical(targetfile, source))
    {
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Destination %s is identical to source"), targetfile.text());
        return -1;
    }

    // Force timeout checking for progress dialog
    checkProgressBarTimeout();

    if (uiUpdateDue())
    {
        // Give cancel button an opportunity to be clicked
        if (cancelButton)
        {
            processProgressEvents();
        }

        // Set labels for progress dialog
        FXString label = _("Source: ") + source;
        if (uplabel)
        {
            setPathLabel(uplabel, label);
        }
        label = _("Target: ") + target;
        if (downlabel)
        {
            setPathLabel(downlabel, label);
        }
        getApp()->repaint();
    }

    if (cancelled) return false;
    int answer = resolveConflict(source, targetfile);
    if (answer == OVWBOX_CLICKED_SKIP) return true;
    if (answer != OVWBOX_CLICKED_OVERWRITE) return answer;

    struct stat srcinfo, dstinfo;
    if (xf_lstat(source.text(), &srcinfo) != 0) return false;
    if (xf_lstat(targetfile.text(), &dstinfo) == 0)
    {
        if (S_ISDIR(srcinfo.st_mode) && S_ISDIR(dstinfo.st_mode))
        {
            return copydir(source, targetfile, hsourcesize, sourcesize, tstart, srcinfo, NULL, true, true);
        }
        if (S_ISDIR(srcinfo.st_mode) || S_ISDIR(dstinfo.st_mode))
        {
            int result = remove(targetfile);
            if (result != true) return result;
        }
    }

    // Get the size of the source
    FXulong srcsize = 0;
    struct stat linfo;
    if (xf_lstat(source.text(), &linfo) == 0)
    {
        char buf[MAXPATHLEN];
        if (S_ISDIR(linfo.st_mode)) // Folder
        {
            // The folder scan only feeds the progress total; skip it when there is none
            // (Move to Trash) so a same-drive rename stays instant for large folders
            if (sourcesize != 0)
            {
                std::map<FXString, FXulong>::const_iterator cached = sourceSizes.find(source);
                if (independent && cached != sourceSizes.end())
                {
                    srcsize = cached->second;
                }
                else if (independent)
                {
                    FXuint nbfiles = 0, nbsubfolders = 0;
                    FXulong total = 0, previous = totalsourcesize;
                    srcsize = computeSourceSize(this, getApp(), source, &nbfiles, &nbsubfolders,
                                                &total, cancelButton, &cancelled);
                    totalsourcesize = previous;
                    if (cancelled) return false;
                }
                else
                {
                    FXuint nbfiles = 0, nbsubfolders = 0;
                    FXulong totalsize = 0;
                    xf_strlcpy(buf, source.text(), source.length() + 1);
                    srcsize = xf_pathsize(buf, &nbfiles, &nbsubfolders, &totalsize);
                }
                totaldataread += srcsize;
            }
        }
        else // File
        {
            srcsize = (FXulong)linfo.st_size;
            totaldataread += srcsize;
        }
    }

    // Rename file using the standard C function
    // This should only work for files that are on the same file system
    if (::rename(source.text(), targetfile.text()) == 0)
    {
        if (progressbar)
        {
            // Percentage
            int pct = (sourcesize == 0 ? 0 : FXMIN(100.0, (100.0 * totaldataread) / sourcesize));
            progressbar->setProgress((int)pct);

            char size[64];

            // Move speed
            FXulong elapsed = xf_getcurrenttime() - tstart;
            double movespeed = elapsed ? totaldataread / (double)elapsed : 0;
            snprintf(size, sizeof(size), "%f", movespeed);
            FXString hspeed = xf_humansize(size);

            // Remaining time in seconds
            FXulong remaining = sourcesize > (FXulong)totaldataread ? sourcesize - totaldataread : 0;
            FXuint remtime = movespeed > 0 ? (FXuint)(remaining / movespeed) : 0;
            FXString hremtime = xf_secondstotimestring(remtime);

            // Total data moved
            FXString hsize;
#if __WORDSIZE == 64
            snprintf(size, sizeof(size), "%ld", totaldataread);
#else
            snprintf(size, sizeof(size), "%lld", totaldataread);
#endif
            hsize = xf_humansize(size);

            snprintf(size, sizeof(size), "%s %s / %s (%s/s)", datatext.text(), hsize.text(), hsourcesize.text(),
                     hspeed.text());
            setInfoLabel(datalabel, size);
            snprintf(size, sizeof(size), "%s %s", timetext.text(), hremtime.text());
            setInfoLabel(timelabel, size);
        }
        notifyChange(source);
        notifyChange(targetfile);
        xf_cleanuptrashinfo(source);
        return true;
    }

    int errcode = errno;
    totaldataread -= srcsize;
    if (errcode != EXDEV)
    {
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Can't rename to target %s: %s"), targetfile.text(),
                          strerror(errcode));
        return -1;
    }

    // If files are on different file systems, use the copy/delete scheme and preserve the original date
    if (S_ISDIR(srcinfo.st_mode))
    {
        return copydir(source, targetfile, hsourcesize, sourcesize, tstart, srcinfo, NULL, true, true);
    }
    int ret = copyrec(source, targetfile, hsourcesize, sourcesize, tstart, NULL, true, false);

    // Success
    if (ret == true)
    {
        if (independent)
        {
            struct stat current;
            if (xf_lstat(source.text(), &current) != 0 ||
                srcinfo.st_dev != current.st_dev || srcinfo.st_ino != current.st_ino ||
                srcinfo.st_mode != current.st_mode || srcinfo.st_uid != current.st_uid ||
                srcinfo.st_gid != current.st_gid || srcinfo.st_size != current.st_size ||
                srcinfo.st_mtim.tv_sec != current.st_mtim.tv_sec ||
                srcinfo.st_mtim.tv_nsec != current.st_mtim.tv_nsec ||
                srcinfo.st_ctim.tv_sec != current.st_ctim.tv_sec ||
                srcinfo.st_ctim.tv_nsec != current.st_ctim.tv_nsec)
            {
                forceTimeout();
                MessageBox::error(this, BOX_OK, _("Move source changed"),
                                  _("Source changed while moving.\nThe source was not removed.\n"
                                    "Check the progress window."));
                return -1;
            }
        }
        return remove(source.text()) == true;
    }
    // Error during copy
    else if (ret == -2)
    {
        return -1;
    }
    // Operation cancelled
    else
    {
        return ret;
    }
}


// Symbolic Link file (no progress dialog)
// Return  0 to allow displaying an error message in the calling function
// Return -1 to prevent displaying an error message in the calling function
int File::symlink(const FXString& source, const FXString& target)
{
    // Source doesn't exist
    if (!xf_existfile(source))
    {
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Source %s doesn't exist"), source.text());
        return -1;
    }

    // Source and target are identical
    if (xf_isidentical(target, source))
    {
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Destination %s is identical to source"), target.text());
        return -1;
    }

    // Target is an existing directory
    FXString targetfile;
    if (xf_isdirectory(target))
    {
        targetfile = target + PATHSEPSTRING + FXPath::name(source);
    }
    else
    {
        targetfile = target;
    }

    // Source and target are identical
    if (xf_isidentical(targetfile, source))
    {
        forceTimeout();
        MessageBox::error(this, BOX_OK, _("Error"), _("Destination %s is identical to source"), targetfile.text());
        return -1;
    }

    int answer = resolveConflict(source, targetfile, false);
    if (answer == OVWBOX_CLICKED_SKIP) return true;
    if (answer != OVWBOX_CLICKED_OVERWRITE) return answer;
    if (xf_existfile(targetfile))
    {
        int result = remove(targetfile);
        if (result != true) return result;
    }

    // Create symbolic link using the standard C function
    errno = 0;

    // Use the relative path for the symbolic link
    FXString relativepath;
    relativepath = FXPath::relative(FXPath::directory(targetfile), source);

    int ret = ::symlink(relativepath.text(), targetfile.text());

    int errcode = errno;
    if (ret == 0)
    {
        return true;
    }
    else
    {
        forceTimeout();
        if (errcode)
        {
            MessageBox::error(this, BOX_OK, _("Error"), _("Can't symlink %s: %s"), target.text(), strerror(errcode));
        }
        else
        {
            MessageBox::error(this, BOX_OK, _("Error"), _("Can't symlink %s"), target.text());
        }
        return -1;
    }
}


// Chmod a file or directory, recursively or not
// We don't process symbolic links (since their permissions cannot be changed)
//
// Note : the variable file returns the last processed file
// It can be different from the initial path, if recursive chmod is used
// (Used to fill an error message, if needed)
int File::chmod(char* path, char* file, mode_t mode, FXbool rec, const FXbool dironly, const FXbool fileonly)
{
    struct stat linfo;

    // Initialize the file variable with the initial path
    xf_strlcpy(file, path, strlen(path) + 1);

    // If it doesn't exist
    if (xf_lstat(path, &linfo))
    {
        return -1;
    }

    // If it's a symbolic link
    if (S_ISLNK(linfo.st_mode))
    {
        return 0;
    }

    if (!S_ISDIR(linfo.st_mode)) // File
    {
        if (dironly)
        {
            return 0;
        }

        // Force timeout checking for progress dialog
        checkProgressBarTimeout();

        // Give cancel button an opportunity to be clicked
        if (cancelButton)
        {
            processProgressEvents();
        }

        // Set labels for progress dialog
        FXString label = _("Changing permissions...");
        setPathLabel(uplabel, label);
        label = _("File: ") + FXString(path);
        setPathLabel(downlabel, label);
        getApp()->repaint();

        // If cancel button was clicked, return
        if (cancelled)
        {
            return -1;
        }

        return ::chmod(path, mode);
    }
    else // Directory
    {
        if ((rec == false) && !fileonly)
        {
            // Force timeout checking for progress dialog
            checkProgressBarTimeout();

            // Give cancel button an opportunity to be clicked
            if (cancelButton)
            {
                processProgressEvents();
            }

            // Set labels for progress dialog
            FXString label = _("Changing permissions...");
            setPathLabel(uplabel, label);
            label = _("Folder: ") + FXString(path);
            setPathLabel(downlabel, label);
            getApp()->repaint();

            // If cancel button was clicked, return
            if (cancelled)
            {
                return -1;
            }

            if (::chmod(path, mode)) // Do not change recursively
            {
                return -1;
            }
        }
        else
        {
            return rchmod(path, file, mode, dironly, fileonly); // Recursive change
        }
    }
    return 0;
}


// Recursive chmod for a directory
// We don't process symbolic links (since their permissions cannot be changed)
int File::rchmod(char* path, char* file, mode_t mode, const FXbool dironly, const FXbool fileonly)
{
    struct stat linfo;

    // Initialize the file variable with the initial path
    xf_strlcpy(file, path, strlen(path) + 1);

    // If it doesn't exist
    if (xf_lstat(path, &linfo))
    {
        return -1;
    }

    // If it's a symbolic link
    if (S_ISLNK(linfo.st_mode))
    {
        return 0;
    }

    if (!S_ISDIR(linfo.st_mode)) // File
    {
        if (dironly)
        {
            return 0;
        }

        // Force timeout checking for progress dialog
        checkProgressBarTimeout();

        // Give cancel button an opportunity to be clicked
        if (cancelButton)
        {
            processProgressEvents();
        }

        // Set labels for progress dialog
        FXString label = _("Changing permissions...");
        setPathLabel(uplabel, label);
        label = _("File: ") + FXString(path);
        setPathLabel(downlabel, label);
        getApp()->repaint();

        // If cancel button was clicked, return
        if (cancelled)
        {
            return -1;
        }

        return ::chmod(path, mode);
    }

    DIR* dir;
    struct dirent* entry;
    int pl = strlen(path);

    if (!(dir = opendir(path)))
    {
        return -1;
    }

    for (; (entry = readdir(dir)); )
    {
        if ((entry->d_name[0] != '.') || ((entry->d_name[1] != '\0') &&
                                          ((entry->d_name[1] != '.') ||
                                           (entry->d_name[2] != '\0'))))
        {
            int pl1 = pl, l = strlen(entry->d_name);
            char* path1 = (char*)alloca(pl1 + l + 2);

            xf_strlcpy(path1, path, strlen(path) + 1);
            if (path1[pl1 - 1] != '/')
            {
                path1[pl1++] = '/';
            }
            xf_strlcpy(path1 + pl1, entry->d_name, strlen(entry->d_name) + 1);

            // Modify the file variable with the new path
            xf_strlcpy(file, path1, strlen(path1) + 1);
            if (rchmod(path1, file, mode, dironly, fileonly))
            {
                closedir(dir);
                return -1;
            }
        }
    }

    if (closedir(dir))
    {
        return -1;
    }

    if (fileonly)
    {
        return 0;
    }
    else
    {
        return ::chmod(path, mode);
    }
}


// Chown a file or directory, recursively or not
// We don't follow symbolic links
//
// Note : the variable file returns the last processed file
// It can be different from the initial path, if recursive chmod is used
// (Used to fill an error message, if needed)
int File::chown(char* path, char* file, uid_t uid, gid_t gid, const FXbool rec,
                const FXbool dironly, const FXbool fileonly)
{
    struct stat linfo;

    // Initialise the file variable with the initial path
    xf_strlcpy(file, path, strlen(path) + 1);

    // If it doesn't exist
    if (xf_lstat(path, &linfo))
    {
        return -1;
    }

    if (!S_ISDIR(linfo.st_mode)) // File
    {
        if (dironly)
        {
            return 0;
        }

        // Force timeout checking for progress dialog
        checkProgressBarTimeout();

        // Give cancel button an opportunity to be clicked
        if (cancelButton)
        {
            processProgressEvents();
        }

        // Set labels for progress dialog
        FXString label = _("Changing owner...");
        setPathLabel(uplabel, label);
        label = _("File: ") + FXString(path);
        setPathLabel(downlabel, label);
        getApp()->repaint();

        // If cancel button was clicked, return
        if (cancelled)
        {
            return -1;
        }

        if (::lchown(path, uid, gid))
        {
            return -1;
        }
    }
    else // Directory
    {
        if ((rec == false) && !fileonly)
        {
            // Force timeout checking for progress dialog
            checkProgressBarTimeout();

            // Give cancel button an opportunity to be clicked
            if (cancelButton)
            {
                processProgressEvents();
            }

            // Set labels for progress dialog
            FXString label = _("Changing owner...");
            setPathLabel(uplabel, label);
            label = _("Folder: ") + FXString(path);
            setPathLabel(downlabel, label);
            getApp()->repaint();

            // If cancel button was clicked, return
            if (cancelled)
            {
                return -1;
            }

            if (::lchown(path, uid, gid)) // Do not change recursively
            {
                return -1;
            }
        }
        else if (rchown(path, file, uid, gid, dironly, fileonly)) // Recursive change
        {
            return -1;
        }
    }
    return 0;
}


// Recursive chown for a directory
// We don't follow symbolic links
int File::rchown(char* path, char* file, uid_t uid, gid_t gid, const FXbool dironly, const FXbool fileonly)
{
    struct stat linfo;

    // Initialise the file variable with the initial path
    xf_strlcpy(file, path, strlen(path) + 1);

    // If it doesn't exist
    if (xf_lstat(path, &linfo))
    {
        return -1;
    }

    if (!S_ISDIR(linfo.st_mode)) // file
    {
        if (dironly)
        {
            return 0;
        }

        // Force timeout checking for progress dialog
        checkProgressBarTimeout();

        // Give cancel button an opportunity to be clicked
        if (cancelButton)
        {
            processProgressEvents();
        }

        // Set labels for progress dialog
        FXString label = _("Changing owner...");
        setPathLabel(uplabel, label);
        label = _("File: ") + FXString(path);
        setPathLabel(downlabel, label);
        getApp()->repaint();

        // If cancel button was clicked, return
        if (cancelled)
        {
            return -1;
        }

        return lchown(path, uid, gid);
    }

    DIR* dir;
    struct dirent* entry;
    int pl = strlen(path);

    if (!(dir = opendir(path)))
    {
        return -1;
    }

    for ( ; (entry = readdir(dir)); )
    {
        if ((entry->d_name[0] != '.') || ((entry->d_name[1] != '\0') &&
                                          ((entry->d_name[1] != '.') ||
                                           (entry->d_name[2] != '\0'))))
        {
            int pl1 = pl, l = strlen(entry->d_name);
            char* path1 = (char*)alloca(pl1 + l + 2);

            xf_strlcpy(path1, path, strlen(path) + 1);
            if (path1[pl1 - 1] != '/')
            {
                path1[pl1++] = '/';
            }
            xf_strlcpy(path1 + pl1, entry->d_name, strlen(entry->d_name) + 1);
            xf_strlcpy(file, path1, strlen(path1) + 1);
            if (rchown(path1, file, uid, gid, dironly, fileonly))
            {
                closedir(dir);
                return -1;
            }
        }
    }

    if (closedir(dir))
    {
        return -1;
    }

    if (fileonly)
    {
        return 0;
    }
    else
    {
        return lchown(path, uid, gid);
    }
}


// Extract an archive in a specified directory
int File::extract(const FXString name, const FXString dir, const FXString cmd)
{
    int ret;

    // Change to the specified directory
    FXString currentdir = FXSystem::getCurrentDirectory();

    ret = chdir(dir.text());
    if (ret < 0)
    {
        int errcode = errno;
        if (errcode)
        {
            MessageBox::error(this, BOX_OK, _("Error"), _("Can't enter folder %s: %s"), dir.text(), strerror(errcode));
        }
        else
        {
            MessageBox::error(this, BOX_OK, _("Error"), _("Can't enter folder %s"), dir.text());
        }

        return 0;
    }

    // Make and show command window
    CommandWindow* cmdwin = new CommandWindow(getApp(), _("Extract archive"), cmd, 30, 80);
    cmdwin->create();
    cmdwin->setIcon(miniarchexticon);

    // The command window object deletes itself after closing the window!

    // Return to initial directory
    ret = chdir(currentdir.text());
    if (ret < 0)
    {
        int errcode = errno;
        if (errcode)
        {
            MessageBox::error(this, BOX_OK, _("Error"), _("Can't enter folder %s: %s"), currentdir.text(),
                              strerror(errcode));
        }
        else
        {
            MessageBox::error(this, BOX_OK, _("Error"), _("Can't enter folder %s"), currentdir.text());
        }

        return 0;
    }

    return 1;
}


// Create an archive
int File::archive(const FXString name, const FXString cmd)
{
    // Target file already exists
    if (xf_existfile(FXPath::dequote(name)))
    {
        FXString msg;
        msg.format(_("File %s already exists.\nOverwrite?"), name.text());
        OverwriteBox* dlg = new OverwriteBox(ownerwin, _("Confirm Overwrite"), msg, OVWBOX_SINGLE_FILE);
        FXuint answer = dlg->execute(PLACEMENT_OWNER);
        delete dlg;
        if (answer == 0)
        {
            return false;
        }
    }

    // Make and show command window
    CommandWindow* cmdwin = new CommandWindow(getApp(), _("Add to Archive "), cmd, 30, 80);
    cmdwin->create();
    cmdwin->setIcon(miniarchaddicon);

    // The command window object deletes itself after closing the window!

    return 1;
}


// Compute data size and show progress dialog
FXString File::sourcesize(const FXString src, FXulong* totalsize, const FXbool url)
{
    std::vector<FXString> paths;
    for (FXint start = 0; start < src.length();)
    {
        FXint end = src.find('\n', start);
        if (end < 0) end = src.length();
        FXString path = src.mid(start, end - start);
        if (url && path.right(1) == "\r") path.trunc(path.length() - 1);
        if (!path.empty()) paths.push_back(url ? FXURL::decode(FXURL::fileFromURL(path)) : path);
        start = end + 1;
    }
    return sourcesize(paths, totalsize);
}


FXString File::sourcesize(const std::vector<FXString>& paths, FXulong* totalsize)
{
    if (independent) getApp()->addTimeout(this, File::ID_SOURCESIZE, SOURCESIZE_REFRESH_DELAY);

    // Set label
    FXString msg;

    if (op == COPY)
    {
        msg = _("Computing the size of files to copy: ");
    }
    else if (op == MOVE)
    {
        msg = _("Computing the size of files to move: ");
    }
    else // Should not happen
    {
        msg = "";
    }
    if (independent) setInfoLabel(datalabel, msg);
    else setPathLabel(uplabel, msg);

    // Compute data size
    *totalsize = 0;
    sourceSizes.clear();
    for (const FXString& path : paths)
    {
        // Force timeout checking for progress dialog
        checkProgressBarTimeout();

        FXuint nbfiles = 0, nbsubfolders = 0;
        computeSourceSize(this, getApp(), path, &nbfiles, &nbsubfolders, totalsize, cancelButton, &cancelled);

        // Give cancel button an opportunity to be clicked
        if (cancelButton)
        {
            processProgressEvents();
        }

        // If cancel button was clicked, return
        if (cancelled)
        {
            return "";
        }
    }
    setTotalSourceSize(*totalsize);
    char buf[64];
#if __WORDSIZE == 64
    snprintf(buf, sizeof(buf), "%lu", *totalsize);
#else
    snprintf(buf, sizeof(buf), "%llu", *totalsize);
#endif

    FXString hsourcesize = xf_humansize(buf);

    return hsourcesize;
}


#if defined(linux)
int File::mount(const FXString dir, const FXString msg, const FXString cmd, FXString* output, int* status)
{
    if (op == UNMOUNT && xf_protectedmount(dir))
    {
        if (status) *status = -1;
        if (output) *output = _("Root and Home cannot be unmounted.");
        appLog(APP_LOG_ERRORS, "protected-unmount-refused", dir);
        if (!output) MessageBox::error(this, BOX_OK, _("Error"), _("Root and Home cannot be unmounted."));
        return -1;
    }
    // Deliver any pending pointer ungrab before the command can block this process
    getApp()->flush(true);
    FXbool mount_messages = getApp()->reg().readUnsignedEntry("OPTIONS", "mount_messages", true);
    FXString command = cmd;
    FXString gvfs = xf_gvfsmount(dir);
    // Companion utilities also use File, but only the file manager owns tabs.
    struct TabRollback
    {
        FXObject* guard = NULL;
        void commit() { if (guard) guard->handle(NULL, 0, NULL); }
        ~TabRollback() { delete guard; }
    } tabs;
    if (op == UNMOUNT && !output)
    {
        UnmountTabRequest request;
        request.path = gvfs.empty() ? dir : gvfs;
        mainWindow->handle(this, FXSEL(SEL_COMMAND, XFileExplorer::ID_PREPARE_UNMOUNT), &request);
        tabs.guard = request.guard;
    }
    if (op == UNMOUNT && !output)
    {
        // Leave the mount first so no tab or tree refresh touches it while it disappears
        FXString target = gvfs.empty() ? dir : gvfs;
        mainWindow->handle(this, FXSEL(SEL_COMMAND, XFileExplorer::ID_NETWORK_UNMOUNTED), &target);
        getApp()->flush(true);
    }
    if (!gvfs.empty())
    {
        FXString uri = xf_gvfsmounturi(dir);
        if (uri.empty())
        {
            MessageBox::error(this, BOX_OK, _("Error"), _("Can't determine the network mount URI for %s"), dir.text());
            return -1;
        }
        command = xf_hostcommand("timeout -k 2 30 gio mount " + FXString(op == UNMOUNT ? "-u " : "") +
                                 xf_quote(uri) + " </dev/null") + " 2>&1";
    }

    // Set labels for progress dialog
    uplabel->setText(msg);
    downlabel->setText(dir.text());

    // Keep the application reaper from consuming popen's child before pclose.
    struct ChildSignalGuard
    {
        FXApp* app;
        FXMainWindow* window;
        ChildSignalGuard(FXApp* application, FXMainWindow* owner) : app(application), window(owner)
        {
            app->removeSignal(SIGCHLD);
        }
        void restore()
        {
            if (app)
            {
                app->addSignal(SIGCHLD, window, XFileExplorer::ID_HARVEST);
                app = NULL;
                // Children may have exited while SIGCHLD was unregistered.
                window->handle(window, FXSEL(SEL_SIGNAL, XFileExplorer::ID_HARVEST), NULL);
            }
        }
        ~ChildSignalGuard() { restore(); }
    } childSignal(getApp(), mainWindow);

    // Perform mount/unmount command
    FILE* pcmd = popen(command.text(), "r");
    if (!pcmd)
    {
        MessageBox::error(this, BOX_OK, _("Error"), _("Failed command: %s"), command.text());
        return -1;
    }

    // Get file descriptor and set it to non-blocking
    int fd = fileno(pcmd);
    if (fd < 0)
    {
        MessageBox::error(this, BOX_OK, _("Error"), "%s", strerror(errno));
        pclose(pcmd);
        return -1;        
    }
    int ret = fcntl(fd, F_SETFL, O_NONBLOCK);
    if (ret < 0)
    {
        MessageBox::error(this, BOX_OK, _("Error"), "%s", strerror(errno));
        pclose(pcmd);
        return -1;       
    }

    // Get error message if any
    char text[256] = { 0 };
    int process_finished = 0;
    FXString buf;
    while (!process_finished)
    {
        if (fgets(text, sizeof(text), pcmd) != NULL)
        {
            buf += text;
        }
        else
        {
            if (feof(pcmd))
            {
                process_finished = 1;  // Command has completed
            }
            else
            {
                checkProgressBarTimeout();
                getApp()->repaint();
                sleep(SHOW_PROGRESSBAR_DELAY / 1000);  // Avoid busy-waiting
            }
        }
    }

    // Close process and get exit status
    int exit_status = pclose(pcmd);

    childSignal.restore();

    // Hide progress dialog
    forceTimeout();

    // The mount point changed state: forget any cached "down" status
    if (updevices)
    {
        updevices->remove(dir.text());
    }

    int code = exit_status != -1 && WIFEXITED(exit_status) ? WEXITSTATUS(exit_status) : -1;

    // Caller wants the command result and handles messages itself
    if (output)
    {
        *output = buf;
        if (status)
        {
            *status = code;
        }
        return 1;
    }

    if (code == 0 && op == UNMOUNT && !gvfs.empty())
    {
        struct stat info;
        if (lstat(gvfs.text(), &info) == 0)
        {
            code = -1;
            buf = _("The network share is still mounted.");
        }
        else if (errno != ENOENT)
        {
            code = -1;
            buf = strerror(errno);
        }
    }
    if (code != 0)
    {
        if (buf.empty())
        {
            buf.format(_("Command failed (exit status %d)."), code);
        }
        if (op == UNMOUNT && (buf.find("busy") >= 0 || buf.find("in use") >= 0))
            buf = xf_devicebusy(gvfs.empty() ? dir : gvfs) + "\n\n" + buf;
        MessageBox::error(this, BOX_OK, _("Error"), "%s", buf.text());
        return -1;
    }

    if (op == UNMOUNT && !gvfs.empty())
    {
        mainWindow->handle(this, FXSEL(SEL_COMMAND, XFileExplorer::ID_NETWORK_UNMOUNTED), &gvfs);
    }

    tabs.commit();

    // Success message
    if (mount_messages)
    {
        if (op == MOUNT)
        {
            MessageBox::information(this, BOX_OK, _("Success"), _("Folder %s was successfully mounted."), dir.text());
        }
        else
        {
            MessageBox::information(this, BOX_OK, _("Success"), _("Folder %s was successfully unmounted."), dir.text());
        }
    }
    return 1;
}


// Install / Upgrade package
int File::pkgInstall(const FXString name, const FXString cmd)
{
    // Make and show command window
    CommandWindow* cmdwin = new CommandWindow(getApp(), _("Install / Upgrade Package"), cmd, 10, 80);

    cmdwin->create();

    FXString msg;
    msg.format(_("Installing package: %s \n"), name.text());
    cmdwin->appendText(msg.text());

    // The command window object deletes itself after closing the window!

    return 1;
}


// Uninstall package
int File::pkgUninstall(const FXString name, const FXString cmd)
{
    // Make and show command window
    CommandWindow* cmdwin = new CommandWindow(getApp(), _("Uninstall Package"), cmd, 10, 80);

    cmdwin->create();

    FXString msg;
    msg.format(_("Uninstalling package: %s \n"), name.text());
    cmdwin->appendText(msg.text());

    // The command window object deletes itself after closing the window!

    return 1;
}

#endif


// Handle cancel button in progress bar dialog
long File::onCmdCancel(FXObject*, FXSelector, void*)
{
    if (independent && finished)
    {
        getApp()->stopModal(this, true);
        hide();
        return 1;
    }
    if (activeChild > 0)
    {
        if (paused) kill(-activeChild, SIGCONT);
        if (kill(-activeChild, SIGTERM) != 0 && errno != ESRCH)
            appLog(APP_LOG_ERRORS, "transfer-cancel-failed", strerror(errno));
    }
    cancelled = true;
    paused = false;
    if (independent)
    {
        if (statusLabel) statusLabel->setText(_("Cancelling..."));
        if (pauseButton) pauseButton->disable();
        if (cancelButton) cancelButton->disable();
    }
    return 1;
}

long File::onDialogCancel(FXObject* sender, FXSelector selector, void* data)
{
    if (independent) return onCmdCancel(sender, selector, data);
    return DialogBox::onCmdCancel(sender, selector, data);
}


long File::onCmdPause(FXObject*, FXSelector, void*)
{
    if (!independent || cancelled || finished) return 1;
    FXbool next = !paused;
    if (activeChild > 0 && kill(-activeChild, next ? SIGSTOP : SIGCONT) != 0 && errno != ESRCH)
    {
        int failure = errno;
        appLog(APP_LOG_ERRORS, "transfer-pause-failed", strerror(failure));
        MessageBox::error(this, BOX_OK, _("Error"), _("Unable to pause or resume the transfer: %s"),
                          strerror(failure));
        return 1;
    }
    paused = next;
    pauseButton->setText(paused ? _("R&esume") : _("&Pause"));
    if (statusLabel)
        statusLabel->setText(paused ? _("Paused") : activityStatus);
    if (paused)
    {
        numcopyspeed = 0;
        prevTotaldataread = totaldataread;
        if (datalabel && !copyspeedHsourcesize.empty())
            setInfoLabel(datalabel, datatext + " " + xf_humansize(FXStringVal(totaldataread).text()) +
                                     " / " + copyspeedHsourcesize);
        if (timelabel) setInfoLabel(timelabel, timetext + " --");
    }
    return 1;
}


long File::onComplete(FXObject*, FXSelector, void*)
{
    getApp()->stopModal(this, true);
    hide();
    return 1;
}


// Handle timeout for progress bar
long File::onTimeout(FXObject*, FXSelector, void*)
{
    show(PLACEMENT_OWNER);
    getApp()->forceRefresh();
    getApp()->flush();
    return 1;
}


// Handle timeout for source size refresh
long File::onSourceSizeRefresh(FXObject*, FXSelector, void*)
{
    char buf[MAXPATHLEN];

#if __WORDSIZE == 64
    snprintf(buf, sizeof(buf), "%lu", totalsourcesize);
#else
    snprintf(buf, sizeof(buf), "%llu", totalsourcesize);
#endif

    FXString srcsize = xf_humansize(buf);

    FXString msg;
    if (op == COPY)
    {
        msg = _("Computing the size of files to copy: ");
    }
    else if (op == MOVE)
    {
        msg = _("Computing the size of files to move: ");
    }
    else // Should not happen
    {
        msg = "";
    }

    // Update label text
    if (independent) setInfoLabel(datalabel, msg + srcsize);
    else uplabel->setText(msg + srcsize);

    // Restart timeout
    getApp()->addTimeout(this, File::ID_SOURCESIZE, SOURCESIZE_REFRESH_DELAY);

    return 1;
}


// Refresh copy speed
long File::onCopySpeed(FXObject*, FXSelector, void*)
{
    if (independent && paused)
    {
        prevTotaldataread = totaldataread;
        getApp()->addTimeout(this, File::ID_COPYSPEED, COPYSPEED_DELAY);
        return 1;
    }
    // Copy speed
    char size[64];
    double copyspeed = (totaldataread - prevTotaldataread) / (double)(COPYSPEED_DELAY / 1000.0);
       
    // Compute moving average of copy speeds
    if (numcopyspeed < NMAX_COPY_SPEED)
    {
        numcopyspeed++;
    }
    double avgcopyspeed = 0;
    for (FXuint i = numcopyspeed - 1; i > 0; i--)
    {
        vecCopyspeed[i] = vecCopyspeed[i - 1];
        avgcopyspeed += vecCopyspeed[i];
    }
    vecCopyspeed[0] = copyspeed;
    avgcopyspeed += vecCopyspeed[0];
    avgcopyspeed /= (double)numcopyspeed;
   
    // Copy speed
    snprintf(size, sizeof(size), "%f", avgcopyspeed);
    FXString hspeed = xf_humansize(size);

    // Remaining time in seconds
    FXulong remaining = copyspeedSourcesize > (FXulong)totaldataread ?
                        copyspeedSourcesize - totaldataread : 0;
    FXuint remtime = avgcopyspeed > 0 ? (FXuint)(remaining / avgcopyspeed) : 0;
    FXString hremtime = remaining && avgcopyspeed <= 0 ? "--" : xf_secondstotimestring(remtime);

    // Total data copied
    FXString hsize;

#if __WORDSIZE == 64
    snprintf(size, sizeof(size), "%ld", totaldataread);
#else
    snprintf(size, sizeof(size), "%lld", totaldataread);
#endif
    hsize = xf_humansize(size);
    snprintf(size, sizeof(size), "%s %s / %s (%s/s)", datatext.text(), hsize.text(),
             copyspeedHsourcesize.text(), hspeed.text());    
    if (datalabel)
    {
        setInfoLabel(datalabel, size);
    }
    
    snprintf(size, sizeof(size), "%s %s", timetext.text(), hremtime.text());
    if (timelabel)
    {
        setInfoLabel(timelabel, size);
    }

    // Restart timeout   
    prevTotaldataread = totaldataread;
    getApp()->addTimeout(this, File::ID_COPYSPEED, COPYSPEED_DELAY);

    return 1;
}
