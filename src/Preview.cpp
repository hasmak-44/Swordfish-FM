#include "config.h"
#include "Preview.h"
#include "AppLogger.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <deque>
#include <set>
#include <vector>
#include <algorithm>
#include <string>

namespace
{
struct Job
{
    FXString source, output, temporary, diagnostic;
    FXint size;
};

class PreviewQueue : public FXObject
{
    FXDECLARE(PreviewQueue)
public:
    enum { ID_TICK = 1 };
    FXApp* app = NULL;
    std::deque<Job> pending;
    std::set<std::string> known;
    Job active;
    pid_t child = -1;
    FXTime started = 0;
    FXulong version = 0;
    FXbool timedOut = false;
    FXString directory;
    FXbool cacheError = false;

    long tick(FXObject*, FXSelector, void*);
    void completed(int status);
    void stop();
    void prune();
    ~PreviewQueue() { stop(); }
};

FXDEFMAP(PreviewQueue) PreviewQueueMap[] =
{
    FXMAPFUNC(SEL_TIMEOUT, PreviewQueue::ID_TICK, PreviewQueue::tick)
};
FXIMPLEMENT(PreviewQueue, FXObject, PreviewQueueMap, ARRAYNUMBER(PreviewQueueMap))
PreviewQueue queue;

void PreviewQueue::stop()
{
    if (child > 0)
    {
        kill(-child, SIGKILL);
        kill(child, SIGKILL);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
        child = -1;
        unlink(active.temporary.text());
        unlink(active.diagnostic.text());
    }
}

void PreviewQueue::prune()
{
    struct Entry { FXString path; time_t time; off_t size; };
    std::vector<Entry> entries;
    off_t bytes = 0;
    GDir* dir = g_dir_open(directory.text(), 0, NULL);
    if (!dir) return;
    const char* name;
    while ((name = g_dir_read_name(dir)))
    {
        FXString path = directory + "/" + name;
        struct stat info;
        if (path.right(4) == ".png" && lstat(path.text(), &info) == 0 && S_ISREG(info.st_mode))
        {
            entries.push_back({path, info.st_mtime, info.st_size});
            bytes += info.st_size;
        }
    }
    g_dir_close(dir);
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.time < b.time; });
    for (size_t i = 0; i < entries.size() && bytes > 128 * 1024 * 1024; ++i)
    {
        if (unlink(entries[i].path.text()) == 0) bytes -= entries[i].size;
        else appLog(APP_LOG_ERRORS, "preview-cache-cleanup-failed", entries[i].path);
    }
}

void PreviewQueue::completed(int status)
{
    child = -1;
    struct stat info;
    if (!timedOut && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
        stat(active.temporary.text(), &info) == 0 && info.st_size > 0)
    {
        if (rename(active.temporary.text(), active.output.text()) != 0)
            appLog(APP_LOG_ERRORS, "preview-cache-publish-failed", active.source + ": " + strerror(errno));
        else known.erase(active.output.text());
    }
    else
    {
        FXString reason = timedOut ? "time limit exceeded" : "decoder failed";
        FXFile log;
        if (log.open(active.diagnostic, FXIO::Reading))
        {
            char buffer[1024];
            FXival length = log.readBlock(buffer, sizeof(buffer));
            if (length > 0) reason = FXString(buffer, length).trim();
        }
        appLog(APP_LOG_ERRORS, "preview-generation-failed", active.source + ": " + reason);
    }
    unlink(active.temporary.text());
    unlink(active.diagnostic.text());
    ++version;
    if (version % 32 == 0) prune();
}

long PreviewQueue::tick(FXObject*, FXSelector, void*)
{
    if (child > 0)
    {
        int status;
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) completed(status);
        else if (result < 0 && errno != EINTR)
        {
            appLog(APP_LOG_ERRORS, "preview-child-status-failed", strerror(errno));
            completed(1 << 8);
        }
        if (child > 0 && FXThread::time() - started > 12 * 1000000000LL && !timedOut)
        {
            timedOut = true;
            kill(-child, SIGKILL);
            kill(child, SIGKILL);
        }
    }
    if (child < 0 && !pending.empty())
    {
        active = pending.front();
        pending.pop_front();
        active.temporary = active.output + ".XXXXXX";
        std::vector<char> name(active.temporary.text(), active.temporary.text() + active.temporary.length() + 1);
        int output = g_mkstemp(name.data());
        if (output < 0)
        {
            appLog(APP_LOG_ERRORS, "preview-cache-create-failed", strerror(errno));
            ++version;
        }
        else
        {
            close(output);
            timedOut = false;
            active.temporary = name.data();
            active.diagnostic = active.temporary + ".log";
            FXString executable;
            // Resolve our own installed directory, never a program supplied in the file name.
            char self[4096];
            ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
            if (length > 0)
            {
                self[length] = 0;
                executable = FXPath::directory(self) + "/swordfish-preview";
            }
            else executable = "swordfish-preview";
            FXString size = FXStringVal(active.size);
            child = fork();
            if (child == 0)
            {
                setpgid(0, 0);
                struct rlimit memory = {512 * 1024 * 1024, 512 * 1024 * 1024};
                struct rlimit cpu = {8, 8};
                struct rlimit file = {16 * 1024 * 1024, 16 * 1024 * 1024};
                if (setrlimit(RLIMIT_AS, &memory) != 0 || setrlimit(RLIMIT_CPU, &cpu) != 0 ||
                    setrlimit(RLIMIT_FSIZE, &file) != 0)
                {
                    perror("preview resource limits");
                    _exit(126);
                }
                int log = open(active.diagnostic.text(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (log >= 0) { dup2(log, STDERR_FILENO); close(log); }
                execlp(executable.text(), executable.text(), active.source.text(),
                       active.temporary.text(), size.text(), static_cast<char*>(NULL));
                perror("swordfish-preview");
                _exit(127);
            }
            if (child < 0)
            {
                appLog(APP_LOG_ERRORS, "preview-start-failed", strerror(errno));
                completed(1 << 8);
            }
            else
            {
                setpgid(child, child);
                started = FXThread::time();
                timedOut = false;
            }
        }
    }
    if (child > 0 || !pending.empty()) app->addTimeout(this, ID_TICK, 100);
    return 1;
}
}

FXString Preview::mimeType(const FXString& path)
{
    char buffer[4096];
    int fd = open(path.text(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return "";
    struct stat info;
    if (fstat(fd, &info) < 0 || !S_ISREG(info.st_mode)) { close(fd); return ""; }
    ssize_t count = read(fd, buffer, sizeof(buffer));
    close(fd);
    if (count < 0) return "";
    gchar* type = g_content_type_guess(path.text(), reinterpret_cast<guchar*>(buffer), count, NULL);
    gchar* mime = type ? g_content_type_get_mime_type(type) : NULL;
    FXString result = mime ? mime : "";
    g_free(mime);
    g_free(type);
    return result;
}

FXbool Preview::textType(const FXString& mime)
{
    return mime.left(6) != "image/" && (g_content_type_is_a(mime.text(), "text/plain") ||
           mime == "application/json" || mime == "application/xml");
}

FXString Preview::request(FXApp* app, const FXString& path, FXint size, FXbool priority)
{
    struct stat info;
    if (stat(path.text(), &info) != 0 || !S_ISREG(info.st_mode)) return "";
    queue.app = app;
    size = FXCLAMP(32, size, 512);
    FXString identity = FXPath::absolute(path) + ":" + FXStringVal(info.st_dev) + ":" +
                        FXStringVal(info.st_ino) + ":" + FXStringVal(info.st_size) + ":" +
                        FXStringVal(info.st_mtim.tv_sec) + ":" + FXStringVal(info.st_mtim.tv_nsec) +
                        ":" + FXStringVal(size) + ":v1";
    gchar* digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, identity.text(), identity.length());
    if (queue.directory.empty())
    {
        queue.directory = FXString(g_get_user_cache_dir()) + "/swordfish/previews";
        queue.prune();
    }
    FXString output = queue.directory + "/" + digest + ".png";
    g_free(digest);
    if (FXStat::isFile(output)) return output;
    if (queue.known.count(output.text()))
    {
        if (priority)
        {
            for (std::deque<Job>::iterator job = queue.pending.begin(); job != queue.pending.end(); ++job)
            {
                if (job->output == output)
                {
                    Job selected = *job;
                    queue.pending.erase(job);
                    queue.pending.push_front(selected);
                    break;
                }
            }
        }
        return "";
    }
    if (queue.pending.size() >= 128 && !priority) return "";
    FXString mime = mimeType(path);
    if (mime.left(6) != "image/" && mime.left(6) != "video/" && mime != "application/pdf") return "";
    if (g_mkdir_with_parents(queue.directory.text(), 0700) != 0)
    {
        if (!queue.cacheError) appLog(APP_LOG_ERRORS, "preview-cache-unavailable", strerror(errno));
        queue.cacheError = true;
        return "";
    }
    Job job = { FXPath::absolute(path), output, "", "", size };
    if (queue.pending.size() >= 128)
    {
        queue.known.erase(queue.pending.back().output.text());
        queue.pending.pop_back();
    }
    if (priority) queue.pending.push_front(job);
    else queue.pending.push_back(job);
    queue.known.insert(output.text());
    app->addTimeout(&queue, PreviewQueue::ID_TICK, 1);
    return "";
}

FXulong Preview::revision() { return queue.version; }
FXbool Preview::harvest(pid_t pid, int status)
{
    if (pid != queue.child) return false;
    queue.completed(status);
    return true;
}
void Preview::shutdown()
{
    if (queue.app) queue.app->removeTimeout(&queue, PreviewQueue::ID_TICK);
    queue.stop();
    queue.pending.clear();
    queue.known.clear();
    queue.app = NULL;
}
