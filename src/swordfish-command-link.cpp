// Bundled or installed, never setuid; executed as root only after system authorization.
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

namespace
{

int fail(const char* message)
{
    fprintf(stderr, "Swordfish command link: %s\n", message);
    return 1;
}

bool safeDirectory(int fd)
{
    struct stat info;
    return fstat(fd, &info) == 0 && S_ISDIR(info.st_mode) && info.st_uid == 0 &&
           (info.st_mode & 022) == 0;
}

int openDirectory(const std::string& path, bool create)
{
    if (path.empty() || path[0] != '/')
    {
        return -1;
    }
    int current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    size_t offset = 1;
    while (current >= 0 && offset < path.size())
    {
        size_t end = path.find('/', offset);
        std::string component = path.substr(offset, end == std::string::npos ? end : end - offset);
        if (component.empty() || component == "." || component == ".." || !safeDirectory(current))
        {
            close(current);
            return -1;
        }
        if (create && mkdirat(current, component.c_str(), 0755) < 0 && errno != EEXIST)
        {
            close(current);
            return -1;
        }
        int next = openat(current, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(current);
        current = next;
        if (end == std::string::npos)
        {
            break;
        }
        offset = end + 1;
    }
    if (current >= 0 && !safeDirectory(current))
    {
        close(current);
        return -1;
    }
    return current;
}

bool readRecord(int fd, std::string& text)
{
    struct stat info;
    if (fstat(fd, &info) < 0 || !S_ISREG(info.st_mode) || info.st_uid != 0 ||
        (info.st_mode & 077) != 0 || info.st_nlink != 1 || info.st_size > 2 * PATH_MAX + 8)
    {
        return false;
    }
    char buffer[2 * PATH_MAX + 8];
    if (lseek(fd, 0, SEEK_SET) < 0)
    {
        return false;
    }
    ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count < 0 || count != info.st_size)
    {
        return false;
    }
    text.assign(buffer, (size_t)count);
    return true;
}

bool saveRecord(int fd, const std::string& text)
{
    if (lseek(fd, 0, SEEK_SET) < 0 || ftruncate(fd, 0) < 0)
    {
        return false;
    }
    size_t offset = 0;
    while (offset < text.size())
    {
        ssize_t count = write(fd, text.data() + offset, text.size() - offset);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            return false;
        }
        offset += (size_t)count;
    }
    return fsync(fd) == 0;
}

}

int main(int argc, char** argv)
{
    if (argc != 4 || geteuid() != 0 || !getenv("PKEXEC_UID"))
    {
        return fail("Use system authorization; expected create/remove, destination and target.");
    }
    char* end;
    errno = 0;
    unsigned long caller = strtoul(getenv("PKEXEC_UID"), &end, 10);
    if (errno || *end || caller == 0 || caller >= (unsigned long)(uid_t)-1)
    {
        return fail("Invalid authorized user.");
    }
    const uid_t uid = (uid_t)caller;
    const std::string action(argv[1]);
    const std::string path(argv[2]);
    const std::string target(argv[3]);
    if ((action != "create" && action != "remove") ||
        (path != "/usr/local/bin/swordfish" && path != "/usr/bin/swordfish" &&
         path != "/usr/local/sbin/swordfish" && path != "/usr/sbin/swordfish") ||
        target.empty() || target[0] != '/' || target.find('\n') != std::string::npos ||
        target.size() >= PATH_MAX)
    {
        return fail("Destination or target is outside the command-link policy.");
    }
    if (action == "create")
    {
        struct stat info;
        char canonical[PATH_MAX];
        const std::string name = target.substr(target.find_last_of('/') + 1);
        if ((name != "swordfish" && name != "AppRun") ||
            !realpath(target.c_str(), canonical) || target != canonical ||
            lstat(target.c_str(), &info) < 0 || !S_ISREG(info.st_mode) ||
            (info.st_uid != uid && info.st_uid != 0) ||
            !(info.st_mode & 0100))
        {
            return fail("Target must be the authorized user's (or root's) executable Swordfish or portable AppRun.");
        }
    }
    const std::string directory = path.substr(0, path.find_last_of('/'));
    int destination = openDirectory(directory, false);
    int state = openDirectory("/var/lib/swordfish-command-links", true);
    if (destination < 0 || state < 0)
    {
        return fail("System directories must be root-owned and not group/other writable.");
    }
    const std::string recordName = std::to_string(caller);
    int record = openat(state, recordName.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    std::string previous;
    if (record < 0 || flock(record, LOCK_EX) < 0 || !readRecord(record, previous))
    {
        return fail("Cannot lock/read the root-owned ownership record.");
    }
    const std::string expected = path + "\n" + target + "\n";
    struct stat linkInfo;
    const bool exists = fstatat(destination, "swordfish", &linkInfo, AT_SYMLINK_NOFOLLOW) == 0;
    if (!exists && errno != ENOENT)
    {
        return fail("Cannot inspect command destination.");
    }
    if (exists)
    {
        char buffer[PATH_MAX];
        ssize_t count = readlinkat(destination, "swordfish", buffer, sizeof(buffer));
        if (previous != expected || !S_ISLNK(linkInfo.st_mode) || linkInfo.st_uid != 0 ||
            count < 0 || std::string(buffer, (size_t)count) != target)
        {
            return fail("Refusing to overwrite/remove an unrelated or changed command.");
        }
    }
    if (!previous.empty() && previous != expected)
    {
        return fail("Remove the previously authorized command link first.");
    }
    if (action == "create")
    {
        // Record intent first: an interrupted creation can be retried without adopting unrelated links.
        if (!saveRecord(record, expected) ||
            (!exists && symlinkat(target.c_str(), destination, "swordfish") < 0))
        {
            return fail("Cannot persist/create the command link.");
        }
    }
    else
    {
        if (previous != expected || (exists && unlinkat(destination, "swordfish", 0) < 0) ||
            !saveRecord(record, ""))
        {
            return fail("Cannot remove the managed command link.");
        }
    }
    fsync(destination);
    close(record);
    close(state);
    close(destination);
    return 0;
}
