#include "SystemIntegration.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <sstream>
#include <vector>
#include <algorithm>

#ifndef SWORDFISH_COMMAND_LINK_HELPER
#define SWORDFISH_COMMAND_LINK_HELPER "/usr/libexec/swordfish-command-link"
#endif

namespace SystemIntegration
{

namespace
{

struct Item
{
    bool owned;
    std::string path;
    std::string target;
    std::string icon;

    Item() : owned(false)
    {
    }
};

struct State
{
    Item desktop;
    Item menu;
    Item command;
};

static const char* const stateHeader = "SWORDFISH_SYSTEM_INTEGRATION_V2\n";
static const char* const legacyStateHeader = "SWORDFISH_SYSTEM_INTEGRATION_V1\n";

std::string envOr(const char* primary, const char* fallback, const std::string& defaultValue)
{
    const char* value = getenv(primary);
    if ((!value || !*value) && fallback)
    {
        value = getenv(fallback);
    }
    return value && *value ? value : defaultValue;
}

std::string homeDirectory()
{
    const char* home = getenv("HOME");
    if (!home || !*home || home[0] != '/')
    {
        return "";
    }
    return home;
}

std::string hostSearchPath()
{
    const char* path = getenv("SWORDFISH_HOST_PATH");
    if (!path)
    {
        path = getenv("PATH");
    }
    return path ? path : "";
}

std::string configHome()
{
    std::string home = homeDirectory();
    return envOr("SWORDFISH_HOST_XDG_CONFIG_HOME", "XDG_CONFIG_HOME",
                 home.empty() ? "" : home + "/.config");
}

std::string dataHome()
{
    std::string home = homeDirectory();
    return envOr("SWORDFISH_HOST_XDG_DATA_HOME", "XDG_DATA_HOME",
                 home.empty() ? "" : home + "/.local/share");
}

std::string normalizeAbsolute(const std::string& path)
{
    if (path.empty() || path[0] != '/')
    {
        return "";
    }
    std::vector<std::string> components;
    size_t offset = 1;
    while (offset <= path.size())
    {
        size_t end = path.find('/', offset);
        std::string component = path.substr(offset, end == std::string::npos ? end : end - offset);
        if (component == "..")
        {
            if (!components.empty())
            {
                components.pop_back();
            }
        }
        else if (!component.empty() && component != ".")
        {
            components.push_back(component);
        }
        if (end == std::string::npos)
        {
            break;
        }
        offset = end + 1;
    }
    std::string result;
    for (size_t i = 0; i < components.size(); ++i)
    {
        result += "/" + components[i];
    }
    return result.empty() ? "/" : result;
}

std::string parentPath(const std::string& path)
{
    size_t slash = path.find_last_of('/');
    return slash == 0 ? "/" : path.substr(0, slash);
}

std::string baseName(const std::string& path)
{
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool openDirectory(const std::string& path, bool create, mode_t mode, int& result, std::string& error,
                   int* errorCode = NULL)
{
    if (errorCode)
    {
        *errorCode = 0;
    }
    std::string normalized = normalizeAbsolute(path);
    if (normalized.empty())
    {
        if (errorCode)
        {
            *errorCode = EINVAL;
        }
        error = "Expected an absolute directory path: " + path;
        return false;
    }

    int current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (current < 0)
    {
        if (errorCode)
        {
            *errorCode = errno;
        }
        error = "Cannot open /: " + std::string(strerror(errno));
        return false;
    }

    size_t offset = 1;
    while (offset < normalized.size())
    {
        size_t end = normalized.find('/', offset);
        std::string component = normalized.substr(offset, end == std::string::npos ? end : end - offset);
        int next = openat(current, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0 && errno == ENOENT && create)
        {
            if (mkdirat(current, component.c_str(), mode) < 0 && errno != EEXIST)
            {
                if (errorCode)
                {
                    *errorCode = errno;
                }
                error = "Cannot create directory " + normalized + ": " + strerror(errno);
                close(current);
                return false;
            }
            next = openat(current, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        }
        if (next < 0)
        {
            if (errorCode)
            {
                *errorCode = errno;
            }
            error = "Cannot safely open directory " + normalized + ": " + strerror(errno);
            close(current);
            return false;
        }
        close(current);
        current = next;
        if (end == std::string::npos)
        {
            break;
        }
        offset = end + 1;
    }
    result = current;
    return true;
}

bool readFileAt(int dirfd, const std::string& name, std::string& contents, bool& exists, std::string& error)
{
    exists = false;
    int fd = openat(dirfd, name.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
    {
        if (errno == ENOENT)
        {
            return true;
        }
        error = "Cannot safely read " + name + ": " + strerror(errno);
        return false;
    }
    struct stat info;
    if (fstat(fd, &info) < 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid() ||
        (name == "system-integration.state" && (info.st_mode & 077) != 0))
    {
        error = "Refusing to use a non-regular or foreign-owned file: " + name;
        close(fd);
        return false;
    }
    contents.clear();
    char buffer[4096];
    ssize_t count;
    while ((count = read(fd, buffer, sizeof(buffer))) > 0)
    {
        if (contents.size() + (size_t)count > 1024 * 1024)
        {
            error = "Refusing an unexpectedly large file: " + name;
            close(fd);
            return false;
        }
        contents.append(buffer, (size_t)count);
    }
    if (count < 0)
    {
        error = "Cannot read " + name + ": " + strerror(errno);
        close(fd);
        return false;
    }
    close(fd);
    exists = true;
    return true;
}

std::string hexEncode(const std::string& value)
{
    static const char digits[] = "0123456789abcdef";
    std::string result;
    for (size_t i = 0; i < value.size(); ++i)
    {
        unsigned char byte = (unsigned char)value[i];
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

bool hexDecode(const std::string& value, std::string& result)
{
    if (value.size() % 2)
    {
        return false;
    }
    result.clear();
    for (size_t i = 0; i < value.size(); i += 2)
    {
        char pair[3] = { value[i], value[i + 1], 0 };
        char* end = NULL;
        long byte = strtol(pair, &end, 16);
        if (!end || *end || byte < 0 || byte > 255)
        {
            return false;
        }
        result += (char)byte;
    }
    return true;
}

std::string serializeState(const State& state)
{
    std::string text = stateHeader;
    const Item* items[] = { &state.desktop, &state.menu, &state.command };
    const char* names[] = { "desktop", "menu", "command" };
    for (int i = 0; i < 3; ++i)
    {
        text += names[i];
        text += "=";
        text += items[i]->owned ? "1," : "0,";
        text += hexEncode(items[i]->path);
        text += ",";
        text += hexEncode(items[i]->target);
        text += ",";
        text += hexEncode(items[i]->icon);
        text += "\n";
    }
    return text;
}

bool parseState(const std::string& contents, State& state)
{
    const bool legacy = contents.compare(0, strlen(legacyStateHeader), legacyStateHeader) == 0;
    const char* header = legacy ? legacyStateHeader : stateHeader;
    if (contents.compare(0, strlen(header), header) != 0)
    {
        return false;
    }
    const char* names[] = { "desktop=", "menu=", "command=" };
    Item* items[] = { &state.desktop, &state.menu, &state.command };
    size_t offset = strlen(header);
    for (int i = 0; i < 3; ++i)
    {
        size_t end = contents.find('\n', offset);
        if (end == std::string::npos)
        {
            return false;
        }
        std::string line = contents.substr(offset, end - offset);
        if (line.compare(0, strlen(names[i]), names[i]) != 0)
        {
            return false;
        }
        std::string fields = line.substr(strlen(names[i]));
        size_t first = fields.find(',');
        size_t second = fields.find(',', first == std::string::npos ? first : first + 1);
        size_t third = legacy ? std::string::npos :
                       fields.find(',', second == std::string::npos ? second : second + 1);
        if (first != 1 || second == std::string::npos ||
            (!legacy && third == std::string::npos) ||
            (fields[0] != '0' && fields[0] != '1'))
        {
            return false;
        }
        items[i]->owned = fields[0] == '1';
        if (!hexDecode(fields.substr(first + 1, second - first - 1), items[i]->path) ||
            !hexDecode(fields.substr(second + 1, (legacy ? std::string::npos : third) - second - 1),
                       items[i]->target) ||
            (!legacy && !hexDecode(fields.substr(third + 1), items[i]->icon)))
        {
            return false;
        }
        if (legacy && items[i]->owned && i < 2)
        {
            items[i]->icon = "swordfish";
        }
        if (!items[i]->owned &&
            (!items[i]->path.empty() || !items[i]->target.empty() || !items[i]->icon.empty()))
        {
            return false;
        }
        if (items[i]->owned &&
            (items[i]->path.find('\0') != std::string::npos ||
             items[i]->target.find('\0') != std::string::npos ||
             items[i]->icon.find('\0') != std::string::npos ||
             normalizeAbsolute(items[i]->path) != items[i]->path ||
             normalizeAbsolute(items[i]->target) != items[i]->target ||
             (!legacy && i < 2 && normalizeAbsolute(items[i]->icon) != items[i]->icon) ||
             baseName(items[i]->path) != (i == 0 ? "Swordfish.desktop" : i == 1 ? "swordfish.desktop" : "swordfish")))
        {
            return false;
        }
        offset = end + 1;
    }
    return offset == contents.size();
}

bool openStateDirectory(bool create, int& dirfd, std::string& statePath, std::string& error,
                        int* errorCode = NULL)
{
    if (errorCode)
    {
        *errorCode = 0;
    }
    std::string config = normalizeAbsolute(configHome());
    if (config.empty())
    {
        if (errorCode)
        {
            *errorCode = EINVAL;
        }
        error = "Cannot determine the host XDG configuration directory.";
        return false;
    }
    statePath = config + "/swordfish/system-integration.state";
    int directoryError = 0;
    if (!openDirectory(parentPath(statePath), create, 0700, dirfd, error, &directoryError))
    {
        if (errorCode)
        {
            *errorCode = directoryError;
        }
        return false;
    }
    struct stat info;
    if (fstat(dirfd, &info) < 0)
    {
        if (errorCode)
        {
            *errorCode = errno;
        }
        error = "Refusing to use an unsafe System Integration ownership directory.";
        close(dirfd);
        return false;
    }
    if (!S_ISDIR(info.st_mode) || info.st_uid != getuid() || (info.st_mode & 022) != 0)
    {
        if (errorCode)
        {
            *errorCode = EPERM;
        }
        error = "Refusing to use an unsafe System Integration ownership directory.";
        close(dirfd);
        return false;
    }
    return true;
}

bool loadState(State& state, bool& exists, std::string& error)
{
    int dirfd;
    std::string path;
    int directoryError = 0;
    if (!openStateDirectory(false, dirfd, path, error, &directoryError))
    {
        if (directoryError == ENOENT)
        {
            error.clear();
            exists = false;
            return true;
        }
        return false;
    }
    std::string contents;
    bool fileExists;
    bool success = readFileAt(dirfd, baseName(path), contents, fileExists, error);
    close(dirfd);
    if (!success)
    {
        return false;
    }
    exists = fileExists;
    if (fileExists && !parseState(contents, state))
    {
        error = "The System Integration ownership file is invalid; refusing to alter existing files.";
        return false;
    }
    return true;
}

bool saveState(const State& state, std::string& error)
{
    int dirfd;
    std::string path;
    if (!openStateDirectory(true, dirfd, path, error))
    {
        return false;
    }
    const std::string name = baseName(path);
    std::string current;
    bool exists;
    if (!readFileAt(dirfd, name, current, exists, error))
    {
        close(dirfd);
        return false;
    }
    if (exists && current.compare(0, strlen(stateHeader), stateHeader) != 0 &&
        current.compare(0, strlen(legacyStateHeader), legacyStateHeader) != 0)
    {
        error = "Refusing to overwrite an unrelated System Integration state file.";
        close(dirfd);
        return false;
    }
    int fd = openat(dirfd, name.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
    {
        error = "Cannot save System Integration ownership state: " + std::string(strerror(errno));
        close(dirfd);
        return false;
    }
    std::string contents = serializeState(state);
    size_t offset = 0;
    while (offset < contents.size())
    {
        ssize_t count = write(fd, contents.data() + offset, contents.size() - offset);
        if (count <= 0)
        {
            error = "Cannot write System Integration ownership state: " + std::string(strerror(errno));
            close(fd);
            close(dirfd);
            return false;
        }
        offset += (size_t)count;
    }
    bool success = fsync(fd) == 0;
    if (!success)
    {
        error = "Cannot flush System Integration ownership state: " + std::string(strerror(errno));
    }
    close(fd);
    close(dirfd);
    return success;
}

bool removeState(std::string& error)
{
    int dirfd;
    std::string path;
    int directoryError = 0;
    if (!openStateDirectory(false, dirfd, path, error, &directoryError))
    {
        if (directoryError == ENOENT)
        {
            error.clear();
            return true;
        }
        return false;
    }
    std::string contents;
    bool exists;
    if (!readFileAt(dirfd, baseName(path), contents, exists, error))
    {
        close(dirfd);
        return false;
    }
    if (exists && contents.compare(0, strlen(stateHeader), stateHeader) != 0 &&
        contents.compare(0, strlen(legacyStateHeader), legacyStateHeader) != 0)
    {
        error = "Refusing to remove an unrelated System Integration state file.";
        close(dirfd);
        return false;
    }
    bool success = !exists || unlinkat(dirfd, baseName(path).c_str(), 0) == 0;
    if (!success)
    {
        error = "Cannot remove System Integration ownership state: " + std::string(strerror(errno));
    }
    close(dirfd);
    return success;
}

std::string executableTarget(std::string& error)
{
    char executable[PATH_MAX + 1];
    ssize_t length = readlink("/proc/self/exe", executable, PATH_MAX);
    if (length <= 0 || length >= PATH_MAX)
    {
        error = "Cannot determine the running Swordfish executable from /proc/self/exe.";
        return "";
    }
    executable[length] = '\0';
    std::string target = normalizeAbsolute(executable);
    if (target.empty())
    {
        error = "The running Swordfish executable path is not absolute.";
        return "";
    }
    for (size_t i = 0; i < target.size(); ++i)
    {
        if ((unsigned char)target[i] < 32 || (unsigned char)target[i] == 127)
        {
            error = "Cannot create system integration for an executable path containing control characters.";
            return "";
        }
    }
    const char* portable = getenv("SWORDFISH_PORTABLE");
    if (portable && *portable && strcmp(portable, "0") != 0)
    {
        std::string root = parentPath(target);
        while (root != "/")
        {
            if (access((root + "/AppRun").c_str(), X_OK) == 0)
            {
                return root + "/AppRun";
            }
            root = parentPath(root);
        }
        error = "Cannot locate the portable folder's AppRun from /proc/self/exe.";
        return "";
    }
    return target;
}

bool existingIcon(const std::string& candidate, std::string& icon)
{
    struct stat info;
    if (stat(candidate.c_str(), &info) < 0 || !S_ISREG(info.st_mode) || access(candidate.c_str(), R_OK) < 0)
    {
        return false;
    }
    char resolved[PATH_MAX];
    if (!realpath(candidate.c_str(), resolved))
    {
        return false;
    }
    icon = resolved;
    for (size_t i = 0; i < icon.size(); ++i)
    {
        if ((unsigned char)icon[i] < 32 || (unsigned char)icon[i] == 127)
        {
            icon.clear();
            return false;
        }
    }
    return true;
}

std::string iconPathForTarget(const std::string& target, std::string& error)
{
    const char* portable = getenv("SWORDFISH_PORTABLE");
    const bool isPortable = portable && *portable && strcmp(portable, "0") != 0;
    std::vector<std::string> candidates;
    if (isPortable)
    {
        const std::string root = parentPath(target);
        candidates.push_back(root + "/usr/share/icons/hicolor/scalable/apps/swordfish.svg");
        candidates.push_back(root + "/swordfish.svg");
    }
    else
    {
        const std::string prefix = parentPath(parentPath(target));
        candidates.push_back(prefix + "/share/icons/hicolor/scalable/apps/swordfish.svg");
        candidates.push_back(prefix + "/share/pixmaps/swordfish.svg");
        candidates.push_back("/usr/local/share/icons/hicolor/scalable/apps/swordfish.svg");
        candidates.push_back("/usr/share/icons/hicolor/scalable/apps/swordfish.svg");
        candidates.push_back("/usr/local/share/pixmaps/swordfish.svg");
        candidates.push_back("/usr/share/pixmaps/swordfish.svg");
    }
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        std::string icon;
        if (existingIcon(candidates[i], icon))
        {
            return icon;
        }
    }
    error = "Cannot find an installed Swordfish SVG icon for the desktop launcher. Checked: ";
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (i)
        {
            error += ", ";
        }
        error += candidates[i];
    }
    return "";
}

std::string desktopDirectory(const std::string& home, const std::string& config, std::string& error)
{
    std::string configFile = config + "/user-dirs.dirs";
    int configfd;
    int configError = 0;
    if (!openDirectory(config, false, 0700, configfd, error, &configError))
    {
        if (configError == ENOENT)
        {
            error.clear();
            return normalizeAbsolute(home + "/Desktop");
        }
        return "";
    }
    std::string contents;
    bool exists;
    if (!readFileAt(configfd, "user-dirs.dirs", contents, exists, error))
    {
        close(configfd);
        return "";
    }
    close(configfd);
    if (exists)
    {
        std::istringstream input(contents);
        std::string line;
        while (std::getline(input, line))
        {
            size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos || line.compare(first, 16, "XDG_DESKTOP_DIR=") != 0)
            {
                continue;
            }
            std::string value = line.substr(first + 16);
            size_t quote = value.find('"');
            size_t endQuote = quote == std::string::npos ? quote : value.find('"', quote + 1);
            if (quote == std::string::npos || endQuote == std::string::npos)
            {
                error = "Cannot safely parse XDG_DESKTOP_DIR in " + configFile;
                return "";
            }
            std::string expanded;
            for (size_t i = quote + 1; i < endQuote; ++i)
            {
                if (value[i] == '\\')
                {
                    if (++i >= endQuote)
                    {
                        error = "Cannot safely parse XDG_DESKTOP_DIR in " + configFile;
                        return "";
                    }
                    expanded += value[i];
                }
                else if (value[i] == '$')
                {
                    if (value.compare(i, 5, "$HOME") == 0)
                    {
                        expanded += home;
                        i += 4;
                    }
                    else if (value.compare(i, 7, "${HOME}") == 0)
                    {
                        expanded += home;
                        i += 6;
                    }
                    else
                    {
                        error = "Unsupported variable in XDG_DESKTOP_DIR; refusing shell-style expansion.";
                        return "";
                    }
                }
                else
                {
                    expanded += value[i];
                }
            }
            std::string normalized = normalizeAbsolute(expanded);
            if (normalized.empty())
            {
                error = "XDG_DESKTOP_DIR must resolve to an absolute path.";
            }
            return normalized;
        }
    }
    return normalizeAbsolute(home + "/Desktop");
}

std::string execValue(const std::string& value)
{
    std::string escaped;
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '\\' || value[i] == '"')
        {
            escaped += '\\';
        }
        escaped += value[i] == '%' ? "%%" : std::string(1, value[i]);
    }
    return escaped;
}

std::string desktopValue(const std::string& value)
{
    std::string escaped;
    for (size_t i = 0; i < value.size(); ++i)
    {
        switch (value[i])
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\t':
            escaped += "\\t";
            break;
        case '\r':
            escaped += "\\r";
            break;
        default:
            escaped += value[i];
            break;
        }
    }
    return escaped;
}

bool isDesktopShortcut(const Item& item)
{
    return baseName(item.path) == "Swordfish.desktop";
}

std::string desktopContents(const std::string& target, const std::string& icon, bool shortcut)
{
    if (icon == "swordfish")
    {
        return "# Swordfish System Integration v1\n"
           "[Desktop Entry]\n"
           "Type=Application\n"
           "Name=Swordfish File Manager\n"
           "Comment=Browse files and folders\n"
           "Exec=\"" + execValue(target) + "\" %U\n"
           "Icon=swordfish\n"
           "Terminal=false\n"
           "Categories=Utility;FileManager;\n"
           "StartupNotify=true\n";
    }
    std::string contents = "# Swordfish System Integration v2\n";
    if (shortcut)
    {
        contents += "# On desktops that request it, right-click this shortcut and choose Allow Launching.\n";
    }
    return contents + "[Desktop Entry]\n"
           "Type=Application\n"
           "Name=Swordfish File Manager\n"
           "Comment=Browse files and folders\n"
           "Exec=\"" + execValue(target) + "\" %U\n"
           "Icon=" + desktopValue(icon) + "\n"
           "Terminal=false\n"
           "Categories=Utility;FileManager;\n"
           "StartupNotify=true\n";
}

// Desktops and older builds may reformat a launcher, so ownership is the Swordfish marker or the recorded Exec line
bool ownedDesktopContents(const Item& item, const std::string& contents)
{
    if (contents == desktopContents(item.target, item.icon, isDesktopShortcut(item)))
    {
        return true;
    }
    if (contents.find("[Desktop Entry]") == std::string::npos ||
        contents.find("Name=Swordfish File Manager") == std::string::npos)
    {
        return false;
    }
    return contents.compare(0, 31, "# Swordfish System Integration ") == 0 ||
           (!item.target.empty() && contents.find("Exec=\"" + execValue(item.target) + "\"") != std::string::npos);
}

bool readLinkAt(int dirfd, const std::string& name, std::string& target, bool& exists, std::string& error,
                bool allowRoot = false)
{
    struct stat info;
    if (fstatat(dirfd, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) < 0)
    {
        if (errno == ENOENT)
        {
            exists = false;
            return true;
        }
        error = "Cannot inspect " + name + ": " + strerror(errno);
        return false;
    }
    if (!S_ISLNK(info.st_mode))
    {
        error = "Refusing to replace or remove a non-symlink at " + name;
        return false;
    }
    if (info.st_uid != getuid() && !(allowRoot && info.st_uid == 0))
    {
        error = "Refusing to use a foreign-owned command symlink: " + name;
        return false;
    }
    char buffer[PATH_MAX + 1];
    ssize_t length = readlinkat(dirfd, name.c_str(), buffer, PATH_MAX);
    if (length < 0 || length >= PATH_MAX)
    {
        error = "Cannot safely inspect symlink " + name + ": " + strerror(errno);
        return false;
    }
    buffer[length] = '\0';
    target.assign(buffer, (size_t)length);
    exists = true;
    return true;
}

bool removeDesktop(Item& item, std::string& error);
bool removeLink(Item& item, std::string& error);

bool systemCommandDirectory(const std::string& directory)
{
    return directory == "/usr/local/bin" || directory == "/usr/bin" ||
           directory == "/usr/local/sbin" || directory == "/usr/sbin";
}

bool trustedHelper(const std::string& helper, bool userOwned)
{
    struct stat info;
    if (normalizeAbsolute(helper) != helper ||
        lstat(helper.c_str(), &info) < 0 || !S_ISREG(info.st_mode) ||
        (info.st_uid != 0 && (!userOwned || info.st_uid != getuid())) ||
        (info.st_mode & (022 | S_ISUID | S_ISGID)) || access(helper.c_str(), X_OK) < 0)
    {
        return false;
    }
    std::string parent = parentPath(helper);
    while (!parent.empty())
    {
        if (lstat(parent.c_str(), &info) < 0 || !S_ISDIR(info.st_mode) ||
            (info.st_uid != 0 && (!userOwned || info.st_uid != getuid())) || (info.st_mode & 022))
        {
            return false;
        }
        if (parent == "/")
        {
            break;
        }
        parent = parentPath(parent);
    }
    return true;
}

bool authorizeLink(const char* operation, const std::string& path, const std::string& target,
                   std::string& error)
{
    const std::string helper = commandLinkHelper(error);
    if (helper.empty())
    {
        return false;
    }
    int output[2];
    if (pipe2(output, O_CLOEXEC) < 0)
    {
        error = "Cannot capture system authorization errors: " + std::string(strerror(errno));
        return false;
    }
    pid_t child = fork();
    if (child == 0)
    {
        close(output[0]);
        if (dup2(output[1], STDERR_FILENO) < 0)
        {
            _exit(127);
        }
        close(output[1]);
        execl("/usr/bin/pkexec", "pkexec", helper.c_str(), operation, path.c_str(), target.c_str(),
              (char*)NULL);
        dprintf(STDERR_FILENO, "Cannot execute /usr/bin/pkexec: %s\n", strerror(errno));
        _exit(127);
    }
    close(output[1]);
    int status = 0;
    if (child < 0)
    {
        close(output[0]);
        error = "Cannot start system authorization: " + std::string(strerror(errno));
        return false;
    }
    std::string diagnostic;
    char buffer[1024];
    ssize_t count;
    while ((count = read(output[0], buffer, sizeof(buffer))) != 0)
    {
        if (count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        if (diagnostic.size() < 8192)
        {
            diagnostic.append(buffer, (size_t)count);
        }
    }
    close(output[0]);
    pid_t waited;
    do
    {
        waited = waitpid(child, &status, 0);
    }
    while (waited < 0 && errno == EINTR);
    if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        error = "System authorization was cancelled, denied, unavailable, or the managed command link "
                "could not be safely changed. No unrelated link will be replaced.";
        if (!diagnostic.empty())
        {
            error += "\n" + diagnostic;
        }
        return false;
    }
    return true;
}

bool administrativeLink(const Item& item)
{
    struct stat info;
    return item.owned && getuid() != 0 && lstat(item.path.c_str(), &info) == 0 &&
           S_ISLNK(info.st_mode) && info.st_uid == 0 &&
           systemCommandDirectory(parentPath(item.path));
}

bool readOwnedDesktop(const Item& item, std::string& contents, bool& exists, std::string& error)
{
    int dirfd;
    int directoryError = 0;
    if (!openDirectory(parentPath(item.path), false, 0700, dirfd, error, &directoryError))
    {
        if (directoryError == ENOENT)
        {
            error.clear();
            exists = false;
            return true;
        }
        return false;
    }
    bool success = readFileAt(dirfd, baseName(item.path), contents, exists, error);
    close(dirfd);
    return success;
}

bool desktopMatches(const Item& item, bool& exists, std::string& error)
{
    std::string contents;
    if (!readOwnedDesktop(item, contents, exists, error))
    {
        return false;
    }
    return !exists || ownedDesktopContents(item, contents);
}

bool linkMatches(const Item& item, bool& exists, std::string& error)
{
    int dirfd;
    int directoryError = 0;
    if (!openDirectory(parentPath(item.path), false, 0700, dirfd, error, &directoryError))
    {
        if (directoryError == ENOENT)
        {
            error.clear();
            exists = false;
            return true;
        }
        return false;
    }
    std::string target;
    bool isLink = false;
    bool success = readLinkAt(dirfd, baseName(item.path), target, isLink, error,
                              systemCommandDirectory(parentPath(item.path)));
    close(dirfd);
    exists = isLink;
    return success && (!isLink || target == item.target);
}

bool replaceDesktop(Item& item, const std::string& path, const std::string& target, const std::string& icon,
                    std::string& error)
{
    if (item.owned && item.path != path && !removeDesktop(item, error))
    {
        return false;
    }
    int dirfd;
    if (!openDirectory(parentPath(path), true, 0755, dirfd, error))
    {
        return false;
    }
    std::string name = baseName(path);
    std::string existing;
    bool exists;
    if (!readFileAt(dirfd, name, existing, exists, error))
    {
        close(dirfd);
        return false;
    }
    if (exists && (!item.owned || item.path != path || !ownedDesktopContents(item, existing)))
    {
        error = "Refusing to overwrite an unrelated desktop entry: " + path;
        close(dirfd);
        return false;
    }
    const bool shortcut = baseName(path) == "Swordfish.desktop";
    const mode_t mode = shortcut ? 0755 : 0644;
    std::string contents = desktopContents(target, icon, shortcut);
    if (!exists)
    {
        int fd = openat(dirfd, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
        if (fd < 0)
        {
            error = "Cannot create desktop entry " + path + ": " + strerror(errno);
            close(dirfd);
            return false;
        }
        size_t offset = 0;
        bool success = true;
        while (offset < contents.size())
        {
            ssize_t count = write(fd, contents.data() + offset, contents.size() - offset);
            if (count <= 0)
            {
                error = "Cannot write desktop entry " + path + ": " + strerror(errno);
                success = false;
                break;
            }
            offset += (size_t)count;
        }
        if (success && fchmod(fd, mode) < 0)
        {
            error = "Cannot set permissions on desktop entry " + path + ": " + strerror(errno);
            success = false;
        }
        if (close(fd) < 0 && success)
        {
            error = "Cannot close desktop entry " + path + ": " + strerror(errno);
            success = false;
        }
        if (!success)
        {
            unlinkat(dirfd, name.c_str(), 0);
            close(dirfd);
            return false;
        }
    }
    else
    {
        const bool contentChanged = existing != contents;
        // An unchanged launcher is opened read-only so desktops do not see a rewrite
        int flags = contentChanged ? (O_WRONLY | O_TRUNC | O_NOFOLLOW | O_CLOEXEC) : (O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        int fd = openat(dirfd, name.c_str(), flags);
        if (fd < 0)
        {
            error = "Cannot update desktop entry " + path + ": " + strerror(errno);
            close(dirfd);
            return false;
        }
        size_t offset = 0;
        bool success = true;
        while (contentChanged && offset < contents.size())
        {
            ssize_t count = write(fd, contents.data() + offset, contents.size() - offset);
            if (count <= 0)
            {
                error = "Cannot update desktop entry " + path + ": " + strerror(errno);
                success = false;
                break;
            }
            offset += (size_t)count;
        }
        struct stat info;
        if (success && fstat(fd, &info) < 0)
        {
            error = "Cannot inspect desktop entry " + path + ": " + strerror(errno);
            success = false;
        }
        if (success && (info.st_mode & 07777) != mode && fchmod(fd, mode) < 0)
        {
            error = "Cannot set permissions on desktop entry " + path + ": " + strerror(errno);
            success = false;
        }
        if (close(fd) < 0 && success)
        {
            error = "Cannot close desktop entry " + path + ": " + strerror(errno);
            success = false;
        }
        if (!success)
        {
            close(dirfd);
            return false;
        }
    }
    item.owned = true;
    item.path = path;
    item.target = target;
    item.icon = icon;
    close(dirfd);
    return true;
}

bool replaceLink(Item& item, const std::string& path, const std::string& target, std::string& error)
{
    if (item.owned && item.path != path)
    {
        struct stat info;
        if (lstat(path.c_str(), &info) == 0 || errno != ENOENT)
        {
            error = "Refusing to overwrite an unrelated command destination: " + path;
            return false;
        }
    }
    if (item.owned &&
        (item.path != path || (administrativeLink(item) && item.target != target)) &&
        !removeLink(item, error))
    {
        return false;
    }
    if (administrativeLink(item) ||
        (systemCommandDirectory(parentPath(path)) &&
         access(parentPath(path).c_str(), W_OK | X_OK) < 0))
    {
        if (!authorizeLink("create", path, target, error))
        {
            return false;
        }
        item.owned = true;
        item.path = path;
        item.target = target;
        return true;
    }
    int dirfd;
    if (!openDirectory(parentPath(path), true, 0755, dirfd, error))
    {
        return false;
    }
    std::string existingTarget;
    bool exists;
    if (!readLinkAt(dirfd, baseName(path), existingTarget, exists, error))
    {
        close(dirfd);
        return false;
    }
    if (exists && (!item.owned || existingTarget != item.target))
    {
        error = "Refusing to overwrite an unrelated item at " + path;
        close(dirfd);
        return false;
    }
    if (exists && existingTarget != target)
    {
        if (unlinkat(dirfd, baseName(path).c_str(), 0) < 0)
        {
            error = "Cannot update command symlink " + path + ": " + strerror(errno);
            close(dirfd);
            return false;
        }
        exists = false;
    }
    if (!exists && symlinkat(target.c_str(), dirfd, baseName(path).c_str()) < 0)
    {
        error = "Cannot create command symlink " + path + ": " + strerror(errno);
        close(dirfd);
        return false;
    }
    item.owned = true;
    item.path = path;
    item.target = target;
    close(dirfd);
    return true;
}

bool removeDesktop(Item& item, std::string& error)
{
    if (!item.owned)
    {
        return true;
    }
    int dirfd;
    int directoryError = 0;
    if (!openDirectory(parentPath(item.path), false, 0700, dirfd, error, &directoryError))
    {
        if (directoryError == ENOENT)
        {
            error.clear();
            item = Item();
            return true;
        }
        return false;
    }
    std::string contents;
    bool exists;
    if (!readFileAt(dirfd, baseName(item.path), contents, exists, error))
    {
        close(dirfd);
        return false;
    }
    if (!exists)
    {
        item = Item();
        close(dirfd);
        return true;
    }
    if (!ownedDesktopContents(item, contents))
    {
        error = "Refusing to remove a changed or unrelated desktop entry: " + item.path;
        close(dirfd);
        return false;
    }
    bool success = unlinkat(dirfd, baseName(item.path).c_str(), 0) == 0;
    if (!success)
    {
        error = "Cannot remove desktop entry " + item.path + ": " + strerror(errno);
    }
    else
    {
        item = Item();
    }
    close(dirfd);
    return success;
}

bool removeLink(Item& item, std::string& error)
{
    if (!item.owned)
    {
        return true;
    }
    if (administrativeLink(item) ||
        (systemCommandDirectory(parentPath(item.path)) &&
         access(parentPath(item.path).c_str(), W_OK | X_OK) < 0))
    {
        if (!authorizeLink("remove", item.path, item.target, error))
        {
            return false;
        }
        item = Item();
        return true;
    }
    int dirfd;
    int directoryError = 0;
    if (!openDirectory(parentPath(item.path), false, 0700, dirfd, error, &directoryError))
    {
        if (directoryError == ENOENT)
        {
            error.clear();
            item = Item();
            return true;
        }
        return false;
    }
    std::string target;
    bool exists;
    if (!readLinkAt(dirfd, baseName(item.path), target, exists, error))
    {
        close(dirfd);
        return false;
    }
    if (!exists)
    {
        item = Item();
        close(dirfd);
        return true;
    }
    if (target != item.target)
    {
        error = "Refusing to remove a changed or unrelated symlink: " + item.path;
        close(dirfd);
        return false;
    }
    bool success = unlinkat(dirfd, baseName(item.path).c_str(), 0) == 0;
    if (!success)
    {
        error = "Cannot remove command symlink " + item.path + ": " + strerror(errno);
    }
    else
    {
        item = Item();
    }
    close(dirfd);
    return success;
}

void appendError(std::string& all, const std::string& error)
{
    if (!all.empty())
    {
        all += "\n";
    }
    all += error;
}

bool inSearchPath(const std::string& directory)
{
    std::string path = hostSearchPath();
    size_t offset = 0;
    while (offset <= path.size())
    {
        size_t end = path.find(':', offset);
        std::string component = path.substr(offset, end == std::string::npos ? end : end - offset);
        if (component.empty())
        {
            component = ".";
        }
        if (component[0] != '/')
        {
            char cwd[PATH_MAX];
            if (!getcwd(cwd, sizeof(cwd)))
            {
                component.clear();
            }
            else
            {
                component = std::string(cwd) + "/" + component;
            }
        }
        char resolved[PATH_MAX];
        if (normalizeAbsolute(component) == directory ||
            (realpath(component.c_str(), resolved) && directory == resolved))
        {
            return true;
        }
        if (end == std::string::npos)
        {
            break;
        }
        offset = end + 1;
    }
    return false;
}

bool pathExecutableConflict(const std::string& target, const std::string& commandPath,
                            const Item& ownedCommand, std::string& conflict)
{
    char targetReal[PATH_MAX];
    if (!realpath(target.c_str(), targetReal))
    {
        conflict = "Cannot resolve the expected Swordfish target: " + target;
        return true;
    }
    const std::string expected(targetReal);
    std::string path = hostSearchPath();
    size_t offset = 0;
    while (offset <= path.size())
    {
        size_t end = path.find(':', offset);
        std::string directory = path.substr(offset, end == std::string::npos ? end : end - offset);
        if (directory.empty())
        {
            directory = ".";
        }
        if (directory[0] != '/')
        {
            char cwd[PATH_MAX];
            if (!getcwd(cwd, sizeof(cwd)))
            {
                directory.clear();
            }
            else
            {
                directory = std::string(cwd) + "/" + directory;
            }
        }
        directory = normalizeAbsolute(directory);
        if (!directory.empty())
        {
            const std::string candidate = directory + "/swordfish";
            struct stat info;
            if (stat(candidate.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
                access(candidate.c_str(), X_OK) == 0)
            {
                if (ownedCommand.owned && candidate == commandPath)
                {
                    std::string currentTarget;
                    bool currentExists = false;
                    std::string ignored;
                    int dirfd;
                    if (openDirectory(parentPath(candidate), false, 0700, dirfd, ignored))
                    {
                        if (readLinkAt(dirfd, baseName(candidate), currentTarget, currentExists, ignored,
                                       systemCommandDirectory(parentPath(candidate))) &&
                            currentExists && currentTarget == ownedCommand.target)
                        {
                            close(dirfd);
                            if (end == std::string::npos)
                            {
                                break;
                            }
                            offset = end + 1;
                            continue;
                        }
                        close(dirfd);
                    }
                }
                char candidateReal[PATH_MAX];
                if (!realpath(candidate.c_str(), candidateReal) ||
                    expected != std::string(candidateReal))
                {
                    conflict = "Another executable named swordfish is already available in the host PATH: " +
                               candidate;
                    return true;
                }
            }
        }
        if (end == std::string::npos)
        {
            break;
        }
        offset = end + 1;
    }
    return false;
}

}

std::string commandLinkHelper(std::string& error)
{
    error.clear();
    char executable[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable));
    std::string bundled;
    if (length > 0 && (size_t)length < sizeof(executable))
    {
        bundled = parentPath(parentPath(std::string(executable, (size_t)length))) +
                  "/libexec/swordfish-command-link";
        if (trustedHelper(bundled, true))
        {
            return bundled;
        }
    }
    const std::string installed = SWORDFISH_COMMAND_LINK_HELPER;
    if (trustedHelper(installed, false))
    {
        return installed;
    }
    error = "Cannot safely use the bundled command-link helper" +
            (bundled.empty() ? std::string("") : " at " + bundled) +
            " or the installed helper at " + installed +
            ". The executable and every parent directory must belong to you or root "
            "(installed fallback: root only), must not be symlinks, and must not be writable "
            "by other users. The helper must be executable and not setuid/setgid. "
            "Choose a writable PATH location, or repair/extract the bundle into a private directory.";
    return "";
}

CommandDestination commandDestinationInfo(const std::string& path)
{
    CommandDestination result;
    result.path = path;
    const std::string home = homeDirectory();
    const std::string directory = parentPath(path);
    result.shared = home.empty() || directory.compare(0, home.size() + 1, home + "/") != 0;
    struct stat info;
    result.requiresAuthorization = access(directory.c_str(), W_OK | X_OK) < 0 ||
                                   (getuid() != 0 && lstat(path.c_str(), &info) == 0 &&
                                    S_ISLNK(info.st_mode) && info.st_uid == 0 &&
                                    systemCommandDirectory(directory));
    return result;
}

std::vector<CommandDestination> commandDestinations()
{
    std::vector<CommandDestination> result;
    const std::string home = homeDirectory();
    const std::string local = home.empty() ? "" : home + "/.local/bin";
    const std::string path = hostSearchPath();
    size_t offset = 0;
    while (offset <= path.size())
    {
        size_t end = path.find(':', offset);
        std::string directory = path.substr(offset, end == std::string::npos ? end : end - offset);
        if (directory.empty())
        {
            directory = ".";
        }
        char resolved[PATH_MAX];
        if (realpath(directory.c_str(), resolved))
        {
            directory = directory == local && directory != resolved ? "" : resolved;
        }
        else if (directory != local)
        {
            directory.clear();
        }
        int dirfd;
        std::string ignored;
        bool usable = !directory.empty() && openDirectory(directory, false, 0755, dirfd, ignored);
        bool writable = false;
        if (usable)
        {
            writable = faccessat(dirfd, ".", W_OK | X_OK, 0) == 0;
            close(dirfd);
        }
        else if (!local.empty() && directory == local)
        {
            // Do not create directories just by opening Preferences.
            std::string parent = parentPath(local);
            while (access(parent.c_str(), F_OK) < 0 && parent != "/")
            {
                parent = parentPath(parent);
            }
            if (openDirectory(parent, false, 0755, dirfd, ignored))
            {
                writable = faccessat(dirfd, ".", W_OK | X_OK, 0) == 0;
                close(dirfd);
                struct stat info;
                usable = writable && lstat(local.c_str(), &info) < 0 && errno == ENOENT;
            }
        }
        if (usable && (writable || systemCommandDirectory(directory)))
        {
            CommandDestination destination = commandDestinationInfo(directory + "/swordfish");
            // A missing ~/.local/bin can be created without administrator authorization.
            if (directory == local && writable)
            {
                destination.requiresAuthorization = false;
            }
            bool duplicate = false;
            for (size_t i = 0; i < result.size(); ++i)
            {
                duplicate = duplicate || result[i].path == destination.path;
            }
            if (!duplicate)
            {
                result.push_back(destination);
            }
        }
        if (end == std::string::npos)
        {
            break;
        }
        offset = end + 1;
    }
    std::stable_sort(result.begin(), result.end(),
                    [&local](const CommandDestination& a, const CommandDestination& b)
                    {
                        if (a.requiresAuthorization != b.requiresAuthorization)
                        {
                            return !a.requiresAuthorization;
                        }
                        return a.path == local + "/swordfish" && b.path != a.path;
                    });
    return result;
}

bool readSelection(Selection& selection, std::string& error)
{
    selection = Selection();
    error.clear();
    State state;
    bool exists;
    if (!loadState(state, exists, error))
    {
        return false;
    }
    if (!exists)
    {
        return true;
    }
    selection.desktopShortcut = state.desktop.owned;
    selection.applicationsMenu = state.menu.owned;
    selection.commandSymlink = state.command.owned;
    selection.commandPath = state.command.path;
    Item* items[] = { &state.desktop, &state.menu, &state.command };
    bool* selected[] = { &selection.desktopShortcut, &selection.applicationsMenu, &selection.commandSymlink };
    for (int i = 0; i < 3; ++i)
    {
        if (!items[i]->owned)
        {
            continue;
        }
        bool itemExists = false;
        std::string verifyError;
        bool matches = i == 2 ? linkMatches(*items[i], itemExists, verifyError) :
                       desktopMatches(*items[i], itemExists, verifyError);
        if (!verifyError.empty())
        {
            error = verifyError;
            return false;
        }
        if (itemExists && !matches)
        {
            error = "A managed System Integration item has changed since Swordfish created it: " + items[i]->path;
            return false;
        }
        *selected[i] = matches && itemExists;
    }
    return true;
}

bool applySelection(const Selection& selection, std::string& error, std::string& warning)
{
    error.clear();
    warning.clear();
    State state;
    bool exists;
    if (!loadState(state, exists, error))
    {
        return false;
    }
    std::string home = homeDirectory();
    std::string target;
    std::string targetError;
    if (selection.desktopShortcut || selection.applicationsMenu || selection.commandSymlink)
    {
        target = executableTarget(targetError);
    }
    std::string icon;
    std::string iconError;
    if (selection.desktopShortcut || selection.applicationsMenu)
    {
        if (target.empty())
        {
            iconError = targetError;
        }
        else
        {
            icon = iconPathForTarget(target, iconError);
        }
    }

    std::string desktopPath;
    std::string desktopPathError;
    if (selection.desktopShortcut)
    {
        std::string config = normalizeAbsolute(configHome());
        std::string desktop;
        if (home.empty() || config.empty())
        {
            desktopPathError = "Cannot determine the host HOME or XDG config home for the desktop shortcut.";
        }
        else
        {
            desktop = desktopDirectory(home, config, desktopPathError);
            if (!desktop.empty())
            {
                desktopPath = desktop + "/Swordfish.desktop";
            }
        }
    }

    bool success = true;
    std::string operationError;

    if (selection.desktopShortcut)
    {
        if (!desktopPathError.empty() || target.empty() || icon.empty())
        {
            appendError(error, !desktopPathError.empty() ? desktopPathError :
                        (!target.empty() && !iconError.empty() ? iconError : targetError));
            success = false;
        }
        else if (!replaceDesktop(state.desktop, desktopPath, target, icon, operationError))
        {
            appendError(error, operationError);
            success = false;
        }
    }
    else if (!removeDesktop(state.desktop, operationError))
    {
        appendError(error, operationError);
        success = false;
    }

    operationError.clear();
    if (selection.applicationsMenu)
    {
        std::string data = normalizeAbsolute(dataHome());
        std::string menuPath = data.empty() ? "" : data + "/applications/swordfish.desktop";
        if (data.empty() || target.empty() || icon.empty())
        {
            appendError(error, target.empty() ? targetError :
                        (!icon.empty() ? "Cannot determine the host XDG data home for the applications menu." :
                         iconError));
            success = false;
        }
        else if (!replaceDesktop(state.menu, menuPath, target, icon, operationError))
        {
            appendError(error, operationError);
            success = false;
        }
    }
    else if (!removeDesktop(state.menu, operationError))
    {
        appendError(error, operationError);
        success = false;
    }

    operationError.clear();
    if (selection.commandSymlink)
    {
        const std::vector<CommandDestination> destinations = commandDestinations();
        std::string commandPath = selection.commandPath;
        if (commandPath.empty())
        {
            commandPath = state.command.owned ? state.command.path :
                          destinations.empty() ? "" : destinations[0].path;
        }
        bool available = false;
        for (size_t i = 0; i < destinations.size(); ++i)
        {
            available = available || destinations[i].path == commandPath;
        }
        if (!available || !inSearchPath(parentPath(commandPath)) || target.empty())
        {
            appendError(error, target.empty() ? targetError :
                        "Select a usable command destination already in the host PATH. PATH is never modified.");
            success = false;
        }
        else if (pathExecutableConflict(target, commandPath, state.command, operationError))
        {
            appendError(error, operationError);
            success = false;
        }
        else if (!replaceLink(state.command, commandPath, target, operationError))
        {
            appendError(error, operationError);
            success = false;
        }
    }
    else if (!removeLink(state.command, operationError))
    {
        appendError(error, operationError);
        success = false;
    }

    std::string stateError;
    if (state.desktop.owned || state.menu.owned || state.command.owned)
    {
        if (!saveState(state, stateError))
        {
            appendError(error, stateError);
            success = false;
        }
    }
    else if (!removeState(stateError))
    {
        appendError(error, stateError);
        success = false;
    }

    const char* portable = getenv("SWORDFISH_PORTABLE");
    if (portable && *portable && strcmp(portable, "0") != 0 &&
        (selection.desktopShortcut || selection.applicationsMenu || selection.commandSymlink))
    {
        warning = "Moving the portable Swordfish folder will break these links and launchers; recreate them from the new location.";
    }
    return success;
}

}
