#include "config.h"
#include "RemoteDirectory.h"
#include "AppLogger.h"
#include "xfeutils.h"
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
    int fd = -1, status = 0;
    FXbool done = false, exited = false, dirty = false;
    FXTime started = 0, completed = 0;
};

class RemoteQueue : public FXObject
{
    FXDECLARE(RemoteQueue)
public:
    FXApp* app = NULL;
    std::map<std::string, Listing> listings;
    std::set<pid_t> cancelled;
    FXulong revision = 0;
    long tick(FXObject*, FXSelector, void*);
    void start(const FXString&, Listing&);
    void close(Listing& listing)
    {
        if (listing.pid > 0)
        {
            kill(listing.pid, SIGKILL);
            cancelled.insert(listing.pid);
        }
        if (listing.fd >= 0) ::close(listing.fd);
        listing.pid = -1;
        listing.fd = -1;
    }
};
FXDEFMAP(RemoteQueue) map[] = { FXMAPFUNC(SEL_TIMEOUT, 1, RemoteQueue::tick) };
FXIMPLEMENT(RemoteQueue, FXObject, map, ARRAYNUMBER(map))
RemoteQueue queue;

std::string key(const FXString& path)
{
    FXString result = FXPath::simplify(path);
    while (result.length() > 1 && result[result.length() - 1] == '/') result.trunc(result.length() - 1);
    return result.text();
}

void RemoteQueue::start(const FXString& path, Listing& listing)
{
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
    char self[4096];
    ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
    FXString helper = "swordfish-network";
    if (length > 0)
    {
        self[length] = 0;
        helper = FXPath::directory(self) + "/swordfish-network";
    }
    FXString command = FXString("exec ") + xf_hostcommand(xf_quote(helper) + " --list " + xf_quote(path));
    listing.pid = fork();
    if (listing.pid == 0)
    {
        ::close(pipefd[0]);
        int flags = fcntl(pipefd[1], F_GETFL);
        if (flags < 0 || fcntl(pipefd[1], F_SETFL, flags & ~O_NONBLOCK) < 0 ||
            dup2(pipefd[1], STDOUT_FILENO) < 0)
            _exit(126);
        ::close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", command.text(), static_cast<char*>(NULL));
        _exit(127);
    }
    ::close(pipefd[1]);
    if (listing.pid < 0)
    {
        ::close(pipefd[0]);
        listing.diagnostic = strerror(errno);
        listing.completed = FXThread::time();
        listing.dirty = true;
        appLog(APP_LOG_ERRORS, "remote-list-start-failed", path + ": " + listing.diagnostic);
    }
    else
    {
        listing.fd = pipefd[0];
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
        if (listing.pid > 0 && FXThread::time() - listing.started > 60 * 1000000000LL)
        {
            listing.diagnostic = "Remote directory loading timed out";
            close(listing);
            listing.exited = true;
        }
        if (listing.exited && listing.fd < 0 && !listing.completed)
        {
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
    if (active) app->addTimeout(this, 1, 50);
    return 1;
}
}

FXbool RemoteDirectory::handles(const FXString& path)
{
    // Only mounted GVFS descendants use the new loader; ordinary filesystems stay unchanged.
    int gvfs = path.find("/gvfs/");
    if (path.left(10) != "/run/user/" || gvfs <= 10 || path.length() <= gvfs + 6) return false;
    for (int i = 10; i < gvfs; ++i)
        if (path[i] < '0' || path[i] > '9') return false;
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
