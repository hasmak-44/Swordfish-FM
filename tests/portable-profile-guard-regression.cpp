#include "config.h"
#include <fx.h>
#include "xfeutils.h"
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <string>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern FXString xdgconfighome;

static const std::string original = "[SETTINGS]\nchoice=original\n";
static const std::string copied = "[SETTINGS]\nchoice=external\n";
static const std::string secondCopy = "[SETTINGS]\nchoice=other-copy\n";
enum Injection
{
    NO_INJECTION, BEFORE_EXCHANGE, AFTER_EXCHANGE, BOTH,
    NO_EXCHANGE, NO_RENAMEAT2, NO_ATOMIC_PRIMITIVE, FALLBACK_BEFORE, FALLBACK_AFTER, FALLBACK_BOTH,
    EXCHANGE_SNAPSHOT_FAILURE, FALLBACK_SNAPSHOT_FAILURE
};
static Injection injection = NO_INJECTION;
static FXString raceTarget;
static int snapshotReads = 0;

static void put(const FXString& path, const std::string& value)
{
    std::ofstream file(path.text(), std::ios::binary | std::ios::trunc);
    assert(file);
    file << value;
    file.close();
    assert(file);
}

static std::string contents(const FXString& path)
{
    std::ifstream file(path.text(), std::ios::binary);
    assert(file);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

extern "C" long __real_syscall(long, ...);
extern "C" long __wrap_syscall(long number, ...)
{
    assert(number == SYS_renameat2);
    va_list arguments;
    va_start(arguments, number);
    int fromDirectory = va_arg(arguments, int);
    const char* from = va_arg(arguments, const char*);
    int toDirectory = va_arg(arguments, int);
    const char* to = va_arg(arguments, const char*);
    unsigned flags = va_arg(arguments, unsigned);
    va_end(arguments);

    Injection active = injection;
    if (active == NO_RENAMEAT2 || active == NO_ATOMIC_PRIMITIVE)
    {
        errno = EOPNOTSUPP;
        return -1;
    }
    if (flags == RENAME_EXCHANGE &&
        (active == NO_EXCHANGE || active == FALLBACK_BEFORE || active == FALLBACK_AFTER || active == FALLBACK_BOTH ||
         active == FALLBACK_SNAPSHOT_FAILURE))
    {
        if (active == NO_EXCHANGE) { injection = NO_INJECTION; }
        errno = EOPNOTSUPP;
        return -1;
    }
    if (active == BEFORE_EXCHANGE || active == AFTER_EXCHANGE || active == BOTH) { injection = NO_INJECTION; }
    if (flags == RENAME_EXCHANGE && (active == BEFORE_EXCHANGE || active == BOTH)) { put(to, copied); }
    long result = __real_syscall(number, fromDirectory, from, toDirectory, to, flags);
    if (result == 0 && flags == RENAME_EXCHANGE && (active == AFTER_EXCHANGE || active == BOTH))
        put(to, secondCopy);
    return result;
}

extern "C" int __real_rename(const char*, const char*);
extern "C" int __wrap_rename(const char* from, const char* to)
{
    Injection active = injection;
    if (raceTarget == from && (active == FALLBACK_BEFORE || active == FALLBACK_AFTER || active == FALLBACK_BOTH))
    {
        injection = NO_INJECTION;
        if (active == FALLBACK_BEFORE || active == FALLBACK_BOTH) { put(from, copied); }
        int result = __real_rename(from, to);
        if (result == 0 && (active == FALLBACK_AFTER || active == FALLBACK_BOTH)) { put(from, secondCopy); }
        return result;
    }
    return __real_rename(from, to);
}

extern "C" int __real_link(const char*, const char*);
extern "C" int __wrap_link(const char* from, const char* to)
{
    if (injection == NO_ATOMIC_PRIMITIVE)
    {
        errno = EOPNOTSUPP;
        return -1;
    }
    return __real_link(from, to);
}

extern "C" int __real_open(const char*, int, ...);
extern "C" int __wrap_open(const char* path, int flags, ...)
{
    mode_t mode = 0;
    bool needsMode = (flags & O_CREAT) != 0;
#ifdef O_TMPFILE
    needsMode = needsMode || (flags & O_TMPFILE) == O_TMPFILE;
#endif
    if (needsMode)
    {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    FXString temporary = raceTarget + "_" + FXStringVal((FXint)getpid());
    const char* name = strrchr(path, '/');
    bool backup = injection == FALLBACK_SNAPSHOT_FAILURE && strstr(path, ".guard-") &&
                  name && strcmp(name, "/previous") == 0;
    if ((backup || (injection == EXCHANGE_SNAPSHOT_FAILURE && temporary == path)) && ++snapshotReads == 2)
    {
        injection = NO_INJECTION;
        errno = EACCES;
        return -1;
    }
    return __real_open(path, flags, mode);
}

static FXString setup(const FXString& work, const char* name, bool portable = true)
{
    FXString root = work + "/" + name;
    assert(mkdir(root.text(), 0700) == 0);
    xdgconfighome = root + "/config";
    assert(mkdir(xdgconfighome.text(), 0700) == 0);
    assert(mkdir((xdgconfighome + "/swordfish").text(), 0700) == 0);
    if (portable)
    {
        assert(setenv("SWORDFISH_PORTABLE", "1", 1) == 0);
        assert(setenv("SWORDFISH_PORTABLE_DATA", root.text(), 1) == 0);
    }
    else
    {
        assert(unsetenv("SWORDFISH_PORTABLE") == 0);
        assert(unsetenv("SWORDFISH_PORTABLE_DATA") == 0);
    }
    return xdgconfighome + "/swordfish/xferc";
}

static void selfSaves(const FXString& work)
{
    FXString path = setup(work, "self");
    put(path, original);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(settings.read());
    FXString problem;
    for (int i = 0; i < 3; ++i)
    {
        settings.writeIntEntry("SETTINGS", "counter", i);
        assert(settings.write());
        assert(!xf_portablesettingschanged(problem));
    }
    FXRegistry other("other", "SwordFish");
    other.writeStringEntry("SETTINGS", "independent", "yes");
    assert(other.write());
    assert(!xf_portablesettingschanged(problem));

    FXRegistry second("xfe", "SwordFish");
    assert(second.read());
    second.writeStringEntry("SETTINGS", "choice", "second-window");
    assert(second.write());
    std::string saved = contents(path);
    assert(xf_portablesettingschanged(problem));
    assert(!settings.read());
    assert(FXString(settings.readStringEntry("SETTINGS", "choice", "")) == "original");
    settings.writeStringEntry("SETTINGS", "choice", "stale");
    assert(!settings.write() && !settings.write() && contents(path) == saved);
}

static void inPlaceCopy(const FXString& work)
{
    FXString path = setup(work, "in-place");
    assert(original.size() == copied.size());
    put(path, original);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(settings.read());
    struct stat info;
    assert(stat(path.text(), &info) == 0);
    put(path, copied);
    struct timespec times[] = {info.st_atim, info.st_mtim};
    assert(utimensat(AT_FDCWD, path.text(), times, 0) == 0);
    FXString problem;
    assert(xf_portablesettingschanged(problem));
    settings.writeStringEntry("SETTINGS", "choice", "stale");
    assert(!settings.write() && contents(path) == copied);
}

static void peerSavesWithoutDisplay(const FXString& work)
{
    FXString path = setup(work, "peer-without-display");
    put(path, original);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(settings.read());
    settings.writeStringEntry("SETTINGS", "local", "kept");
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        FXRegistry peer("xfe", "SwordFish");
        xf_watchportablesettings(&peer);
        assert(peer.read());
        peer.writeStringEntry("SETTINGS", "choice", "peer");
        assert(peer.write());
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    FXString problem;
    assert(!xf_portablesettingschanged(problem));
    assert(FXString(settings.readStringEntry("SETTINGS", "choice", "")) == "peer");
    assert(settings.write());
    FXRegistry saved("xfe", "SwordFish");
    assert(saved.read());
    assert(FXString(saved.readStringEntry("SETTINGS", "choice", "")) == "peer");
    assert(FXString(saved.readStringEntry("SETTINGS", "local", "")) == "kept");
    // Copying a peer's save record must not authorize an externally replaced file.
    FXString replacement = path + ".replacement";
    put(replacement, copied);
    assert(rename(replacement.text(), path.text()) == 0);
    assert(xf_portablesettingschanged(problem));
    assert(!settings.write() && contents(path) == copied);
}

static void unchangedSettingsMetadata(const FXString& work)
{
    FXString path = setup(work, "unchanged-metadata");
    put(path, original);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(settings.read());
    assert(chmod(path.text(), 0600) == 0);
    FXString problem;
    assert(!xf_portablesettingschanged(problem));
    FXString replacement = path + ".replacement";
    put(replacement, original);
    assert(rename(replacement.text(), path.text()) == 0);
    assert(!xf_portablesettingschanged(problem));
    settings.writeStringEntry("SETTINGS", "local", "saved");
    assert(settings.write());
}

static void concurrentPeerSaves(const FXString& work)
{
    FXString path = setup(work, "concurrent-peers");
    put(path, original);
    int ready[2], start[2];
    assert(pipe(ready) == 0 && pipe(start) == 0);
    pid_t children[3];
    for (int i = 0; i < 3; ++i)
    {
        children[i] = fork();
        assert(children[i] >= 0);
        if (children[i] == 0)
        {
            close(ready[0]);
            close(start[1]);
            FXRegistry settings("xfe", "SwordFish");
            xf_watchportablesettings(&settings);
            assert(settings.read());
            FXString key = "peer" + FXStringVal(i);
            settings.writeStringEntry("SETTINGS", key.text(), "kept");
            char byte = 0;
            assert(write(ready[1], &byte, 1) == 1 && read(start[0], &byte, 1) == 1);
            assert(settings.write());
            _exit(0);
        }
    }
    close(ready[1]);
    close(start[0]);
    char byte = 0;
    for (int i = 0; i < 3; ++i) assert(read(ready[0], &byte, 1) == 1);
    for (int i = 0; i < 3; ++i) assert(write(start[1], &byte, 1) == 1);
    close(ready[0]);
    close(start[1]);
    for (pid_t child : children)
    {
        int status;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    FXRegistry settings("xfe", "SwordFish");
    assert(settings.read());
    for (int i = 0; i < 3; ++i)
    {
        FXString key = "peer" + FXStringVal(i);
        assert(FXString(settings.readStringEntry("SETTINGS", key.text(), "")) == "kept");
    }
}

static void saveRecordFailure(const FXString& work)
{
    FXString path = setup(work, "save-record-failure");
    put(path, original);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(settings.read());
    assert(mkdir((path + ".saved").text(), 0700) == 0);
    settings.writeStringEntry("SETTINGS", "local", "saved");
    assert(!settings.write());
    FXString problem;
    assert(xf_portablesettingschanged(problem) && problem.contains("Cannot publish"));
    assert(contents(path).find("local=saved") != std::string::npos);
}

static void missingFile(const FXString& work)
{
    FXString path = setup(work, "missing");
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    settings.writeStringEntry("SETTINGS", "choice", "new");
    assert(settings.write());
    FXString problem;
    assert(!xf_portablesettingschanged(problem));
    assert(unlink(path.text()) == 0);
    assert(xf_portablesettingschanged(problem));
    settings.writeStringEntry("SETTINGS", "choice", "stale");
    assert(!settings.write() && access(path.text(), F_OK) != 0);
}

static void externallyCreated(const FXString& work)
{
    FXString path = setup(work, "created");
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    put(path, copied);
    settings.writeStringEntry("SETTINGS", "choice", "stale");
    FXString problem;
    assert(!settings.write() && xf_portablesettingschanged(problem) && contents(path) == copied);
}

static void writeFailure(const FXString& work)
{
    FXString path = setup(work, "write-failure");
    put(path, original);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(settings.read());
    FXString temporary = path + "_" + FXStringVal((FXint)getpid());
    assert(mkdir(temporary.text(), 0700) == 0);
    settings.writeStringEntry("SETTINGS", "choice", "stale");
    FXString problem;
    assert(!settings.write() && xf_portablesettingschanged(problem));
    assert(problem.contains("Cannot write") && contents(path) == original);
    assert(rmdir(temporary.text()) == 0);
}

static void temporaryCollision(const FXString& work)
{
    FXString path = setup(work, "temporary-collision");
    put(path, original);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(settings.read());
    FXString temporary = path + "_" + FXStringVal((FXint)getpid());
    put(temporary, copied);
    settings.writeStringEntry("SETTINGS", "choice", "stale");
    FXString problem;
    assert(!settings.write() && xf_portablesettingschanged(problem));
    assert(problem.contains("Cannot write") && contents(path) == original && contents(temporary) == copied);
}

static void racedSave(const FXString& work, const char* name, Injection timing)
{
    FXString path = setup(work, name);
    put(path, original);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(settings.read());
    settings.writeStringEntry("SETTINGS", "choice", "stale");
    injection = timing;
    assert(!settings.write());
    FXString problem;
    assert(xf_portablesettingschanged(problem));
    FXString temporary = path + "_" + FXStringVal((FXint)getpid());
    if (timing == BEFORE_EXCHANGE)
    {
        assert(contents(path) == copied && access(temporary.text(), F_OK) != 0);
    }
    else if (timing == AFTER_EXCHANGE)
    {
        assert(contents(path) == secondCopy && access(temporary.text(), F_OK) != 0);
    }
    else if (timing == BOTH)
    {
        assert(contents(path) == secondCopy && contents(temporary) == copied);
        assert(problem.contains(temporary));
    }
}

static void beforeExchange(const FXString& work) { racedSave(work, "race-before", BEFORE_EXCHANGE); }
static void afterExchange(const FXString& work) { racedSave(work, "race-after", AFTER_EXCHANGE); }
static void bothExchanges(const FXString& work) { racedSave(work, "race-both", BOTH); }
static void fallbackSave(const FXString& work, const char* name, Injection mode)
{
    FXString path = setup(work, name);
    put(path, original);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(settings.read());
    settings.writeStringEntry("SETTINGS", "choice", "fallback-saved");
    injection = mode;
    FXString problem;
    assert(settings.write() && !xf_portablesettingschanged(problem));
    assert(contents(path).find("fallback-saved") != std::string::npos);
}

static void noExchange(const FXString& work) { fallbackSave(work, "no-exchange", NO_EXCHANGE); }
static void hardLinkFallback(const FXString& work) { fallbackSave(work, "hard-link", NO_RENAMEAT2); }

static void fallbackMissingFile(const FXString& work)
{
    FXString path = setup(work, "missing-no-renameat2");
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    injection = NO_RENAMEAT2;
    settings.writeStringEntry("SETTINGS", "choice", "first-save");
    assert(settings.write());
    settings.writeStringEntry("SETTINGS", "choice", "second-save");
    FXString problem;
    assert(settings.write() && !xf_portablesettingschanged(problem));
    assert(contents(path).find("second-save") != std::string::npos);
}

static FXString preservedBackup(const FXString& path)
{
    FXString parent = FXPath::directory(path);
    DIR* directory = opendir(parent.text());
    assert(directory);
    FXString backup;
    while (struct dirent* entry = readdir(directory))
    {
        if (FXString(entry->d_name).left(12) == "xferc.guard-")
        {
            assert(backup.empty());
            backup = parent + "/" + entry->d_name + "/previous";
        }
    }
    closedir(directory);
    assert(!backup.empty());
    return backup;
}

static void fallbackRace(const FXString& work, const char* name, Injection mode, bool symlinked = false)
{
    FXString path = setup(work, name);
    FXString originalPath = symlinked ? FXPath::directory(path) + "/actual" : path;
    put(originalPath, original);
    if (symlinked) { assert(symlink("actual", path.text()) == 0); }
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(settings.read());
    settings.writeStringEntry("SETTINGS", "choice", "stale");
    raceTarget = path;
    injection = mode;
    FXString problem;
    assert(!settings.write() && xf_portablesettingschanged(problem));
    if (mode == FALLBACK_BEFORE)
    {
        assert(contents(path) == copied);
        if (symlinked)
        {
            struct stat info;
            assert(lstat(path.text(), &info) == 0 && S_ISLNK(info.st_mode));
        }
    }
    else if (mode == FALLBACK_AFTER) { assert(contents(path) == secondCopy); }
    else
    {
        FXString backup = preservedBackup(path);
        assert(contents(path) == secondCopy && contents(backup) == copied && problem.contains(backup));
    }
}

static void fallbackBefore(const FXString& work) { fallbackRace(work, "fallback-before", FALLBACK_BEFORE); }
static void fallbackAfter(const FXString& work) { fallbackRace(work, "fallback-after", FALLBACK_AFTER); }
static void fallbackBoth(const FXString& work) { fallbackRace(work, "fallback-both", FALLBACK_BOTH); }
static void fallbackSymlink(const FXString& work)
{
    fallbackRace(work, "fallback-symlink", FALLBACK_BEFORE, true);
}

static void exchangeSnapshotFailure(const FXString& work)
{
    FXString path = setup(work, "exchange-snapshot-failure");
    put(path, original);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(settings.read());
    settings.writeStringEntry("SETTINGS", "choice", "stale");
    raceTarget = path;
    injection = EXCHANGE_SNAPSHOT_FAILURE;
    FXString problem;
    assert(!settings.write() && xf_portablesettingschanged(problem));
    assert(problem.contains(strerror(EACCES)) && contents(path) == original);
}

static void fallbackSnapshotFailure(const FXString& work)
{
    FXString path = setup(work, "fallback-snapshot-failure");
    put(path, original);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(settings.read());
    settings.writeStringEntry("SETTINGS", "choice", "stale");
    injection = FALLBACK_SNAPSHOT_FAILURE;
    FXString problem;
    assert(!settings.write() && xf_portablesettingschanged(problem));
    FXString backup = preservedBackup(path);
    assert(problem.contains(strerror(EACCES)) && problem.contains(backup));
    assert(contents(backup) == original);
}

static void interruptedExchange(const FXString& work)
{
    FXString path = setup(work, "interrupted-exchange");
    FXString saved = path + "_12345";
    put(path, original);
    put(saved, copied);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    FXString problem;
    assert(xf_portablesettingschanged(problem) && problem.contains(saved));
    settings.writeStringEntry("SETTINGS", "choice", "stale");
    assert(!settings.write() && contents(path) == original && contents(saved) == copied);
}

static void interruptedFallback(const FXString& work)
{
    FXString path = setup(work, "interrupted-fallback");
    FXString directory = path + ".guard-orphan";
    assert(mkdir(directory.text(), 0700) == 0);
    FXString saved = directory + "/previous";
    put(saved, copied);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    FXString problem;
    assert(xf_portablesettingschanged(problem) && problem.contains(saved));
    settings.writeStringEntry("SETTINGS", "choice", "stale");
    assert(!settings.write() && access(path.text(), F_OK) != 0 && contents(saved) == copied);
}

static void unrelatedBackupNames(const FXString& work)
{
    FXString path = setup(work, "unrelated-names");
    put(path, original);
    put(path + "_notes", copied);
    FXString directory = path + ".guard-empty";
    assert(mkdir(directory.text(), 0700) == 0);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    FXString problem;
    assert(settings.read() && !xf_portablesettingschanged(problem));
    settings.writeStringEntry("SETTINGS", "choice", "saved");
    assert(settings.write() && !xf_portablesettingschanged(problem));
}

static void noSafePrimitive(const FXString& work)
{
    FXString path = setup(work, "no-safe-primitive");
    put(path, original);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(settings.read());
    settings.writeStringEntry("SETTINGS", "choice", "stale");
    injection = NO_ATOMIC_PRIMITIVE;
    FXString problem;
    assert(!settings.write() && xf_portablesettingschanged(problem));
    assert(problem.contains("cannot safely save") && contents(path) == original);
}

static void installedMode(const FXString& work)
{
    FXString path = setup(work, "installed", false);
    put(path, original);
    FXRegistry settings("xfe", "SwordFish");
    xf_watchportablesettings(&settings);
    assert(!xf_portablesettingswatching() && settings.read());
    put(path, copied);
    settings.writeStringEntry("SETTINGS", "choice", "installed-save");
    assert(settings.write() && contents(path).find("installed-save") != std::string::npos);
}

static void run(const FXString& root, const char* name, void (*test)(const FXString&))
{
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        test(root);
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(EXIT_FAILURE);
    }
    printf("PASS: %s\n", name);
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    FXString root = argv[1];
    run(root, "self-saves, another window and blocked rereads", selfSaves);
    run(root, "in-place changes with preserved timestamps", inPlaceCopy);
    run(root, "peer saves merge without an X display; external replacement is still protected", peerSavesWithoutDisplay);
    run(root, "unchanged settings content does not conflict after metadata or inode changes", unchangedSettingsMetadata);
    run(root, "three simultaneous processes retain each other's settings", concurrentPeerSaves);
    run(root, "save-record failure is reported after preserving the written settings", saveRecordFailure);
    run(root, "first save and external deletion", missingFile);
    run(root, "external creation before first save", externallyCreated);
    run(root, "temporary write failure is reported", writeFailure);
    run(root, "existing temporary settings are not overwritten or removed", temporaryCollision);
    run(root, "copy races just before replacement", beforeExchange);
    run(root, "copy races just after replacement", afterExchange);
    run(root, "two racing copies are both preserved", bothExchanges);
    run(root, "no-exchange fallback uses no-replace rename", noExchange);
    run(root, "older kernels fall back to hard links", hardLinkFallback);
    run(root, "first and subsequent saves without renameat2", fallbackMissingFile);
    run(root, "fallback preserves a copy before moving the old file", fallbackBefore);
    run(root, "fallback preserves a copy created during saving", fallbackAfter);
    run(root, "fallback preserves both racing copies", fallbackBoth);
    run(root, "fallback restores relative settings symlinks", fallbackSymlink);
    run(root, "exchange snapshot failure reports its cause and restores settings", exchangeSnapshotFailure);
    run(root, "fallback snapshot failure reports its cause and preserves the backup", fallbackSnapshotFailure);
    run(root, "interrupted exchange leaves settings versions protected", interruptedExchange);
    run(root, "interrupted fallback protects its saved settings", interruptedFallback);
    run(root, "unrelated settings filenames do not block saving", unrelatedBackupNames);
    run(root, "unsupported atomic primitives are reported without data loss", noSafePrimitive);
    run(root, "installed-mode settings behavior is unchanged", installedMode);
    return 0;
}
