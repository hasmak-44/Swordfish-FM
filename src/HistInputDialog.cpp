// Input dialog with history list and an optional check box

#include "config.h"
#include "i18n.h"

#include <fx.h>
#include <fxkeys.h>

#include "xfedefs.h"
#include "icons.h"
#include "xfeutils.h"
#include "FileDialog.h"
#include "HistInputDialog.h"
#include "AppLogger.h"
#include <FXIconSource.h>

#include <dirent.h>
#include <algorithm>
#include <set>
#include <errno.h>


// Global variables
extern FXString homedir;


namespace
{
void addDataDirs(std::vector<FXString>& dirs, const char* hostvar, const char* var, const FXString& fallback)
{
    const char* value = getenv(hostvar);
    if (!value)
    {
        value = getenv(var);
    }
    FXString list = (value && *value) ? FXString(value) : fallback;
    for (FXint i = 0; i < list.contains(':') + 1; i++)
    {
        FXString dir = list.section(':', i);
        if (!dir.empty() && std::find(dirs.begin(), dirs.end(), dir) == dirs.end())
        {
            dirs.push_back(dir);
        }
    }
}


// Remove desktop-entry field codes; Open With appends the selected files itself
FXString desktopExecCommand(const FXString& exec)
{
    FXString cmd;
    for (FXint i = 0; i < exec.length(); i++)
    {
        if (exec[i] == '%' && i + 1 < exec.length())
        {
            if (exec[i + 1] == '%')
            {
                cmd += '%';
            }
            i++;
            continue;
        }
        cmd += exec[i];
    }
    cmd = cmd.simplify();

    // Unquote a quoted executable when it can be safely represented without quotes
    if (!cmd.empty() && cmd[0] == '"')
    {
        FXint end = cmd.find('"', 1);
        FXString program = (end > 0) ? cmd.mid(1, end - 1) : FXString("");
        if (!program.empty() && program.find(' ') < 0)
        {
            cmd = program + cmd.mid(end + 1, cmd.length());
        }
    }
    return cmd;
}


FXString localizedKey(const FXString& key, const char* locale)
{
    return key + "[" + locale + "]";
}


FXbool readDesktopApplication(const FXString& path, DesktopApplication& app, FXbool includeUnlisted)
{
    FILE* fp = fopen(path.text(), "r");
    if (!fp)
    {
        return false;
    }

    FXString locale = getenv("LC_ALL") ? getenv("LC_ALL") : "";
    if (locale.empty())
    {
        locale = getenv("LC_MESSAGES") ? getenv("LC_MESSAGES") : "";
    }
    if (locale.empty())
    {
        locale = getenv("LANG") ? getenv("LANG") : "";
    }
    locale = locale.before('.').before('@');
    FXString language = locale.before('_');

    FXString name, localname, languagename, exec, tryexec, type;
    FXbool inentry = false, hidden = false, nodisplay = false, terminal = false;
    char line[4096];
    while (fgets(line, sizeof(line), fp))
    {
        FXString text = FXString(line).trim();
        if (text.empty() || text[0] == '#')
        {
            continue;
        }
        if (text[0] == '[')
        {
            inentry = (text == "[Desktop Entry]");
            continue;
        }
        if (!inentry || text.find('=') < 0)
        {
            continue;
        }
        FXString key = text.before('=').trim();
        FXString value = text.after('=').trim();
        if (key == "Type")
        {
            type = value;
        }
        else if (key == "Name")
        {
            name = value;
        }
        else if (!locale.empty() && key == localizedKey("Name", locale.text()))
        {
            localname = value;
        }
        else if (!language.empty() && key == localizedKey("Name", language.text()))
        {
            languagename = value;
        }
        else if (key == "Exec")
        {
            exec = value;
        }
        else if (key == "TryExec")
        {
            tryexec = value;
        }
        else if (key == "MimeType")
        {
            app.mimetypes = value;
        }
        else if (key == "Categories")
        {
            app.categories = value;
        }
        else if (key == "Icon")
        {
            app.icon = value;
        }
        else if (key == "Hidden" && value.lower() == "true")
        {
            hidden = true;
        }
        else if (key == "NoDisplay" && value.lower() == "true")
        {
            nodisplay = true;
        }
        else if (key == "Terminal" && value.lower() == "true")
        {
            terminal = true;
        }
    }
    fclose(fp);

    app.name = !localname.empty() ? localname : (!languagename.empty() ? languagename : name);
    app.command = desktopExecCommand(exec);
    return type == "Application" && !hidden && (includeUnlisted || (!terminal && !nodisplay)) &&
           !app.name.empty() && !app.command.empty() &&
           (tryexec.empty() || xf_existcommand(tryexec)) && xf_existcommand(app.command);
}


void scanApplications(const FXString& root, const FXString& dir, std::set<FXString>& ids,
                      std::vector<DesktopApplication>& apps, int depth, FXbool includeUnlisted)
{
    DIR* dp = opendir(dir.text());
    if (!dp)
    {
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(dp)) != NULL)
    {
        FXString file = entry->d_name;
        if (file == "." || file == "..")
        {
            continue;
        }
        FXString path = dir + PATHSEPSTRING + file;
        if (xf_isdirectory(path))
        {
            if (depth < 4)
            {
                scanApplications(root, path, ids, apps, depth + 1, includeUnlisted);
            }
            continue;
        }
        if (FXPath::extension(file) != "desktop")
        {
            continue;
        }

        // Earlier XDG data directories override later desktop files with the same ID
        FXString id = path.mid(root.length() + 1, path.length()).substitute(PATHSEPCHAR, '-');
        if (!ids.insert(id).second)
        {
            continue;
        }
        DesktopApplication app;
        if (readDesktopApplication(path, app, includeUnlisted))
        {
            app.id = id;
            apps.push_back(app);
        }
    }
    closedir(dp);
}
}


// Read host defaults directly rather than spawning xdg-mime once per file type.
std::map<FXString, FXString> xf_defaultApplications(FXString& error)
{
    error.clear();
    std::vector<FXString> configdirs, datadirs, files;
    FXString home = FXSystem::getHomeDirectory();
    addDataDirs(configdirs, "SWORDFISH_HOST_XDG_CONFIG_HOME", "XDG_CONFIG_HOME", home + "/.config");
    addDataDirs(configdirs, "SWORDFISH_HOST_XDG_CONFIG_DIRS", "XDG_CONFIG_DIRS", "/etc/xdg");
    addDataDirs(datadirs, "SWORDFISH_HOST_XDG_DATA_HOME", "XDG_DATA_HOME", home + "/.local/share");
    addDataDirs(datadirs, "SWORDFISH_HOST_XDG_DATA_DIRS", "XDG_DATA_DIRS", "/usr/local/share:/usr/share");
    const char* hostdesktop = getenv("SWORDFISH_HOST_XDG_CURRENT_DESKTOP");
    const char* desktop = hostdesktop ? hostdesktop : getenv("XDG_CURRENT_DESKTOP");
    FXString desktops = desktop ? FXString(desktop).lower() : FXString::null;
    for (size_t kind = 0; kind < 2; kind++)
    {
        const std::vector<FXString>& dirs = kind ? datadirs : configdirs;
        for (size_t i = 0; i < dirs.size(); i++)
        {
            FXString root = dirs[i] + (kind ? "/applications/" : "/");
            for (int j = 0; j <= desktops.contains(':'); j++)
            {
                FXString name = desktops.section(':', j);
                if (!name.empty() && name.find('/') < 0)
                {
                    files.push_back(root + name + "-mimeapps.list");
                }
            }
            files.push_back(root + "mimeapps.list");
            if (kind)
            {
                files.push_back(root + "defaults.list");
            }
        }
    }
    std::set<FXString> installed;
    std::vector<DesktopApplication> applications = xf_installedApplications(true);
    for (size_t i = 0; i < applications.size(); i++)
    {
        installed.insert(applications[i].id);
    }
    std::map<FXString, FXString> defaults;
    for (size_t i = 0; i < files.size(); i++)
    {
        FILE* file = fopen(files[i].text(), "r");
        if (!file)
        {
            if (errno != ENOENT)
            {
                error = _("Unable to read system application defaults");
                appLog(APP_LOG_ERRORS, "mime-defaults-read-failed", files[i] + ": " + strerror(errno));
            }
            continue;
        }
        FXbool indefaults = false;
        char buffer[8192];
        while (fgets(buffer, sizeof(buffer), file))
        {
            FXString line = FXString(buffer).trim();
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            if (line[0] == '[')
            {
                indefaults = line == "[Default Applications]";
                continue;
            }
            if (!indefaults || line.find('=') < 0)
            {
                continue;
            }
            FXString mime = line.before('=').trim();
            FXString ids = line.after('=').trim();
            if (defaults.find(mime) != defaults.end())
            {
                continue;
            }
            for (int j = 0; j <= ids.contains(';'); j++)
            {
                FXString id = ids.section(';', j).trim();
                if (installed.count(id))
                {
                    defaults[mime] = id;
                    break;
                }
            }
        }
        if (ferror(file))
        {
            error = _("Unable to read system application defaults");
            appLog(APP_LOG_ERRORS, "mime-defaults-read-failed", files[i]);
        }
        fclose(file);
    }
    return defaults;
}


// Object implementation
FXIMPLEMENT(HistComboBox, ComboBox, NULL, 0)

HistComboBox::HistComboBox(FXComposite* p, int cols, FXbool addr, FXbool clrbtn, FXObject* tgt, FXSelector sel, FXuint opts) :
    ComboBox(p, cols, addr, clrbtn, tgt, sel, opts)
{
}


void HistComboBox::create()
{
    ComboBox::create();
    setFocus();
}


FXDEFMAP(HistInputDialog) HistInputDialogMap[] =
{
    FXMAPFUNC(SEL_KEYPRESS, 0, HistInputDialog::onCmdKeyPress),
    FXMAPFUNC(SEL_COMMAND, HistInputDialog::ID_BROWSE_PATH, HistInputDialog::onCmdBrowsePath),
    FXMAPFUNC(SEL_UPDATE, HistInputDialog::ID_ACCEPT, HistInputDialog::onUpdAccept),
    FXMAPFUNC(SEL_COMMAND, HistInputDialog::ID_ACCEPT, HistInputDialog::onAccept),
    FXMAPFUNC(SEL_CHANGED, HistInputDialog::ID_FILTER_APPLICATIONS, HistInputDialog::onFilterApplications),
    FXMAPFUNC(SEL_COMMAND, HistInputDialog::ID_APPLICATION_SELECTED, HistInputDialog::onApplicationSelected),
    FXMAPFUNC(SEL_CHANGED, HistInputDialog::ID_APPLICATION_SELECTED, HistInputDialog::onApplicationSelected),
    FXMAPFUNC(SEL_DOUBLECLICKED, HistInputDialog::ID_APPLICATION_SELECTED, HistInputDialog::onApplicationOpened),
    FXMAPFUNC(SEL_COMMAND, HistInputDialog::ID_CUSTOM_COMMAND, HistInputDialog::onCustomCommand),
};


// Object implementation
FXIMPLEMENT(HistInputDialog, DialogBox, HistInputDialogMap, ARRAYNUMBER(HistInputDialogMap))

// Construct a dialog box with an optional check box
HistInputDialog::HistInputDialog(FXWindow* w, FXString inp, FXString message, FXString title, FXString label,
                                 FXIcon* ic, FXbool addr, FXbool clrbtn, FXuint browse, FXbool option, FXString optiontext) :
    DialogBox(w, title, DECOR_TITLE | DECOR_BORDER | DECOR_STRETCHABLE | DECOR_SHRINKABLE |
              DECOR_MINIMIZE | DECOR_MAXIMIZE | DECOR_CLOSE)
{
    // Browse type flag
    browsetype = browse;

    // Buttons
    buttons = new FXHorizontalFrame(this, PACK_UNIFORM_WIDTH | LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X, 0, 0, 0, 0, 10, 10,
                                    5, 5);

    // Accept
    accept = new FXButton(buttons, _("&Accept"), NULL, this, ID_ACCEPT, FRAME_GROOVE | LAYOUT_RIGHT, 0, 0, 0, 0, 20, 20);

    // Cancel
    cancel = new FXButton(buttons, _("&Cancel"), NULL, this, ID_CANCEL, FRAME_GROOVE | LAYOUT_RIGHT, 0, 0, 0, 0, 20, 20);

    // Optional check box
    checkbutton = new FXHorizontalFrame(this, JUSTIFY_RIGHT | LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X, 0, 0, 0, 0, 10, 10, 0, 0);

    if (option)
    {
        associationOption = new FXCheckButton(checkbutton, optiontext + FXString(" "), this, ID_TOGGLE_OPTION);
    }

    // Vertical frame
    contents = new FXVerticalFrame(this, LAYOUT_SIDE_TOP | FRAME_NONE | LAYOUT_FILL_X | LAYOUT_FILL_Y);

    // Icon and first line
    FXMatrix* matrix1 = new FXMatrix(contents, 2, MATRIX_BY_COLUMNS | LAYOUT_SIDE_TOP | LAYOUT_FILL_X);
    new FXLabel(matrix1, "", ic, LAYOUT_LEFT | LAYOUT_CENTER_Y | LAYOUT_FILL_ROW);
    messageLabel = new FXLabel(matrix1, message, NULL, LAYOUT_LEFT | LAYOUT_CENTER_Y | LAYOUT_FILL_ROW | FRAME_NONE);

    // Label and input field (combo box)
    FXMatrix* matrix2 = new FXMatrix(contents, 3, MATRIX_BY_COLUMNS | LAYOUT_SIDE_TOP | LAYOUT_FILL_X);
    customCommand = matrix2;
    new FXLabel(matrix2, label, NULL, LAYOUT_LEFT | LAYOUT_CENTER_Y | LAYOUT_FILL_ROW);
    input = new HistComboBox(matrix2, 40, addr, clrbtn, NULL, 0, COMBOBOX_INSERT_LAST | LAYOUT_CENTER_Y | LAYOUT_CENTER_X |
                             LAYOUT_FILL_COLUMN | LAYOUT_FILL_ROW | LAYOUT_FILL_X);
    input->setNumVisible(8);
    input->setText(inp);
    new FXButton(matrix2, _("\tSelect Destination..."), minifiledialogicon, this, ID_BROWSE_PATH, FRAME_GROOVE |
                 LAYOUT_RIGHT | LAYOUT_CENTER_Y, 0, 0, 0, 0, 20, 20);
    if (!xf_isutf8(message.text(), message.length()))
    {
        new FXLabel(contents, _("=> Warning: file name is not UTF-8 encoded!"), NULL, LAYOUT_LEFT |
                    LAYOUT_CENTER_Y | LAYOUT_FILL_ROW);
    }

    // Default initial directory for browsing
    initialdir = homedir;
}


void HistInputDialog::create()
{
    DialogBox::create();
    if (appFilter)
    {
        appFilter->setFocus();
    }
    else
    {
        input->setFocus();
    }
}

void HistInputDialog::setAssociationOption(const FXString& extension, FXbool editing)
{
    contextExtension = extension;
    editingAssociation = editing;
    _option = 0;
    if (!associationOption)
    {
        return;
    }
    associationOption->setCheck(false);
    if (extension.empty())
    {
        associationOption->setText(_("Saving requires files with the same non-empty extension"));
        associationOption->disable();
    }
    else
    {
        FXString label;
        label.format(editing ? _("Remember for editing .%s files in Swordfish only") :
                               _("Remember for .%s files in Swordfish only"), extension.text());
        associationOption->setText(label);
        associationOption->enable();
    }
    associationOption->setTipText(editing ? _("Only editing in Swordfish is affected. Open and system defaults will not change.") :
                                           _("Your system's default applications will not be changed."));
}


void HistInputDialog::CursorEnd()
{
    input->CursorEnd();
    input->setFocus();
}


void HistInputDialog::selectAll()
{
    input->onFwdToText(this, FXSEL(SEL_FOCUSIN, 0), NULL);
    input->onFwdToText(this, FXSEL(SEL_COMMAND, FXTextField::ID_SELECT_ALL), NULL);
}


long HistInputDialog::onCmdKeyPress(FXObject* sender, FXSelector sel, void* ptr)
{
    FXEvent* event = (FXEvent*)ptr;

    switch (event->code)
    {
    case KEY_Escape:
        if (appFilter && !appFilter->getText().empty())
        {
            appFilter->setText(FXString::null);
            onFilterApplications(NULL, 0, NULL);
            appFilter->setFocus();
            return 1;
        }
        handle(this, FXSEL(SEL_COMMAND, ID_CANCEL), NULL);
        return 1;

    case KEY_KP_Enter:
    case KEY_Return:
        handle(this, FXSEL(SEL_COMMAND, ID_ACCEPT), NULL);
        return 1;

    default:
        if (appFilter && appFilter->hasFocus() &&
            (event->code == KEY_Down || event->code == KEY_Up))
        {
            for (int i = 0; i < applicationList->getNumItems(); i++)
            {
                if (applicationList->isItemEnabled(i))
                {
                    applicationList->setCurrentItem(i, true);
                    applicationList->selectItem(i);
                    applicationList->setFocus();
                    onApplicationSelected(NULL, 0, NULL);
                    return 1;
                }
            }
        }
        FXTopWindow::onKeyPress(sender, sel, ptr);
        return 1;
    }
    return 0;
}


long HistInputDialog::onCmdBrowsePath(FXObject* sender, FXSelector sel, void* ptr)
{
    FXString title;

    if (browsetype == HIST_INPUT_FOLDER)
    {
        title = _("Select a Destination Folder");
    }
    else if (browsetype == HIST_INPUT_FILE)
    {
        title = _("Select a File");
    }
    else if (browsetype == HIST_INPUT_EXECUTABLE_FILE)
    {
        title = _("Select an Executable File");
    }
    else
    {
        title = _("Select a File or a Destination Folder");
    }

    // File dialog
    FileDialog browse(this, title);

    const char* patterns[] =
    {
        _("All Files"), "*", NULL
    };
    browse.setDirectory(initialdir);
    browse.setPatternList(patterns);

    // Browse files in directory, or existing, or mixed mode depending on the flag
    if (browsetype == HIST_INPUT_FOLDER)
    {
        browse.setSelectMode(SELECT_FILE_DIRECTORY);
    }
    else if (browsetype == HIST_INPUT_FILE)
    {
        browse.setSelectMode(SELECT_FILE_EXISTING);
    }
    else if (browsetype == HIST_INPUT_EXECUTABLE_FILE)
    {
        browse.setSelectMode(SELECT_FILE_EXISTING);
    }
    else
    {
        browse.setSelectMode(SELECT_FILE_MIXED);
    }
    if (browse.execute())
    {
        FXString path = browse.getFilename();
        input->setText(path);
    }

    return 1;
}


// Set initial directory
void HistInputDialog::setDirectory(const FXString& path)
{
    initialdir = path;
}


FXString HistInputDialog::getHistoryItem(int pos)
{
    for (int i = 0, count = 0; i < input->getNumItems(); i++)
    {
        if (input->getItemData(i) == NULL && count++ == pos)
        {
            return input->getItemText(i);
        }
    }
    return FXString::null;
}


int HistInputDialog::getHistorySize()
{
    int count = 0;
    for (int i = 0; i < input->getNumItems(); i++)
    {
        if (input->getItemData(i) == NULL)
        {
            count++;
        }
    }
    return count;
}


std::vector<DesktopApplication> xf_installedApplications(FXbool includeUnlisted)
{
    std::vector<FXString> datadirs;
    addDataDirs(datadirs, "SWORDFISH_HOST_XDG_DATA_HOME", "XDG_DATA_HOME", homedir + "/.local/share");
    addDataDirs(datadirs, "SWORDFISH_HOST_XDG_DATA_DIRS", "XDG_DATA_DIRS", "/usr/local/share:/usr/share");

    std::set<FXString> ids;
    std::vector<DesktopApplication> apps;
    for (size_t i = 0; i < datadirs.size(); i++)
    {
        FXString root = datadirs[i] + "/applications";
        scanApplications(root, root, ids, apps, 0, includeUnlisted);
    }
    std::sort(apps.begin(), apps.end(), [](const DesktopApplication& a, const DesktopApplication& b)
    {
        return comparecase(a.name, b.name) < 0;
    });
    return apps;
}

void HistInputDialog::appendInstalledApplications()
{
    if (!applicationList)
    {
        FXVerticalFrame* choices = new FXVerticalFrame(contents, FRAME_NONE | LAYOUT_FILL_X | LAYOUT_FILL_Y,
                                                       0, 0, 0, 0, 0, 0, 0, 0);
        choices->reparent(contents, customCommand);
        new FXLabel(choices, _("Search applications:"), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        appFilter = new FXTextField(choices, 40, this, ID_FILTER_APPLICATIONS, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        applicationList = new FXList(choices, this, ID_APPLICATION_SELECTED,
                                     LIST_BROWSESELECT | FRAME_SUNKEN | FRAME_THICK | LAYOUT_FILL_X | LAYOUT_FILL_Y);
        applicationList->setNumVisible(14);
        emptyApplications = new FXLabel(choices, _("No matching applications"), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        customToggle = new FXButton(choices, _("Custom command..."), NULL, this, ID_CUSTOM_COMMAND,
                                     FRAME_GROOVE | LAYOUT_LEFT);
        if (id())
        {
            choices->create();
        }
    }
    for (size_t i = 0; i < applicationIcons.size(); i++)
    {
        if (applicationIcons[i] != minirunicon)
        {
            delete applicationIcons[i];
        }
    }
    applicationIcons.clear();
    applications = xf_installedApplications();
    for (size_t i = 0; i < applications.size(); i++)
    {
        applicationIcons.push_back(applicationIcon(applications[i].icon));
    }
    FXString error;
    std::map<FXString, FXString> defaults = xf_defaultApplications(error);
    defaultApplication = defaults[contextMime];
    savedCommand = contextExtension.empty() ? FXString::null :
        FXString(getApp()->reg().readStringEntry(editingAssociation ? "EDIT_ASSOCIATIONS" : "USER_ASSOCIATIONS",
                                                 contextExtension.text(), "")).section(',', 0);
    if (!error.empty())
    {
        messageLabel->setTipText(error);
    }
    accept->setText(editingAssociation ? _("&Edit") : _("&Open"));
    appFilter->setText(FXString::null);
    customVisible = false;
    customCommand->hide();
    customToggle->setText(_("Custom command..."));
    rebuildApplications();
}


HistInputDialog::~HistInputDialog()
{
    for (size_t i = 0; i < applicationIcons.size(); i++)
    {
        if (applicationIcons[i] != minirunicon)
        {
            delete applicationIcons[i];
        }
    }
}


FXIcon* HistInputDialog::applicationIcon(const FXString& name)
{
    if (name.empty())
    {
        return minirunicon;
    }
    std::vector<FXString> paths;
    if (FXPath::isAbsolute(name))
    {
        paths.push_back(name);
    }
    else
    {
        std::vector<FXString> dirs;
        addDataDirs(dirs, "SWORDFISH_HOST_XDG_DATA_HOME", "XDG_DATA_HOME", homedir + "/.local/share");
        addDataDirs(dirs, "SWORDFISH_HOST_XDG_DATA_DIRS", "XDG_DATA_DIRS", "/usr/local/share:/usr/share");
        for (size_t i = 0; i < dirs.size(); i++)
        {
            paths.push_back(dirs[i] + "/icons/hicolor/32x32/apps/" + name + ".png");
            paths.push_back(dirs[i] + "/icons/hicolor/48x48/apps/" + name + ".png");
            paths.push_back(dirs[i] + "/pixmaps/" + name + ".png");
            paths.push_back(dirs[i] + "/pixmaps/" + name + ".xpm");
        }
    }
    FXIconSource source(getApp());
    for (size_t i = 0; i < paths.size(); i++)
    {
        if (xf_existfile(paths[i]))
        {
            FXIcon* icon = source.loadIconFile(paths[i]);
            if (icon)
            {
                icon->scale(24, 24);
                icon->create();
                return icon;
            }
        }
    }
    return minirunicon;
}


void HistInputDialog::setOpenWithContext(const FXString& pathname, FXint count)
{
    contextPath = pathname;
    contextMime = pathname.empty() ? FXString::null : xf_mimetype(pathname).before(';').trim();
    FXString message;
    if (count == 1)
    {
        message = _("Open ") + FXPath::name(pathname);
    }
    else
    {
        message.format(_("Open %d selected files"), count);
        if (contextExtension.empty())
        {
            contextMime.clear();
        }
    }
    if (!contextMime.empty())
    {
        message += " (" + contextMime + ")";
    }
    message += editingAssociation ? _(" for editing with:") : _(" with:");
    messageLabel->setText(message);
}


void HistInputDialog::rebuildApplications()
{
    applicationList->clearItems();
    rowCommands.clear();
    FXString filter = appFilter->getText().lower().trim();
    int matches = 0;
    for (int group = 0; group < 4; group++)
    {
        FXbool heading = false;
        const char* headings[] = {_("Swordfish saved association"), _("Default application"),
                                  _("Recommended applications"), _("Other applications")};
        if (group == 0 && !savedCommand.empty() &&
            (filter.empty() || savedCommand.lower().find(filter) >= 0))
        {
            applicationList->disableItem(applicationList->appendItem(headings[group]));
            rowCommands.push_back(FXString::null);
            applicationList->appendItem(savedCommand, minirunicon);
            rowCommands.push_back(savedCommand);
            matches++;
        }
        if (group == 0)
        {
            continue;
        }
        for (size_t i = 0; i < applications.size(); i++)
        {
            const DesktopApplication& app = applications[i];
            int appgroup = app.id == defaultApplication ? 1 :
                (!contextMime.empty() &&
                 (FXString(";") + app.mimetypes).find(";" + contextMime + ";") >= 0 ? 2 : 3);
            if (appgroup != group ||
                (!filter.empty() && (app.name + " " + app.command).lower().find(filter) < 0))
            {
                continue;
            }
            if (!heading)
            {
                applicationList->disableItem(applicationList->appendItem(headings[group]));
                rowCommands.push_back(FXString::null);
                heading = true;
            }
            applicationList->appendItem(app.name, applicationIcons[i]);
            rowCommands.push_back(app.command);
            matches++;
        }
    }
    applicationList->setCurrentItem(-1);
    applicationList->killSelection();
    if (matches)
    {
        emptyApplications->hide();
    }
    else
    {
        emptyApplications->show();
    }
    contents->recalc();
}


long HistInputDialog::onFilterApplications(FXObject*, FXSelector, void*)
{
    input->setText(FXString::null);
    rebuildApplications();
    return 1;
}


long HistInputDialog::onApplicationSelected(FXObject*, FXSelector, void*)
{
    int index = applicationList->getCurrentItem();
    if (index >= 0 && index < (int)rowCommands.size() && !rowCommands[index].empty())
    {
        input->setText(rowCommands[index]);
    }
    return 1;
}


long HistInputDialog::onApplicationOpened(FXObject* sender, FXSelector sel, void* ptr)
{
    int index = applicationList->getCurrentItem();
    if (index >= 0 && applicationList->isItemEnabled(index))
    {
        onApplicationSelected(sender, sel, ptr);
        onAccept(sender, sel, ptr);
    }
    return 1;
}


long HistInputDialog::onCustomCommand(FXObject*, FXSelector, void*)
{
    customVisible = !customVisible;
    if (customVisible)
    {
        customCommand->show();
        customToggle->setText(_("Hide custom command"));
        input->setFocus();
    }
    else
    {
        customCommand->hide();
        customToggle->setText(_("Custom command..."));
        appFilter->setFocus();
    }
    contents->recalc();
    return 1;
}


long HistInputDialog::onAccept(FXObject* sender, FXSelector sel, void* ptr)
{
    if (!getText().trim().empty())
    {
        return DialogBox::onCmdAccept(sender, sel, ptr);
    }
    return 1;
}


FXuint HistInputDialog::execute(FXuint placement)
{
    if (applicationList)
    {
        create();
        int width = getApp()->reg().readIntEntry("SETTINGS", "openwith_width", 620);
        int height = getApp()->reg().readIntEntry("SETTINGS", "openwith_height", 560);
        resize(FXCLAMP(width, 400, FXMAX(400, getRoot()->getWidth())),
               FXCLAMP(height, 300, FXMAX(300, getRoot()->getHeight())));
        appFilter->setFocus();
    }
    FXuint result = DialogBox::execute(placement);
    if (applicationList && !isMaximized() && !isMinimized())
    {
        getApp()->reg().writeIntEntry("SETTINGS", "openwith_width", getWidth());
        getApp()->reg().writeIntEntry("SETTINGS", "openwith_height", getHeight());
    }
    return result;
}


// Update accept button
long HistInputDialog::onUpdAccept(FXObject* sender, FXSelector sel, void* ptr)
{
    if (input->getText() == "")
    {
        accept->disable();
    }
    else
    {
        accept->enable();
    }
    
    return 1;    
}
