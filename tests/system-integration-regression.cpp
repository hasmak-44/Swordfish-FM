#include "../src/SystemIntegration.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{

bool ensureDirectory(const std::string& path)
{
    if (mkdir(path.c_str(), 0700) == 0 || errno == EEXIST)
    {
        struct stat info;
        return lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
    }
    return false;
}

bool exists(const std::string& path)
{
    struct stat info;
    return lstat(path.c_str(), &info) == 0;
}

bool executableMode(const std::string& path, bool shouldBeExecutable)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0 &&
           (((info.st_mode & 0111) != 0) == shouldBeExecutable);
}

std::string readLink(const std::string& path)
{
    char buffer[4096];
    ssize_t length = readlink(path.c_str(), buffer, sizeof(buffer));
    return length < 0 ? "" : std::string(buffer, (size_t)length);
}

bool writeFile(const std::string& path, const std::string& text)
{
    std::ofstream file(path.c_str());
    file << text;
    return file.good();
}

std::string readFile(const std::string& path)
{
    std::ifstream file(path.c_str());
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool ensurePath(const std::string& path)
{
    std::string current;
    for (size_t i = 0; i < path.size(); ++i)
    {
        current += path[i];
        if (path[i] == '/' && current.size() > 1)
        {
            if (!ensureDirectory(current.substr(0, current.size() - 1)))
            {
                return false;
            }
        }
    }
    return ensureDirectory(path);
}

bool check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "System integration regression failed: " << message << std::endl;
    }
    return condition;
}

}

int main()
{
    const char* homeValue = getenv("HOME");
    if (!homeValue || !*homeValue)
    {
        return 2;
    }
    const std::string home(homeValue);
    const std::string config = home + "/.config";
    const std::string data = home + "/.local/share";
    const std::string desktop = home + "/Desktop";
    if (!check(ensureDirectory(home), "test home must exist") ||
        !check(ensureDirectory(config), "test config directory must exist") ||
        !check(ensureDirectory(home + "/.local"), "test local data directory must exist") ||
        !check(ensureDirectory(data), "test data directory must exist") ||
        !check(ensureDirectory(desktop), "test desktop must exist") ||
        !check(writeFile(config + "/user-dirs.dirs", "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"),
               "host user directories config must be writable"))
    {
        return 2;
    }

    char executable[4096];
    ssize_t executableLength = readlink("/proc/self/exe", executable, sizeof(executable));
    if (!check(executableLength > 0, "test executable should be discoverable via /proc/self/exe"))
    {
        return 1;
    }
    std::string executablePath(executable, (size_t)executableLength);
    std::string appRoot = executablePath.substr(0, executablePath.find_last_of('/'));
    const std::string portableRoot = appRoot.substr(0, appRoot.find_last_of('/'));
    const std::string bundleRoot = portableRoot.substr(0, portableRoot.find_last_of('/'));
    const std::string helperDirectory = portableRoot + "/libexec";
    const std::string helper = helperDirectory + "/swordfish-command-link";
    std::string helperError;
    if (!check(ensureDirectory(helperDirectory) &&
               writeFile(helper, "#!/bin/sh\nexit 1\n") && chmod(helper.c_str(), 0700) == 0,
               "bundled helper discovery fixture should exist"))
    {
        return 1;
    }
    // Shared development workspaces may themselves be writable by other users.
    bool trustedParents = true;
    std::string parent = helperDirectory;
    while (true)
    {
        struct stat info;
        if (lstat(parent.c_str(), &info) < 0 || !S_ISDIR(info.st_mode) ||
            (info.st_uid != 0 && info.st_uid != getuid()) || (info.st_mode & 022))
        {
            trustedParents = false;
        }
        if (parent == "/")
        {
            break;
        }
        parent = parent.substr(0, parent.find_last_of('/'));
        if (parent.empty())
        {
            parent = "/";
        }
    }
    std::string discoveredHelper = SystemIntegration::commandLinkHelper(helperError);
    if (!check(trustedParents ? discoveredHelper == helper :
                               discoveredHelper.empty() && !helperError.empty(),
               "bundled helper requires an executable owned by caller/root and a safe complete directory chain") ||
        !check(chmod(helper.c_str(), 0722) == 0 &&
               SystemIntegration::commandLinkHelper(helperError).empty() && !helperError.empty(),
               "group/other writable bundled helpers must be rejected") ||
        !check(chmod(helper.c_str(), 0600) == 0 &&
               SystemIntegration::commandLinkHelper(helperError).empty(),
               "non-executable bundled helpers must be rejected") ||
        !check(unlink(helper.c_str()) == 0 && symlink(executablePath.c_str(), helper.c_str()) == 0 &&
               SystemIntegration::commandLinkHelper(helperError).empty(),
               "symlinked helper executables must be rejected") ||
        !check(unlink(helper.c_str()) == 0 &&
               writeFile(helper, "#!/bin/sh\nexit 1\n") && chmod(helper.c_str(), 0700) == 0 &&
               chmod(helperDirectory.c_str(), 0777) == 0 &&
               SystemIntegration::commandLinkHelper(helperError).empty(),
               "writable helper directory chains must be rejected") ||
        !check(chmod(helperDirectory.c_str(), 0700) == 0 && unlink(helper.c_str()) == 0,
               "helper fixtures must be removed before protected destination tests"))
    {
        return 1;
    }
    if (!check(writeFile(bundleRoot + "/AppRun", "#!/bin/sh\nexit 0\n"), "portable AppRun fixture should be created") ||
        !check(chmod((bundleRoot + "/AppRun").c_str(), 0700) == 0, "portable AppRun fixture should be executable") ||
        !check(ensurePath(portableRoot + "/share/icons/hicolor/scalable/apps"),
               "portable icon directory should be created") ||
        !check(writeFile(portableRoot + "/share/icons/hicolor/scalable/apps/swordfish.svg",
                         "<svg xmlns=\"http://www.w3.org/2000/svg\"/>\n"),
               "portable icon fixture should be created"))
    {
        return 1;
    }

    SystemIntegration::Selection selection;
    std::string error;
    std::string warning;
    if (!check(SystemIntegration::readSelection(selection, error) && !selection.desktopShortcut &&
               !selection.applicationsMenu && !selection.commandSymlink,
               "all options start unchecked"))
    {
        return 1;
    }

    selection.desktopShortcut = true;
    selection.applicationsMenu = true;
    selection.commandSymlink = true;
    selection.commandPath = home + "/.local/bin/swordfish";
    if (!check(SystemIntegration::applySelection(selection, error, warning), "all integrations should apply") ||
        !check(exists(desktop + "/Swordfish.desktop"), "desktop shortcut should be created") ||
        !check(exists(data + "/applications/swordfish.desktop"), "applications launcher should be created") ||
        !check(readFile(desktop + "/Swordfish.desktop").find(
                   "Icon=" + portableRoot + "/share/icons/hicolor/scalable/apps/swordfish.svg\n") != std::string::npos,
               "desktop shortcut should use an absolute installed SVG icon path") ||
        !check(readFile(desktop + "/Swordfish.desktop").find(
                   "Exec=\"" + executablePath.substr(0, executablePath.find('%')) + "%%" +
                   executablePath.substr(executablePath.find('%') + 1) + "\" %U\n") != std::string::npos,
               "Exec must use desktop Exec escaping for percent characters") ||
        !check(readFile(desktop + "/Swordfish.desktop").find("Allow Launching") != std::string::npos,
               "desktop entry should include an Allow Launching instruction") ||
        !check(executableMode(desktop + "/Swordfish.desktop", true),
               "desktop shortcut should be executable") ||
        !check(executableMode(data + "/applications/swordfish.desktop", false),
               "applications menu launcher should remain non-executable"))
    {
        std::cerr << error << std::endl;
        return 1;
    }
    if (!check(readLink(home + "/.local/bin/swordfish") == executablePath,
               "command link should target the running executable") ||
        !check(SystemIntegration::readSelection(selection, error) && selection.desktopShortcut &&
               selection.applicationsMenu && selection.commandSymlink,
               "owned integrations should be reported as selected"))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    const std::string desktopPath = desktop + "/Swordfish.desktop";
    if (!check(chmod(desktopPath.c_str(), 0644) == 0, "desktop launcher permissions should be alterable") ||
        !check(setenv("SWORDFISH_HOST_PATH",
                      (home + "/.local/bin:/usr/bin:/bin").c_str(), 1) == 0,
               "host PATH should include the owned command link") ||
        !check(SystemIntegration::applySelection(selection, error, warning),
               "owned command link in PATH must not be treated as a conflicting installation") ||
        !check(executableMode(desktopPath, true),
               "reapplying should restore the desktop shortcut executable bit"))
    {
        std::cerr << error << std::endl;
        return 1;
    }
    if (!check(setenv("SWORDFISH_HOST_PATH", (home + "/.local/bin").c_str(), 1) == 0,
               "host PATH should be restored"))
    {
        return 1;
    }

    struct stat desktopBefore, menuBefore, desktopAfter, menuAfter;
    const std::string menuPath = data + "/applications/swordfish.desktop";
    if (!check(lstat(desktopPath.c_str(), &desktopBefore) == 0 &&
               lstat(menuPath.c_str(), &menuBefore) == 0, "launcher state should be readable"))
    {
        return 1;
    }
    usleep(20000);
    if (!check(SystemIntegration::applySelection(selection, error, warning), "reapplying integrations is safe") ||
        !check(SystemIntegration::readSelection(selection, error) && selection.desktopShortcut &&
               selection.applicationsMenu && selection.commandSymlink,
               "reapplying should preserve ownership") ||
        !check(lstat(desktopPath.c_str(), &desktopAfter) == 0 &&
               lstat(menuPath.c_str(), &menuAfter) == 0, "reapplied launcher state should be readable") ||
        !check(desktopBefore.st_ino == desktopAfter.st_ino && menuBefore.st_ino == menuAfter.st_ino &&
               desktopBefore.st_mtim.tv_sec == desktopAfter.st_mtim.tv_sec &&
               desktopBefore.st_mtim.tv_nsec == desktopAfter.st_mtim.tv_nsec &&
               desktopBefore.st_ctim.tv_sec == desktopAfter.st_ctim.tv_sec &&
               desktopBefore.st_ctim.tv_nsec == desktopAfter.st_ctim.tv_nsec &&
               menuBefore.st_mtim.tv_sec == menuAfter.st_mtim.tv_sec &&
               menuBefore.st_mtim.tv_nsec == menuAfter.st_mtim.tv_nsec &&
               menuBefore.st_ctim.tv_sec == menuAfter.st_ctim.tv_sec &&
               menuBefore.st_ctim.tv_nsec == menuAfter.st_ctim.tv_nsec,
               "unchanged launchers must not be rewritten, replaced or chmodded"))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    const std::string originalDesktop = readFile(desktopPath);
    if (!check(writeFile(desktopPath, originalDesktop + "# user modification\n"),
               "modified desktop entry fixture should be writable") ||
        !check(!SystemIntegration::readSelection(selection, error),
               "modified owned desktop entry must report a verification error") ||
        !check(selection.desktopShortcut && selection.applicationsMenu && selection.commandSymlink,
               "a verification error must retain the recorded integration choices"))
    {
        return 1;
    }
    selection.desktopShortcut = true;
    selection.applicationsMenu = true;
    selection.commandSymlink = true;
    selection.commandPath = home + "/.local/bin/swordfish";
    if (!check(!SystemIntegration::applySelection(selection, error, warning),
               "modified owned desktop entry must not be overwritten") ||
        !check(readFile(desktopPath) == originalDesktop + "# user modification\n",
               "modified owned desktop entry must be preserved"))
    {
        return 1;
    }
    if (!check(!SystemIntegration::applySelection(SystemIntegration::Selection(), error, warning),
               "modified owned desktop entry must not be deleted on deselect") ||
        !check(readFile(desktopPath) == originalDesktop + "# user modification\n",
               "modified owned desktop entry must survive deselection"))
    {
        return 1;
    }
    if (!check(writeFile(desktopPath, originalDesktop), "owned desktop entry should be restorable"))
    {
        return 1;
    }
    selection.desktopShortcut = true;
    selection.applicationsMenu = true;
    selection.commandSymlink = true;
    selection.commandPath = home + "/.local/bin/swordfish";
    if (!check(SystemIntegration::applySelection(selection, error, warning),
               "restoring the owned desktop entry should allow reapplying options"))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    const std::string commandLink = home + "/.local/bin/swordfish";
    if (!check(unlink(commandLink.c_str()) == 0, "owned command symlink should be replaceable for the test") ||
        !check(symlink("/usr/bin/true", commandLink.c_str()) == 0, "changed symlink fixture should be created"))
    {
        return 1;
    }
    selection = SystemIntegration::Selection();
    if (!check(!SystemIntegration::applySelection(selection, error, warning),
               "changed owned items must not be removed") ||
        !check(readLink(commandLink) == "/usr/bin/true",
               "changed command symlink must be preserved") ||
        !check(!exists(desktop + "/Swordfish.desktop") &&
               !exists(data + "/applications/swordfish.desktop"),
               "independently owned desktop entries should still be removed"))
    {
        return 1;
    }
    if (!check(unlink(commandLink.c_str()) == 0, "changed symlink fixture should be removable") ||
        !check(symlink(executablePath.c_str(), commandLink.c_str()) == 0,
               "owned symlink target should be restored") ||
        !check(SystemIntegration::applySelection(selection, error, warning),
               "restored owned command symlink should be removable") ||
        !check(!exists(commandLink), "deselecting removes the restored owned symlink"))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    if (!check(setenv("SWORDFISH_PORTABLE", "1", 1) == 0, "portable mode should be enabled"))
    {
        return 1;
    }
    selection = SystemIntegration::Selection();
    selection.commandSymlink = true;
    selection.commandPath = home + "/.local/bin/swordfish";
    if (!check(SystemIntegration::applySelection(selection, error, warning), "portable command symlink should apply") ||
        !check(readLink(home + "/.local/bin/swordfish") == bundleRoot + "/AppRun",
               "portable command symlink must target AppRun") ||
        !check(warning.find("Moving the portable") != std::string::npos,
               "portable link creation must warn that moving the folder breaks it"))
    {
        std::cerr << error << std::endl;
        return 1;
    }
    selection = SystemIntegration::Selection();
    if (!check(SystemIntegration::applySelection(selection, error, warning),
               "portable command symlink should be removable"))
    {
        std::cerr << error << std::endl;
        return 1;
    }
    unsetenv("SWORDFISH_PORTABLE");

    const std::string unrelated = desktop + "/Swordfish.desktop";
    if (!check(writeFile(unrelated, "unrelated user desktop entry\n"), "unrelated file should be writable"))
    {
        return 1;
    }
    selection.desktopShortcut = true;
    if (!check(!SystemIntegration::applySelection(selection, error, warning),
               "unrelated desktop entries must not be overwritten") ||
        !check(exists(unrelated), "unrelated desktop entry must remain"))
    {
        return 1;
    }
    unlink(unrelated.c_str());

    const std::string linkDirectory = home + "/.local/bin";
    const std::string outsideDirectory = home + "/outside";
    if (!check(ensureDirectory(home + "/.local"), "local data parent should exist") ||
        !check(ensureDirectory(outsideDirectory), "outside directory should exist") ||
        !check(rmdir(linkDirectory.c_str()) == 0, "empty command directory should be removable") ||
        !check(symlink(outsideDirectory.c_str(), linkDirectory.c_str()) == 0,
               "symlink attack fixture should be created"))
    {
        return 1;
    }

    selection = SystemIntegration::Selection();
    selection.commandSymlink = true;
    if (!check(!SystemIntegration::applySelection(selection, error, warning),
               "symlinked command directory must be rejected") ||
        !check(!exists(outsideDirectory + "/swordfish"),
               "symlink attack must not create files outside the selected home"))
    {
        return 1;
    }

    unlink(linkDirectory.c_str());
    if (!check(ensureDirectory(linkDirectory), "safe command directory should be restored"))
    {
        return 1;
    }
    const std::string conflictDirectory = home + "/conflicting-bin";
    const std::string conflictExecutable = conflictDirectory + "/swordfish";
    if (!check(ensureDirectory(conflictDirectory), "conflicting PATH directory should be created") ||
        !check(writeFile(conflictExecutable, "#!/bin/sh\nexit 0\n"), "conflicting executable should be created") ||
        !check(chmod(conflictExecutable.c_str(), 0700) == 0, "conflicting executable should be executable") ||
        !check(setenv("SWORDFISH_HOST_PATH", (conflictDirectory + ":" + linkDirectory).c_str(), 1) == 0,
               "host PATH conflict should be set"))
    {
        return 1;
    }
    selection = SystemIntegration::Selection();
    selection.commandSymlink = true;
    selection.commandPath = home + "/.local/bin/swordfish";
    if (!check(!SystemIntegration::applySelection(selection, error, warning),
               "another host swordfish executable must block a shadowing command symlink") ||
        !check(!exists(home + "/.local/bin/swordfish"),
               "conflict must not create a shadowing command symlink"))
    {
        return 1;
    }
    if (!check(unlink(conflictExecutable.c_str()) == 0, "conflict fixture should be removed"))
    {
        return 1;
    }

    const std::string firstBin = home + "/first-bin";
    const std::string secondBin = home + "/second-bin";
    const std::string originalPath = getenv("PATH") ? getenv("PATH") : "";
    if (!check(ensureDirectory(firstBin) && ensureDirectory(secondBin), "PATH alternatives should exist") ||
        !check(setenv("SWORDFISH_HOST_PATH", (firstBin + ":" + secondBin).c_str(), 1) == 0,
               "host PATH should contain alternatives without ~/.local/bin"))
    {
        return 1;
    }
    std::vector<SystemIntegration::CommandDestination> destinations = SystemIntegration::commandDestinations();
    if (!check(destinations.size() == 2 && destinations[0].path == firstBin + "/swordfish" &&
               destinations[1].path == secondBin + "/swordfish" &&
               !destinations[0].requiresAuthorization,
               "existing writable host PATH alternatives should be offered in order") ||
        !check(setenv("SWORDFISH_HOST_PATH",
                      (firstBin + ":" + linkDirectory + ":" + secondBin).c_str(), 1) == 0,
               "host PATH should include the local candidate"))
    {
        return 1;
    }
    destinations = SystemIntegration::commandDestinations();
    if (!check(!destinations.empty() && destinations[0].path == commandLink,
               "~/.local/bin is preferred only when it is on host PATH"))
    {
        return 1;
    }
    setenv("SWORDFISH_HOST_PATH", (firstBin + ":" + secondBin).c_str(), 1);
    selection = SystemIntegration::Selection();
    selection.commandSymlink = true;
    selection.commandPath = secondBin + "/swordfish";
    if (!check(SystemIntegration::applySelection(selection, error, warning),
               "explicit second PATH destination should apply") ||
        !check(readLink(selection.commandPath) == executablePath && !exists(commandLink),
               "only the exact selected destination should be created"))
    {
        std::cerr << error << std::endl;
        return 1;
    }
    SystemIntegration::Selection reloaded;
    if (!check(SystemIntegration::readSelection(reloaded, error) && reloaded.commandSymlink &&
               reloaded.commandPath == secondBin + "/swordfish",
               "exact destination should persist across state reload") ||
        !check(setenv("SWORDFISH_HOST_PATH", firstBin.c_str(), 1) == 0,
               "host PATH should change after creation") ||
        !check(SystemIntegration::applySelection(SystemIntegration::Selection(), error, warning),
               "deselection should remove persisted destination even after PATH changes") ||
        !check(!exists(secondBin + "/swordfish"), "the exact persisted link should be removed"))
    {
        std::cerr << error << std::endl;
        return 1;
    }
    selection.commandPath = firstBin + "/swordfish";
    if (!check(writeFile(selection.commandPath, "unrelated\n"), "unrelated command fixture should exist") ||
        !check(!SystemIntegration::applySelection(selection, error, warning),
               "unrelated destination files must never be replaced") ||
        !check(readFile(selection.commandPath) == "unrelated\n", "unrelated command should remain unchanged"))
    {
        return 1;
    }
    unlink(selection.commandPath.c_str());
    selection.commandPath = commandLink;
    if (!check(!SystemIntegration::applySelection(selection, error, warning) && !exists(commandLink),
               "a destination outside host PATH must not be used or create PATH changes") ||
        !check(originalPath == (getenv("PATH") ? getenv("PATH") : ""),
               "integration must never alter process PATH"))
    {
        return 1;
    }
    unsetenv("SWORDFISH_HOST_PATH");
    setenv("PATH", firstBin.c_str(), 1);
    destinations = SystemIntegration::commandDestinations();
    if (!check(destinations.size() == 1 && destinations[0].path == firstBin + "/swordfish",
               "native installations must use actual PATH when host override is absent"))
    {
        return 1;
    }
    setenv("SWORDFISH_HOST_PATH", (std::string("/usr/bin:") + firstBin).c_str(), 1);
    destinations = SystemIntegration::commandDestinations();
    if (access("/usr/bin", W_OK | X_OK) < 0)
    {
        if (!check(destinations.size() == 2 && destinations[0].path == firstBin + "/swordfish" &&
                   destinations[1].path == "/usr/bin/swordfish" &&
                   destinations[1].shared && destinations[1].requiresAuthorization,
                   "writable alternatives precede protected, explicitly shared/admin locations"))
        {
            return 1;
        }
        selection.commandPath = "/usr/bin/swordfish";
        if (!check(!SystemIntegration::applySelection(selection, error, warning),
                   "missing trusted authorization helper must fail closed without requesting authorization"))
        {
            return 1;
        }
    }
    setenv("PATH", originalPath.c_str(), 1);
    return 0;
}
