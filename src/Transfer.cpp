#include "config.h"
#include "i18n.h"

#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <limits>
#include <map>
#include <stdint.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#if defined(linux)
#include <sys/syscall.h>
#endif

#include <glib.h>

#include "AppLogger.h"
#include "xfedefs.h"
#include "File.h"
#include "FilePanel.h"
#include "Transfer.h"
#include "XFileExplorer.h"
#include "icons.h"
#include "xfeutils.h"

extern FXString execpath;
extern FXint scaleint;

namespace
{
const uint32_t transferMagic = 0x53465433;
const size_t maxRequestSize = 64 * 1024 * 1024;
const size_t maxStringSize = 1024 * 1024;

struct SettingKey
{
    const char* section;
    const char* name;
};

struct PathIdentity
{
    uint64_t device = 0;
    uint64_t inode = 0;
    uint32_t type = 0;
    uint32_t exists = 0;
    int64_t changeSeconds = 0;
    uint32_t changeNanoseconds = 0;
    int64_t modifiedSeconds = 0;
    uint32_t modifiedNanoseconds = 0;
    int64_t size = 0;
    uint64_t links = 0;
};

const SettingKey settingKeys[] =
{
    {"OPTIONS", "copysuffix"}, {"OPTIONS", "copysuffix_pos"}, {"OPTIONS", "confirm_overwrite"},
    {"OPTIONS", "preserve_date_warn"}, {"OPTIONS", "verify_copy"}, {"OPTIONS", "use_trash_can"},
    {"OPTIONS", "log_level"}, {"SETTINGS", "screenres"}, {"SETTINGS", "font"},
    {"SETTINGS", "time_format"}, {"SETTINGS", "iconpath"}, {"SETTINGS", "theme_details"},
    {"SETTINGS", "theme_asset_folder"}, {"SETTINGS", "list_background_effect"},
    {"SETTINGS", "bar_shading"}, {"SETTINGS", "basecolor"}, {"SETTINGS", "bordercolor"},
    {"SETTINGS", "backcolor"}, {"SETTINGS", "forecolor"}, {"SETTINGS", "selbackcolor"},
    {"SETTINGS", "selforecolor"}, {"SETTINGS", "listbackcolor"}, {"SETTINGS", "listforecolor"},
    {"SETTINGS", "highlightcolor"}, {"SETTINGS", "pbarcolor"}, {"SETTINGS", "attentioncolor"},
    {"SETTINGS", "scrollbarcolor"}
};

struct Request
{
    FXuint operation;
    std::vector<TransferItem> items;
    std::vector<FXbool> targetDirectories;
    std::vector<PathIdentity> sourceIdentities;
    std::vector<PathIdentity> targetParentIdentities;
    std::vector<FXString> settings;
    std::vector<FXbool> present;
};

FXbool writeBytes(int fd, const void* data, size_t length)
{
    const char* bytes = static_cast<const char*>(data);
    while (length)
    {
        ssize_t written = write(fd, bytes, length);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        bytes += written;
        length -= written;
    }
    return true;
}

FXbool readBytes(int fd, void* data, size_t length)
{
    char* bytes = static_cast<char*>(data);
    while (length)
    {
        ssize_t count = read(fd, bytes, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        bytes += count;
        length -= count;
    }
    return true;
}

FXbool writeString(int fd, const FXString& value)
{
    if (value.length() < 0 || static_cast<size_t>(value.length()) > maxStringSize)
    {
        errno = E2BIG;
        return false;
    }
    uint32_t length = value.length();
    return writeBytes(fd, &length, sizeof(length)) && writeBytes(fd, value.text(), length);
}

FXbool readString(int fd, FXString& value)
{
    uint32_t length;
    if (!readBytes(fd, &length, sizeof(length)) || length > maxStringSize) return false;
    std::vector<char> bytes(length);
    if (length && !readBytes(fd, bytes.data(), length)) return false;
    if (length && memchr(bytes.data(), '\0', length)) return false;
    value = length ? FXString(bytes.data(), length) : FXString::null;
    return true;
}

PathIdentity identityFromStat(const struct stat& info)
{
    PathIdentity identity;
    identity.device = static_cast<uint64_t>(info.st_dev);
    identity.inode = static_cast<uint64_t>(info.st_ino);
    identity.type = info.st_mode & S_IFMT;
    identity.exists = 1;
    identity.changeSeconds = static_cast<int64_t>(info.st_ctim.tv_sec);
    identity.changeNanoseconds = static_cast<uint32_t>(info.st_ctim.tv_nsec);
    identity.modifiedSeconds = static_cast<int64_t>(info.st_mtim.tv_sec);
    identity.modifiedNanoseconds = static_cast<uint32_t>(info.st_mtim.tv_nsec);
    identity.size = static_cast<int64_t>(info.st_size);
    identity.links = static_cast<uint64_t>(info.st_nlink);
    return identity;
}

FXbool pathIdentity(const FXString& path, FXbool follow, PathIdentity& identity)
{
    struct stat info;
    if ((follow ? stat(path.text(), &info) : lstat(path.text(), &info)) != 0)
    {
        if (errno != ENOENT && errno != ENOTDIR) return false;
        identity = PathIdentity();
        return true;
    }
    identity = identityFromStat(info);
    return true;
}

FXbool sameIdentity(const PathIdentity& a, const PathIdentity& b, FXbool checkChange = false)
{
    return a.exists == b.exists && a.device == b.device && a.inode == b.inode && a.type == b.type &&
           (!checkChange || (a.changeSeconds == b.changeSeconds && a.changeNanoseconds == b.changeNanoseconds));
}

FXbool writeIdentity(int fd, const PathIdentity& identity)
{
    return writeBytes(fd, &identity.device, sizeof(identity.device)) &&
           writeBytes(fd, &identity.inode, sizeof(identity.inode)) &&
           writeBytes(fd, &identity.type, sizeof(identity.type)) &&
           writeBytes(fd, &identity.exists, sizeof(identity.exists)) &&
           writeBytes(fd, &identity.changeSeconds, sizeof(identity.changeSeconds)) &&
           writeBytes(fd, &identity.changeNanoseconds, sizeof(identity.changeNanoseconds)) &&
           writeBytes(fd, &identity.modifiedSeconds, sizeof(identity.modifiedSeconds)) &&
           writeBytes(fd, &identity.modifiedNanoseconds, sizeof(identity.modifiedNanoseconds)) &&
           writeBytes(fd, &identity.size, sizeof(identity.size)) &&
           writeBytes(fd, &identity.links, sizeof(identity.links));
}

FXbool readIdentity(int fd, PathIdentity& identity)
{
    return readBytes(fd, &identity.device, sizeof(identity.device)) &&
           readBytes(fd, &identity.inode, sizeof(identity.inode)) &&
           readBytes(fd, &identity.type, sizeof(identity.type)) &&
           readBytes(fd, &identity.exists, sizeof(identity.exists)) &&
           readBytes(fd, &identity.changeSeconds, sizeof(identity.changeSeconds)) &&
           readBytes(fd, &identity.changeNanoseconds, sizeof(identity.changeNanoseconds)) &&
           readBytes(fd, &identity.modifiedSeconds, sizeof(identity.modifiedSeconds)) &&
           readBytes(fd, &identity.modifiedNanoseconds, sizeof(identity.modifiedNanoseconds)) &&
           readBytes(fd, &identity.size, sizeof(identity.size)) &&
           readBytes(fd, &identity.links, sizeof(identity.links)) &&
           identity.exists <= 1 && identity.changeNanoseconds < 1000000000 &&
           identity.modifiedNanoseconds < 1000000000;
}

FXString targetParent(const FXString& target, FXbool directory)
{
    return directory ? target : FXPath::directory(FXPath::absolute(target));
}

FXbool checkAvailableSpace(File* progress, const Request& request, const FXString& trashFiles,
                           FXbool useTrash)
{
    struct SpaceRequirement
    {
        FXString path;
        FXulong required = 0;
        FXulong available = 0;
    };
    std::map<dev_t, SpaceRequirement> requirements;

    for (size_t i = 0; i < request.items.size(); ++i)
    {
        const TransferItem& item = request.items[i];
        FXString destination = item.target;
        FXString directory;
        if (useTrash && destination == trashFiles) directory = trashFiles;
        else directory = targetParent(destination, request.targetDirectories[i]);

        if (item.source.contains("mtp:host=") || directory.contains("mtp:host="))
            continue;

        FXint locality = xf_filesystemlocality(directory);
        if (locality < 0)
        {
            int failure = errno;
            MessageBox::error(progress, BOX_OK, _("Can't check available space"),
                              _("Can't check the destination filesystem for %s: %s"),
                              directory.text(), strerror(failure));
            return false;
        }
        if (locality == 0) continue;

        struct stat targetInfo;
        if (stat(directory.text(), &targetInfo) != 0)
        {
            int failure = errno;
            MessageBox::error(progress, BOX_OK, _("Can't check available space"),
                              _("Can't check the destination folder %s: %s"),
                              directory.text(), strerror(failure));
            return false;
        }

        if (request.operation == MOVE)
        {
            struct stat sourceInfo;
            if (lstat(item.source.text(), &sourceInfo) == 0 && sourceInfo.st_dev == targetInfo.st_dev)
                continue;
        }

        FXulong bytes = progress->sourceSize(item.source);
        SpaceRequirement& requirement = requirements[targetInfo.st_dev];
        if (requirement.path.empty())
        {
            requirement.path = directory;
            struct statvfs space;
            if (statvfs(directory.text(), &space) != 0)
            {
                int failure = errno;
                MessageBox::error(progress, BOX_OK, _("Can't check available space"),
                                  _("Can't check available space in %s: %s"),
                                  directory.text(), strerror(failure));
                return false;
            }
            FXulong blockSize = space.f_frsize ? space.f_frsize : space.f_bsize;
            FXulong blocks = geteuid() == 0 ? space.f_bfree : space.f_bavail;
            requirement.available = blockSize &&
                                    blocks > std::numeric_limits<FXulong>::max() / blockSize ?
                                    std::numeric_limits<FXulong>::max() : blocks * blockSize;
        }
        if (bytes > static_cast<FXulong>(std::numeric_limits<FXulong>::max() - requirement.required))
        {
            MessageBox::error(progress, BOX_OK, _("Can't check available space"),
                              _("The transfer size is too large to check safely."));
            return false;
        }
        requirement.required += bytes;
    }

    for (const auto& entry : requirements)
    {
        const SpaceRequirement& requirement = entry.second;
        if (requirement.required > requirement.available)
        {
            FXString needed = xf_humansize(FXStringVal(requirement.required).text());
            FXString available = xf_humansize(FXStringVal(requirement.available).text());
            MessageBox::error(progress, BOX_OK, _("Not enough space"),
                              _("The transfer needs about %s, but only %s is available in %s."),
                              needed.text(), available.text(), requirement.path.text());
            return false;
        }
    }
    return true;
}


// Keep destination directories alive so a replacement cannot reuse their inode.
class DirectoryPins
{
    std::map<FXString, int> paths;
public:
    std::vector<int> descriptors;
    ~DirectoryPins()
    {
        for (int fd : descriptors) close(fd);
    }

    FXbool add(const FXString& path, const PathIdentity& expected)
    {
        if (!expected.exists) return true;
        if (expected.type != S_IFDIR) { errno = ENOTDIR; return false; }
        std::map<FXString, int>::const_iterator existing = paths.find(path);
        int fd = existing == paths.end() ? -1 : existing->second;
        if (fd < 0)
        {
            int flags = O_DIRECTORY | O_CLOEXEC;
#ifdef O_PATH
            flags |= O_PATH;
#else
            flags |= O_RDONLY;
#endif
            int opened = open(path.text(), flags);
            if (opened < 0) return false;
            fd = fcntl(opened, F_DUPFD_CLOEXEC, 5);
            int failure = errno;
            close(opened);
            if (fd < 0) { errno = failure; return false; }
            paths[path] = fd;
            descriptors.push_back(fd);
        }
        struct stat info;
        if (fstat(fd, &info) != 0) return false;
        if (!sameIdentity(identityFromStat(info), expected, true)) { errno = ESTALE; return false; }
        return true;
    }
};

FXbool writeRequest(int fd, FXApp* app, FXuint operation, const std::vector<TransferItem>& items,
                    DirectoryPins& pins)
{
    if ((operation != COPY && operation != MOVE) || items.empty() || items.size() > 1000000)
    {
        errno = EINVAL;
        return false;
    }
    uint32_t header[] = {transferMagic, operation, static_cast<uint32_t>(items.size())};
    if (!writeBytes(fd, header, sizeof(header))) return false;
    for (const TransferItem& item : items)
    {
        uint32_t restore = item.restore ? 1 : 0;
        if (item.source.empty() || item.target.empty())
        {
            errno = EINVAL;
            return false;
        }
        PathIdentity source, target, parent;
        if (!pathIdentity(item.source, false, source) || !pathIdentity(item.target, true, target)) return false;
        uint32_t directory = target.exists && target.type == S_IFDIR;
        if (directory) parent = target;
        else if (!pathIdentity(targetParent(item.target, false), true, parent)) return false;
        if (!pins.add(targetParent(item.target, directory != 0), parent)) return false;
        if (!writeString(fd, item.source) || !writeString(fd, item.target) ||
            !writeBytes(fd, &restore, sizeof(restore)) ||
            !writeBytes(fd, &directory, sizeof(directory)) ||
            !writeIdentity(fd, source) || !writeIdentity(fd, parent)) return false;
    }
    uint32_t pinCount = static_cast<uint32_t>(pins.descriptors.size());
    if (!writeBytes(fd, &pinCount, sizeof(pinCount))) return false;
    for (const SettingKey& key : settingKeys)
    {
        const char* value = app->reg().readStringEntry(key.section, key.name, NULL);
        uint32_t present = value != NULL;
        if (!writeBytes(fd, &present, sizeof(present)) ||
            (present && !writeString(fd, FXString(value)))) return false;
    }
    off_t size = lseek(fd, 0, SEEK_CUR);
    if (size < 0 || static_cast<size_t>(size) > maxRequestSize || lseek(fd, 0, SEEK_SET) < 0)
    {
        if (size >= 0 && static_cast<size_t>(size) > maxRequestSize) errno = E2BIG;
        return false;
    }
    return true;
}

FXbool readRequest(int fd, Request& request)
{
    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<size_t>(info.st_size) > maxRequestSize) return false;
    uint32_t header[3];
    if (!readBytes(fd, header, sizeof(header)) || header[0] != transferMagic ||
        (header[1] != COPY && header[1] != MOVE) || !header[2] || header[2] > 1000000) return false;
    request.operation = header[1];
    for (uint32_t i = 0; i < header[2]; i++)
    {
        FXString source, target;
        uint32_t restore, directory;
        PathIdentity sourceIdentity, parentIdentity;
        if (!readString(fd, source) || !readString(fd, target) || source.empty() || target.empty() ||
            !readBytes(fd, &restore, sizeof(restore)) || restore > 1 ||
            !readBytes(fd, &directory, sizeof(directory)) || directory > 1 ||
            !readIdentity(fd, sourceIdentity) || !readIdentity(fd, parentIdentity)) return false;
        request.items.push_back(TransferItem(source, target, restore != 0));
        request.targetDirectories.push_back(directory != 0);
        request.sourceIdentities.push_back(sourceIdentity);
        request.targetParentIdentities.push_back(parentIdentity);
    }
    uint32_t pinCount;
    if (!readBytes(fd, &pinCount, sizeof(pinCount)) || pinCount > header[2]) return false;
    // The inherited directory descriptors stay open in the worker, but not its children.
    for (uint32_t i = 0; i < pinCount; i++)
    {
        int directory = static_cast<int>(i) + 5;
        int flags = fcntl(directory, F_GETFD);
        struct stat pinned;
        if (flags < 0 || fcntl(directory, F_SETFD, flags | FD_CLOEXEC) != 0 ||
            fstat(directory, &pinned) != 0 || !S_ISDIR(pinned.st_mode)) return false;
    }
    for (const SettingKey& key : settingKeys)
    {
        (void)key;
        uint32_t present;
        FXString value;
        if (!readBytes(fd, &present, sizeof(present)) || present > 1 ||
            (present && !readString(fd, value))) return false;
        request.present.push_back(present != 0);
        request.settings.push_back(value);
    }
    return true;
}

FXbool sendPacket(int channel, char value)
{
    ssize_t sent;
    do
    {
        sent = send(channel, &value, 1, MSG_DONTWAIT | MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    if (sent == 1) return true;
    if (sent < 0 && errno != EPIPE && errno != ECONNRESET &&
        (value != 'C' || (errno != EAGAIN && errno != EWOULDBLOCK)))
        appLog(APP_LOG_ERRORS, "transfer-notify-failed", strerror(errno));
    else if (sent >= 0)
        appLog(APP_LOG_ERRORS, "transfer-notify-failed", "Incomplete transfer status message");
    return false;
}

struct Notifier
{
    int channel;
    FXbool awaiting;
};

void notifyTransfer(void* data, const FXString&)
{
    Notifier* notifier = static_cast<Notifier*>(data);
    // Keep at most one change message queued so the completion message has room.
    if (notifier->awaiting)
    {
        char reply;
        ssize_t count;
        do
        {
            count = recv(notifier->channel, &reply, 1, MSG_DONTWAIT);
        } while (count < 0 && errno == EINTR);
        if (count == 1 && reply == 'A')
            notifier->awaiting = false;
        else if (count == 1)
            appLog(APP_LOG_ERRORS, "transfer-notify-failed", "Invalid transfer acknowledgement");
        else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNRESET)
            appLog(APP_LOG_ERRORS, "transfer-notify-failed", strerror(errno));
    }
    if (!notifier->awaiting) notifier->awaiting = sendPacket(notifier->channel, 'C');
}

void finishNotifications(int channel)
{
    if (!sendPacket(channel, 'F')) return;
    // Unread acknowledgements can otherwise reset the socket and discard completion.
    for (;;)
    {
        char reply;
        ssize_t count = recv(channel, &reply, 1, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0)
        {
            if (count < 0 && errno != ECONNRESET)
                appLog(APP_LOG_ERRORS, "transfer-notify-failed", strerror(errno));
            return;
        }
        if (reply != 'A')
            appLog(APP_LOG_ERRORS, "transfer-notify-failed", "Invalid transfer acknowledgement");
    }
}

class DirectoryLocks
{
    struct Lock
    {
        int descriptor;
        int mode;
    };
    std::vector<Lock> descriptors;

    static void addPath(std::map<FXString, FXbool>& paths, FXString path, FXbool exclusive)
    {
        path = xf_realpath(FXPath::absolute(path));
        for (;;)
        {
            std::map<FXString, FXbool>::iterator found = paths.find(path);
            if (found == paths.end()) paths.insert(std::make_pair(path, exclusive));
            else if (exclusive) found->second = true;
            FXString parent = FXPath::directory(path);
            if (parent.empty() || parent == path) break;
            path = parent;
            exclusive = false;
        }
    }

public:
    ~DirectoryLocks() { release(); }

    void release()
    {
        for (const Lock& lock : descriptors) close(lock.descriptor);
        descriptors.clear();
    }

    FXbool open(const Request& request, FXString& error)
    {
        FXString root;
        root.format("/tmp/swordfish-transfer-%lu", static_cast<unsigned long>(getuid()));
        if (mkdir(root.text(), 0700) != 0 && errno != EEXIST)
        {
            error = root + ": " + strerror(errno);
            return false;
        }
        int directory = ::open(root.text(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        struct stat info;
        if (directory < 0 || fstat(directory, &info) != 0 || !S_ISDIR(info.st_mode) ||
            info.st_uid != getuid() || (info.st_mode & 077) != 0)
        {
            error = _("Unsafe or inaccessible transfer lock folder: ") + root;
            if (directory >= 0) close(directory);
            return false;
        }

        std::map<FXString, FXbool> paths;
        for (size_t i = 0; i < request.items.size(); ++i)
        {
            const TransferItem& item = request.items[i];
            FXString target = targetParent(item.target, request.targetDirectories[i]);
            FXString source = FXPath::absolute(item.source);
            addPath(paths, target, true);
            addPath(paths, source, request.operation == MOVE);
            addPath(paths, FXPath::directory(source), request.operation == MOVE);
        }
        for (const auto& entry : paths)
        {
            const FXString& path = entry.first;
            gchar* digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, path.text(), path.length());
            int fd = openat(directory, digest, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
            g_free(digest);
            if (fd < 0 || fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid())
            {
                error = _("Unable to create transfer lock for ") + path;
                if (fd >= 0) close(fd);
                close(directory);
                return false;
            }
            descriptors.push_back({fd, entry.second ? LOCK_EX : LOCK_SH});
        }
        close(directory);
        return true;
    }

    FXbool acquire(File* progress, FXString& error)
    {
        for (;;)
        {
            size_t acquired = 0;
            while (acquired < descriptors.size())
            {
                if (flock(descriptors[acquired].descriptor, descriptors[acquired].mode | LOCK_NB) == 0)
                {
                    acquired++;
                    continue;
                }
                int failure = errno;
                while (acquired) flock(descriptors[--acquired].descriptor, LOCK_UN);
                if (failure != EWOULDBLOCK && failure != EAGAIN && failure != EINTR)
                {
                    error = strerror(failure);
                    return false;
                }
                break;
            }
            if (acquired == descriptors.size()) return true;
            progress->setIndependentStatus(_("Waiting for another transfer..."));
            progress->processProgressEvents();
            if (progress->isCancelled()) return false;
            struct timespec delay = {0, 100000000};
            nanosleep(&delay, NULL);
        }
    }
};

FXbool validateItem(const Request& request, size_t index, File* progress,
                    const PathIdentity* movedHardLink = NULL)
{
    const TransferItem& item = request.items[index];
    PathIdentity source, target, parent;
    if (!pathIdentity(item.source, false, source))
    {
        int failure = errno;
        progress->setIndependentPaths(item.source, item.target);
        MessageBox::error(progress, BOX_OK, _("Transfer source changed"),
                          _("Can't check the source:\n%s"), strerror(failure));
        return false;
    }
    const PathIdentity& original = request.sourceIdentities[index];
    // A prior move of this inode changes ctime; compare with its post-move snapshot instead.
    if (!sameIdentity(source, movedHardLink ? *movedHardLink : original, true) ||
        (movedHardLink && (source.size != original.size || source.modifiedSeconds != original.modifiedSeconds ||
                           source.modifiedNanoseconds != original.modifiedNanoseconds)))
    {
        progress->setIndependentPaths(item.source, item.target);
        MessageBox::error(progress, BOX_OK, _("Transfer source changed"),
                          _("Source changed.\nCheck the progress window.\nStart the transfer again."));
        return false;
    }
    FXString directory = targetParent(item.target, request.targetDirectories[index]);
    if (!pathIdentity(item.target, true, target) ||
        (!request.targetDirectories[index] && !pathIdentity(directory, true, parent)))
    {
        int failure = errno;
        progress->setIndependentPaths(item.source, item.target);
        MessageBox::error(progress, BOX_OK, _("Transfer destination changed"),
                          _("Can't check the destination:\n%s"), strerror(failure));
        return false;
    }
    if (request.targetDirectories[index]) parent = target;
    FXbool isDirectory = target.exists && target.type == S_IFDIR;
    if (isDirectory != request.targetDirectories[index] ||
        !sameIdentity(parent, request.targetParentIdentities[index]))
    {
        progress->setIndependentPaths(item.source, item.target);
        MessageBox::error(progress, BOX_OK, _("Transfer destination changed"),
                          _("Destination changed.\nCheck the progress window.\nStart the transfer again."));
        return false;
    }
    return true;
}
}

int xf_runTransferWorker(int requestFd, int channel, char* executable)
{
    int channelFlags = fcntl(channel, F_GETFD);
    if (channelFlags < 0 || fcntl(channel, F_SETFD, channelFlags | FD_CLOEXEC) != 0)
    {
        sendPacket(channel, 'E');
        fprintf(stderr, "Unable to secure the transfer status channel: %s\n", strerror(errno));
        close(requestFd);
        close(channel);
        return EXIT_FAILURE;
    }
    Request request;
    if (!readRequest(requestFd, request))
    {
        sendPacket(channel, 'E');
        fprintf(stderr, "Invalid Swordfish transfer request\n");
        close(requestFd);
        close(channel);
        return EXIT_FAILURE;
    }
    close(requestFd);

    FXApp* app = new XFApp("xfe", "SwordFish");
    int argc = 1;
    char* argv[] = {executable, NULL};
    app->init(argc, argv);
    app->reg().read();
    for (size_t i = 0; i < sizeof(settingKeys) / sizeof(settingKeys[0]); i++)
    {
        const SettingKey& key = settingKeys[i];
        if (request.present[i])
            app->reg().writeStringEntry(key.section, key.name, request.settings[i].text());
        else
            app->reg().deleteEntry(key.section, key.name);
    }
    appLogSetLevel(app->reg().readUnsignedEntry("OPTIONS", "log_level", APP_LOG_LEVEL_NORMAL));
    scaleint = round(app->reg().readUnsignedEntry("SETTINGS", "screenres", 100) / 100.0);
    app->setBaseColor(app->reg().readColorEntry("SETTINGS", "basecolor", FXRGB(15, 46, 54)));
    app->setBorderColor(app->reg().readColorEntry("SETTINGS", "bordercolor", FXRGB(7, 21, 25)));
    app->setBackColor(app->reg().readColorEntry("SETTINGS", "backcolor", FXRGB(12, 37, 44)));
    app->setForeColor(app->reg().readColorEntry("SETTINGS", "forecolor", FXRGB(226, 244, 246)));
    app->setSelbackColor(app->reg().readColorEntry("SETTINGS", "selbackcolor", FXRGB(191, 67, 42)));
    app->setSelforeColor(app->reg().readColorEntry("SETTINGS", "selforecolor", FXRGB(255, 255, 255)));
    FXString fontspec = app->reg().readStringEntry("SETTINGS", "font", DEFAULT_NORMAL_FONT);
    if (!fontspec.empty()) app->setNormalFont(new FXFont(app, fontspec));
    execpath = xf_execpath(executable);
    FXuint iconStatus;
    if (!loadAppIcons(app, &iconStatus))
        appLog(APP_LOG_ERRORS, "transfer-icons", "Some progress window icons could not be loaded");

    new FXToolTip(app, 0);
    File* progress = new File(app, request.operation == COPY ? _("Copy Files") : _("Move Files"),
                              request.operation, request.items.size());
    app->create();
    Notifier notifier = {channel, false};
    progress->setChangeCallback(notifyTransfer, &notifier);
    progress->setIndependentPaths(request.items.front().source, request.items.front().target);
    progress->startIndependent();
    sendPacket(channel, 'R');

    FXbool errors = false;
    FXString problem;
    DirectoryLocks locks;
    if (!locks.open(request, problem) || !locks.acquire(progress, problem))
    {
        if (!progress->isCancelled())
        {
            errors = true;
            MessageBox::error(progress, BOX_OK, _("Transfer could not start"), "%s", problem.text());
        }
    }
    else
    {
        progress->setIndependentStatus(_("Preparing..."));
        std::vector<FXString> sources;
        for (size_t i = 0; i < request.items.size(); ++i)
        {
            if (progress->uiUpdateDue()) progress->processProgressEvents();
            if (progress->isCancelled()) break;
            if (!validateItem(request, i, progress))
            {
                errors = true;
                break;
            }
            sources.push_back(request.items[i].source);
        }
        FXulong total = 0;
        FXString humanSize;
        if (!errors && !progress->isCancelled()) humanSize = progress->sourcesize(sources, &total);
        FXString trashFiles = xf_trashdatahome() + PATHSEPSTRING TRASHFILESPATH;
        FXString trashInfo = xf_trashdatahome() + PATHSEPSTRING TRASHINFOPATH;
        FXbool useTrash = app->reg().readUnsignedEntry("OPTIONS", "use_trash_can", true);
        if (!errors && !progress->isCancelled())
        {
            progress->setIndependentStatus(_("Checking available space..."));
            if (!checkAvailableSpace(progress, request, trashFiles, useTrash)) errors = true;
        }
        FXulong started = xf_getcurrenttime();
        if (!errors && !progress->isCancelled()) progress->beginTransfer();

        std::map<std::pair<uint64_t, uint64_t>, PathIdentity> movedHardLinks;
        for (size_t i = 0; i < request.items.size(); ++i)
        {
            const TransferItem& item = request.items[i];
            if (errors || progress->isCancelled()) break;
            FXString target = item.target;
            progress->setIndependentPaths(item.source, target);
            const PathIdentity& identity = request.sourceIdentities[i];
            std::pair<uint64_t, uint64_t> key(identity.device, identity.inode);
            std::map<std::pair<uint64_t, uint64_t>, PathIdentity>::const_iterator previous = movedHardLinks.find(key);
            const PathIdentity* previousMove = previous == movedHardLinks.end() ? NULL : &previous->second;
            if (!validateItem(request, i, progress, previousMove))
            {
                errors = true;
                break;
            }
            FXString trashPath;
            if (useTrash && target == trashFiles)
            {
                trashPath = xf_create_trashpathname(item.source, trashFiles);
                if (xf_create_trashinfo(item.source, trashPath, trashFiles, trashInfo) != 0)
                {
                    errors = true;
                    MessageBox::error(progress, BOX_OK, _("Error"), _("Can't create trash information for %s: %s"),
                                      item.source.text(), strerror(errno));
                    break;
                }
                target = trashPath;
                progress->setIndependentPaths(item.source, target);
            }
            int sourcePin = -1;
            if (request.operation == MOVE && identity.exists && identity.type != S_IFDIR && identity.links > 1)
            {
#ifdef O_PATH
                sourcePin = open(item.source.text(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
#else
                sourcePin = open(item.source.text(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
#endif
                struct stat pinned;
                int failure = sourcePin < 0 ? errno : fstat(sourcePin, &pinned) != 0 ? errno : 0;
                if (failure || !sameIdentity(identityFromStat(pinned), previousMove ? *previousMove : identity, true))
                {
                    if (sourcePin >= 0) close(sourcePin);
                    if (!trashPath.empty()) xf_cleanuptrashinfo(trashPath);
                    errors = true;
                    if (failure)
                        MessageBox::error(progress, BOX_OK, _("Transfer source changed"),
                                          _("Can't check the source:\n%s"), strerror(failure));
                    else
                        MessageBox::error(progress, BOX_OK, _("Transfer source changed"),
                                          _("Source changed.\nCheck the progress window.\nStart the transfer again."));
                    break;
                }
            }
            int result = request.operation == COPY ?
                         progress->copy(item.source, target, humanSize, total, started) :
                         progress->fmove(item.source, target, humanSize, total, started, item.restore);
            if (sourcePin >= 0)
            {
                PathIdentity remaining;
                struct stat after;
                if (result == true && (!pathIdentity(item.source, false, remaining) ||
                                       (!remaining.exists && fstat(sourcePin, &after) != 0)))
                {
                    int failure = errno;
                    errors = true;
                    MessageBox::error(progress, BOX_OK, _("Transfer source changed"),
                                      _("Can't check the source:\n%s"), strerror(failure));
                }
                else if (result == true && !remaining.exists)
                    movedHardLinks[key] = identityFromStat(after);
                close(sourcePin);
            }
            if (!trashPath.empty()) xf_cleanuptrashinfo(trashPath);
            if (request.operation == MOVE) xf_cleanuptrashinfo(item.source);
            if (result != true && !progress->isCancelled())
            {
                errors = true;
                if (result == 0)
                {
                    MessageBox::error(progress, BOX_OK, _("Error"),
                                      request.operation == COPY ? _("Unable to copy %s") : _("Unable to move %s"),
                                      item.source.text());
                    break;
                }
            }
        }
    }
    locks.release();
    notifyTransfer(&notifier, FXString::null);
    progress->finishIndependent(errors);
    finishNotifications(channel);
    delete progress;
    close(channel);
    return errors ? EXIT_FAILURE : EXIT_SUCCESS;
}

class TransferWatch;

class TransferManager : public FXObject
{
    FXDECLARE(TransferManager)
protected:
    TransferManager() {}
private:
    XFileExplorer* owner = NULL;
    std::vector<TransferWatch*> watches;
    static TransferManager* current;
    static FXbool startTransfer(FXWindow*, FXuint, const std::vector<TransferItem>&);

public:
    enum { ID_REFRESH = 1, ID_LAST };
    explicit TransferManager(XFileExplorer*);
    ~TransferManager();
    FXbool start(FXWindow*, FXuint, const std::vector<TransferItem>&);
    FXApp* getApp() const { return owner->getApp(); }
    void changed();
    void ended(TransferWatch*, FXbool);
    long onRefresh(FXObject*, FXSelector, void*);
};

class TransferWatch : public FXObject
{
    FXDECLARE(TransferWatch)
protected:
    TransferWatch() {}
private:
    TransferManager* manager = NULL;
    int channel = -1;
    FXbool registered = false;
    FXbool ready = false;
    FXbool finished = false;
    FXbool error = false;
public:
    enum { ID_READ = 1, ID_LAST };
    TransferWatch(TransferManager* parent, int fd) : manager(parent), channel(fd) {}
    ~TransferWatch();
    FXbool start();
    long onRead(FXObject*, FXSelector, void*);
};

FXDEFMAP(TransferManager) TransferManagerMap[] =
{
    FXMAPFUNC(SEL_TIMEOUT, TransferManager::ID_REFRESH, TransferManager::onRefresh)
};
FXIMPLEMENT(TransferManager, FXObject, TransferManagerMap, ARRAYNUMBER(TransferManagerMap))

FXDEFMAP(TransferWatch) TransferWatchMap[] =
{
    FXMAPFUNC(SEL_IO_READ, TransferWatch::ID_READ, TransferWatch::onRead)
};
FXIMPLEMENT(TransferWatch, FXObject, TransferWatchMap, ARRAYNUMBER(TransferWatchMap))

TransferManager* TransferManager::current = NULL;

TransferManager::TransferManager(XFileExplorer* window) : owner(window)
{
    current = this;
    xf_setTransferStarter(startTransfer);
}

TransferManager::~TransferManager()
{
    if (current == this)
    {
        xf_setTransferStarter(NULL);
        current = NULL;
    }
    owner->getApp()->removeTimeout(this, ID_REFRESH);
    for (TransferWatch* watch : watches) delete watch;
}

FXbool TransferManager::startTransfer(FXWindow* owner, FXuint operation,
                                     const std::vector<TransferItem>& items)
{
    return current && current->start(owner, operation, items);
}

TransferWatch::~TransferWatch()
{
    if (registered) manager->getApp()->removeInput(channel, INPUT_READ);
    if (channel >= 0) close(channel);
}

FXbool TransferWatch::start()
{
    registered = manager->getApp()->addInput(channel, INPUT_READ, this, ID_READ);
    return registered;
}

long TransferWatch::onRead(FXObject*, FXSelector, void*)
{
    for (int i = 0; i < 128; i++)
    {
        char packet;
        ssize_t count = recv(channel, &packet, 1, MSG_DONTWAIT);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 1;
        if (count <= 0)
        {
            manager->ended(this, error || !ready || !finished || count < 0);
            return 1;
        }
        switch (packet)
        {
        case 'C':
            manager->changed();
            sendPacket(channel, 'A');
            break;
        case 'R':
            ready = true;
            break;
        case 'F':
            finished = true;
            manager->ended(this, error || !ready);
            return 1;
        case 'E':
            error = true;
            break;
        default:
            error = true;
            break;
        }
    }
    return 1;
}

void TransferManager::changed()
{
    if (!owner->getApp()->hasTimeout(this, ID_REFRESH))
        owner->getApp()->addTimeout(this, ID_REFRESH, 200);
}

long TransferManager::onRefresh(FXObject*, FXSelector, void*)
{
    FileList* left = owner->getLeftPanel()->getList();
    FileList* right = owner->getRightPanel()->getList();
    left->scan(true);
    if (right != left) right->scan(true);
    return 1;
}

void TransferManager::ended(TransferWatch* watch, FXbool error)
{
    watches.erase(std::remove(watches.begin(), watches.end(), watch), watches.end());
    delete watch;
    changed();
    if (error)
    {
        appLog(APP_LOG_ERRORS, "transfer-worker-stopped", "Transfer process stopped unexpectedly");
        MessageBox::error(owner, BOX_OK, _("Transfer stopped"),
                          _("The transfer process stopped unexpectedly. Check the source and destination."));
    }
}

FXbool TransferManager::start(FXWindow* initiator, FXuint operation, const std::vector<TransferItem>& items)
{
    DirectoryPins pins;
    char name[] = "/tmp/swordfish-transfer-request-XXXXXX";
    int temporary = mkstemp(name);
    if (temporary < 0)
    {
        MessageBox::error(initiator, BOX_OK, _("Transfer could not start"), "%s", strerror(errno));
        return false;
    }
    if (unlink(name) != 0)
    {
        int failure = errno;
        close(temporary);
        MessageBox::error(initiator, BOX_OK, _("Transfer could not start"), "%s", strerror(failure));
        return false;
    }
    if (!writeRequest(temporary, owner->getApp(), operation, items, pins))
    {
        int failure = errno;
        close(temporary);
        MessageBox::error(initiator, BOX_OK, _("Transfer could not start"), "%s", strerror(failure));
        return false;
    }
    int request = fcntl(temporary, F_DUPFD_CLOEXEC, 5);
    int failure = errno;
    close(temporary);
    if (request < 0)
    {
        MessageBox::error(initiator, BOX_OK, _("Transfer could not start"), "%s", strerror(failure));
        return false;
    }

    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0)
    {
        failure = errno;
        close(request);
        MessageBox::error(initiator, BOX_OK, _("Transfer could not start"), "%s", strerror(failure));
        return false;
    }
    int flags = fcntl(sockets[0], F_GETFD);
    if (flags < 0 || fcntl(sockets[0], F_SETFD, flags | FD_CLOEXEC) != 0)
    {
        failure = errno;
        close(sockets[0]);
        close(sockets[1]);
        close(request);
        MessageBox::error(initiator, BOX_OK, _("Transfer could not start"), "%s", strerror(failure));
        return false;
    }
    int childChannel = fcntl(sockets[1], F_DUPFD_CLOEXEC, 5);
    failure = errno;
    close(sockets[1]);
    if (childChannel < 0)
    {
        close(sockets[0]);
        close(request);
        MessageBox::error(initiator, BOX_OK, _("Transfer could not start"), "%s", strerror(failure));
        return false;
    }
    TransferWatch* watch = new TransferWatch(this, sockets[0]);
    if (!watch->start())
    {
        delete watch;
        close(childChannel);
        close(request);
        MessageBox::error(initiator, BOX_OK, _("Transfer could not start"),
                          _("Unable to monitor the transfer process."));
        return false;
    }
    watches.push_back(watch);

    std::sort(pins.descriptors.begin(), pins.descriptors.end());
    long maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0) maxfd = 1024;
    pid_t pid = fork();
    if (pid == 0)
    {
        if (dup2(request, 3) < 0 || dup2(childChannel, 4) < 0 || setsid() < 0)
        {
            send(4, "E", 1, MSG_NOSIGNAL);
            _exit(126);
        }
        // Pack directory pins after the request and notification channels.
        for (size_t i = 0; i < pins.descriptors.size(); i++)
        {
            int destination = static_cast<int>(i) + 5;
            if ((pins.descriptors[i] != destination && dup2(pins.descriptors[i], destination) < 0))
            {
                send(4, "E", 1, MSG_NOSIGNAL);
                _exit(126);
            }
            int flags = fcntl(destination, F_GETFD);
            if (flags < 0 || fcntl(destination, F_SETFD, flags & ~FD_CLOEXEC) != 0)
            {
                send(4, "E", 1, MSG_NOSIGNAL);
                _exit(126);
            }
        }
        unsigned int firstUnused = static_cast<unsigned int>(pins.descriptors.size()) + 5;
        FXbool closed = false;
#if defined(linux) && defined(SYS_close_range)
        closed = syscall(SYS_close_range, firstUnused, ~0U, 0U) == 0;
#endif
        if (!closed)
            for (long fd = firstUnused; fd < maxfd; fd++) close(static_cast<int>(fd));
        execl(execpath.text(), execpath.text(), "--internal-transfer-worker", static_cast<char*>(NULL));
        send(4, "E", 1, MSG_NOSIGNAL);
        _exit(127);
    }
    failure = errno;
    close(childChannel);
    close(request);
    if (pid < 0)
    {
        watches.erase(std::remove(watches.begin(), watches.end(), watch), watches.end());
        delete watch;
        MessageBox::error(initiator, BOX_OK, _("Transfer could not start"), "%s", strerror(failure));
        return false;
    }
    return true;
}

TransferManager* xf_createTransferManager(XFileExplorer* owner)
{
    return new TransferManager(owner);
}

void xf_destroyTransferManager(TransferManager* manager)
{
    delete manager;
}
