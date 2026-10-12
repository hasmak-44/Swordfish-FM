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

#include <dirent.h>
#include <algorithm>
#include <set>


// Global variables
extern FXString homedir;


namespace
{
struct DesktopApplication
{
    FXString name;
    FXString command;
};


void addDataDirs(std::vector<FXString>& dirs, const char* hostvar, const char* var, const FXString& fallback)
{
    const char* value = getenv(hostvar);
    if (!value || !*value)
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


FXbool readDesktopApplication(const FXString& path, DesktopApplication& app)
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
    FXbool inentry = false, hidden = false, terminal = false;
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
        else if ((key == "NoDisplay" || key == "Hidden") && value.lower() == "true")
        {
            hidden = true;
        }
        else if (key == "Terminal" && value.lower() == "true")
        {
            terminal = true;
        }
    }
    fclose(fp);

    app.name = !localname.empty() ? localname : (!languagename.empty() ? languagename : name);
    app.command = desktopExecCommand(exec);
    return type == "Application" && !hidden && !terminal && !app.name.empty() && !app.command.empty() &&
           (tryexec.empty() || xf_existcommand(tryexec)) && xf_existcommand(app.command);
}


void scanApplications(const FXString& root, const FXString& dir, std::set<FXString>& ids,
                      std::vector<DesktopApplication>& apps, int depth)
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
                scanApplications(root, path, ids, apps, depth + 1);
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
        if (readDesktopApplication(path, app))
        {
            apps.push_back(app);
        }
    }
    closedir(dp);
}
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
};


// Object implementation
FXIMPLEMENT(HistInputDialog, DialogBox, HistInputDialogMap, ARRAYNUMBER(HistInputDialogMap))

// Construct a dialog box with an optional check box
HistInputDialog::HistInputDialog(FXWindow* w, FXString inp, FXString message, FXString title, FXString label,
                                 FXIcon* ic, FXbool addr, FXbool clrbtn, FXuint browse, FXbool option, FXString optiontext) :
    DialogBox(w, title, DECOR_TITLE | DECOR_BORDER | DECOR_STRETCHABLE | DECOR_MAXIMIZE | DECOR_CLOSE)
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
        new FXCheckButton(checkbutton, optiontext + FXString(" "), this, ID_TOGGLE_OPTION);
    }

    // Vertical frame
    FXVerticalFrame* contents = new FXVerticalFrame(this, LAYOUT_SIDE_TOP | FRAME_NONE | LAYOUT_FILL_X | LAYOUT_FILL_Y);

    // Icon and first line
    FXMatrix* matrix1 = new FXMatrix(contents, 2, MATRIX_BY_COLUMNS | LAYOUT_SIDE_TOP | LAYOUT_FILL_X | LAYOUT_FILL_Y);
    new FXLabel(matrix1, "", ic, LAYOUT_LEFT | LAYOUT_CENTER_Y | LAYOUT_FILL_ROW);
    new FXLabel(matrix1, message, NULL, LAYOUT_LEFT | LAYOUT_CENTER_Y | LAYOUT_FILL_ROW | FRAME_NONE);

    // Label and input field (combo box)
    FXMatrix* matrix2 = new FXMatrix(contents, 3, MATRIX_BY_COLUMNS | LAYOUT_SIDE_TOP | LAYOUT_FILL_X | LAYOUT_FILL_Y);
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
    input->setFocus();
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
        handle(this, FXSEL(SEL_COMMAND, ID_CANCEL), NULL);
        return 1;

    case KEY_KP_Enter:
    case KEY_Return:
        handle(this, FXSEL(SEL_COMMAND, ID_ACCEPT), NULL);
        return 1;

    default:
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


void HistInputDialog::appendInstalledApplications()
{
    std::vector<FXString> datadirs;
    addDataDirs(datadirs, "SWORDFISH_HOST_XDG_DATA_HOME", "XDG_DATA_HOME", homedir + "/.local/share");
    addDataDirs(datadirs, "SWORDFISH_HOST_XDG_DATA_DIRS", "XDG_DATA_DIRS", "/usr/local/share:/usr/share");

    std::set<FXString> ids;
    std::vector<DesktopApplication> apps;
    for (size_t i = 0; i < datadirs.size(); i++)
    {
        FXString root = datadirs[i] + "/applications";
        scanApplications(root, root, ids, apps, 0);
    }
    std::sort(apps.begin(), apps.end(), [](const DesktopApplication& a, const DesktopApplication& b)
    {
        return comparecase(a.name, b.name) < 0;
    });

    for (size_t i = 0; i < apps.size(); i++)
    {
        if (std::find(appnames.begin(), appnames.end(), apps[i].name) != appnames.end())
        {
            continue;
        }
        appnames.push_back(apps[i].name);
        appcommands.push_back(apps[i].command);
        input->appendItem(apps[i].name, (void*)1);
    }
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
