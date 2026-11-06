#ifndef SYSTEM_INTEGRATION_H
#define SYSTEM_INTEGRATION_H

#include <string>
#include <vector>

namespace SystemIntegration
{

struct Selection
{
    bool desktopShortcut;
    bool applicationsMenu;
    bool commandSymlink;
    std::string commandPath;

    Selection() : desktopShortcut(false), applicationsMenu(false), commandSymlink(false)
    {
    }
};

struct CommandDestination
{
    std::string path;
    bool requiresAuthorization;
    bool shared;
};

// Only existing host PATH directories (or a usable ~/.local/bin already in PATH).
std::vector<CommandDestination> commandDestinations();
CommandDestination commandDestinationInfo(const std::string& path);
// Returns the validated bundled helper, or the configured root-owned installed helper.
std::string commandLinkHelper(std::string& error);
bool readSelection(Selection& selection, std::string& error);
bool applySelection(const Selection& selection, std::string& error, std::string& warning);

}

#endif
