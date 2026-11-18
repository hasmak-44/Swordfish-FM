#include "CustomActions.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <map>
#include <set>
#include <algorithm>
#include <functional>

#if defined(linux)
#include <linux/fs.h>
#endif

bool CustomAction::operator==(const CustomAction& b) const
{
    return id == b.id && parent == b.parent && name == b.name && description == b.description &&
        icon == b.icon && command == b.command && storagePath == b.storagePath && order == b.order &&
        program == b.program && parameters == b.parameters && workingDirectory == b.workingDirectory &&
        patterns == b.patterns && kind == b.kind &&
        toolbar == b.toolbar && files == b.files && folders == b.folders && single == b.single &&
        multiple == b.multiple && background == b.background && terminal == b.terminal &&
        confirm == b.confirm && showOutput == b.showOutput && programMode == b.programMode;
}

FXString CustomActions::duplicate(const FXString& id, const FXString& suffix, FXString& error)
{
    const CustomAction* original = find(id);
    if (!original || original->kind != CustomAction::ACTION)
    { error = "Select an action to duplicate."; return FXString::null; }
    if (items.size() >= MAX_ITEMS)
    { error = "At most 256 entries are supported."; return FXString::null; }
    unsigned position = original - &items[0];
    CustomAction copy = *original;
    copy.id = newId(); copy.name += suffix;
    items.insert(items.begin() + position + 1, copy);
    return copy.id;
}
#include <fnmatch.h>
#include <string>
#include <ctime>
#include <cstdlib>
#include <sys/wait.h>

namespace
{
const char* section = "CUSTOM ACTIONS";

FXString key(unsigned n, const char* field)
{
    FXString result;
    result.format("item%u.%s", n, field);
    return result;
}

bool integer(FXSettings& settings, const FXString& key, int maximum, int& value)
{
    FXString text = settings.readStringEntry(section, key.text(), "");
    text.trim();
    if (text.empty()) { return false; }
    value = 0;
    for (int i = 0; i < text.length(); ++i)
    {
        if (text[i] < '0' || text[i] > '9' || value > maximum / 10) { return false; }
        value = value * 10 + text[i] - '0';
        if (value > maximum) { return false; }
    }
    return true;
}

bool validId(const FXString& id)
{
    if (id.empty() || id.length() > 80) { return false; }
    for (int i = 0; i < id.length(); ++i)
    {
        char c = id[i];
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-') { return false; }
    }
    return true;
}

FXString quoted(const FXString& text, char quote)
{
    FXString result;
    if (!quote) { result += '\''; }
    for (int i = 0; i < text.length(); ++i)
    {
        char c = text[i];
        if (quote == '"')
        {
            if (c == '"' || c == '\\' || c == '$' || c == '`') { result += '\\'; }
            result += c;
        }
        else if (c == '\'') { result += "'\\''"; }
        else { result += c; }
    }
    if (!quote) { result += '\''; }
    return result;
}

bool boundary(char c)
{
    return !c || c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '|' || c == '&' ||
           c == '(' || c == ')' || c == '<' || c == '>';
}

FXString uri(const FXString& text)
{
    FXString result = "file://";
    const char* hex = "0123456789ABCDEF";
    for (int i = 0; i < text.length(); ++i)
    {
        unsigned char c = text[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
        {
            result += c;
        }
        else
        {
            result += '%'; result += hex[c >> 4]; result += hex[c & 15];
        }
    }
    return result;
}

void appendMessage(FXString& list, const FXString& text)
{
    if (!list.empty()) list += "\n";
    list += text;
}

bool readIniBoolean(FXSettings& settings, const char* group, const char* name, bool fallback, bool& value)
{
    if (!settings.existingEntry(group, name)) { value = fallback; return true; }
    FXString text = settings.readStringEntry(group, name, "");
    text.trim();
    if (text != "0" && text != "1") return false;
    value = text == "1";
    return true;
}

bool readIniOrder(FXSettings& settings, const char* group, int& order)
{
    if (!settings.existingEntry(group, "Order")) { order = -1; return true; }
    FXString text = settings.readStringEntry(group, "Order", "");
    text.trim();
    if (text.empty()) return false;
    order = 0;
    for (FXint i = 0; i < text.length(); ++i)
    {
        if (text[i] < '0' || text[i] > '9' || order > 100000000) return false;
        order = order * 10 + text[i] - '0';
    }
    return true;
}

bool readString(FXSettings& settings, const char* group, const char* name, FXString& value)
{
    value = settings.readStringEntry(group, name, "");
    return true;
}

FXString pathId(const FXString& path)
{
    uint64_t hash = 1469598103934665603ULL;
    for (FXint i = 0; i < path.length(); ++i)
        hash = (hash ^ static_cast<unsigned char>(path[i])) * 1099511628211ULL;
    FXString id;
    id.format("a-%016llx", static_cast<unsigned long long>(hash));
    return id;
}

bool readSettingsFile(const FXString& path, FXSettings& settings, FXString& error)
{
    struct stat info;
    if (lstat(path.text(), &info) != 0 || !S_ISREG(info.st_mode))
    {
        error = "not a regular file";
        return false;
    }
    if (!settings.parseFile(path.text(), true))
    {
        error = "cannot parse settings file";
        return false;
    }
    return true;
}

bool directoryNames(const FXString& path, std::vector<FXString>& names, FXString& error)
{
    DIR* directory = opendir(path.text());
    if (!directory)
    {
        error = strerror(errno);
        return false;
    }
    int failure = 0;
    while (true)
    {
        errno = 0;
        struct dirent* entry = readdir(directory);
        if (!entry)
        {
            failure = errno;
            break;
        }
        if (entry->d_name[0] == '.' && (!entry->d_name[1] ||
            (entry->d_name[1] == '.' && !entry->d_name[2]))) continue;
        FXString child = path + PATHSEPSTRING + entry->d_name;
        struct stat info;
        if (lstat(child.text(), &info) != 0)
        {
            if (errno != ENOENT) failure = errno;
            if (failure) break;
            continue;
        }
        names.push_back(entry->d_name);
    }
    if (closedir(directory) != 0 && !failure) failure = errno;
    if (failure)
    {
        error = strerror(failure);
        return false;
    }
    std::sort(names.begin(), names.end());
    return true;
}

bool loadActionTree(CustomActions& result, const FXString& root, const FXString& relative,
                    const FXString& parent, unsigned depth, FXString& warnings, FXString& error)
{
    if (depth > 16)
    {
        appendMessage(warnings, relative + ": nesting exceeds 16 folders");
        return true;
    }
    FXString directory = relative.empty() ? root : root + PATHSEPSTRING + relative;
    std::vector<FXString> names;
    if (!directoryNames(directory, names, error))
    {
        error = directory + ": " + error;
        return false;
    }
    struct LoadedEntry
    {
        CustomAction action;
        FXint order;
    };
    std::vector<LoadedEntry> entries;
    for (const FXString& name : names)
    {
        if (result.items.size() + entries.size() >= CustomActions::MAX_ITEMS)
        {
            appendMessage(warnings, "Only the first 256 custom-action entries are loaded.");
            break;
        }
        FXString itemPath = relative.empty() ? name : relative + PATHSEPSTRING + name;
        FXString folder = root + PATHSEPSTRING + itemPath;
        struct stat info;
        if (lstat(folder.text(), &info) != 0)
        {
            appendMessage(warnings, itemPath + ": no longer a directory");
            continue;
        }
        if (!S_ISDIR(info.st_mode)) continue;
        CustomAction action;
        action.id = pathId(itemPath);
        while (result.find(action.id) ||
               std::find_if(entries.begin(), entries.end(), [&action](const LoadedEntry& entry)
               { return entry.action.id == action.id; }) != entries.end())
            action.id += "-1";
        action.parent = parent;
        action.name = name;
        action.storagePath = itemPath;
        FXString actionFile = folder + PATHSEPSTRING "action.ini";
        FXString separatorFile = folder + PATHSEPSTRING "separator.ini";
        FXString groupFile = folder + PATHSEPSTRING "group.ini";
        struct stat metadata;
        bool actionFileExists = lstat(actionFile.text(), &metadata) == 0;
        bool separatorFileExists = lstat(separatorFile.text(), &metadata) == 0;
        bool groupFileExists = lstat(groupFile.text(), &metadata) == 0;
        FXSettings settings;
        FXString parseError;
        if (actionFileExists)
        {
            action.kind = CustomAction::ACTION;
            if (!readSettingsFile(actionFile, settings, parseError))
            {
                appendMessage(warnings, itemPath + "/action.ini: " + parseError);
                continue;
            }
            readString(settings, "Action", "Description", action.description);
            readString(settings, "Action", "Icon", action.icon);
            readString(settings, "Action", "Command", action.command);
            readString(settings, "Action", "Program", action.program);
            readString(settings, "Action", "Parameters", action.parameters);
            readString(settings, "Action", "WorkingDirectory", action.workingDirectory);
            readString(settings, "Action", "Patterns", action.patterns);
            if (action.patterns.empty()) action.patterns = "*";
            if (!readIniOrder(settings, "Action", action.order) ||
                !readIniBoolean(settings, "Action", "Toolbar", false, action.toolbar) ||
                !readIniBoolean(settings, "Action", "Files", true, action.files) ||
                !readIniBoolean(settings, "Action", "Folders", true, action.folders) ||
                !readIniBoolean(settings, "Action", "Single", true, action.single) ||
                !readIniBoolean(settings, "Action", "Multiple", true, action.multiple) ||
                !readIniBoolean(settings, "Action", "Background", false, action.background) ||
                !readIniBoolean(settings, "Action", "Terminal", false, action.terminal) ||
                !readIniBoolean(settings, "Action", "Confirm", false, action.confirm) ||
                !readIniBoolean(settings, "Action", "ShowOutput", false, action.showOutput) ||
                !readIniBoolean(settings, "Action", "ProgramMode", false, action.programMode))
            {
                appendMessage(warnings, itemPath + "/action.ini: invalid boolean or order value");
                continue;
            }
            if (!action.icon.empty() && action.icon.left(6) != "theme:" && !FXPath::isAbsolute(action.icon))
            {
                if (action.icon.contains('/') || action.icon == "." || action.icon == "..")
                {
                    appendMessage(warnings, itemPath + "/action.ini: invalid icon filename");
                    continue;
                }
                FXString iconPath = folder + PATHSEPSTRING + action.icon;
                if (lstat(iconPath.text(), &metadata) != 0 || !S_ISREG(metadata.st_mode))
                {
                    appendMessage(warnings, itemPath + "/action.ini: referenced icon is missing");
                    continue;
                }
                action.icon = iconPath;
            }
        }
        else if (separatorFileExists)
        {
            action.kind = CustomAction::SEPARATOR;
            if (!readSettingsFile(separatorFile, settings, parseError) ||
                !readIniOrder(settings, "Separator", action.order))
            {
                appendMessage(warnings, itemPath + "/separator.ini: " +
                              (parseError.empty() ? FXString("invalid order value") : parseError));
                continue;
            }
        }
        else
        {
            action.kind = CustomAction::FOLDER;
            if (groupFileExists)
            {
                if (!readSettingsFile(groupFile, settings, parseError))
                    appendMessage(warnings, itemPath + "/group.ini: " + parseError);
                else
                {
                    readString(settings, "Group", "Description", action.description);
                    readString(settings, "Group", "Icon", action.icon);
                    if (!readIniOrder(settings, "Group", action.order) ||
                        !readIniBoolean(settings, "Group", "Toolbar", false, action.toolbar))
                    {
                        appendMessage(warnings, itemPath + "/group.ini: invalid boolean or order value");
                        action.order = -1;
                        action.description.clear();
                        action.icon.clear();
                        action.toolbar = false;
                    }
                    else if (!action.icon.empty() && action.icon.left(6) != "theme:" &&
                             !FXPath::isAbsolute(action.icon))
                    {
                        if (!action.icon.contains('/') && action.icon != "." && action.icon != ".." &&
                            lstat((folder + PATHSEPSTRING + action.icon).text(), &metadata) == 0 &&
                            S_ISREG(metadata.st_mode))
                            action.icon = folder + PATHSEPSTRING + action.icon;
                        else
                        {
                            appendMessage(warnings, itemPath + "/group.ini: referenced icon is missing");
                            action.icon.clear();
                        }
                    }
                }
            }
        }

        CustomActions candidate = result;
        candidate.items.push_back(action);
        FXString validation;
        if (!candidate.validate(validation))
        {
            appendMessage(warnings, (action.kind == CustomAction::ACTION ? itemPath + "/action.ini" : itemPath) +
                          ": " + validation);
            continue;
        }
        entries.push_back({action, action.order});
    }
    std::stable_sort(entries.begin(), entries.end(), [](const LoadedEntry& a, const LoadedEntry& b)
    {
        if (a.order >= 0 && b.order >= 0 && a.order != b.order) return a.order < b.order;
        if ((a.order >= 0) != (b.order >= 0)) return a.order >= 0;
        return a.action.name < b.action.name;
    });
    for (const LoadedEntry& entry : entries)
    {
        result.items.push_back(entry.action);
        if (entry.action.kind == CustomAction::FOLDER &&
            !loadActionTree(result, root, entry.action.storagePath, entry.action.id, depth + 1, warnings, error))
            return false;
    }
    return true;
}

struct TreeDigest
{
    uint64_t first = 1469598103934665603ULL;
    uint64_t second = 1099511628211ULL;
    void update(const void* data, size_t size)
    {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            first = (first ^ bytes[i]) * 1099511628211ULL;
            second = (second ^ bytes[i]) * 14029467366897019727ULL;
        }
    }
    void update(const FXString& text) { update(text.text(), text.length()); }
    FXString value() const
    {
        FXString result;
        result.format("%016llx%016llx", static_cast<unsigned long long>(first),
                      static_cast<unsigned long long>(second));
        return result;
    }
};

bool digestNode(TreeDigest& digest, const FXString& path, const FXString& relative, FXString& error)
{
    struct stat info;
    if (lstat(path.text(), &info) != 0)
    {
        error = path + ": " + strerror(errno);
        return false;
    }
    digest.update(relative);
    digest.update(&info.st_mode, sizeof(info.st_mode));
    digest.update(&info.st_size, sizeof(info.st_size));
    digest.update(&info.st_mtim, sizeof(info.st_mtim));
    if (S_ISDIR(info.st_mode))
    {
        std::vector<FXString> names;
        if (!directoryNames(path, names, error)) return false;
        for (const FXString& name : names)
        {
            FXString child = relative.empty() ? name : relative + PATHSEPSTRING + name;
            if (!digestNode(digest, path + PATHSEPSTRING + name, child, error)) return false;
        }
        return true;
    }
    if (S_ISREG(info.st_mode))
    {
        int fd = open(path.text(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0)
        {
            error = path + ": " + strerror(errno);
            return false;
        }
        char buffer[32768];
        bool success = true;
        while (true)
        {
            ssize_t count = read(fd, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            if (count < 0)
            {
                error = path + ": " + strerror(errno);
                success = false;
                break;
            }
            if (!count) break;
            digest.update(buffer, static_cast<size_t>(count));
        }
        if (close(fd) != 0 && success)
        {
            error = path + ": " + strerror(errno);
            success = false;
        }
        return success;
    }
    if (S_ISLNK(info.st_mode))
    {
        char target[PATH_MAX + 1];
        ssize_t length = readlink(path.text(), target, PATH_MAX);
        if (length < 0)
        {
            error = path + ": " + strerror(errno);
            return false;
        }
        digest.update(target, static_cast<size_t>(length));
        return true;
    }
    error = path + ": unsupported special file";
    return false;
}

bool copyFile(const FXString& source, const FXString& destination, mode_t mode, FXString& error)
{
    int input = open(source.text(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (input < 0)
    {
        error = source + ": " + strerror(errno);
        return false;
    }
    struct stat sourceInfo;
    if (fstat(input, &sourceInfo) != 0 || !S_ISREG(sourceInfo.st_mode))
    {
        int failure = errno ? errno : EINVAL;
        close(input);
        error = source + ": " + strerror(failure);
        return false;
    }
    int output = open(destination.text(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode & 0777);
    if (output < 0)
    {
        int failure = errno;
        close(input);
        error = destination + ": " + strerror(failure);
        return false;
    }
    char buffer[32768];
    int failure = 0;
    while (!failure)
    {
        ssize_t count = read(input, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { failure = errno; break; }
        if (!count) break;
        ssize_t offset = 0;
        while (offset < count)
        {
            ssize_t written = write(output, buffer + offset, count - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { failure = written < 0 ? errno : EIO; break; }
            offset += written;
        }
    }
    if (fchmod(output, mode & 0777) != 0 && !failure) failure = errno;
    if (close(input) != 0 && !failure) failure = errno;
    if (close(output) != 0 && !failure) failure = errno;
    if (!failure) return true;
    unlink(destination.text());
    error = source + ": " + strerror(failure);
    return false;
}

bool copyContents(const FXString& source, const FXString& destination,
                  const std::set<std::string>& excluded, FXString& error)
{
    std::vector<FXString> names;
    if (!directoryNames(source, names, error)) return false;
    for (const FXString& name : names)
    {
        if (excluded.count(name.text())) continue;
        FXString from = source + PATHSEPSTRING + name;
        FXString to = destination + PATHSEPSTRING + name;
        struct stat info;
        if (lstat(from.text(), &info) != 0)
        {
            error = from + ": " + strerror(errno);
            return false;
        }
        if (S_ISDIR(info.st_mode))
        {
            if (mkdir(to.text(), info.st_mode & 0777) != 0 ||
                !copyContents(from, to, std::set<std::string>(), error))
            {
                if (error.empty()) error = to + ": " + strerror(errno);
                return false;
            }
        }
        else if (S_ISREG(info.st_mode))
        {
            if (!copyFile(from, to, info.st_mode, error)) return false;
        }
        else if (S_ISLNK(info.st_mode))
        {
            char target[PATH_MAX + 1];
            ssize_t length = readlink(from.text(), target, PATH_MAX);
            if (length < 0)
            {
                error = from + ": " + strerror(errno);
                return false;
            }
            target[length] = '\0';
            if (symlink(target, to.text()) != 0)
            {
                error = to + ": " + strerror(errno);
                return false;
            }
        }
        else
        {
            error = from + ": unsupported special file";
            return false;
        }
    }
    return true;
}

bool makeDirectories(const FXString& path, mode_t mode, FXString& error)
{
    struct stat info;
    if (lstat(path.text(), &info) == 0)
    {
        if (S_ISDIR(info.st_mode)) return true;
        error = path + ": not a directory";
        return false;
    }
    if (errno != ENOENT)
    {
        error = path + ": " + strerror(errno);
        return false;
    }
    FXString parent = FXPath::directory(path);
    if (parent == path || parent.empty() || !makeDirectories(parent, mode, error)) return false;
    if (mkdir(path.text(), mode) == 0 || errno == EEXIST)
    {
        if (lstat(path.text(), &info) == 0 && S_ISDIR(info.st_mode)) return true;
    }
    error = path + ": " + strerror(errno);
    return false;
}

bool safeComponent(const FXString& name)
{
    if (name.empty() || name == "." || name == ".." || name.length() > 200 ||
        name.contains('/') || name.contains('\t') || name.contains('\n') || name.contains('\r'))
        return false;
    return true;
}

FXString targetPath(const CustomAction& action, const std::map<std::string, FXString>& paths)
{
    if (action.kind == CustomAction::SEPARATOR)
        return "separator-" + action.id;
    FXString parent;
    if (!action.parent.empty())
    {
        std::map<std::string, FXString>::const_iterator found = paths.find(action.parent.text());
        if (found == paths.end()) return FXString::null;
        parent = found->second + PATHSEPSTRING;
    }
    return parent + action.name;
}

bool writeItemFile(const FXString& path, const CustomAction& action, int order,
                   const FXString& iconName, FXString& error)
{
    FXSettings settings;
    const char* group = action.kind == CustomAction::ACTION ? "Action" :
                        action.kind == CustomAction::FOLDER ? "Group" : "Separator";
    settings.writeIntEntry(group, "Order", order);
    if (action.kind == CustomAction::ACTION)
    {
        settings.writeStringEntry(group, "Description", action.description.text());
        settings.writeStringEntry(group, "Icon", iconName.text());
        settings.writeStringEntry(group, "Command", action.command.text());
        settings.writeStringEntry(group, "Program", action.program.text());
        settings.writeStringEntry(group, "Parameters", action.parameters.text());
        settings.writeStringEntry(group, "WorkingDirectory", action.workingDirectory.text());
        settings.writeStringEntry(group, "Patterns", action.patterns.text());
#define WRITE_ACTION_BOOL(keyName, field) settings.writeIntEntry(group, keyName, action.field ? 1 : 0)
        WRITE_ACTION_BOOL("Toolbar", toolbar);
        WRITE_ACTION_BOOL("Files", files);
        WRITE_ACTION_BOOL("Folders", folders);
        WRITE_ACTION_BOOL("Single", single);
        WRITE_ACTION_BOOL("Multiple", multiple);
        WRITE_ACTION_BOOL("Background", background);
        WRITE_ACTION_BOOL("Terminal", terminal);
        WRITE_ACTION_BOOL("Confirm", confirm);
        WRITE_ACTION_BOOL("ShowOutput", showOutput);
        WRITE_ACTION_BOOL("ProgramMode", programMode);
#undef WRITE_ACTION_BOOL
    }
    else if (action.kind == CustomAction::FOLDER)
    {
        settings.writeStringEntry(group, "Description", action.description.text());
        settings.writeStringEntry(group, "Icon", iconName.text());
        settings.writeIntEntry(group, "Toolbar", action.toolbar ? 1 : 0);
    }
    if (settings.unparseFile(path.text())) return true;
    error = path + ": cannot write action settings";
    return false;
}

bool removeTree(const FXString& path, FXString& error)
{
    struct stat info;
    if (lstat(path.text(), &info) != 0)
    {
        if (errno == ENOENT) return true;
        error = path + ": " + strerror(errno);
        return false;
    }
    if (S_ISDIR(info.st_mode))
    {
        std::vector<FXString> names;
        if (!directoryNames(path, names, error)) return false;
        for (const FXString& name : names)
            if (!removeTree(path + PATHSEPSTRING + name, error)) return false;
        if (rmdir(path.text()) != 0)
        {
            error = path + ": " + strerror(errno);
            return false;
        }
        return true;
    }
    if (unlink(path.text()) == 0) return true;
    error = path + ": " + strerror(errno);
    return false;
}

bool writeMarker(const FXString& path, const char* text, FXString& error)
{
    int fd = open(path.text(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
    {
        error = path + ": " + strerror(errno);
        return false;
    }
    size_t length = strlen(text);
    size_t offset = 0;
    while (offset < length)
    {
        ssize_t count = write(fd, text + offset, length - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0)
        {
            int failure = count < 0 ? errno : EIO;
            close(fd);
            error = path + ": " + strerror(failure);
            return false;
        }
        offset += static_cast<size_t>(count);
    }
    int failure = fsync(fd) == 0 ? 0 : errno;
    if (close(fd) != 0 && !failure) failure = errno;
    if (failure)
    {
        error = path + ": " + strerror(failure);
        return false;
    }
    return true;
}

bool relativePathSafe(const FXString& path)
{
    if (path.empty() || FXPath::isAbsolute(path)) return false;
    for (FXint i = 0; i <= path.contains('/'); ++i)
        if (!safeComponent(path.section('/', i))) return false;
    return true;
}

bool pathWithin(const FXString& path, const FXString& root)
{
    return path == root || (path.left(root.length()) == root && path[root.length()] == '/');
}

bool canonicalPath(const FXString& path, FXString& result)
{
    char resolved[PATH_MAX + 1];
    if (!realpath(path.text(), resolved)) return false;
    result = resolved;
    return true;
}

bool portableDataForActions(const FXString& root, FXString& result, FXString& error)
{
    result.clear();
    const char* data = getenv("SWORDFISH_PORTABLE_DATA");
    if (!data || !*data || FXPath::absolute(root) != FXPath::absolute(FXString(data) + "/custom-actions"))
        return true;
    if (canonicalPath(data, result)) return true;
    error = FXString(data) + ": cannot resolve portable data folder: " + strerror(errno);
    return false;
}

FXString relocatedPortablePath(const FXString& path, const FXString& data)
{
    const char* markers[] = { "/portable-data/", ".data/" };
    if (!pathWithin(path, data))
    {
        for (const char* marker : markers)
        {
            FXint offset = path.rfind(marker);
            if (offset < 0) continue;
            FXString candidate = FXPath::absolute(data + "/" + path.mid(offset + strlen(marker), path.length()));
            FXString resolved;
            if (pathWithin(candidate, data) && canonicalPath(candidate, resolved) && pathWithin(resolved, data))
                return resolved;
        }
    }
    FXString resolved;
    return canonicalPath(path, resolved) ? resolved : FXString::null;
}

bool systemProgramPath(const FXString& path)
{
    const char* roots[] = { "/usr", "/bin", "/sbin", "/lib", "/lib64", "/opt", "/snap",
                            "/var/lib/flatpak" };
    for (const char* root : roots)
        if (pathWithin(path, root)) return true;
    const char* appdir = getenv("APPDIR");
    FXString bundled;
    return appdir && *appdir && canonicalPath(appdir, bundled) && pathWithin(path, bundled);
}

bool scriptExtension(const FXString& path)
{
    FXString extension = FXPath::extension(path).lower();
    const char* extensions[] = { "sh", "bash", "zsh", "ksh", "py", "pl", "rb", "js", "php", "lua", "awk" };
    for (const char* candidate : extensions)
        if (extension == candidate) return true;
    return false;
}

struct ShellWord
{
    FXint start, end;
    FXString value;
    bool literal;
};

ShellWord shellWord(const FXString& command, FXint& position)
{
    ShellWord word = { position, position, FXString::null, true };
    char quote = 0;
    while (position < command.length())
    {
        char c = command[position];
        if (!quote && boundary(c)) break;
        if ((c == '\'' || c == '"') && (!quote || quote == c))
        {
            quote = quote ? 0 : c;
            ++position;
            continue;
        }
        if (c == '\\' && quote != '\'' && position + 1 < command.length())
        {
            char next = command[position + 1];
            if (!quote || next == '$' || next == '`' || next == '"' || next == '\\' || next == '\n')
            {
                if (next != '\n') word.value += next;
                position += 2;
                continue;
            }
        }
        if (c == '%' && position + 1 < command.length())
        {
            char next = command[position + 1];
            if (next != '%') word.literal = false;
            word.value += c;
            if (next != '%') word.value += next;
            position += 2;
            continue;
        }
        if (quote != '\'' && (c == '$' || c == '`')) word.literal = false;
        if (!quote && (c == '*' || c == '?' || c == '[' || (c == '~' && word.value.empty())))
            word.literal = false;
        word.value += c;
        ++position;
    }
    word.end = position;
    return word;
}

bool shellAssignment(const FXString& command, const ShellWord& word)
{
    FXint i = word.start;
    if (i == word.end || !((command[i] >= 'a' && command[i] <= 'z') ||
                           (command[i] >= 'A' && command[i] <= 'Z') || command[i] == '_'))
        return false;
    while (++i < word.end)
    {
        char c = command[i];
        if (c == '=') return true;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return false;
}

bool interpreterName(const FXString& value)
{
    if (value.contains('/') && !FXPath::isAbsolute(value)) return false;
    FXString name = FXPath::name(value);
    const char* names[] = { "sh", "bash", "dash", "zsh", "ksh", "perl", "ruby", "node", "php", "lua" };
    for (const char* candidate : names)
        if (name == candidate) return true;
    if (name.left(6) != "python") return false;
    for (FXint i = 6; i < name.length(); ++i)
        if ((name[i] < '0' || name[i] > '9') && name[i] != '.') return false;
    return true;
}

bool interpreterFlag(const FXString& interpreter, const FXString& flag)
{
    if (flag == "--") return true;
    FXString name = FXPath::name(interpreter);
    if (name == "sh" || name == "bash" || name == "dash" || name == "zsh" || name == "ksh")
        return flag == "-e" || flag == "-n" || flag == "-u" || flag == "-v" || flag == "-x";
    if (name.left(6) == "python")
        return flag == "-u" || flag == "-B" || flag == "-E" || flag == "-I" ||
               flag == "-S" || flag == "-s" || flag == "-v";
    if (name == "perl") return flag == "-w" || flag == "-T" || flag == "-t" || flag == "-W";
    if (name == "ruby") return flag == "-w";
    if (name == "node") return flag == "--no-warnings";
    if (name == "lua") return flag == "-E" || flag == "-v";
    return false;
}

FXString scriptFileName(const FXString& source, const FXString& destination, FXString& error)
{
    FXString name = FXPath::name(source);
    for (FXint i = 0; i < name.length(); ++i)
        if (static_cast<unsigned char>(name[i]) < 32 || name[i] == 127) name[i] = '_';
    if (!safeComponent(name)) name = "script";
    if (name == "action.ini" || name == "group.ini" || name == "separator.ini")
        name = "script-" + name;
    FXString title = FXPath::title(name), extension = FXPath::extension(name), candidate = name;
    for (unsigned suffix = 2; suffix < 10000; ++suffix)
    {
        FXString path = destination + PATHSEPSTRING + candidate;
        struct stat info;
        if (lstat(path.text(), &info) != 0)
        {
            if (errno == ENOENT) return candidate;
            error = path + ": " + strerror(errno);
            return FXString::null;
        }
        candidate = title + " (" + FXStringVal(suffix) + ")" +
                    (extension.empty() ? FXString::null : "." + extension);
    }
    error = "No unused filename is available for the action script: " + source;
    return FXString::null;
}

bool backupActionSettings(const FXString& path, FXString& error)
{
    struct stat source;
    if (lstat(path.text(), &source) != 0)
    {
        error = path + ": cannot preserve the original action settings: " + strerror(errno);
        return false;
    }
    if (!S_ISREG(source.st_mode))
    {
        error = path + ": the original action settings are not a regular file";
        return false;
    }
    FXString base = FXPath::directory(path) + "/.action.ini-before-script-migration";
    for (unsigned suffix = 1; suffix < 10000; ++suffix)
    {
        FXString destination = suffix == 1 ? base : base + "." + FXStringVal(suffix);
        struct stat existing;
        if (lstat(destination.text(), &existing) == 0) continue;
        if (errno != ENOENT)
        {
            error = destination + ": " + strerror(errno);
            return false;
        }
        return copyFile(path, destination, source.st_mode, error);
    }
    error = path + ": no unused filename is available for the settings backup";
    return false;
}

FXString referenceSuffix(const FXString& path, const FXString& root)
{
    if (path == root) return FXString::null;
    return path.mid(root.length(), path.length());
}

bool portableScriptReference(const CustomAction& action, const FXString& path, bool interpreterArgument,
                             const FXString& data, const FXString& root, const FXString& destination,
                             std::map<std::string, FXString>& copied, FXString& reference,
                             bool& identified, FXString& warnings, FXString& error)
{
    reference.clear();
    identified = false;
    if (!FXPath::isAbsolute(path)) return true;
    FXString source = relocatedPortablePath(path, data);
    if (source.empty())
    {
        if (interpreterArgument || scriptExtension(path))
            appendMessage(warnings, action.name + ": script could not be resolved and was not copied: " +
                          path + ": " + strerror(errno));
        return true;
    }
    if (!pathWithin(source, data) && systemProgramPath(source)) return true;
    struct stat info;
    if (stat(source.text(), &info) != 0)
    {
        error = action.name + ": cannot inspect " + source + ": " + strerror(errno);
        return false;
    }
    if (!S_ISREG(info.st_mode))
    {
        if (interpreterArgument || scriptExtension(source))
            appendMessage(warnings, action.name + ": script is not a regular file and was not copied: " + source);
        return true;
    }
    int fd = open(source.text(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
    {
        if (!interpreterArgument && !scriptExtension(source))
        {
            appendMessage(warnings, action.name + ": executable could not be inspected for portability: " +
                          source + ": " + strerror(errno));
            return true;
        }
        error = action.name + ": cannot read script " + source + ": " + strerror(errno);
        return false;
    }
    char bytes[4096];
    ssize_t count;
    do { count = read(fd, bytes, sizeof(bytes)); } while (count < 0 && errno == EINTR);
    if (count < 0)
    {
        int failure = errno;
        close(fd);
        error = action.name + ": cannot read script " + source + ": " + strerror(failure);
        return false;
    }
    int failure = 0;
    if (close(fd) != 0 && !failure) failure = errno;
    if (failure)
    {
        error = action.name + ": cannot read script " + source + ": " + strerror(failure);
        return false;
    }
    bool text = memchr(bytes, '\0', static_cast<size_t>(count)) == NULL;
    bool shebang = count >= 2 && bytes[0] == '#' && bytes[1] == '!';
    identified = text && (interpreterArgument || shebang || (scriptExtension(source) && (info.st_mode & 0111)));
    if (!identified) return true;

    FXString actionRoot;
    if (!action.storagePath.empty() && relativePathSafe(action.storagePath))
        canonicalPath(root + PATHSEPSTRING + action.storagePath, actionRoot);
    if (!actionRoot.empty() && pathWithin(source, actionRoot))
    {
        reference = "%a" + referenceSuffix(source, actionRoot);
        return true;
    }
    FXString actionsRoot;
    canonicalPath(root, actionsRoot);
    if (pathWithin(source, data) && (actionsRoot.empty() || !pathWithin(source, actionsRoot)))
    {
        reference = "%r" + referenceSuffix(source, data);
        return true;
    }
    if (destination.empty())
    {
        reference = "%a/script";
        return true;
    }
    std::map<std::string, FXString>::const_iterator existing = copied.find(source.text());
    if (existing != copied.end())
    {
        reference = existing->second;
        return true;
    }
    FXString name = scriptFileName(source, destination, error);
    FXString target = destination + PATHSEPSTRING + name;
    if (name.empty() || !copyFile(source, target, info.st_mode, error))
        return false;
    struct stat after;
    if (stat(source.text(), &after) != 0 || info.st_dev != after.st_dev || info.st_ino != after.st_ino ||
        info.st_size != after.st_size || info.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        info.st_mtim.tv_nsec != after.st_mtim.tv_nsec || info.st_ctim.tv_sec != after.st_ctim.tv_sec ||
        info.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
    {
        unlink(target.text());
        error = action.name + ": script changed while it was being copied: " + source;
        return false;
    }
    reference = "%a/" + name;
    copied[source.text()] = reference;
    return true;
}

FXString commandReference(const FXString& reference)
{
    FXString suffix = reference.mid(2, reference.length());
    suffix.substitute("%", "%%");
    return quoted(reference.left(2) + suffix, 0);
}

bool relocateCommandScripts(CustomAction& action, FXString& command, const FXString& data,
                            const FXString& root, const FXString& destination, bool argumentsOnly,
                            const FXString& initialInterpreter,
                            std::map<std::string, FXString>& copied, bool& changed,
                            FXString& warnings, FXString& error)
{
    FXString rewritten;
    FXint position = 0, last = 0;
    FXString interpreter = initialInterpreter;
    bool expecting = !argumentsOnly, env = false, redirect = false;
    while (position < command.length())
    {
        char c = command[position];
        if (c == ' ' || c == '\t') { ++position; continue; }
        if (c == '\n' || c == ';' || c == '|' || c == '&' || c == '(' || c == ')')
        {
            ++position;
            expecting = true; interpreter.clear(); env = false; redirect = false;
            continue;
        }
        if (c == '<' || c == '>')
        {
            ++position;
            if (position < command.length() &&
                (command[position] == c || command[position] == '&' ||
                 (c == '>' && command[position] == '|') || (c == '<' && command[position] == '>')))
                ++position;
            redirect = true;
            continue;
        }
        ShellWord word = shellWord(command, position);
        if (redirect) { redirect = false; continue; }
        if (expecting && shellAssignment(command, word)) continue;
        if (expecting && env && word.value.left(1) == "-")
        {
            if (word.value != "--" && word.value != "-i" && word.value != "--ignore-environment")
                expecting = false;
            continue;
        }
        if (expecting && word.literal &&
            (word.value == "env" || word.value == "/usr/bin/env" || word.value == "/bin/env"))
        {
            env = true;
            continue;
        }
        if (!expecting && interpreter.empty()) continue;
        if (!interpreter.empty() && word.value.left(1) == "-")
        {
            if (interpreterFlag(interpreter, word.value)) continue;
            interpreter.clear();
            continue;
        }
        FXString reference;
        bool identified = false;
        if (word.literal)
        {
            if (!portableScriptReference(action, word.value, !interpreter.empty(), data, root, destination, copied,
                                         reference, identified, warnings, error)) return false;
            if (!FXPath::isAbsolute(word.value) && scriptExtension(word.value))
                appendMessage(warnings, action.name + ": relative script path was not copied: " + word.value);
        }
        else if ((expecting || !interpreter.empty()) && scriptExtension(word.value) &&
                 word.value.left(3) != "%a/" && word.value.left(3) != "%r/")
            appendMessage(warnings, action.name + ": dynamic script path was not copied: " + word.value);
        if (!reference.empty())
        {
            rewritten += command.mid(last, word.start - last) + commandReference(reference);
            last = word.end;
            changed = true;
        }
        if (expecting)
        {
            interpreter = word.literal && !identified && interpreterName(word.value) ? word.value : FXString::null;
            expecting = false;
        }
        else interpreter.clear();
    }
    if (last)
    {
        rewritten += command.mid(last, command.length());
        command = rewritten;
    }
    return true;
}

bool relocatePortableAction(CustomAction& action, const FXString& data, const FXString& root,
                            const FXString& destination, bool& changed, FXString& warnings, FXString& error)
{
    if (action.kind != CustomAction::ACTION) return true;
    std::map<std::string, FXString> copied;
    if (action.programMode)
    {
        FXString reference;
        bool identified;
        if (!portableScriptReference(action, action.program, false, data, root, destination, copied,
                                     reference, identified, warnings, error)) return false;
        FXString interpreter = !identified && interpreterName(action.program) ? action.program : FXString::null;
        if (!reference.empty()) { action.program = reference; changed = true; }
        if (!relocateCommandScripts(action, action.parameters, data, root, destination, true, interpreter,
                                    copied, changed, warnings, error)) return false;
    }
    else if (!relocateCommandScripts(action, action.command, data, root, destination, false, FXString::null,
                                     copied, changed, warnings, error)) return false;

    FXString directory;
    bool literal = true;
    for (FXint i = 0; i < action.workingDirectory.length(); ++i)
    {
        char c = action.workingDirectory[i];
        if (c == '%')
        {
            if (i + 1 == action.workingDirectory.length() || action.workingDirectory[++i] != '%')
            { literal = false; break; }
        }
        directory += c;
    }
    if (literal && FXPath::isAbsolute(directory))
    {
        FXString source = relocatedPortablePath(directory, data), actionRoot;
        if (!action.storagePath.empty() && relativePathSafe(action.storagePath))
            canonicalPath(root + PATHSEPSTRING + action.storagePath, actionRoot);
        FXString reference;
        if (!source.empty() && !actionRoot.empty() && pathWithin(source, actionRoot))
            reference = "%a" + referenceSuffix(source, actionRoot);
        else if (!source.empty() && pathWithin(source, data))
            reference = "%r" + referenceSuffix(source, data);
        if (!reference.empty())
        {
            FXString suffix = reference.mid(2, reference.length());
            suffix.substitute("%", "%%");
            action.workingDirectory = reference.left(2) + suffix;
            changed = true;
        }
    }
    return true;
}

FXString iconFileName(const FXString& source, const FXString& destination, FXString& error)
{
    FXString name = FXPath::name(source);
    if (!safeComponent(name)) name = "icon." + FXPath::extension(source);
    FXString title = FXPath::title(name), extension = FXPath::extension(name);
    FXString candidate = name;
    for (unsigned suffix = 1; suffix < 10000; ++suffix)
    {
        FXString target = destination + PATHSEPSTRING + candidate;
        struct stat info;
        if (lstat(target.text(), &info) != 0)
        {
            if (errno == ENOENT) return candidate;
            error = target + ": " + strerror(errno);
            return FXString::null;
        }
        candidate = title + " (" + FXStringVal(suffix + 1) + ")." + extension;
    }
    error = "No unused filename is available for the action icon: " + source;
    return FXString::null;
}

bool renameDirectoryExchange(const FXString& path, const FXString& stage, FXString& error)
{
#if defined(linux) && defined(SYS_renameat2) && defined(RENAME_EXCHANGE)
    if (syscall(SYS_renameat2, AT_FDCWD, path.text(), AT_FDCWD, stage.text(), RENAME_EXCHANGE) == 0)
        return true;
    int exchangeError = errno;
    if (exchangeError != ENOSYS && exchangeError != EINVAL && exchangeError != EOPNOTSUPP &&
        exchangeError != EXDEV)
    {
        error = path + ": " + strerror(exchangeError);
        return false;
    }
#endif
    FXString pattern = path + ".previous-XXXXXX";
    std::vector<char> buffer(pattern.text(), pattern.text() + pattern.length() + 1);
    char* backupName = mkdtemp(buffer.data());
    if (!backupName)
    {
        error = path + ": cannot create a recovery folder: " + strerror(errno);
        return false;
    }
    FXString backup = backupName;
    if (rmdir(backup.text()) != 0 || rename(path.text(), backup.text()) != 0)
    {
        int failure = errno;
        rmdir(backup.text());
        error = path + ": cannot preserve the previous actions: " + strerror(failure);
        return false;
    }
    if (rename(stage.text(), path.text()) != 0)
    {
        int failure = errno;
        if (rename(backup.text(), path.text()) != 0)
        {
            error = "Action replacement failed and the previous folder could not be restored: " + backup;
            return false;
        }
        error = path + ": cannot install new actions: " + strerror(failure);
        return false;
    }
    return removeTree(backup, error);
}
}

FXString CustomActions::storageDirectory()
{
    const char* portable = getenv("SWORDFISH_PORTABLE_DATA");
    if (portable && *portable) return FXString(portable) + "/custom-actions";
    const char* data = getenv("XDG_DATA_HOME");
    if (data && *data) return FXString(data) + "/swordfish/custom-actions";
    const char* home = getenv("HOME");
    return FXString(home && *home ? home : ".") + "/.local/share/swordfish/custom-actions";
}

FXString CustomActions::directoryFingerprint(const FXString& root, FXString& error)
{
    error.clear();
    struct stat info;
    if (lstat(root.text(), &info) != 0)
    {
        if (errno == ENOENT) return FXString::null;
        error = root + ": " + strerror(errno);
        return FXString::null;
    }
    if (!S_ISDIR(info.st_mode))
    {
        error = root + ": not a directory";
        return FXString::null;
    }
    TreeDigest digest;
    if (!digestNode(digest, root, "", error)) return FXString::null;
    return digest.value();
}

bool CustomActions::loadDirectory(const FXString& root, FXString& error, FXString& warnings)
{
    error.clear();
    warnings.clear();
    struct stat info;
    if (lstat(root.text(), &info) != 0)
    {
        if (errno == ENOENT) { items.clear(); return true; }
        error = root + ": " + strerror(errno);
        return false;
    }
    if (!S_ISDIR(info.st_mode))
    {
        error = root + ": not a directory";
        return false;
    }
    CustomActions loaded;
    if (!loadActionTree(loaded, root, "", "", 0, warnings, error)) return false;
    items.swap(loaded.items);
    return true;
}

bool CustomActions::portableScriptsNeedMigration(const FXString& root, bool& needed,
                                                  FXString& warnings, FXString& error) const
{
    needed = false;
    warnings.clear();
    error.clear();
    FXString data;
    if (!portableDataForActions(root, data, error)) return false;
    if (data.empty()) return true;
    for (const CustomAction& original : items)
    {
        CustomAction action = original;
        if (!relocatePortableAction(action, data, root, "", needed, warnings, error)) return false;
    }
    return true;
}

bool CustomActions::migratePortableScripts(const FXString& root, const FXString& expectedFingerprint,
                                           FXString& error, FXString& warnings) const
{
    error.clear();
    warnings.clear();
    FXString data;
    if (!portableDataForActions(root, data, error)) return false;
    if (data.empty()) return true;
    FXString original = directoryFingerprint(root, error);
    if (!error.empty()) return false;
    if (!expectedFingerprint.empty() && original != expectedFingerprint)
    {
        error = "Custom actions changed on disk before their scripts could be migrated.";
        return false;
    }

    FXString pattern = root + ".stage-XXXXXX";
    std::vector<char> templateName(pattern.text(), pattern.text() + pattern.length() + 1);
    char* made = mkdtemp(templateName.data());
    if (!made)
    {
        error = root + ": cannot create a temporary action folder: " + strerror(errno);
        return false;
    }
    FXString stage = made;
    bool success = copyContents(root, stage, std::set<std::string>(), error), anyChanged = false;
    CustomActions rewritten = *this;
    for (size_t index = 0; index < items.size(); ++index)
    {
        const CustomAction& action = items[index];
        if (!success || action.kind != CustomAction::ACTION) continue;
        if (!relativePathSafe(action.storagePath))
        {
            error = "An action folder has an invalid stored path.";
            success = false;
            break;
        }
        FXString destination = stage + PATHSEPSTRING + action.storagePath;
        CustomAction saved = action;
        bool changed = false;
        if (!relocatePortableAction(saved, data, root, destination, changed, warnings, error))
        {
            success = false;
            break;
        }
        if (!changed) continue;
        anyChanged = true;
        rewritten.items[index] = saved;
        FXString path = destination + "/action.ini", parseError;
        FXSettings settings;
        if (!readSettingsFile(path, settings, parseError))
        {
            error = path + ": " + parseError;
            success = false;
            break;
        }
        if (FXString(settings.readStringEntry("Action", "Command", "")) != action.command ||
            FXString(settings.readStringEntry("Action", "Program", "")) != action.program ||
            FXString(settings.readStringEntry("Action", "Parameters", "")) != action.parameters ||
            FXString(settings.readStringEntry("Action", "WorkingDirectory", "")) != action.workingDirectory)
        {
            error = path + ": the action changed before its script could be migrated";
            success = false;
            break;
        }
        if (!backupActionSettings(path, error))
        {
            success = false;
            break;
        }
        if (saved.command != action.command) settings.writeStringEntry("Action", "Command", saved.command.text());
        if (saved.program != action.program) settings.writeStringEntry("Action", "Program", saved.program.text());
        if (saved.parameters != action.parameters)
            settings.writeStringEntry("Action", "Parameters", saved.parameters.text());
        if (saved.workingDirectory != action.workingDirectory)
            settings.writeStringEntry("Action", "WorkingDirectory", saved.workingDirectory.text());
        if (!settings.unparseFile(path.text()))
        {
            error = path + ": cannot write action settings";
            success = false;
            break;
        }
    }
    if (success && anyChanged) success = rewritten.validate(error);
    if (success && anyChanged)
    {
        FXString current = directoryFingerprint(root, error);
        if (!error.empty() || current != original)
        {
            if (error.empty()) error = "Custom actions changed on disk while their scripts were being migrated.";
            success = false;
        }
        if (success) success = renameDirectoryExchange(root, stage, error);
    }
    FXString cleanup;
    if (!removeTree(stage, cleanup))
        appendMessage(warnings, "The temporary action snapshot could not be removed: " + cleanup);
    return success;
}

bool CustomActions::saveDirectory(const FXString& root, FXString& error, FXString& warnings,
                                 const FXString& expectedFingerprint)
{
    error.clear();
    warnings.clear();
    std::map<std::string, std::set<std::string>> usedNames;
    for (CustomAction& action : items)
    {
        if (action.kind == CustomAction::SEPARATOR) continue;
        FXString original = action.name;
        for (FXint i = 0; i < action.name.length(); ++i)
        {
            unsigned char character = action.name[i];
            if (character == '/' || character < 32 || character == 127) action.name[i] = '-';
        }
        action.name = action.name.trim();
        if (action.name.empty() || action.name == "." || action.name == "..") action.name = "Action";
        if (action.name.length() > 200) action.name = action.name.left(200);
        std::set<std::string>& siblings = usedNames[action.parent.text()];
        FXString base = action.name;
        for (unsigned suffix = 2; siblings.count(action.name.text()); ++suffix)
        {
            action.name = base + " (" + FXStringVal(suffix) + ")";
            if (action.name.length() > 200)
                action.name = base.left(200 - FXStringVal(suffix).length() - 3) + " (" + FXStringVal(suffix) + ")";
        }
        siblings.insert(action.name.text());
        if (action.name != original)
            appendMessage(warnings, "Custom action folder renamed to avoid an invalid or duplicate name: " +
                          original + " -> " + action.name);
    }
    if (!validate(error)) return false;
    for (const CustomAction& action : items)
    {
        if (action.kind != CustomAction::SEPARATOR && !safeComponent(action.name))
        {
            error = "Action and group names must be valid folder names without slashes or control characters.";
            return false;
        }
    }
    FXString parent = FXPath::directory(root);
    if (parent.empty()) parent = ".";
    if (!makeDirectories(parent, 0700, error) || !makeDirectories(root, 0700, error)) return false;
    FXString portableData;
    if (!portableDataForActions(root, portableData, error)) return false;
    FXString original = directoryFingerprint(root, error);
    if (!error.empty()) return false;
    if (!expectedFingerprint.empty() && original != expectedFingerprint)
    {
        error = "Custom actions changed on disk while they were being edited.";
        return false;
    }

    std::map<std::string, FXString> destinations;
    std::set<std::string> resolving;
    std::function<bool(const CustomAction&)> resolveDestination = [&](const CustomAction& action)
    {
        if (destinations.find(action.id.text()) != destinations.end()) return true;
        if (!resolving.insert(action.id.text()).second) return false;
        if (!action.parent.empty())
        {
            const CustomAction* parentAction = find(action.parent);
            if (!parentAction || !resolveDestination(*parentAction))
            {
                resolving.erase(action.id.text());
                return false;
            }
        }
        FXString target = targetPath(action, destinations);
        if (target.empty() || !relativePathSafe(target)) return false;
        destinations[action.id.text()] = target;
        resolving.erase(action.id.text());
        return true;
    };
    for (const CustomAction& action : items)
    {
        if (!resolveDestination(action))
        {
            error = "The custom-action folders cannot be written in this order.";
            return false;
        }
    }

    FXString pattern = root + ".stage-XXXXXX";
    std::vector<char> templateName(pattern.text(), pattern.text() + pattern.length() + 1);
    char* made = mkdtemp(templateName.data());
    if (!made)
    {
        error = root + ": cannot create a temporary action folder: " + strerror(errno);
        return false;
    }
    FXString stage = made;
    bool success = true;
    CustomActions rewritten = *this;
    std::set<std::string> topLevelItems;
    for (const CustomAction& action : items)
        if (action.parent.empty() && !action.storagePath.empty())
            topLevelItems.insert(action.storagePath.section('/', 0).text());
    success = copyContents(root, stage, topLevelItems, error);

    for (size_t index = 0; index < items.size(); ++index)
    {
        if (!success) break;
        const CustomAction& action = items[index];
        FXString destination = stage + PATHSEPSTRING + destinations[action.id.text()];
        if (!makeDirectories(destination, 0700, error)) { success = false; break; }

        if (!action.storagePath.empty())
        {
            if (!relativePathSafe(action.storagePath))
            {
                error = "An action folder has an invalid stored path.";
                success = false;
                break;
            }
            FXString source = root + PATHSEPSTRING + action.storagePath;
            struct stat sourceInfo;
            if (lstat(source.text(), &sourceInfo) != 0 || !S_ISDIR(sourceInfo.st_mode))
            {
                error = source + ": the action folder changed outside the editor";
                success = false;
                break;
            }
            std::set<std::string> childFolders;
            for (const CustomAction& child : items)
            {
                if (child.parent != action.id || child.storagePath.empty()) continue;
                if (child.storagePath.left(action.storagePath.length() + 1) != action.storagePath + PATHSEPSTRING)
                    continue;
                FXString remainder = child.storagePath.mid(action.storagePath.length() + 1,
                                                             child.storagePath.length());
                if (remainder.find('/') < 0) childFolders.insert(remainder.text());
            }
            if (!copyContents(source, destination, childFolders, error)) { success = false; break; }
        }

        FXString iconName = action.icon.left(6) == "theme:" ? action.icon : FXString::null;
        if (!action.icon.empty() && action.icon.left(6) != "theme:")
        {
            if (!FXPath::isAbsolute(action.icon))
            {
                error = action.name + ": the icon path is not a readable image file";
                success = false;
                break;
            }
            struct stat iconInfo;
            if (lstat(action.icon.text(), &iconInfo) != 0 || !S_ISREG(iconInfo.st_mode))
            {
                error = action.name + ": the referenced icon is missing or is not a regular file";
                success = false;
                break;
            }
            iconName = FXPath::name(action.icon);
            FXString existing = destination + PATHSEPSTRING + iconName;
            struct stat existingInfo;
            bool alreadyCopied = action.storagePath.length() &&
                action.icon.left((root + PATHSEPSTRING + action.storagePath + PATHSEPSTRING).length()) ==
                    root + PATHSEPSTRING + action.storagePath + PATHSEPSTRING;
            if (!alreadyCopied || lstat(existing.text(), &existingInfo) != 0)
            {
                iconName = iconFileName(action.icon, destination, error);
                if (iconName.empty()) { success = false; break; }
                if (!copyFile(action.icon, destination + PATHSEPSTRING + iconName, iconInfo.st_mode, error))
                { success = false; break; }
            }
        }
        int order = 0;
        for (const CustomAction& sibling : items)
        {
            if (sibling.parent == action.parent)
            {
                if (sibling.id == action.id) break;
                ++order;
            }
        }
        FXString file = destination + PATHSEPSTRING +
            (action.kind == CustomAction::ACTION ? "action.ini" :
             action.kind == CustomAction::FOLDER ? "group.ini" : "separator.ini");
        CustomAction saved = action;
        bool relocated = false;
        if (!portableData.empty() &&
            !relocatePortableAction(saved, portableData, root, destination, relocated, warnings, error))
        { success = false; break; }
        rewritten.items[index] = saved;
        if (!writeItemFile(file, saved, order, iconName, error)) { success = false; break; }
    }

    if (success) success = rewritten.validate(error);
    if (success)
        success = writeMarker(stage + PATHSEPSTRING ".swordfish-storage", "1\n", error);
    if (success)
    {
        FXString current = directoryFingerprint(root, error);
        if (!error.empty() || current != original)
        {
            if (error.empty()) error = "Custom actions changed on disk while they were being saved.";
            success = false;
        }
    }
    if (success) success = renameDirectoryExchange(root, stage, error);
    if (!success)
    {
        FXString cleanup;
        if (!removeTree(stage, cleanup))
        {
            appendMessage(warnings, "The incomplete action snapshot could not be removed: " + cleanup);
        }
        return false;
    }
    FXString cleanup;
    if (!removeTree(stage, cleanup))
        appendMessage(warnings, "Actions were saved, but the previous snapshot could not be removed: " + cleanup);
    return true;
}

const CustomAction* CustomActions::find(const FXString& id) const
{
    for (unsigned i = 0; i < items.size(); ++i) { if (items[i].id == id) { return &items[i]; } }
    return NULL;
}

FXString CustomActions::newId() const
{
    static unsigned serial = 0;
    FXString id;
    do { id.format("a-%lx-%lx-%x", (unsigned long)time(NULL), (unsigned long)getpid(), ++serial); }
    while (find(id));
    return id;
}

bool CustomActions::validate(FXString& error) const
{
    error.clear();
    if (items.size() > MAX_ITEMS) { error = "At most 256 custom action entries are supported."; return false; }
    std::set<std::string> ids;
    CustomActionContext sample;
    sample.directory = "/current"; sample.opposite = "/opposite";
    sample.paths.push_back("/current/example"); sample.directories.push_back(false);
    for (unsigned i = 0; i < items.size(); ++i)
    {
        const CustomAction& a = items[i];
        if (!validId(a.id) || !ids.insert(a.id.text()).second)
        { error = "Invalid or duplicate custom action ID."; return false; }
        if (a.kind < CustomAction::ACTION || a.kind > CustomAction::SEPARATOR ||
            a.name.length() > 256 || a.description.length() > 2048 || a.icon.length() > 4096 ||
            a.command.length() > 16384 || a.patterns.length() > 4096)
        { error = "Invalid entry type or an excessively long field."; return false; }
        if (a.kind != CustomAction::SEPARATOR &&
            (FXString(a.name).trim().empty() || a.name.contains('\n') || a.name.contains('\t') || a.name.contains('\r')))
        { error = "Actions and folders need a name without tabs or newlines."; return false; }
        if (!a.parent.empty())
        {
            const CustomAction* p = find(a.parent);
            if (!p || p->kind != CustomAction::FOLDER)
            { error = "An entry's parent must be an existing folder."; return false; }
        }
        FXString parent = a.parent;
        unsigned depth = 0;
        while (!parent.empty())
        {
            if (parent == a.id || ++depth > 16)
            { error = "Folders cannot contain themselves; nesting is limited to 16 levels."; return false; }
            const CustomAction* p = find(parent);
            if (!p) { error = "Missing parent folder."; return false; }
            parent = p->parent;
        }
        if (a.kind == CustomAction::ACTION)
        {
            if (a.program.length() > 4096 || a.parameters.length() > 16384 || a.workingDirectory.length() > 4096 ||
                (a.programMode && (a.program.empty() || a.program.contains('\n') || a.program.contains('\r'))))
            { error = "Program mode needs one executable name or path, without newlines; a field is too long."; return false; }
            bool usablePattern = false;
            for (int n = 0; n <= a.patterns.contains(';'); ++n)
            { if (!a.patterns.section(';', n).trim().empty()) { usablePattern = true; break; } }
            if ((!a.programMode && FXString(a.command).trim().empty()) || (!a.files && !a.folders && !a.background) ||
                (!a.single && !a.multiple && !a.background) || !usablePattern)
            { error = "Actions need a command, filename patterns, and at least one usable selection condition."; return false; }
            FXString expanded, directory;
            if (!prepare(a, sample, expanded, directory, error, false)) { error = a.name + ": " + error; return false; }
        }
    }
    return true;
}

bool CustomActions::load(FXSettings& settings, FXString& error)
{
    CustomActions loaded;
    if (!settings.existingEntry(section, "version"))
    {
        if (settings.existingEntry(section, "count"))
        { error = "Custom action settings have no version."; return false; }
        items.clear(); error.clear(); return true;
    }
    int count, version;
    if (!integer(settings, "version", 1, version) || version != 1 ||
        !integer(settings, "count", MAX_ITEMS, count))
    { error = "Unsupported or invalid custom action settings."; return false; }
    for (int i = 0; i < count; ++i)
    {
        CustomAction a;
#define READ_STRING(field) a.field = settings.readStringEntry(section, key(i, #field).text(), "")
        READ_STRING(id); READ_STRING(parent); READ_STRING(name); READ_STRING(description);
        READ_STRING(icon); READ_STRING(command); READ_STRING(patterns);
        READ_STRING(program); READ_STRING(parameters); READ_STRING(workingDirectory);
#undef READ_STRING
        if (!integer(settings, key(i, "kind"), CustomAction::SEPARATOR, a.kind))
        { error = "Invalid custom action entry type."; return false; }
#define READ_BOOL(field) \
        { int v; if (!integer(settings, key(i, #field), 1, v)) \
          { error = "Invalid custom action boolean setting."; return false; } a.field = v; }
        READ_BOOL(toolbar); READ_BOOL(files); READ_BOOL(folders); READ_BOOL(single);
        READ_BOOL(multiple); READ_BOOL(background); READ_BOOL(terminal); READ_BOOL(confirm);
        if (settings.existingEntry(section, key(i, "showOutput").text()))
        {
            READ_BOOL(showOutput);
        }
        if (settings.existingEntry(section, key(i, "programMode").text())) { READ_BOOL(programMode); }
#undef READ_BOOL
        loaded.items.push_back(a);
    }
    if (!loaded.validate(error)) { return false; }
    items.swap(loaded.items);
    return true;
}

void CustomActions::save(FXSettings& settings) const
{
    settings.deleteSection(section);
    settings.writeIntEntry(section, "version", 1);
    settings.writeIntEntry(section, "count", items.size());
    for (unsigned i = 0; i < items.size(); ++i)
    {
        const CustomAction& a = items[i];
#define WRITE_STRING(field) settings.writeStringEntry(section, key(i, #field).text(), a.field.text())
        WRITE_STRING(id); WRITE_STRING(parent); WRITE_STRING(name); WRITE_STRING(description);
        WRITE_STRING(icon); WRITE_STRING(command); WRITE_STRING(patterns);
        WRITE_STRING(program); WRITE_STRING(parameters); WRITE_STRING(workingDirectory);
#undef WRITE_STRING
        settings.writeIntEntry(section, key(i, "kind").text(), a.kind);
#define WRITE_BOOL(field) settings.writeIntEntry(section, key(i, #field).text(), a.field ? 1 : 0)
        WRITE_BOOL(toolbar); WRITE_BOOL(files); WRITE_BOOL(folders); WRITE_BOOL(single);
        WRITE_BOOL(multiple); WRITE_BOOL(background); WRITE_BOOL(terminal); WRITE_BOOL(confirm);
        WRITE_BOOL(showOutput);
        WRITE_BOOL(programMode);
#undef WRITE_BOOL
    }
}

std::vector<unsigned> CustomActions::children(const FXString& parent) const
{
    std::vector<unsigned> result;
    for (unsigned i = 0; i < items.size(); ++i) { if (items[i].parent == parent) { result.push_back(i); } }
    return result;
}

bool CustomActions::move(const FXString& id, const FXString& parent, FXString& error)
{
    for (unsigned i = 0; i < items.size(); ++i)
    {
        if (items[i].id != id) { continue; }
        FXString old = items[i].parent;
        items[i].parent = parent;
        if (!validate(error)) { items[i].parent = old; return false; }
        CustomAction moved = items[i];
        items.erase(items.begin() + i);
        items.push_back(moved);
        return true;
    }
    error = "Entry not found."; return false;
}

void CustomActions::remove(const FXString& id)
{
    std::vector<unsigned> nested = children(id);
    std::vector<FXString> ids;
    for (unsigned i = 0; i < nested.size(); ++i) { ids.push_back(items[nested[i]].id); }
    for (unsigned i = 0; i < ids.size(); ++i) { remove(ids[i]); }
    for (unsigned i = 0; i < items.size(); ++i)
    {
        if (items[i].id == id) { items.erase(items.begin() + i); break; }
    }
}

void CustomActions::reorder(const FXString& id, int direction)
{
    const CustomAction* a = find(id);
    if (!a) { return; }
    std::vector<unsigned> siblings = children(a->parent);
    for (unsigned i = 0; i < siblings.size(); ++i)
    {
        if (items[siblings[i]].id != id) { continue; }
        int next = (int)i + direction;
        if (next >= 0 && next < (int)siblings.size()) { std::swap(items[siblings[i]], items[siblings[next]]); }
        return;
    }
}

std::vector<unsigned> CustomActions::toolbarItems() const
{
    std::vector<unsigned> result;
    // Traverse hierarchy order; an exposed folder already includes its descendants.
    std::vector<FXString> parents(1, FXString::null);
    std::vector<unsigned> positions(1, 0);
    while (!parents.empty())
    {
        std::vector<unsigned> nodes = children(parents.back());
        if (positions.back() >= nodes.size()) { parents.pop_back(); positions.pop_back(); continue; }
        unsigned n = nodes[positions.back()++];
        if (items[n].toolbar) { result.push_back(n); }
        else if (items[n].kind == CustomAction::FOLDER) { parents.push_back(items[n].id); positions.push_back(0); }
    }
    return result;
}

bool CustomActions::matches(const CustomAction& a, const CustomActionContext& ctx) const
{
    if (!ctx.local || a.kind != CustomAction::ACTION || ctx.paths.size() != ctx.directories.size()) { return false; }
    if (ctx.paths.empty()) { return a.background || a.folders; }
    if ((ctx.paths.size() == 1 && !a.single) || (ctx.paths.size() > 1 && !a.multiple)) { return false; }
    for (unsigned i = 0; i < ctx.paths.size(); ++i)
    {
        if ((ctx.directories[i] && !a.folders) || (!ctx.directories[i] && !a.files)) { return false; }
        if (ctx.directories[i]) { continue; }
        FXString name = FXPath::name(ctx.paths[i]);
        bool matched = false;
        for (int p = 0; p <= a.patterns.contains(';'); ++p)
        {
            FXString pattern = a.patterns.section(';', p).trim();
            if (!pattern.empty() && pattern[0] == '.' && pattern.find('*') < 0 &&
                pattern.find('?') < 0 && pattern.find('[') < 0)
            { pattern.prepend('*'); }
            if (!pattern.empty() && fnmatch(pattern.text(), name.text(), 0) == 0) { matched = true; break; }
        }
        if (!matched) { return false; }
    }
    return true;
}

bool CustomActions::visible(unsigned i, const CustomActionContext& ctx) const
{
    if (items[i].kind == CustomAction::ACTION) { return matches(items[i], ctx); }
    if (items[i].kind == CustomAction::SEPARATOR) { return false; }
    std::vector<unsigned> nodes = children(items[i].id);
    for (unsigned n = 0; n < nodes.size(); ++n) { if (visible(nodes[n], ctx)) { return true; } }
    return false;
}

bool CustomActions::expand(const FXString& input, const CustomActionContext& ctx, FXString& output, FXString& error,
                           bool currentFolder, const FXString& actionDirectory)
{
    output.clear(); error.clear();
    char quote = 0;
    for (int i = 0; i < input.length(); ++i)
    {
        char c = input[i];
        if (c == '\\' && quote != '\'')
        {
            if (i + 1 == input.length()) { error = "Trailing backslash in command."; return false; }
            output += c; output += input[++i]; continue;
        }
        if ((quote != '\'' && (c == '`' || (c == '$' && (input[i + 1] == '(' || input[i + 1] == '{')))) ||
            (!quote && c == '<' && input[i + 1] == '<'))
        { error = "Use a script for backticks, shell substitutions, or here-documents."; return false; }
        if ((c == '\'' || c == '"') && (!quote || quote == c))
        {
            quote = quote ? 0 : c; output += c; continue;
        }
        if (c == '#' && !quote)
        { error = "Use a script for shell comments, or quote literal # characters."; return false; }
        if (c != '%') { output += c; continue; }
        if (++i == input.length()) { error = "A trailing % needs escaping as %%."; return false; }
        c = input[i];
        if (c == '%') { output += '%'; continue; }
        bool list = c == 'F' || c == 'N' || c == 'U' || c == 'D' || c == 'W' || c == 'X';
        if (list && (quote || (i > 1 && !boundary(input[i - 2])) || !boundary(input[i + 1])))
        { error = "List placeholders (%F, %N, %U, %D, %W, %X) must be separate, unquoted arguments."; return false; }
        std::vector<FXString> values;
        if (c == 'p') { values.push_back(ctx.directory); }
        else if (c == 'c')
        {
            FXString count; count.format("%u", (unsigned)ctx.paths.size()); values.push_back(count);
        }
        else if (c == 'o')
        {
            if (ctx.opposite.empty()) { error = "The opposite pane has no local directory."; return false; }
            values.push_back(ctx.opposite);
        }
        else if (c == 'a')
        {
            if (actionDirectory.empty()) { error = "Save this action before using its action folder."; return false; }
            values.push_back(actionDirectory);
        }
        else if (c == 'r')
        {
            const char* data = getenv("SWORDFISH_PORTABLE_DATA");
            if (!data || !*data) { error = "The portable-data folder is unavailable."; return false; }
            values.push_back(data);
        }
        else if (c == 'd' || c == 'f' || c == 'n' || c == 'u' || c == 'w' || c == 'x' || list)
        {
            if (ctx.paths.empty())
            {
                if (c == 'd' || c == 'D') { values.push_back(ctx.directory); }
                else if (currentFolder)
                {
                    FXString value = ctx.directory;
                    if (c == 'n' || c == 'N') { value = FXPath::name(value); }
                    else if (c == 'u' || c == 'U') { value = uri(value); }
                    values.push_back(value);
                }
                else { error = "This command needs selected files; use %p for the current directory."; return false; }
            }
            for (unsigned n = 0; n < ctx.paths.size(); ++n)
            {
                FXString value = ctx.paths[n];
                if (c == 'n' || c == 'N') { value = FXPath::name(value); }
                else if (c == 'u' || c == 'U') { value = uri(value); }
                else if (c == 'd' || c == 'D') { value = FXPath::directory(value); }
                if (std::find(values.begin(), values.end(), value) == values.end() || (c != 'd' && c != 'D'))
                { values.push_back(value); }
                if (!list) { break; }
            }
            if (c == 'w' || c == 'W' || c == 'x' || c == 'X')
            {
                for (unsigned n = 0; n < values.size(); ++n)
                {
                    FXString base = FXPath::name(values[n]);
                    int dot = base.rfind('.');
                    if (dot <= 0) { dot = base.length(); }
                    values[n] = (c == 'w' || c == 'W') ? base.left(dot) :
                                (dot < base.length() ? base.mid(dot + 1, base.length() - dot - 1) : FXString::null);
                }
            }
        }
        else { error = "Unknown placeholder. Use %f %F %n %N %d %D %u %U %p %o %a %r %c %w %W %x %X or %%."; return false; }
        for (unsigned n = 0; n < values.size(); ++n)
        {
            if (n) { output += ' '; }
            output += quoted(values[n], quote);
        }
    }
    if (quote) { error = "Unclosed quote in command."; return false; }
    return true;
}

bool CustomActions::prepare(const CustomAction& action, const CustomActionContext& ctx, FXString& command,
                            FXString& directory, FXString& error, bool verify)
{
    command.clear(); directory.clear(); error.clear();
    FXString actionDirectory;
    if (!action.storagePath.empty() && relativePathSafe(action.storagePath))
        actionDirectory = FXPath::absolute(storageDirectory() + PATHSEPSTRING + action.storagePath);
    else if (!verify)
        actionDirectory = "/custom-action";
    const char* portable = getenv("SWORDFISH_PORTABLE_DATA");
    const FXString& input = action.workingDirectory;
    for (int i = 0; i < input.length(); ++i)
    {
        if (input[i] != '%') { directory += input[i]; continue; }
        if (++i == input.length()) { error = "Trailing % in working directory; use %% for a literal percent."; return false; }
        char token = input[i];
        if (token == '%') { directory += '%'; }
        else if (token == 'p') { directory += ctx.directory; }
        else if (token == 'o')
        {
            if (ctx.opposite.empty()) { error = "The opposite pane has no local directory."; return false; }
            directory += ctx.opposite;
        }
        else if (token == 'd') { directory += ctx.paths.empty() ? ctx.directory : FXPath::directory(ctx.paths[0]); }
        else if (token == 'f')
        {
            if (ctx.paths.empty() && !action.folders)
            { error = "Working directory %f requires a selection, or Folders enabled."; return false; }
            directory += ctx.paths.empty() ? ctx.directory : ctx.paths[0];
        }
        else if (token == 'a')
        {
            if (actionDirectory.empty()) { error = "Save this action before using its action folder."; return false; }
            directory += actionDirectory;
        }
        else if (token == 'r')
        {
            if (!portable || !*portable) { error = "The portable-data folder is unavailable."; return false; }
            directory += portable;
        }
        else { error = "Working directory accepts only %p, %o, %d, %f, %a, %r and %% (one path, no shell evaluation)."; return false; }
    }
    if (directory.empty()) { directory = ctx.directory; }
    directory = FXPath::absolute(ctx.directory, directory);
    if (verify)
    {
        struct stat info;
        if (stat(directory.text(), &info) != 0 || !S_ISDIR(info.st_mode) || access(directory.text(), X_OK) != 0)
        { error = "Working directory is missing, not a directory, or inaccessible: " + directory; return false; }
    }
    if (!action.programMode) { return expand(action.command, ctx, command, error, action.folders, actionDirectory); }
    if (action.program.empty()) { error = "Enter one program name or executable path."; return false; }
    FXString executable = action.program;
    if (executable.left(3) == "%a/")
    {
        if (actionDirectory.empty()) { error = "Save this action before using its action folder."; return false; }
        executable = actionDirectory + executable.mid(2, executable.length());
    }
    else if (executable.left(3) == "%r/")
    {
        if (!portable || !*portable) { error = "The portable-data folder is unavailable."; return false; }
        executable = FXString(portable) + executable.mid(2, executable.length());
    }
    if (verify)
    {
        if (executable.contains('/')) { executable = FXPath::absolute(directory, executable); }
        else
        {
            executable.clear();
            const char* path = getenv("SWORDFISH_HOST_PATH");
            if (!path || !*path) { path = getenv("PATH"); }
            FXString search = path ? path : "/usr/local/bin:/usr/bin:/bin";
            for (int n = 0; n <= search.contains(':'); ++n)
            {
                FXString entry = search.section(':', n);
                FXString candidate = FXPath::absolute(directory, entry.empty() ? "." : entry) + "/" + action.program;
                struct stat info;
                if (stat(candidate.text(), &info) == 0 && S_ISREG(info.st_mode) && access(candidate.text(), X_OK) == 0)
                { executable = candidate; break; }
            }
        }
        struct stat info;
        if (executable.empty() || stat(executable.text(), &info) != 0 || !S_ISREG(info.st_mode) ||
            access(executable.text(), X_OK) != 0)
        { error = "Program not found or not executable on the host: " + action.program; return false; }
    }
    FXString arguments;
    if (!expand(action.parameters, ctx, arguments, error, action.folders, actionDirectory)) { return false; }
    command = quoted(executable, 0);
    if (!arguments.empty()) { command += " " + arguments; }
    return true;
}

bool CustomActions::check(const CustomAction& action, const CustomActionContext& ctx, FXString& error)
{
    FXString command, directory;
    if (!prepare(action, ctx, command, directory, error)) { return false; }
    int channel[2];
    if (pipe(channel) != 0)
    { error = FXString("Cannot start syntax check: ") + strerror(errno); return false; }
    pid_t child = fork();
    if (child == 0)
    {
        close(channel[0]);
        if (dup2(channel[1], STDERR_FILENO) < 0 || dup2(channel[1], STDOUT_FILENO) < 0) _exit(126);
        close(channel[1]);
        execl("/bin/sh", "sh", "-n", "-c", command.text(), (char*)NULL);
        _exit(126);
    }
    close(channel[1]);
    if (child < 0)
    {
        int failure = errno;
        close(channel[0]);
        error = FXString("Cannot start syntax check: ") + strerror(failure);
        return false;
    }
    char buffer[1024];
    FXString diagnostic;
    ssize_t count;
    while ((count = read(channel[0], buffer, sizeof(buffer))) != 0)
    {
        if (count < 0)
        {
            if (errno == EINTR) continue;
            error = FXString("Cannot read syntax check: ") + strerror(errno);
            break;
        }
        diagnostic.append(buffer, count);
    }
    close(channel[0]);
    int status;
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result < 0) { error = FXString("Cannot obtain syntax check status: ") + strerror(errno); return false; }
    if (!error.empty()) return false;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        error = diagnostic.empty() ? FXString("Shell syntax check failed.") : diagnostic;
        return false;
    }
    return true;
}

FXString CustomActions::commandText(const CustomAction& a)
{
    if (!a.programMode) { return a.command; }
    FXString program = a.program;
    bool portablePath = program.left(3) == "%a/" || program.left(3) == "%r/";
    program.substitute("%", "%%");
    if (portablePath) program.erase(0, 1);
    bool plain = !program.empty();
    for (int i = 0; i < program.length(); ++i)
    {
        char c = program[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '/' || c == '.' || c == '_' || c == '-' || c == '+' || c == '%'))
        { plain = false; break; }
    }
    if (!plain)
    {
        program.substitute("'", "'\\''");
        program = "'" + program + "'";
    }
    return a.parameters.empty() ? program : program + " " + a.parameters;
}
