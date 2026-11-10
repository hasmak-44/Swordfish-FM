#include "config.h"
#include "RemoteDirectory.h"
#include "AppLogger.h"
#include "xfeutils.h"
#include "DialogBox.h"
#include "MessageBox.h"
#include "i18n.h"
#include <gio/gio.h>
#include <map>
#include <string>
#include <set>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>

namespace
{
struct Watch
{
    FXObject* object;
    FXSelector selector;
};
struct Listing
{
    std::vector<RemoteDirectory::Entry> rows;
    std::map<std::string, size_t> existing;
    std::set<std::string> seen;
    std::vector<Watch> watchers;
    FXString buffer, diagnostic;
    pid_t pid = -1;
    int fd = -1, input = -1, status = 0;
    FXbool authenticate = false;
    FXbool prompting = false;
    FXbool done = false, exited = false, dirty = false;
    FXTime started = 0, completed = 0;
};

struct Download
{
    FXString url, destination, output, pending;
    pid_t pid = -1;
    int fd = -1, status = 0;
    FXbool exited = false;
    FXlong received = 0, total = -1;
};

FXString byteText(FXlong bytes)
{
    return xf_humansize(FXStringVal(static_cast<FXulong>(bytes < 0 ? 0 : bytes)).text());
}

// Non-modal progress for the media download queue; closing it only hides it.
class DownloadWindow : public DialogBox
{
    FXDECLARE(DownloadWindow)
    DownloadWindow()
    {
    }
public:
    enum
    {
        ID_CANCEL_DOWNLOADS = DialogBox::ID_LAST,
        ID_LAST
    };
    FXLabel* file = NULL;
    FXLabel* amount = NULL;
    FXProgressBar* bar = NULL;
    FXbool dismissed = false;  // Hidden by the user until the current batch ends
    explicit DownloadWindow(FXWindow* owner) :
        DialogBox(owner, _("Media Download"), DECOR_TITLE | DECOR_BORDER | DECOR_CLOSE | DECOR_STRETCHABLE)
    {
        FXHorizontalFrame* buttons = new FXHorizontalFrame(this, LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X | PACK_UNIFORM_WIDTH,
                                                           0, 0, 0, 0, 10, 10, 5, 5);
        new FXButton(buttons, _("Cancel &Downloads"), NULL, this, ID_CANCEL_DOWNLOADS,
                     FRAME_GROOVE | LAYOUT_RIGHT, 0, 0, 0, 0, 20, 20);
        new FXButton(buttons, _("&Hide"), NULL, this, ID_CANCEL, FRAME_GROOVE | LAYOUT_RIGHT, 0, 0, 0, 0, 20, 20);
        FXVerticalFrame* contents = new FXVerticalFrame(this, LAYOUT_SIDE_TOP | LAYOUT_FILL_X | LAYOUT_FILL_Y);
        file = new FXLabel(contents, FXString::null, NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        bar = new FXProgressBar(contents, NULL, 0, LAYOUT_FILL_X | LAYOUT_FIX_WIDTH | TEXTFIELD_NORMAL |
                                PROGRESSBAR_PERCENTAGE, 0, 0, 420, 0, 0, 0, 0, 0);
        bar->setBarColor(getApp()->reg().readColorEntry("SETTINGS", "pbarcolor", FXRGB(10, 36, 106)));
        amount = new FXLabel(contents, FXString::null, NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    }
    long onCmdHide(FXObject*, FXSelector, void*)
    {
        dismissed = true;
        hide();
        return 1;
    }
    long onCmdCancelDownloads(FXObject*, FXSelector, void*)
    {
        RemoteDirectory::cancelDownloads();
        return 1;
    }
};
FXDEFMAP(DownloadWindow) downloadWindowMap[] =
{
    FXMAPFUNC(SEL_COMMAND, DialogBox::ID_CANCEL, DownloadWindow::onCmdHide),
    FXMAPFUNC(SEL_COMMAND, DownloadWindow::ID_CANCEL_DOWNLOADS, DownloadWindow::onCmdCancelDownloads),
};
FXIMPLEMENT(DownloadWindow, DialogBox, downloadWindowMap, ARRAYNUMBER(downloadWindowMap))

struct MediaServer
{
    FXString location, name;
};

std::map<std::string, MediaServer>& mediaServers()
{
    static std::map<std::string, MediaServer> servers;
    return servers;
}

// "host:port" of a dlna:// path, lower case
std::string mediaAuthority(const FXString& path)
{
    FXString authority = path.mid(7, path.length() - 7).before('/');
    authority.lower();
    return authority.text();
}

FXString helperPath()
{
    char self[4096];
    ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (length <= 0) return "swordfish-network";
    self[length] = 0;
    return FXPath::directory(self) + "/swordfish-network";
}

class RemoteQueue : public FXObject
{
    FXDECLARE(RemoteQueue)
public:
    FXApp* app = NULL;
    std::map<std::string, Listing> listings;
    std::set<pid_t> cancelled;
    std::vector<Download> downloads;
    FXString downloadErrors;
    FXWindow* downloadOwner = NULL;
    DownloadWindow* downloadWindow = NULL;
    FXuint batchTotal = 0, batchDone = 0;
    FXTime batchStarted = 0;
    void showDownloadProgress();
    FXulong revision = 0;
    long tick(FXObject*, FXSelector, void*);
    void start(const FXString&, Listing&);
    FXbool tickDownloads();
    void close(Listing& listing)
    {
        if (listing.pid > 0)
        {
            kill(listing.pid, SIGKILL);
            cancelled.insert(listing.pid);
        }
        if (listing.fd >= 0) ::close(listing.fd);
        if (listing.input >= 0) ::close(listing.input);
        listing.pid = -1;
        listing.fd = -1;
        listing.input = -1;
    }
};
FXDEFMAP(RemoteQueue) map[] = { FXMAPFUNC(SEL_TIMEOUT, 1, RemoteQueue::tick) };
FXIMPLEMENT(RemoteQueue, FXObject, map, ARRAYNUMBER(map))
RemoteQueue queue;

std::string key(const FXString& path)
{
    FXString result = RemoteDirectory::isServer(path) ? path : FXPath::simplify(path);
    while (result.length() > 1 && result[result.length() - 1] == '/') result.trunc(result.length() - 1);
    return result.text();
}

void RemoteQueue::start(const FXString& path, Listing& listing)
{
    FXString arguments = (RemoteDirectory::isMedia(path) ? " --media " : RemoteDirectory::isServer(path) ?
                          " --shares " : " --list ");
    if (RemoteDirectory::isMedia(path))
    {
        const FXString location = RemoteDirectory::mediaServerLocation(path);
        if (location.empty())
        {
            listing.diagnostic = _("This media server is not known yet. Refresh Network in the sidebar to find it.");
            listing.completed = FXThread::time();
            listing.dirty = true;
            appLog(APP_LOG_ERRORS, "remote-list-start-failed", path + ": unknown media server");
            app->addTimeout(this, 1, 50);
            return;
        }
        arguments += xf_quote(location) + " ";
    }
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) != 0)
    {
        listing.diagnostic = strerror(errno);
        listing.completed = FXThread::time();
        listing.dirty = true;
        appLog(APP_LOG_ERRORS, "remote-list-start-failed", path + ": " + listing.diagnostic);
        app->addTimeout(this, 1, 50);
        return;
    }
    int input[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, input) != 0)
    {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        listing.diagnostic = strerror(errno);
        listing.completed = FXThread::time();
        listing.dirty = true;
        appLog(APP_LOG_ERRORS, "remote-list-start-failed", path + ": " + listing.diagnostic);
        app->addTimeout(this, 1, 50);
        return;
    }
    FXString command = FXString("exec ") + xf_hostcommand(xf_quote(helperPath()) + arguments + xf_quote(path));
    listing.pid = fork();
    if (listing.pid == 0)
    {
        ::close(pipefd[0]);
        ::close(input[1]);
        int flags = fcntl(pipefd[1], F_GETFL);
        if (flags < 0 || fcntl(pipefd[1], F_SETFL, flags & ~O_NONBLOCK) < 0 ||
            dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(input[0], STDIN_FILENO) < 0)
            _exit(126);
        ::close(pipefd[1]);
        ::close(input[0]);
        execl("/bin/sh", "sh", "-c", command.text(), static_cast<char*>(NULL));
        _exit(127);
    }
    ::close(pipefd[1]);
    ::close(input[0]);
    if (listing.pid < 0)
    {
        ::close(pipefd[0]);
        ::close(input[1]);
        listing.diagnostic = strerror(errno);
        listing.completed = FXThread::time();
        listing.dirty = true;
        appLog(APP_LOG_ERRORS, "remote-list-start-failed", path + ": " + listing.diagnostic);
    }
    else
    {
        listing.fd = pipefd[0];
        listing.input = input[1];
        listing.authenticate = false;
        listing.existing.clear();
        listing.seen.clear();
        for (size_t i = 0; i < listing.rows.size(); ++i)
            listing.existing[listing.rows[i].name.text()] = i;
        listing.buffer.clear();
        listing.diagnostic.clear();
        listing.done = listing.exited = false;
        listing.started = FXThread::time();
        listing.completed = 0;
        listing.dirty = true;
    }
    app->addTimeout(this, 1, 50);
}

void parse(Listing& listing, const FXString& line)
{
    if (!listing.diagnostic.empty()) return;
    if (listing.done)
    {
        listing.diagnostic = "Unexpected data after remote directory completion";
        return;
    }
    if (line == "DONE")
    {
        listing.done = true;
        listing.rows.erase(std::remove_if(listing.rows.begin(), listing.rows.end(),
            [&listing](const RemoteDirectory::Entry& entry)
            { return listing.seen.find(entry.name.text()) == listing.seen.end(); }), listing.rows.end());
        listing.dirty = true;
        return;
    }
    if (line == "AUTH")
    {
        listing.authenticate = true;
        return;
    }
    if (line.left(6) == "ERROR ")
    {
        listing.diagnostic = line.mid(6, line.length() - 6);
        return;
    }
    GError* error = NULL;
    GVariant* value = g_variant_parse(G_VARIANT_TYPE("(suuuxxxsbii)"), line.text(), NULL, NULL, &error);
    if (!value)
    {
        listing.diagnostic = FXString("Invalid remote directory response: ") + error->message;
        g_error_free(error);
        return;
    }
    const char* name;
    const char* link;
    guint32 mode, uid, gid;
    gint64 modified, changed, size;
    gboolean symlink;
    gint readable, executable;
    g_variant_get(value, "(&suuuxxx&sbii)", &name, &mode, &uid, &gid, &modified,
                  &changed, &size, &link, &symlink, &readable, &executable);
    gsize nameSize, linkSize;
    guchar* decodedName = g_base64_decode(name, &nameSize);
    guchar* decodedLink = g_base64_decode(link, &linkSize);
    gchar* canonicalName = g_base64_encode(decodedName, nameSize);
    gchar* canonicalLink = g_base64_encode(decodedLink, linkSize);
    FXString filename(reinterpret_cast<char*>(decodedName), nameSize);
    if (filename.empty() || filename.find('/') >= 0 || filename.find('\0') >= 0 ||
        filename == "." || filename == ".." || (linkSize && memchr(decodedLink, 0, linkSize)) ||
        strcmp(canonicalName, name) || strcmp(canonicalLink, link) ||
        readable < -1 || readable > 1 || executable < -1 || executable > 1 || size < 0)
    {
        listing.diagnostic = "Invalid remote directory entry name";
    }
    else
    {
        RemoteDirectory::Entry entry;
        entry.name = filename;
        entry.link = FXString(reinterpret_cast<char*>(decodedLink), linkSize);
        memset(&entry.info, 0, sizeof(entry.info));
        entry.info.st_mode = mode;
        entry.info.st_uid = uid;
        entry.info.st_gid = gid;
        entry.info.st_mtime = modified;
        entry.info.st_ctime = changed;
        entry.info.st_size = size;
        entry.symlink = symlink;
        entry.readable = readable;
        entry.executable = executable;
        entry.revision = ++queue.revision;
        if (!listing.seen.insert(entry.name.text()).second)
            listing.diagnostic = "Duplicate remote directory entry name";
        else
        {
            auto previous = listing.existing.find(entry.name.text());
            if (previous == listing.existing.end()) listing.rows.push_back(entry);
            else listing.rows[previous->second] = entry;
        }
        listing.dirty = true;
    }
    g_free(decodedName);
    g_free(decodedLink);
    g_free(canonicalName);
    g_free(canonicalLink);
    g_variant_unref(value);
}

long RemoteQueue::tick(FXObject*, FXSelector, void*)
{
    FXbool active = false;
    std::vector<Watch> notify;
    std::vector<std::string> authentication;
    for (auto& pair : listings)
    {
        Listing& listing = pair.second;
        if (listing.completed && listing.diagnostic.empty() &&
            FXThread::time() - listing.completed > 30 * 1000000000LL)
            start(pair.first.c_str(), listing);
        if (listing.fd >= 0)
        {
            char buffer[16384];
            // Bound each tick so large or slow streams cannot monopolize FOX.
            for (int n = 0; n < 8; ++n)
            {
                ssize_t count = read(listing.fd, buffer, sizeof(buffer));
                if (count > 0)
                {
                    listing.buffer.append(buffer, count);
                    if (listing.buffer.length() > 1024 * 1024)
                    {
                        listing.diagnostic = "Remote directory response exceeds record limit";
                        close(listing);
                        listing.exited = true;
                        break;
                    }
                    int end;
                    while ((end = listing.buffer.find('\n')) >= 0)
                    {
                        parse(listing, listing.buffer.left(end));
                        listing.buffer.erase(0, end + 1);
                    }
                }
                else if (count == 0 || (errno != EAGAIN && errno != EINTR))
                {
                    if (count < 0) listing.diagnostic = strerror(errno);
                    ::close(listing.fd);
                    listing.fd = -1;
                    break;
                }
                else break;
            }
        }
        if (listing.pid > 0 && !listing.prompting &&
            FXThread::time() - listing.started > 60 * 1000000000LL)
        {
            listing.diagnostic = "Remote directory loading timed out";
            close(listing);
            listing.exited = true;
        }
        if (listing.exited && listing.fd < 0 && !listing.completed)
        {
            if (listing.input >= 0) ::close(listing.input);
            listing.input = -1;
            if (listing.diagnostic.empty() && (!listing.done || !WIFEXITED(listing.status) ||
                                               WEXITSTATUS(listing.status) != 0))
                listing.diagnostic = "Remote directory helper ended before completing the listing";
            listing.completed = FXThread::time();
            listing.dirty = true;
            if (!listing.diagnostic.empty())
                appLog(APP_LOG_ERRORS, "remote-list-failed", FXString(pair.first.c_str()) + ": " + listing.diagnostic);
            appLog(APP_LOG_FILE_OPERATIONS, "remote-list-completed",
                   FXString("path=\"") + pair.first.c_str() + "\" entries=" + FXStringVal(listing.rows.size()) +
                   " elapsed_ms=" + FXStringVal((listing.completed - listing.started) / 1000000));
        }
        if (listing.dirty)
        {
            notify.insert(notify.end(), listing.watchers.begin(), listing.watchers.end());
            listing.dirty = false;
        }
        active |= !listing.watchers.empty();
        if (listing.authenticate)
        {
            listing.authenticate = false;
            listing.prompting = true;
            authentication.push_back(pair.first);
        }
    }
    for (const std::string& path : authentication)
    {
        FXWindow* owner = app->getActiveWindow();
        if (!owner) owner = app->getRootWindow();
        DialogBox dialog(owner, _("Server authentication"),
                         DECOR_TITLE | DECOR_BORDER | DECOR_CLOSE);
        FXVerticalFrame* frame = new FXVerticalFrame(&dialog, LAYOUT_FILL_X | LAYOUT_FILL_Y);
        new FXLabel(frame, FXString(_("Authentication is required to list shares on ")) + path.c_str(),
                    NULL, JUSTIFY_LEFT);
        FXMatrix* fields = new FXMatrix(frame, 2, MATRIX_BY_COLUMNS | LAYOUT_FILL_X);
        new FXLabel(fields, _("Domain:"));
        FXTextField* domain = new FXTextField(fields, 30, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        domain->setText("WORKGROUP");
        new FXLabel(fields, _("User name:"));
        FXTextField* user = new FXTextField(fields, 30, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        new FXLabel(fields, _("Password:"));
        FXTextField* password = new FXTextField(fields, 30, NULL, 0,
                                              TEXTFIELD_NORMAL | TEXTFIELD_PASSWD | LAYOUT_FILL_X);
        FXHorizontalFrame* buttons = new FXHorizontalFrame(frame, LAYOUT_FILL_X);
        new FXButton(buttons, _("Cancel"), NULL, &dialog, DialogBox::ID_CANCEL, BUTTON_NORMAL);
        new FXButton(buttons, _("Connect"), NULL, &dialog, DialogBox::ID_ACCEPT, BUTTON_NORMAL);
        user->setFocus();
        FXbool accepted = dialog.execute(PLACEMENT_OWNER);
        auto found = listings.find(path);
        if (found == listings.end()) continue;
        Listing& listing = found->second;
        listing.prompting = false;
        if (!accepted)
        {
            listing.diagnostic = _("Share listing cancelled.");
            close(listing);
            listing.exited = true;
            listing.dirty = true;
            continue;
        }
        gchar* encodedUser = g_base64_encode(reinterpret_cast<const guchar*>(user->getText().text()),
                                            user->getText().length());
        gchar* encodedDomain = g_base64_encode(reinterpret_cast<const guchar*>(domain->getText().text()),
                                              domain->getText().length());
        gchar* encodedPassword = g_base64_encode(reinterpret_cast<const guchar*>(password->getText().text()),
                                                password->getText().length());
        FXString credentials = FXString(encodedUser) + "\t" + encodedDomain + "\t" + encodedPassword + "\n";
        g_free(encodedUser);
        g_free(encodedDomain);
        g_free(encodedPassword);
        if (credentials.length() > 4096 || listing.input < 0 ||
            send(listing.input, credentials.text(), credentials.length(), MSG_NOSIGNAL) != credentials.length())
        {
            listing.diagnostic = _("Unable to send server credentials.");
            close(listing);
            listing.exited = true;
            listing.dirty = true;
        }
        listing.started = FXThread::time();
        credentials.clear();
        password->setText("");
    }
    // Callbacks may release subscriptions or request another listing.
    std::set<std::pair<FXObject*, FXSelector>> delivered;
    for (const Watch& watch : notify)
    {
        if (!delivered.insert(std::make_pair(watch.object, watch.selector)).second) continue;
        FXbool subscribed = false;
        for (const auto& pair : listings)
            for (const Watch& current : pair.second.watchers)
                if (watch.object == current.object && watch.selector == current.selector) subscribed = true;
        if (subscribed) watch.object->handle(this, FXSEL(SEL_COMMAND, watch.selector), NULL);
    }
    active |= tickDownloads();
    if (active) app->addTimeout(this, 1, 50);
    return 1;
}

// One download at a time; returns true while work remains.
FXbool RemoteQueue::tickDownloads()
{
    if (downloads.empty()) return false;
    Download& current = downloads.front();
    if (current.pid < 0 && !current.exited)
    {
        int pipefd[2];
        const FXString command = FXString("exec ") + xf_hostcommand(xf_quote(helperPath()) + " --download " +
                                                                    xf_quote(current.url) + " " +
                                                                    xf_quote(current.destination));
        if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) != 0)
        {
            current.output = FXString("ERROR ") + strerror(errno);
            current.exited = true;
        }
        else
        {
            current.pid = fork();
            if (current.pid == 0)
            {
                int flags = fcntl(pipefd[1], F_GETFL);
                if (flags < 0 || fcntl(pipefd[1], F_SETFL, flags & ~O_NONBLOCK) < 0 ||
                    dup2(pipefd[1], STDOUT_FILENO) < 0)
                    _exit(126);
                execl("/bin/sh", "sh", "-c", command.text(), static_cast<char*>(NULL));
                _exit(127);
            }
            ::close(pipefd[1]);
            if (current.pid < 0)
            {
                ::close(pipefd[0]);
                current.output = FXString("ERROR ") + strerror(errno);
                current.exited = true;
            }
            else
            {
                current.fd = pipefd[0];
                appLog(APP_LOG_FILE_OPERATIONS, "media-download-started", current.destination);
            }
        }
    }
    if (current.fd >= 0)
    {
        char buffer[4096];
        ssize_t count;
        while ((count = read(current.fd, buffer, sizeof(buffer))) > 0)
        {
            current.pending.append(buffer, count);
            FXint end;
            while ((end = current.pending.find('\n')) >= 0)
            {
                const FXString line = current.pending.left(end + 1);
                current.pending.erase(0, end + 1);
                if (line.left(9) == "PROGRESS ")
                {
                    current.received = FXLongVal(line.section(' ', 1));
                    current.total = FXLongVal(line.section(' ', 2));
                }
                else if (current.output.length() < 4096)
                {
                    current.output += line;
                }
            }
            if (current.pending.length() > 4096) current.pending.clear();
        }
        if (count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR))
        {
            if (current.output.length() < 4096) current.output += current.pending;
            current.pending.clear();
            ::close(current.fd);
            current.fd = -1;
        }
    }
    if (current.exited && current.fd < 0)
    {
        FXString result = current.output;
        while (!result.empty() && (result[result.length() - 1] == '\n' || result[result.length() - 1] == '\r'))
            result.trunc(result.length() - 1);
        if (result == "DONE" && WIFEXITED(current.status) && WEXITSTATUS(current.status) == 0)
        {
            appLog(APP_LOG_FILE_OPERATIONS, "media-download-finished", current.destination);
        }
        else
        {
            FXString message = result.left(6) == "ERROR " ? result.mid(6, result.length() - 6) :
                               WIFEXITED(current.status) && WEXITSTATUS(current.status) == 127 ?
                               FXString(_("The swordfish-network helper is not available.")) :
                               FXString(_("The download ended unexpectedly."));
            gchar* plain = g_strcompress(message.text());
            message = plain;
            g_free(plain);
            appLog(APP_LOG_ERRORS, "media-download-failed", current.destination + ": " + message);
            if (downloadErrors.length() < 2000)
                downloadErrors += FXPath::name(current.destination) + ": " + message + "\n";
        }
        downloads.erase(downloads.begin());
        ++batchDone;
        if (downloads.empty())
        {
            batchTotal = batchDone = 0;
            if (downloadWindow) downloadWindow->hide();
        }
        if (downloads.empty() && !downloadErrors.empty())
        {
            const FXString errors = downloadErrors;
            downloadErrors.clear();
            FXWindow* owner = app->getActiveWindow();
            if (!owner) owner = app->getRootWindow();
            MessageBox::error(owner, BOX_OK, _("Download failed"), "%s", errors.text());
        }
    }
    showDownloadProgress();
    return !downloads.empty();
}

void RemoteQueue::showDownloadProgress()
{
    if (downloads.empty() || !downloadOwner || (downloadWindow && downloadWindow->dismissed)) return;
    // Quick downloads finish without a window flashing up
    if (!downloadWindow || !downloadWindow->shown())
    {
        if (FXThread::time() - batchStarted < 500000000) return;
        if (!downloadWindow)
        {
            downloadWindow = new DownloadWindow(downloadOwner->getShell());
            downloadWindow->create();
        }
        downloadWindow->show(PLACEMENT_OWNER);
    }
    const Download& current = downloads.front();
    FXString text;
    text.format(_("Downloading %u of %u: %s"), batchDone + 1, batchTotal, FXPath::name(current.destination).text());
    downloadWindow->file->setText(text);
    if (current.total > 0)
    {
        downloadWindow->bar->setTotal(static_cast<FXuint>(current.total / 1024 + 1));
        downloadWindow->bar->setProgress(static_cast<FXuint>(current.received / 1024));
        downloadWindow->amount->setText(byteText(current.received) + _(" of ") + byteText(current.total));
    }
    else
    {
        // Size unknown: the bar follows the number of files instead
        downloadWindow->bar->setTotal(batchTotal);
        downloadWindow->bar->setProgress(batchDone);
        downloadWindow->amount->setText(byteText(current.received) + _(" received"));
    }
}
}

FXbool RemoteDirectory::handles(const FXString& path)
{
    if (isServer(path)) return true;
    // Only mounted GVFS descendants use the new loader; ordinary filesystems stay unchanged.
    int gvfs = path.find("/gvfs/");
    if (path.left(10) != "/run/user/" || gvfs <= 10 || path.length() <= gvfs + 6) return false;
    for (int i = 10; i < gvfs; ++i)
        if (path[i] < '0' || path[i] > '9') return false;
    return true;
}

FXbool RemoteDirectory::isServer(const FXString& path)
{
    if (isMedia(path)) return true;
    if (path.left(6) != "smb://") return false;
    FXString host = path.mid(6, path.length() - 6);
    if (!host.empty() && host[host.length() - 1] == '/') host.trunc(host.length() - 1);
    if (host.empty() || host.length() > 253 || host[0] == '-') return false;
    for (FXint i = 0; i < host.length(); ++i)
        if (!Ascii::isAlphaNumeric(host[i]) && host[i] != '.' && host[i] != '-' && host[i] != '_')
            return false;
    return true;
}

const std::vector<RemoteDirectory::Entry>& RemoteDirectory::entries(
    FXApp* app, const FXString& path, FXObject* object, FXSelector selector)
{
    queue.app = app;
    Listing& listing = queue.listings[key(path)];
    FXbool found = false;
    for (const Watch& watch : listing.watchers)
        if (watch.object == object && watch.selector == selector) found = true;
    if (!found) listing.watchers.push_back({object, selector});
    if (!listing.completed && listing.pid < 0 && listing.fd < 0) queue.start(path, listing);
    return listing.rows;
}

FXbool RemoteDirectory::loading(const FXString& path)
{
    auto it = queue.listings.find(key(path));
    return it != queue.listings.end() && !it->second.completed;
}

FXbool RemoteDirectory::loading(FXObject* object)
{
    for (const auto& pair : queue.listings)
        if (!pair.second.completed)
            for (const Watch& watch : pair.second.watchers)
                if (watch.object == object) return true;
    return false;
}

FXString RemoteDirectory::error(const FXString& path)
{
    auto it = queue.listings.find(key(path));
    return it == queue.listings.end() ? FXString::null : it->second.diagnostic;
}

void RemoteDirectory::refresh(const FXString& path)
{
    auto it = queue.listings.find(key(path));
    if (it != queue.listings.end() && it->second.completed)
    {
        it->second.completed = 0;
        queue.start(path, it->second);
    }
}

void RemoteDirectory::release(FXObject* object, const FXString& subtree)
{
    const std::string root = subtree.empty() ? "" : key(subtree);
    for (auto it = queue.listings.begin(); it != queue.listings.end();)
    {
        if (!root.empty() && it->first != root && it->first.compare(0, root.length() + 1, root + "/") != 0)
        {
            ++it;
            continue;
        }
        auto& watchers = it->second.watchers;
        for (auto watch = watchers.begin(); watch != watchers.end();)
            if (watch->object == object) watch = watchers.erase(watch);
            else ++watch;
        if (watchers.empty())
        {
            queue.close(it->second);
            it = queue.listings.erase(it);
        }
        else ++it;
    }
}

FXbool RemoteDirectory::harvest(pid_t pid, int status)
{
    if (queue.cancelled.erase(pid)) return true;
    for (Download& download : queue.downloads)
    {
        if (download.pid == pid)
        {
            download.pid = -1;
            download.status = status;
            download.exited = true;
            return true;
        }
    }
    for (auto& pair : queue.listings)
    {
        if (pair.second.pid == pid)
        {
            pair.second.pid = -1;
            pair.second.status = status;
            pair.second.exited = true;
            return true;
        }
    }
    return false;
}

namespace
{
std::map<std::string, FXString>& hostNames()
{
    static std::map<std::string, FXString> names;
    return names;
}
}

void RemoteDirectory::setHostName(const FXString& host, const FXString& name)
{
    FXString lower = host;
    lower.lower();
    if (name.empty() || comparecase(name, host) == 0)
        hostNames().erase(lower.text());
    else
        hostNames()[lower.text()] = name;
}

FXString RemoteDirectory::hostName(const FXString& host)
{
    FXString lower = host;
    lower.lower();
    auto it = hostNames().find(lower.text());
    return it == hostNames().end() ? host : it->second;
}

static FXString mountField(const FXString& name, const char* key)
{
    int pos = name.find(key);
    if (pos < 0) return FXString::null;
    pos += strlen(key);
    return name.mid(pos, name.length() - pos).before(',').substitute("%20", " ");
}

FXString RemoteDirectory::mountLabel(const FXString& gvfsname)
{
    if (gvfsname.left(10) == "smb-share:")
        return mountField(gvfsname, "share=") + "@" + hostName(mountField(gvfsname, "server="));
    if (gvfsname.left(4) == "mtp:") return gvfsname.after('_').before('_', 2);
    // sftp, ftp, ftps and dav mounts
    if (gvfsname.find(":host=") >= 0)
    {
        FXString user = mountField(gvfsname, "user=");
        return (user.empty() ? FXString::null : user + "@") + mountField(gvfsname, "host=");
    }
    return FXString::null;
}

// Short form of a folder for the window title: GVFS mounts as share@computer/subfolder
FXString RemoteDirectory::titlePath(const FXString& path)
{
    int gvfs = path.find("/gvfs/");
    if (gvfs >= 0)
    {
        FXString rest = path.mid(gvfs + 6, path.length() - gvfs - 6);
        FXString label = mountLabel(rest.before('/'));
        if (!label.empty())
            return rest.find('/') < 0 ? label : label + "/" + rest.after('/');
    }
    return displayPath(path);
}

FXString RemoteDirectory::displayPath(const FXString& path)
{
    if (isMedia(path))
    {
        auto server = mediaServers().find(mediaAuthority(path));
        FXString result = "dlna://" + (server == mediaServers().end() || server->second.name.empty() ?
                                       FXString(mediaAuthority(path).c_str()) : server->second.name);
        FXString rest = path.mid(7, path.length() - 7);
        if (rest.find('/') < 0) return result;
        rest = rest.after('/');
        gchar* plain = g_uri_unescape_string(rest.text(), NULL);
        result += FXString("/") + (plain ? plain : rest.text());
        g_free(plain);
        return result;
    }
    if (comparecase(path.left(6), "smb://") != 0) return path;
    FXString rest = path.mid(6, path.length() - 6);
    FXString host = rest.before('/');
    FXString name = hostName(host);
    return name == host ? path : "smb://" + name + rest.mid(host.length(), rest.length() - host.length());
}

FXbool RemoteDirectory::isMedia(const FXString& path)
{
    if (comparecase(path.left(7), "dlna://") != 0) return false;
    const FXString rest = path.mid(7, path.length() - 7);
    const FXString authority = rest.before('/');
    const FXString host = authority.before(':');
    const FXString port = authority.after(':');
    if (host.empty() || host.length() > 253 || host[0] == '-' || port.empty() || port.length() > 5 ||
        authority.contains(':') != 1)
        return false;
    for (FXint i = 0; i < host.length(); ++i)
        if (!Ascii::isAlphaNumeric(host[i]) && host[i] != '.' && host[i] != '-' && host[i] != '_') return false;
    for (FXint i = 0; i < port.length(); ++i)
        if (!Ascii::isDigit(port[i])) return false;
    if (FXIntVal(port) <= 0 || FXIntVal(port) > 65535) return false;
    FXString folders = rest.mid(authority.length(), rest.length() - authority.length());
    while (folders.length() > 0 && folders[folders.length() - 1] == '/') folders.trunc(folders.length() - 1);
    if (folders.empty()) return true;
    for (FXint i = 0; i < folders.length(); ++i)
        if (static_cast<FXuchar>(folders[i]) < 0x20 || folders[i] == 0x7f) return false;
    const FXint count = folders.contains('/');
    for (FXint i = 1; i <= count; ++i)
    {
        const FXString segment = folders.section('/', i);
        if (segment.empty() || segment == "." || segment == "..") return false;
    }
    return true;
}

FXString RemoteDirectory::mediaRoot(const FXString& location)
{
    if (comparecase(location.left(7), "http://") != 0) return FXString::null;
    FXString authority = location.mid(7, location.length() - 7).before('/');
    if (authority.find('@') >= 0) return FXString::null;
    if (authority.find(':') < 0) authority += ":80";
    authority.lower();
    const FXString root = "dlna://" + authority;
    if (!isMedia(root)) return FXString::null;
    for (FXint i = 0; i < location.length(); ++i)
        if (static_cast<FXuchar>(location[i]) <= 0x20 || location[i] == 0x7f) return FXString::null;
    return root;
}

FXString RemoteDirectory::addMediaServer(const FXString& location, const FXString& name)
{
    const FXString root = mediaRoot(location);
    if (!root.empty()) mediaServers()[mediaAuthority(root)] = MediaServer{location, name};
    return root;
}

FXString RemoteDirectory::mediaServerLocation(const FXString& path)
{
    if (!isMedia(path)) return FXString::null;
    auto server = mediaServers().find(mediaAuthority(path));
    return server == mediaServers().end() ? FXString::null : server->second.location;
}

FXString RemoteDirectory::mediaParent(const FXString& path)
{
    if (!isMedia(path)) return FXString::null;
    FXString clean = path;
    while (clean.length() > 0 && clean[clean.length() - 1] == '/') clean.trunc(clean.length() - 1);
    const FXint slash = clean.rfind('/');
    return slash < 7 + (FXint)mediaAuthority(path).size() ? FXString::null : clean.left(slash);
}

FXString RemoteDirectory::mediaLabel(const FXString& path)
{
    const FXString parent = mediaParent(path);
    if (parent.empty())
    {
        auto server = mediaServers().find(mediaAuthority(path));
        return server == mediaServers().end() || server->second.name.empty() ?
               FXString(mediaAuthority(path).c_str()) : server->second.name;
    }
    FXString segment = path.mid(parent.length() + 1, path.length() - parent.length() - 1);
    while (segment.length() > 0 && segment[segment.length() - 1] == '/') segment.trunc(segment.length() - 1);
    gchar* plain = g_uri_unescape_string(segment.text(), NULL);
    const FXString label = plain ? plain : segment.text();
    g_free(plain);
    return label;
}

FXString RemoteDirectory::mediaLink(const FXString& path)
{
    const FXString parent = mediaParent(path);
    if (parent.empty()) return FXString::null;
    auto listing = queue.listings.find(key(parent));
    if (listing == queue.listings.end()) return FXString::null;
    const FXString name = mediaLabel(path);
    for (const Entry& entry : listing->second.rows)
        if (entry.name == name && S_ISREG(entry.info.st_mode)) return entry.link;
    return FXString::null;
}

void RemoteDirectory::download(FXWindow* owner, const FXString& url, const FXString& destination)
{
    queue.app = owner->getApp();
    queue.downloadOwner = owner;
    if (queue.downloads.empty())
    {
        queue.batchStarted = FXThread::time();
        if (queue.downloadWindow) queue.downloadWindow->dismissed = false;
    }
    Download download;
    download.url = url;
    download.destination = destination;
    queue.downloads.push_back(download);
    ++queue.batchTotal;
    queue.app->addTimeout(&queue, 1, 50);
}

void RemoteDirectory::cancelDownloads()
{
    if (queue.downloads.empty()) return;
    Download& current = queue.downloads.front();
    if (current.pid > 0)
    {
        kill(current.pid, SIGTERM);
        queue.cancelled.insert(current.pid);
        // The helper removes it too; this covers a helper killed before it could
        unlink((current.destination + ".part").text());
    }
    if (current.fd >= 0) ::close(current.fd);
    appLog(APP_LOG_FILE_OPERATIONS, "media-download-cancelled",
           FXStringVal((FXuint)queue.downloads.size()) + " remaining, current " + current.destination);
    queue.downloads.clear();
    queue.batchTotal = queue.batchDone = 0;
    if (queue.downloadWindow) queue.downloadWindow->hide();
}

FXuint RemoteDirectory::downloadsPending()
{
    return queue.downloads.size();
}
