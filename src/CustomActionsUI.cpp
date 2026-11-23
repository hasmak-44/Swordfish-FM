#include "config.h"
#include "i18n.h"
#include "CustomActionsUI.h"
#include "BackgroundImage.h"
#include "XFileExplorer.h"
#include "FileList.h"
#include "CommandWindow.h"
#include "MessageBox.h"
#include "FileDialog.h"
#include "RemoteDirectory.h"
#include "icons.h"
#include "AppLogger.h"
#include "SystemIcons.h"
#include "HistInputDialog.h"
#include "Theme.h"
#include <stdint.h>
#include <fxkeys.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
class CustomActionToolBar : public ThemeToolBar
{
    struct Entry
    {
        FXWindow* button;
        FXWindow* menuItem;
        bool separator;
    };
    std::vector<Entry> entries;
    FXButton* configure = NULL;
    FXMenuButton* overflow = NULL;

    static FXint preferredWidth(FXWindow* widget)
    {
        return widget->getLayoutHints() & LAYOUT_FIX_WIDTH ? widget->getWidth() : widget->getDefaultWidth();
    }

    static FXint preferredHeight(FXWindow* widget)
    {
        return widget->getLayoutHints() & LAYOUT_FIX_HEIGHT ? widget->getHeight() : widget->getDefaultHeight();
    }

public:
    CustomActionToolBar(FXComposite* parent) :
        ThemeToolBar(parent, FRAME_NONE | LAYOUT_SIDE_TOP | LAYOUT_DOCK_NEXT | LAYOUT_FILL_X) {}

    void reset()
    {
        entries.clear();
        configure = NULL;
        overflow = NULL;
    }

    void setControls(FXButton* editor, FXMenuButton* more)
    {
        configure = editor;
        overflow = more;
    }

    void append(FXWindow* button, FXWindow* menuItem, bool separator = false)
    {
        entries.push_back({button, menuItem, separator});
    }

    FXint getDefaultWidth() override
    {
        return configure && overflow ?
            getPadLeft() + getPadRight() + 2 * getBorderWidth() +
            configure->getDefaultWidth() + overflow->getDefaultWidth() + getHSpacing() :
            ThemeToolBar::getDefaultWidth();
    }

    FXint getDefaultHeight() override
    {
        FXint tallest = configure ? configure->getDefaultHeight() : 0;
        if (overflow) tallest = FXMAX(tallest, overflow->getDefaultHeight());
        for (const Entry& entry : entries) tallest = FXMAX(tallest, preferredHeight(entry.button));
        return tallest + getPadTop() + getPadBottom() + 2 * getBorderWidth();
    }

    void layout() override
    {
        if (!configure || !overflow) { ThemeToolBar::layout(); return; }
        const FXint left = getPadLeft() + getBorderWidth();
        const FXint right = FXMAX(left, getWidth() - getPadRight() - getBorderWidth());
        const FXint top = getPadTop() + getBorderWidth();
        const FXint contentHeight = FXMAX(1, getHeight() - top - getPadBottom() - getBorderWidth());
        const FXint spacing = getHSpacing();
        const FXint configureWidth = FXMIN(configure->getDefaultWidth(), FXMAX(1, right - left));
        const FXint configureX = FXMAX(left, right - configureWidth);
        configure->show();
        configure->position(configureX, top, configureWidth, contentHeight);

        FXint total = left;
        for (const Entry& entry : entries) total += preferredWidth(entry.button) + spacing;
        const bool needsOverflow = total > configureX;
        const FXint overflowWidth = FXMIN(overflow->getDefaultWidth(), FXMAX(1, configureX - left - spacing));
        const FXint overflowX = FXMAX(left, configureX - spacing - overflowWidth);
        const FXint limit = needsOverflow ? overflowX : configureX;
        unsigned visible = 0;
        FXint x = left;
        while (visible < entries.size() && x + preferredWidth(entries[visible].button) + spacing <= limit)
        {
            x += preferredWidth(entries[visible].button) + spacing;
            ++visible;
        }
        while (visible && entries[visible - 1].separator) --visible;
        x = left;
        bool haveOverflowItem = false;
        bool menuChanged = false;
        for (unsigned i = 0; i < entries.size(); ++i)
        {
            const Entry& entry = entries[i];
            const FXbool wasShown = entry.menuItem->shown();
            if (i < visible)
            {
                const FXint height = FXMIN(preferredHeight(entry.button), contentHeight);
                entry.button->show();
                entry.button->position(x, top + (contentHeight - height) / 2,
                                       preferredWidth(entry.button), height);
                x += preferredWidth(entry.button) + spacing;
                entry.menuItem->hide();
            }
            else
            {
                entry.button->hide();
                if (!entry.separator || haveOverflowItem) entry.menuItem->show();
                else entry.menuItem->hide();
                if (!entry.separator) haveOverflowItem = true;
            }
            if (wasShown != entry.menuItem->shown()) menuChanged = true;
        }
        if (menuChanged) overflow->getMenu()->recalc();
        if (needsOverflow)
        {
            overflow->show();
            overflow->position(overflowX, top, overflowWidth, contentHeight);
        }
        else overflow->hide();
        flags &= ~FLAG_DIRTY;
    }
};

enum { ENTRY_SAVE = FXDialogBox::ID_LAST, ENTRY_BROWSE, ENTRY_INSERT, ENTRY_COMMAND, ENTRY_ICON,
       ENTRY_DIRECTORY, ENTRY_LEGEND, ENTRY_TEST, ENTRY_SUGGEST };

bool discardChanges(FXWindow* owner)
{
    FXDialogBox question(owner, _("Unsaved Custom Action Changes"), DECOR_TITLE | DECOR_BORDER | DECOR_CLOSE);
    FXVerticalFrame* body = new FXVerticalFrame(&question, LAYOUT_FILL_X);
    new FXLabel(body, _("Discard your unsaved changes?"), NULL, JUSTIFY_LEFT);
    FXHorizontalFrame* buttons = new FXHorizontalFrame(body, LAYOUT_FILL_X);
    new FXButton(buttons, _("Keep editing"), NULL, &question, FXDialogBox::ID_CANCEL,
                 BUTTON_DEFAULT | BUTTON_INITIAL | LAYOUT_RIGHT);
    new FXButton(buttons, _("Discard"), NULL, &question, FXDialogBox::ID_ACCEPT, BUTTON_NORMAL | LAYOUT_RIGHT);
    return question.execute(PLACEMENT_OWNER);
}

const char* parameterLegend = N_(
    "%f   First selected absolute path\n%F   All selected absolute paths\n"
    "%n   First selected basename (including extension)\n%N   All selected basenames\n"
    "%d   First selected parent directory\n%D   Distinct selected parent directories\n"
    "%u   First selected file URI\n%U   All selected file URIs\n"
    "%p   Active pane directory\n%o   Opposite pane directory\n"
    "%a   This action's folder (after saving)\n%r   Portable data folder (portable mode only)\n"
    "%c   Actual selection count (0 on the background)\n"
    "%w   First basename without its last extension\n%W   All basenames without their last extension\n"
    "%x   First last extension, without the dot\n%X   All last extensions, without dots\n"
    "%%   Literal percent\n\n"
    "Names: archive.tar.gz -> %w archive.tar, %x gz\n"
    ".hidden -> %w .hidden, %x empty\n.hidden.txt -> %w .hidden, %x txt\n"
    "name. -> %w name, %x empty; names without a dot have an empty extension.\n\n"
    "List parameters %F/%N/%D/%U/%W/%X must be separate, unquoted arguments.\n"
    "Single parameters may be unquoted, single-quoted or double-quoted.\n"
    "Filenames are quoted automatically. Empty extensions remain empty arguments.\n\n"
    "With Show without selection and Folders enabled, file/name/URI/extension parameters\n"
    "refer to the current folder. %d/%D still mean the current directory; %c remains 0.\n"
    "Without Folders, file parameters require selected files; %p and %d remain usable.\n\n"
    "Working directory accepts %p, %o, %d, %f, %a, %r and %%; it is one literal path,\n"
    "not shell code. Leave it empty to use the active pane directory.\n\n"
    "The command runs through /bin/sh. Quote a program path that contains spaces,\n"
    "for example '/opt/My App/run' %F.\n"
    "Use an external script for nested shell substitutions, comments or here-documents.");

FXIcon* loadActionIcon(FXApp* app, const FXString& path, FXint size)
{
    return loadCustomActionIcon(app, path, size);
}

void appendIconError(FXString& errors, const FXString& error)
{
    if (!errors.empty()) { errors += "\n"; }
    errors += error;
}

void showIconMessage(FXWindow* owner, const FXString& title, const FXString& message, FXIcon* icon)
{
    MessageBox box(owner, title, message, icon, BOX_OK,
                   JUSTIFY_LEFT | ICON_BEFORE_TEXT | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y,
                   0, 0, true);
    box.execute(PLACEMENT_OWNER);
}

void showIconWarning(FXWindow* owner, const FXString& warnings)
{
    showIconMessage(owner, _("Custom Action Icons Not Copied"),
        _("Some custom-action icons could not be stored in portable data. Their original references were kept. "
          "Fix the errors or select the images again before copying the profile.\n\n") + warnings, bigwarningicon);
}

class ActionImageChooser : public FileDialog
{
    enum { PREVIEW = DialogBox::ID_LAST };
    FXLabel* preview;
    FXIcon* previewIcon = NULL;
    FXString previewPath;

    void updatePreview()
    {
        FXString path = getFilename();
        if (path == previewPath) { return; }
        previewPath = path;
        preview->setIcon(NULL);
        delete previewIcon;
        previewIcon = NULL;
        if (path.empty() || xf_isdirectory(path))
            preview->setText(_("Select an image\nto preview."));
        else
        {
            previewIcon = loadActionIcon(getApp(), path, 128);
            preview->setIcon(previewIcon);
            preview->setText(previewIcon ? FXString::null : _("Preview unavailable.\nCannot load this image."));
        }
    }

public:
    ActionImageChooser(FXWindow* owner, const FXString& current) :
        FileDialog(owner, _("Select an Icon Image File"))
    {
        setSelectMode(SELECT_FILE_EXISTING);
        setPatternList(_("Supported Images (*.png,*.PNG,*.jpg,*.JPG,*.jpeg,*.JPEG,*.xpm,*.XPM,*.svg,*.SVG)\nAll Files (*)"));
        preview = new FXLabel(list->getSidebarFrame(), _("Select an image\nto preview."), NULL,
            LAYOUT_FILL_X | LAYOUT_FIX_HEIGHT | JUSTIFY_CENTER_X | JUSTIFY_CENTER_Y |
            ICON_ABOVE_TEXT | FRAME_SUNKEN, 0, 0, 0, 160);
        preview->setTarget(this);
        preview->setSelector(PREVIEW);
        preview->setBackColor(getApp()->reg().readColorEntry("SETTINGS", "listbackcolor", FXRGB(255, 255, 255)));
        FXString currentPath = customActionIconPath(current);
        if (current.empty() || current.left(6) == "theme:" || currentPath.empty())
        {
            const char* portable = getenv("SWORDFISH_PORTABLE_DATA");
            FXString folder = portable && *portable ?
                FXPath::absolute(portable) + PATHSEPSTRING "icons" PATHSEPSTRING "custom-actions" : FXString::null;
            setDirectory(!folder.empty() && xf_isdirectory(folder) ? folder : FXSystem::getHomeDirectory());
        }
        else
            setFilename(currentPath);
        getApp()->addTimeout(this, PREVIEW, 100);
    }

    FXString value() const { return getFilename(); }

    ~ActionImageChooser()
    {
        getApp()->removeTimeout(this, PREVIEW);
        preview->setIcon(NULL);
        delete previewIcon;
    }

    long handle(FXObject* sender, FXSelector sel, void* ptr)
    {
        if (FXSELTYPE(sel) == SEL_TIMEOUT && FXSELID(sel) == PREVIEW)
        {
            updatePreview();
            getApp()->addTimeout(this, PREVIEW, 100);
            return 1;
        }
        if (FXSELTYPE(sel) == SEL_COMMAND && FXSELID(sel) == DialogBox::ID_ACCEPT)
        {
            FXString path = getFilename();
            FXIcon* test = loadActionIcon(getApp(), path, 48);
            if (!test)
            {
                MessageBox::error(this, BOX_OK, _("Cannot Load Icon"),
                    _("The selected image is missing or cannot be decoded.\nUse PNG, JPEG or XPM; SVG requires rsvg-convert.\n%s"),
                    path.text());
                return 1;
            }
            delete test;
        }
        return FileDialog::handle(sender, sel, ptr);
    }
};

class ActionIconChooser : public FXDialogBox
{
    enum { SEARCH = FXDialogBox::ID_LAST, MODE, LOAD, SELECT, ACCEPT };
    enum { LOAD_BATCH = 24 };
    enum { ALL_ICONS, FIRST_CATEGORY, IMAGE_FILES = FIRST_CATEGORY + SystemActionIcon::UNCATEGORIZED + 1,
           NO_ICON };
    const std::vector<SystemActionIcon>& catalog;
    std::vector<unsigned> matches;
    std::vector<FXIcon*> images;
    std::vector<bool> loaded;
    FXTextField* search;
    FXComboBox* mode;
    FXIconList* list;
    FXLabel* status;
    FXString selection;
    int lastMode = ALL_ICONS;

    void clearImages()
    {
        getApp()->removeTimeout(this, LOAD);
        list->clearItems();
        for (size_t i = 0; i < images.size(); ++i) { delete images[i]; }
        images.clear();
        loaded.clear();
    }
    void populate()
    {
        clearImages();
        int current = -1;
        for (unsigned i = 0; i < matches.size(); ++i)
        {
            const SystemActionIcon& item = catalog[matches[i]];
            list->appendItem(item.name, minirunicon, minirunicon);
            if (selection == "theme:" + item.name) { current = i; }
        }
        images.resize(matches.size(), NULL);
        loaded.resize(matches.size(), false);
        if (current >= 0) { list->setCurrentItem(current); list->selectItem(current); list->makeItemVisible(current); }
        status->setText(matches.empty() ? _("No matching icons") :
            matches.size() == 1 ? FXString(_("1 icon")) :
            FXStringFormat(_("%u icons"), (unsigned)matches.size()));
        if (!matches.empty()) { getApp()->addTimeout(this, LOAD, 10); }
    }
    // Images are decoded only once their item scrolls into view, so large themes open quickly.
    void loadVisible()
    {
        unsigned count = 0, remaining = 0;
        for (unsigned row = 0; row < loaded.size(); ++row)
        {
            if (loaded[row]) { continue; }
            if (count >= LOAD_BATCH || !list->isItemVisible(row)) { ++remaining; continue; }
            loaded[row] = true; ++count;
            images[row] = loadActionIcon(getApp(), catalog[matches[row]].path, 48);
            if (images[row]) { list->setItemBigIcon(row, images[row]); }
        }
        if (remaining) { getApp()->addTimeout(this, LOAD, count ? 1 : 100); }
    }
    void filter()
    {
        matches.clear();
        FXString query = search->getText().lower().trim();
        int category = mode->getCurrentItem();
        for (unsigned i = 0; i < catalog.size(); ++i)
            if ((category == ALL_ICONS || category == FIRST_CATEGORY + catalog[i].category) &&
                (query.empty() || FXString(catalog[i].name).lower().find(query) >= 0))
                matches.push_back(i);
        populate();
    }
    bool valid(const FXString& value)
    {
        FXIcon* test = loadActionIcon(getApp(), value, 48);
        if (test) { delete test; return true; }
        MessageBox::error(this, BOX_OK, _("Cannot Load Icon"),
            _("The icon is missing or cannot be decoded.\nUse PNG, JPEG or XPM; SVG requires rsvg-convert.\n%s"), value.text());
        return false;
    }
    void browseImage()
    {
        ActionImageChooser dialog(this, selection);
        if (dialog.execute())
        {
            selection = dialog.value();
            FXDialogBox::handle(this, FXSEL(SEL_COMMAND, ID_ACCEPT), NULL);
        }
        mode->setCurrentItem(lastMode);
    }
public:
    ActionIconChooser(FXWindow* owner, const FXString& current) :
        FXDialogBox(owner, _("Choose an Icon"),
                    DECOR_TITLE | DECOR_BORDER | DECOR_RESIZE | DECOR_CLOSE | DECOR_MAXIMIZE,
                    0, 0, 800, 600), catalog(systemActionIcons(getApp())), selection(current)
    {
        FXHorizontalFrame* buttons = new FXHorizontalFrame(this, LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
        new FXButton(buttons, _("Cancel"), NULL, this, ID_CANCEL, BUTTON_NORMAL | LAYOUT_RIGHT);
        new FXButton(buttons, _("Select"), NULL, this, ACCEPT, BUTTON_DEFAULT | BUTTON_INITIAL | LAYOUT_RIGHT);
        FXVerticalFrame* body = new FXVerticalFrame(this, LAYOUT_FILL_X | LAYOUT_FILL_Y);
        new FXLabel(body, _("Select icon from:"), NULL, JUSTIFY_LEFT);
        mode = new FXComboBox(body, 20, this, MODE, COMBOBOX_STATIC | FRAME_SUNKEN | LAYOUT_FILL_X);
        mode->appendItem(_("All Icons"));
        const char* categories[] = {N_("Action Icons"), N_("Animations"), N_("Application Icons"),
            N_("Menu Icons"), N_("Device Icons"), N_("Emblems"), N_("Emoticons"), N_("File Type Icons"),
            N_("Location Icons"), N_("Status Icons"), N_("Stock Icons"), N_("Uncategorized Icons")};
        for (unsigned i = 0; i < ARRAYNUMBER(categories); ++i) { mode->appendItem(_(categories[i])); }
        mode->appendItem(_("Image Files")); mode->appendItem(_("No Icon"));
        mode->setNumVisible(15); mode->setCurrentItem(ALL_ICONS);
        new FXLabel(body, _("Filter installed host icons:"), NULL, JUSTIFY_LEFT);
        FXHorizontalFrame* filterRow = new FXHorizontalFrame(body, LAYOUT_FILL_X, 0, 0, 0, 0, 0, 0, 0, 0);
        new FXLabel(filterRow, FXString::null, minisearchicon, LAYOUT_CENTER_Y);
        search = new FXTextField(filterRow, 35, this, SEARCH, TEXTFIELD_NORMAL | LAYOUT_FILL_X | LAYOUT_CENTER_Y);
        search->setTipText(_("Show only icons whose name contains this text"));
        list = new FXIconList(body, this, SELECT,
            ICONLIST_BROWSESELECT | ICONLIST_BIG_ICONS | ICONLIST_COLUMNS |
            LAYOUT_FILL_X | LAYOUT_FILL_Y | FRAME_SUNKEN);
        list->setItemSpace(120);
        status = new FXLabel(body, "", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        const char* portable = getenv("SWORDFISH_PORTABLE_DATA");
        new FXLabel(body, portable && *portable ?
                    _("System icons are stored by name and follow the host theme.\n"
                      "Image files are copied into portable data on Save. SVG requires rsvg-convert.") :
                    _("System icons are stored by name and follow the host theme.\n"
                      "Image files keep their absolute path. SVG requires rsvg-convert."),
                    NULL, JUSTIFY_LEFT);
        filter();
        xf_scrollabledialog(this);
        xf_dialogcolors(this);
    }
    ~ActionIconChooser() { clearImages(); }
    const FXString& value() const { return selection; }
    long handle(FXObject* sender, FXSelector sel, void* ptr)
    {
        FXuint id = FXSELID(sel), type = FXSELTYPE(sel);
        if ((type == SEL_CHANGED || type == SEL_COMMAND) && id == SEARCH) { filter(); return 1; }
        if (type == SEL_TIMEOUT && id == LOAD) { loadVisible(); return 1; }
        if (type == SEL_DOUBLECLICKED && id == SELECT)
            return handle(sender, FXSEL(SEL_COMMAND, ACCEPT), ptr);
        if (type != SEL_COMMAND) { return FXDialogBox::handle(sender, sel, ptr); }
        switch (id)
        {
        case MODE:
            if (mode->getCurrentItem() == IMAGE_FILES) { browseImage(); return 1; }
            if (mode->getCurrentItem() == NO_ICON)
            {
                list->disable(); search->disable();
            }
            else
            {
                list->enable(); search->enable(); filter();
            }
            lastMode = mode->getCurrentItem();
            return 1;
        case ACCEPT:
            if (mode->getCurrentItem() == NO_ICON) { selection.clear(); }
            else
            {
                int row = list->getCurrentItem();
                if (row < 0 || row >= (int)matches.size()) { return 1; }
                FXString value = "theme:" + catalog[matches[row]].name;
                if (!valid(value)) { return 1; }
                selection = value;
            }
            return FXDialogBox::handle(sender, FXSEL(SEL_COMMAND, ID_ACCEPT), NULL);
        }
        return FXDialogBox::handle(sender, sel, ptr);
    }
};

class ActionProperties : public FXDialogBox
{
    CustomActions& model;
    unsigned index;
    FXTextField *name, *description, *icon, *command = NULL, *patterns = NULL;
    FXCheckButton *toolbar, *files = NULL, *folders = NULL, *single = NULL, *multiple = NULL;
    FXCheckButton *background = NULL, *terminal = NULL, *confirm = NULL, *showOutput = NULL;
    FXComboBox* placeholders = NULL;
    FXTextField* workingDirectory = NULL;
    FXString initialCommand;
    CustomActionContext previewContext;
    FXButton* iconPreview = NULL;
    FXIcon* previewIcon = NULL;
    CustomAction edited() const
    {
        CustomAction a = model.items[index];
        a.name = name->getText().trim(); a.description = description->getText(); a.icon = icon->getText().trim();
        a.toolbar = toolbar->getCheck();
        if (command)
        {
            a.patterns = patterns->getText().trim();
            // An unedited older program/parameters action stays exactly as stored
            if (!a.programMode || command->getText() != initialCommand)
            {
                a.command = command->getText(); a.programMode = false;
                a.program.clear(); a.parameters.clear();
            }
            a.workingDirectory = workingDirectory->getText();
            a.files = files->getCheck(); a.folders = folders->getCheck();
            a.single = single->getCheck(); a.multiple = multiple->getCheck(); a.background = background->getCheck();
            a.terminal = terminal->getCheck(); a.confirm = confirm->getCheck(); a.showOutput = showOutput->getCheck();
        }
        return a;
    }
    void suggestIcon()
    {
        if (!command) { return; }
        FXString text = command->getText().trim(), executable;
        if (!text.empty() && (text[0] == '\'' || text[0] == '"'))
        {
            int end = text.find(text[0], 1);
            if (end > 0) { executable = text.mid(1, end - 1); }
        }
        else { executable = text.before(' '); }
        executable.substitute("%%", "%");
        if (executable.empty()) { return; }
        std::vector<DesktopApplication> applications = xf_installedApplications(true);
        for (size_t i = 0; i < applications.size(); ++i)
        {
            FXString cmd = applications[i].command.trim(), first;
            if (!cmd.empty() && cmd[0] == '"')
            {
                int end = cmd.find('"', 1);
                if (end > 0) { first = cmd.mid(1, end - 1); }
            }
            else { first = cmd.before(' '); }
            if (first != executable)
            {
                if (first.contains('/') == executable.contains('/')) { continue; }
                if (FXPath::name(first) != FXPath::name(executable)) { continue; }
                FXString resolved = FXPath::search(getenv("SWORDFISH_HOST_PATH") ?
                    getenv("SWORDFISH_HOST_PATH") : (getenv("PATH") ? getenv("PATH") : ""), FXPath::name(first));
                if (resolved != (first.contains('/') ? first : executable)) { continue; }
            }
            FXString candidate = applications[i].icon;
            if (candidate.empty()) { continue; }
            if (!FXPath::isAbsolute(candidate)) { candidate = "theme:" + candidate; }
            FXIcon* image = loadActionIcon(getApp(), candidate, 48);
            if (!image) { continue; }
            delete image;
            // This is an explicit suggestion button, never an automatic overwrite.
            if (icon->getText().trim() != candidate && !icon->getText().trim().empty() &&
                MessageBox::question(this, BOX_OK_CANCEL, _("Replace Action Icon"),
                    _("Replace the current icon with the installed application's icon?")) != BOX_CLICKED_OK)
                return;
            icon->setText(candidate); updateIcon(); return;
        }
        MessageBox::information(this, BOX_OK, _("Application Icon"),
            _("No matching installed application with a loadable icon was found."));
    }
    void updateIcon()
    {
        FXIcon* replacement = loadActionIcon(getApp(), icon->getText().trim(), 48);
        iconPreview->setIcon(replacement ? replacement : minirunicon);
        icon->setTipText(!icon->getText().trim().empty() && !replacement ?
            _("Icon unavailable on this host. Choose another icon or No Icon.") :
            _("theme:NAME for an installed host icon, or an image filename."));
        delete previewIcon; previewIcon = replacement;
    }
public:
    ActionProperties(FXWindow* owner, CustomActions& actions, unsigned n, const CustomActionContext& ctx) :
        FXDialogBox(owner, _("Custom Action Properties"),
                    DECOR_TITLE | DECOR_BORDER | DECOR_RESIZE | DECOR_MAXIMIZE | DECOR_CLOSE, 0, 0, 720, 660),
        model(actions), index(n), previewContext(ctx)
    {
        const CustomAction& a = model.items[n];
        FXHorizontalFrame* buttons = new FXHorizontalFrame(this, LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
        new FXButton(buttons, _("Cancel"), NULL, this, ID_CANCEL, BUTTON_NORMAL | LAYOUT_RIGHT);
        new FXButton(buttons, _("Apply"), NULL, this, ENTRY_SAVE, BUTTON_DEFAULT | BUTTON_INITIAL | LAYOUT_RIGHT);
        FXTabBook* tabs = new FXTabBook(this, NULL, 0, LAYOUT_FILL_X | LAYOUT_FILL_Y);
        new DialogTabItem(tabs, _("Basic"));
        FXScrollWindow* scroll = new FXScrollWindow(tabs, LAYOUT_FILL_X | LAYOUT_FILL_Y | FRAME_THICK | FRAME_RAISED);
        xf_readablescrolling(scroll);
        FXVerticalFrame* body = new FXVerticalFrame(scroll, LAYOUT_FILL_X | LAYOUT_FILL_Y);
        FXMatrix* form = new FXMatrix(body, 2, MATRIX_BY_COLUMNS | LAYOUT_FILL_X);
        new FXLabel(form, _("Name:"), NULL, JUSTIFY_LEFT);
        name = new FXTextField(form, 40, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        name->setText(a.name);
        new FXLabel(form, _("Description:"), NULL, JUSTIFY_LEFT);
        description = new FXTextField(form, 40, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        description->setText(a.description);
        if (a.kind == CustomAction::ACTION)
        {
            new FXLabel(form, _("Command:"), NULL, JUSTIFY_LEFT);
            FXHorizontalFrame* commandrow = new FXHorizontalFrame(form, LAYOUT_FILL_X, 0, 0, 0, 0, 0, 0, 0, 0);
            command = new FXTextField(commandrow, 24, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
            initialCommand = CustomActions::commandText(a);
            command->setText(initialCommand);
            command->setTipText(_("Program followed by its arguments and placeholders, e.g. gimp %F"));
            new FXButton(commandrow, _("Browse...\tChoose a program or script (replaces the command)"),
                         minifiledialogicon, this, ENTRY_COMMAND, BUTTON_NORMAL);
            new FXLabel(form, _("Working directory:"), NULL, JUSTIFY_LEFT);
            FXHorizontalFrame* directoryrow = new FXHorizontalFrame(form, LAYOUT_FILL_X, 0, 0, 0, 0, 0, 0, 0, 0);
            workingDirectory = new FXTextField(directoryrow, 24, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
            workingDirectory->setText(a.workingDirectory);
            workingDirectory->setTipText(_("Empty uses the active folder. One literal path; %p, %o, %d, %f, %a, %r and %% are supported."));
            new FXButton(directoryrow, _("Browse...\tChoose a working directory"), minifoldericon, this, ENTRY_DIRECTORY, BUTTON_NORMAL);
            new FXLabel(body, _("Enter the program followed by its arguments, as you would in a terminal.\n"
                               "Browse inserts a program or script path; add placeholders after it."),
                        NULL, JUSTIFY_LEFT);
            const char* portable = getenv("SWORDFISH_PORTABLE_DATA");
            if (portable && *portable)
                new FXLabel(body, _("Scripts are run from where they are; they are not copied.\n"
                                   "To keep a script with the portable profile, use %a or %r/scripts."),
                            NULL, JUSTIFY_LEFT);
        }
        new FXLabel(form, _("Icon:"), NULL, JUSTIFY_LEFT);
        FXHorizontalFrame* iconrow = new FXHorizontalFrame(form, LAYOUT_FILL_X, 0, 0, 0, 0, 0, 0, 0, 0);
        iconPreview = new FXButton(iconrow, _("\tChoose an icon"), minirunicon, this, ENTRY_BROWSE,
                                   BUTTON_NORMAL | LAYOUT_FIX_WIDTH | LAYOUT_FIX_HEIGHT, 0, 0, 56, 56);
        icon = new FXTextField(iconrow, 30, this, ENTRY_ICON, TEXTFIELD_NORMAL | LAYOUT_FILL_X | LAYOUT_CENTER_Y);
        icon->setText(a.icon);
        new FXButton(iconrow, _("Browse..."), NULL, this, ENTRY_BROWSE, BUTTON_NORMAL | LAYOUT_CENTER_Y);
        if (a.kind == CustomAction::ACTION)
            new FXButton(iconrow, _("Use Program Icon"), NULL, this, ENTRY_SUGGEST, BUTTON_NORMAL | LAYOUT_CENTER_Y);
        updateIcon();
        if (a.kind != CustomAction::ACTION)
        {
            toolbar = new FXCheckButton(body, _("Show on custom toolbar"));
            toolbar->setCheck(a.toolbar);
        }
        if (a.kind == CustomAction::ACTION)
        {
            FXGroupBox* execution = new FXGroupBox(body, _("Execution"), GROUPBOX_NORMAL | LAYOUT_FILL_X);
            terminal = new FXCheckButton(execution, _("Run in configured terminal (wait for Enter on completion)"));
            terminal->setCheck(a.terminal);
            showOutput = new FXCheckButton(execution, _("Show command output (otherwise show only on failure)"));
            showOutput->setCheck(a.showOutput);
            confirm = new FXCheckButton(execution, _("Ask for confirmation before running"));
            confirm->setCheck(a.confirm);
            FXGroupBox* help = new FXGroupBox(body, _("Command parameters"), GROUPBOX_NORMAL | LAYOUT_FILL_X);
            FXHorizontalFrame* row = new FXHorizontalFrame(help, LAYOUT_FILL_X);
            placeholders = new FXComboBox(row, 34, NULL, 0, COMBOBOX_STATIC | FRAME_SUNKEN);
            const char* labels[] = {
                N_("%f  First selected path"), N_("%F  All selected paths"), N_("%n  First selected filename"),
                N_("%N  All selected filenames"), N_("%d  First selected parent directory"),
                N_("%D  All selected parent directories"), N_("%u  First selected file URI"), N_("%U  All selected file URIs"),
                N_("%p  Current pane directory"), N_("%o  Opposite pane directory"), N_("%%  Literal percent"),
                N_("%c  Selection count"), N_("%w  First name without extension"), N_("%W  All names without extension"),
                N_("%x  First extension"), N_("%X  All extensions"),
                N_("%a  Action folder"), N_("%r  Portable data folder")
            };
            for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) { placeholders->appendItem(_(labels[i])); }
            placeholders->setNumVisible(11);
            placeholders->setCurrentItem(0);
            new FXButton(row, _("Insert"), NULL, this, ENTRY_INSERT, BUTTON_NORMAL);
            FXHorizontalFrame* tools = new FXHorizontalFrame(help, LAYOUT_FILL_X);
            new FXButton(tools, _("Parameter Legend..."), NULL, this, ENTRY_LEGEND, BUTTON_NORMAL);
            new FXButton(tools, _("Test Action"), NULL, this, ENTRY_TEST, BUTTON_NORMAL);
            new FXLabel(help, _("Test checks syntax, placeholders and working directory without running the action.\n"
                                 "It cannot verify an application's supported options or guarantee runtime success."),
                        NULL, JUSTIFY_LEFT);
            new FXLabel(help, _("%f / %F  First / all selected paths    %n / %N  First / all filenames\n"
                               "%d / %D  Selected parent directories    %u / %U  Selected file URIs\n"
                               "%p  Current directory    %o  Opposite pane directory    %%  Literal percent\n"
                               "%a  Action folder    %r  Portable data folder (portable mode)\n\n"
                               "Filenames are quoted automatically. Use %F/%N/%D/%U as separate,\n"
                               "unquoted arguments. Use a script for complex shell substitutions."),
                        NULL, JUSTIFY_LEFT);
            new DialogTabItem(tabs, _("Appearance Conditions"));
            FXScrollWindow* appearance = new FXScrollWindow(tabs, LAYOUT_FILL_X | LAYOUT_FILL_Y | FRAME_THICK | FRAME_RAISED);
            xf_readablescrolling(appearance);
            FXVerticalFrame* conditions = new FXVerticalFrame(appearance, LAYOUT_FILL_X | LAYOUT_FILL_Y);
            FXGroupBox* placement = new FXGroupBox(conditions, _("Toolbar"), GROUPBOX_NORMAL | LAYOUT_FILL_X);
            toolbar = new FXCheckButton(placement, _("Show on custom toolbar"));
            toolbar->setCheck(a.toolbar);
            FXGroupBox* noSelection = new FXGroupBox(conditions, _("Without a selection"), GROUPBOX_NORMAL | LAYOUT_FILL_X);
            background = new FXCheckButton(noSelection, _("Show without selection (folder background)"));
            background->setCheck(a.background);
            new FXLabel(noSelection, _("Always show on the background, regardless of the conditions below.\n"
                                      "Use %p for the current directory when Folders is not checked."),
                        NULL, JUSTIFY_LEFT);
            new FXLabel(conditions, _("File Pattern:"), NULL, JUSTIFY_LEFT);
            patterns = new FXTextField(conditions, 55, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
            patterns->setText(a.patterns);
            new FXLabel(conditions, _("Case-sensitive patterns separated by semicolons, e.g. *.txt;*.md\n"
                                     "Extension shorthand is also accepted: .jpg;.png;.tar.gz\n"
                                     "Patterns restrict files, not folders. * matches every filename."),
                        NULL, JUSTIFY_LEFT);
            FXHorizontalFrame* types = new FXHorizontalFrame(conditions, LAYOUT_FILL_X);
            files = new FXCheckButton(types, _("Files")); files->setCheck(a.files);
            folders = new FXCheckButton(types, _("Folders")); folders->setCheck(a.folders);
            FXHorizontalFrame* counts = new FXHorizontalFrame(conditions, LAYOUT_FILL_X);
            single = new FXCheckButton(counts, _("Single selection")); single->setCheck(a.single);
            multiple = new FXCheckButton(counts, _("Multiple selection")); multiple->setCheck(a.multiple);
            new FXLabel(conditions, _("Every selected file must match a pattern. Selection counts apply to selected items.\n"
                                     "With Show without selection and Folders checked,\n"
                                     "%f/%F/%n/%N/%u/%U then refer to that folder; %d/%D remain the current directory.\n"
                                     "Unrestricted range: * (no additional range restriction)."),
                        NULL, JUSTIFY_LEFT);
        }
        else if (a.kind == CustomAction::SEPARATOR)
        {
            new FXLabel(body, _("Separators are displayed between visible entries, never at the edges."));
        }
        xf_scrollabledialog(this);
        xf_dialogcolors(this);
    }

    ~ActionProperties()
    {
        iconPreview->setIcon(NULL);
        delete previewIcon;
    }

    long handle(FXObject* sender, FXSelector sel, void* ptr)
    {
        if (FXSELTYPE(sel) == SEL_KEYPRESS && ptr && ((FXEvent*)ptr)->code == KEY_Escape)
            return handle(sender, FXSEL(SEL_COMMAND, ID_CANCEL), NULL);
        if (FXSELTYPE(sel) == SEL_CLOSE ||
            (FXSELTYPE(sel) == SEL_COMMAND && FXSELID(sel) == ID_CANCEL))
        {
            if (!(edited() == model.items[index]) && !discardChanges(this)) { return 1; }
            return FXDialogBox::handle(sender, FXSEL(SEL_COMMAND, ID_CANCEL), NULL);
        }
        if ((FXSELTYPE(sel) == SEL_CHANGED || FXSELTYPE(sel) == SEL_COMMAND) && FXSELID(sel) == ENTRY_ICON)
        { updateIcon(); return 1; }
        if (FXSELTYPE(sel) != SEL_COMMAND) { return FXDialogBox::handle(sender, sel, ptr); }
        switch (FXSELID(sel))
        {
        case ENTRY_SUGGEST: suggestIcon(); return 1;
        case ENTRY_TEST:
        {
            CustomAction action = edited();
            FXString error;
            if (CustomActions::check(action, previewContext, error))
                MessageBox::information(this, BOX_OK, _("Checks Passed"),
                    _("Checks passed: syntax, placeholders and working directory.\n"
                      "The action was not run. Application options and runtime success are not verified."));
            else
                MessageBox::error(this, BOX_OK, _("Action Check Failed"), "%s", error.text());
            return 1;
        }
        case ENTRY_INSERT:
        {
            const char* tokens[] = { "%f", "%F", "%n", "%N", "%d", "%D", "%u", "%U", "%p", "%o", "%%",
                                     "%c", "%w", "%W", "%x", "%X", "%a", "%r" };
            int choice = placeholders->getCurrentItem();
            if (choice < 0 || choice >= (int)(sizeof(tokens) / sizeof(tokens[0]))) { return 1; }
            FXString token = tokens[choice];
            FXTextField* destination = command;
            FXString value = destination->getText();
            int position = destination->getCursorPos();
            value.insert(position, token);
            destination->setText(value); destination->setCursorPos(position + token.length()); destination->setFocus();
            return 1;
        }
        case ENTRY_BROWSE:
        {
            ActionIconChooser dialog(this, icon->getText().trim());
            if (dialog.execute(PLACEMENT_OWNER)) { icon->setText(dialog.value()); updateIcon(); }
            return 1;
        }
        case ENTRY_COMMAND:
        {
            FileDialog dialog(this, _("Select a Program or Script"));
            dialog.setSelectMode(SELECTFILE_EXISTING);
            if (dialog.execute())
            {
                FXString path = xf_quote(dialog.getFilename()); path.substitute("%", "%%");
                command->setText(path);
                command->setCursorPos(path.length()); command->setFocus();
            }
            return 1;
        }
        case ENTRY_DIRECTORY:
        {
            FileDialog dialog(this, _("Select Working Directory"));
            dialog.setSelectMode(SELECTFILE_DIRECTORY);
            dialog.setDirectory(previewContext.directory);
            if (dialog.execute())
            {
                FXString path = dialog.getFilename(); path.substitute("%", "%%");
                workingDirectory->setText(path);
            }
            return 1;
        }
        case ENTRY_LEGEND:
        {
            FXDialogBox dialog(this, _("Parameter Legend"), DECOR_TITLE | DECOR_BORDER | DECOR_RESIZE | DECOR_CLOSE,
                               0, 0, 620, 420);
            FXHorizontalFrame* buttons = new FXHorizontalFrame(&dialog, LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
            new FXButton(buttons, _("Close"), NULL, &dialog, ID_CANCEL, BUTTON_NORMAL | LAYOUT_RIGHT);
            FXText* legend = new FXText(&dialog, NULL, 0, TEXT_READONLY | TEXT_WORDWRAP | LAYOUT_FILL_X | LAYOUT_FILL_Y);
            legend->setText(_(parameterLegend)); dialog.execute(PLACEMENT_OWNER); return 1;
        }
        case ID_ACCEPT:
        case ENTRY_SAVE:
        {
            CustomAction old = model.items[index];
            model.items[index] = edited();
            FXString error;
            if (!model.validate(error))
            {
                model.items[index] = old;
                MessageBox::error(this, BOX_OK, _("Invalid Custom Action"), "%s", error.text());
                return 1;
            }
            return FXDialogBox::handle(sender, FXSEL(SEL_COMMAND, ID_ACCEPT), NULL);
        }
        }
        return FXDialogBox::handle(sender, sel, ptr);
    }
};

class ActionTreeItem : public FXTreeItem
{
    FXString description;
    static const FXint ICON_SIZE = 16, TEXT_GAP = 6, DETAIL_GAP = 12;
public:
    void draw(const FXTreeList* list, FXDC& dc, FXint x, FXint y, FXint w, FXint h) const
    {
        dc.setForeground(isSelected() ? list->getSelBackColor() : list->getBackColor());
        dc.fillRectangle(x, y, w, h);
        FXIcon* image = isExpanded() ? getOpenIcon() : getClosedIcon();
        if (image) { dc.drawIcon(image, x + 2 + (ICON_SIZE - image->getWidth()) / 2, y + (h - image->getHeight()) / 2); }
        FXFont* font = list->getFont();
        const FXint textX = x + 2 + ICON_SIZE + TEXT_GAP, baseline = y + (h - font->getFontHeight()) / 2 + font->getFontAscent();
        dc.setFont(font);
        FXColor ink = isSelected() ? list->getSelTextColor() : list->getTextColor();
        dc.setForeground(ink);
        dc.drawText(textX, baseline, getText());
        if (!description.empty())
        {
            dc.setForeground(isSelected() ? ink : themeBlend(list->getBackColor(), ink, 0.62));
            dc.drawText(textX + font->getTextWidth(getText()) + DETAIL_GAP, baseline, description);
        }
        if (hasFocus()) { dc.drawFocusRectangle(x + 1, y + 1, w - 2, h - 2); }
    }
protected:
    FXint hitItem(const FXTreeList* list, FXint x, FXint y) const
    {
        return x >= 0 && y >= 0 && x < getWidth(list) && y < getHeight(list) ? 2 : 0;
    }
public:
    ActionTreeItem(const FXString& name, const FXString& detail, FXIcon* openImage, FXIcon* closedImage, unsigned n) :
        FXTreeItem(name, openImage, closedImage, (void*)(uintptr_t)(n + 1)), description(detail) {}
    FXint getHeight(const FXTreeList* list) const { return FXMAX(ICON_SIZE + 4, list->getFont()->getFontHeight() + 6); }
    FXint getWidth(const FXTreeList* list) const
    {
        FXFont* font = list->getFont();
        FXint width = 2 + ICON_SIZE + TEXT_GAP + font->getTextWidth(getText()) + 4;
        if (!description.empty()) { width += DETAIL_GAP + font->getTextWidth(description); }
        return width;
    }
};

// Solid connector lines and larger expand boxes; geometry matches FXTreeList so box clicks still hit
class ActionTree : public FXTreeList
{
    static const FXint HALF_BOX = 5;
public:
    ActionTree(FXComposite* parent, FXObject* target, FXSelector selector, FXuint options) :
        FXTreeList(parent, target, selector, options) {}
    long handle(FXObject* sender, FXSelector sel, void* ptr)
    {
        if (FXSELTYPE(sel) != SEL_PAINT || FXSELID(sel) != 0) { return FXTreeList::handle(sender, sel, ptr); }
        FXEvent* event = (FXEvent*)ptr;
        FXDCWindow dc(this, event);
        dc.setFont(font);
        FXint x = pos_x + 4 + indent, y = pos_y;
        for (FXTreeItem* item = firstitem; item && y < event->rect.y + event->rect.h;)
        {
            const FXint w = item->getWidth(this), h = item->getHeight(this);
            if (event->rect.y <= y + h)
            {
                dc.setForeground(backColor);
                dc.fillRectangle(0, y, width, h);
                static_cast<ActionTreeItem*>(item)->draw(this, dc, x, y, w, h);
                const FXint yh = y + h / 2, xh = x - indent + 2;
                const bool box = item->hasItems() || item->getFirst();
                dc.setForeground(lineColor);
                FXint xp = xh;
                for (FXTreeItem* p = item->getParent(); p; p = p->getParent())
                {
                    xp -= indent + p->getHeight(this) / 2;
                    if (p->getNext()) { dc.fillRectangle(xp, y, 1, h); }
                }
                const FXint gap = box ? HALF_BOX : 0;
                if (item->getPrev() || item->getParent()) { dc.fillRectangle(xh, y, 1, h / 2 - gap); }
                if (item->getNext()) { dc.fillRectangle(xh, yh + gap, 1, y + h - yh - gap); }
                dc.fillRectangle(xh + gap, yh, FXMAX(0, x - xh - gap), 1);
                if (box)
                {
                    dc.setForeground(backColor);
                    dc.fillRectangle(xh - HALF_BOX, yh - HALF_BOX, 2 * HALF_BOX, 2 * HALF_BOX);
                    dc.setForeground(lineColor);
                    dc.drawRectangle(xh - HALF_BOX, yh - HALF_BOX, 2 * HALF_BOX, 2 * HALF_BOX);
                    dc.setForeground(textColor);
                    dc.fillRectangle(xh - HALF_BOX + 3, yh, 2 * HALF_BOX - 5, 1);
                    if (!item->isExpanded()) { dc.fillRectangle(xh, yh - HALF_BOX + 3, 1, 2 * HALF_BOX - 5); }
                }
            }
            y += h;
            if (item->getFirst() && item->isExpanded())
            {
                x += indent + h / 2;
                item = item->getFirst();
                continue;
            }
            while (!item->getNext() && item->getParent())
            {
                item = item->getParent();
                x -= indent + item->getHeight(this) / 2;
            }
            item = item->getNext();
        }
        if (y < event->rect.y + event->rect.h)
        {
            dc.setForeground(backColor);
            dc.fillRectangle(event->rect.x, y, event->rect.w, event->rect.y + event->rect.h - y);
        }
        return 1;
    }
};

class ActionEditor : public FXDialogBox
{
    enum { ADD_ACTION = FXDialogBox::ID_LAST, ADD_FOLDER, ADD_SEPARATOR, EDIT, REMOVE, UP, DOWN, MOVE, DUPLICATE, SAVE };
    CustomActionsUI& actions;
    CustomActions working;
    CustomActionContext editorContext;
    FXTreeList* tree;
    std::vector<FXIcon*> itemIcons;
    std::set<FXString> collapsedFolders;
    void rememberCollapsed(FXTreeItem* item)
    {
        for (; item; item = item->getNext())
        {
            if (item->getFirst())
            {
                const FXString& id = working.items[(uintptr_t)item->getData() - 1].id;
                if (item->isExpanded()) { collapsedFolders.erase(id); }
                else { collapsedFolders.insert(id); }
                rememberCollapsed(item->getFirst());
            }
        }
    }
    FXString selectedId() const
    {
        FXTreeItem* item = tree->getCurrentItem();
        return item ? working.items[(uintptr_t)item->getData() - 1].id : FXString::null;
    }
    void populate(FXTreeItem* parent, const FXString& id, const FXString& select)
    {
        std::vector<unsigned> children = working.children(id);
        for (unsigned i = 0; i < children.size(); ++i)
        {
            unsigned n = children[i];
            const CustomAction& a = working.items[n];
            FXString text = a.kind == CustomAction::SEPARATOR ? _("--- Separator ---") : a.name;
            if (a.toolbar) { text += _("  [toolbar]"); }
            FXIcon* nodeIcon = loadActionIcon(getApp(), a.icon, 16);
            FXIcon* openIcon = nodeIcon;
            if (nodeIcon) { itemIcons.push_back(nodeIcon); }
            else if (a.kind == CustomAction::FOLDER) { nodeIcon = minifoldericon; openIcon = minifolderopenicon; }
            else { nodeIcon = openIcon = minirunicon; }
            FXString detail = a.description;
            detail.substitute("\n", " "); detail.substitute("\r", " "); detail.substitute("\t", " ");
            if (a.kind == CustomAction::FOLDER && detail.empty()) { detail = _("Cascading folder"); }
            FXTreeItem* item = tree->appendItem(parent, new ActionTreeItem(text, detail, openIcon, nodeIcon, n));
            populate(item, a.id, select);
            if (!collapsedFolders.count(a.id)) { tree->expandTree(item); }
            if (a.id == select) { tree->setCurrentItem(item); }
        }
    }
    void refresh(const FXString& select)
    {
        rememberCollapsed(tree->getFirstItem());
        tree->clearItems();
        for (unsigned i = 0; i < itemIcons.size(); ++i) { delete itemIcons[i]; }
        itemIcons.clear();
        populate(NULL, "", select);
        if (!tree->getCurrentItem() && tree->getFirstItem()) { tree->setCurrentItem(tree->getFirstItem()); }
        for (FXTreeItem* p = tree->getCurrentItem() ? tree->getCurrentItem()->getParent() : NULL; p; p = p->getParent())
        {
            collapsedFolders.erase(working.items[(uintptr_t)p->getData() - 1].id);
            tree->expandTree(p);
        }
        if (tree->getCurrentItem()) { tree->makeItemVisible(tree->getCurrentItem()); }
    }
    FXString folderPath(const CustomAction& a) const
    {
        FXString result = a.name, parent = a.parent;
        while (!parent.empty())
        {
            const CustomAction* p = working.find(parent);
            result = p->name + " / " + result; parent = p->parent;
        }
        return result;
    }
public:
    ActionEditor(FXWindow* owner, CustomActionsUI& ui) :
        FXDialogBox(owner, _("Custom Actions"), DECOR_TITLE | DECOR_BORDER | DECOR_RESIZE | DECOR_CLOSE,
                    0, 0, 780, 560), actions(ui), working(ui.model), editorContext(ui.context())
    {
        FXHorizontalFrame* buttons = new FXHorizontalFrame(this, LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
        new FXButton(buttons, _("Cancel"), NULL, this, ID_CANCEL, BUTTON_NORMAL | LAYOUT_RIGHT);
        new FXButton(buttons, _("Save"), NULL, this, SAVE, BUTTON_DEFAULT | BUTTON_INITIAL | LAYOUT_RIGHT);
        new FXLabel(this, _("Double-click an entry to edit. Checked folders expose all children on the toolbar;\n"
                           "checked descendants are not duplicated. Use Move to Folder to change nesting."),
                    NULL, JUSTIFY_LEFT | LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
        new FXLabel(this, _("Configure actions for the Custom Actions menu, file and background menus,\n"
                           "and the independent custom toolbar. Folders create cascading submenus."),
                    NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        FXHorizontalFrame* body = new FXHorizontalFrame(this, LAYOUT_FILL_X | LAYOUT_FILL_Y);
        tree = new ActionTree(body, this, EDIT, TREELIST_SHOWS_LINES | TREELIST_SHOWS_BOXES | TREELIST_ROOT_BOXES |
                              TREELIST_BROWSESELECT | LAYOUT_FILL_X | LAYOUT_FILL_Y | FRAME_SUNKEN);
        tree->setIndent(10);
        FXVerticalFrame* tools = new FXVerticalFrame(body, LAYOUT_FILL_Y | PACK_UNIFORM_WIDTH, 0, 0, 0, 0, 0, 0);
        const char* tips[] = { N_("Add Action..."), N_("Add Folder..."), N_("Add Separator"), N_("Edit..."), N_("Remove"),
                               N_("Move Up"), N_("Move Down"), N_("Move to Folder..."), N_("Duplicate Action") };
        FXIcon* images[] = { minirunicon, minifoldericon, miniaddicon, miniediticon, minideleteicon,
                             miniupicon, minidownicon, minifoldericon, miniaddicon };
        for (unsigned i = 0; i < sizeof(tips) / sizeof(tips[0]); ++i)
        {
            new FXButton(tools, _(tips[i]), images[i], this, ADD_ACTION + i,
                         BUTTON_NORMAL | ICON_BEFORE_TEXT | LAYOUT_FILL_X | LAYOUT_FIX_HEIGHT, 0, 0, 0, 34);
        }
        refresh("");
        xf_scrollabledialog(this);
        xf_dialogcolors(this);
        tree->setLineColor(DialogColors::scrollthumb);
    }
    ~ActionEditor()
    {
        tree->clearItems();
        for (unsigned i = 0; i < itemIcons.size(); ++i) { delete itemIcons[i]; }
    }
    long handle(FXObject* sender, FXSelector sel, void* ptr)
    {
        unsigned command = FXSELID(sel);
        if (FXSELTYPE(sel) == SEL_KEYPRESS && ptr && ((FXEvent*)ptr)->code == KEY_Escape)
            return handle(sender, FXSEL(SEL_COMMAND, ID_CANCEL), NULL);
        if (FXSELTYPE(sel) == SEL_CLOSE || (FXSELTYPE(sel) == SEL_COMMAND && command == ID_CANCEL))
        {
            if (working.items != actions.model.items && !discardChanges(this)) { return 1; }
            return FXDialogBox::handle(sender, FXSEL(SEL_COMMAND, ID_CANCEL), NULL);
        }
        if (sender == tree && FXSELTYPE(sel) != SEL_DOUBLECLICKED)
        { return FXDialogBox::handle(sender, sel, ptr); }
        if (FXSELTYPE(sel) != SEL_COMMAND && !(sender == tree && FXSELTYPE(sel) == SEL_DOUBLECLICKED))
        { return FXDialogBox::handle(sender, sel, ptr); }
        FXString selected = selectedId();
        const CustomAction* a = working.find(selected);
        if (command == DUPLICATE)
        {
            FXString error, id = working.duplicate(selected, _(" (copy)"), error);
            if (id.empty()) { MessageBox::error(this, BOX_OK, _("Cannot Duplicate Action"), "%s", error.text()); }
            else { refresh(id); }
            return 1;
        }
        if (command >= ADD_ACTION && command <= ADD_SEPARATOR)
        {
            if (working.items.size() >= CustomActions::MAX_ITEMS)
            { MessageBox::error(this, BOX_OK, _("Custom Actions"), _("At most 256 entries are supported.")); return 1; }
            CustomAction item;
            item.id = working.newId(); item.kind = command - ADD_ACTION;
            item.parent = a ? (a->kind == CustomAction::FOLDER ? a->id : a->parent) : FXString::null;
            if (item.kind == CustomAction::SEPARATOR) { item.name = _("Separator"); }
            working.items.push_back(item);
            if (item.kind == CustomAction::SEPARATOR ||
                ActionProperties(this, working, working.items.size() - 1, editorContext).execute(PLACEMENT_OWNER))
            { refresh(item.id); }
            else { working.items.pop_back(); }
            return 1;
        }
        if (command == EDIT && a)
        {
            unsigned n = (unsigned)(a - &working.items[0]);
            ActionProperties(this, working, n, editorContext).execute(PLACEMENT_OWNER); refresh(selected); return 1;
        }
        if (command == REMOVE && a)
        {
            if (MessageBox::question(this, BOX_OK_CANCEL, _("Remove Custom Entry"),
                _("Remove '%s' and all its children?\n\nWhen you save, its folder and any files in it are deleted."), a->name.text()) == BOX_CLICKED_OK)
            { working.remove(selected); refresh(""); }
            return 1;
        }
        if ((command == UP || command == DOWN) && a)
        { working.reorder(selected, command == UP ? -1 : 1); refresh(selected); return 1; }
        if (command == MOVE && a)
        {
            FXDialogBox dialog(this, _("Move to Folder"), DECOR_TITLE | DECOR_BORDER | DECOR_CLOSE);
            FXVerticalFrame* frame = new FXVerticalFrame(&dialog, LAYOUT_FILL_X);
            FXComboBox* folders = new FXComboBox(frame, 50, NULL, 0, COMBOBOX_STATIC | FRAME_SUNKEN | LAYOUT_FILL_X);
            std::vector<FXString> ids(1, FXString::null);
            folders->appendItem(_("(Top level)"));
            for (unsigned n = 0; n < working.items.size(); ++n)
            {
                if (working.items[n].kind != CustomAction::FOLDER) { continue; }
                folders->appendItem(folderPath(working.items[n])); ids.push_back(working.items[n].id);
            }
            folders->setNumVisible(12); folders->setCurrentItem(0);
            for (unsigned n = 0; n < ids.size(); ++n) { if (ids[n] == a->parent) { folders->setCurrentItem(n); } }
            FXHorizontalFrame* row = new FXHorizontalFrame(frame, LAYOUT_FILL_X);
            new FXButton(row, _("Cancel"), NULL, &dialog, ID_CANCEL, BUTTON_NORMAL | LAYOUT_RIGHT);
            new FXButton(row, _("Move"), NULL, &dialog, ID_ACCEPT, BUTTON_DEFAULT | BUTTON_INITIAL | LAYOUT_RIGHT);
            if (dialog.execute(PLACEMENT_OWNER))
            {
                FXString error;
                if (!working.move(selected, ids[folders->getCurrentItem()], error))
                { MessageBox::error(this, BOX_OK, _("Cannot Move Entry"), "%s", error.text()); }
                refresh(selected);
            }
            return 1;
        }
        if (command == SAVE || command == ID_ACCEPT)
        {
            FXString error;
            if (!working.validate(error))
            { MessageBox::error(this, BOX_OK, _("Invalid Custom Actions"), "%s", error.text()); return 1; }
            CustomActions saved = working;
            FXString warnings;
            if (!actions.saveStorage(saved, error, warnings))
            {
                MessageBox::error(this, BOX_OK, _("Cannot Save Custom Actions"),
                    _("Changes have not been applied.\n\n%s"), error.text());
                return 1;
            }
            if (!warnings.empty())
                showIconMessage(this, _("Custom Actions Saved with Warnings"), warnings, bigwarningicon);
            return FXDialogBox::handle(sender, FXSEL(SEL_COMMAND, ID_ACCEPT), NULL);
        }
        return FXDialogBox::handle(sender, sel, ptr);
    }
};

FXString label(const FXString& value)
{
    FXString text = value;
    text.substitute("&", "&&");
    text.substitute("\t", " "); text.substitute("\n", " "); text.substitute("\r", " ");
    return text;
}

bool hasAction(const CustomActions& model, const FXString& parent)
{
    for (unsigned n : model.children(parent))
    {
        const CustomAction& child = model.items[n];
        if (child.kind == CustomAction::ACTION ||
            (child.kind == CustomAction::FOLDER && hasAction(model, child.id)))
            return true;
    }
    return false;
}

bool readKnownActions(const FXString& root, std::set<std::string>& known, FXString& error)
{
    FXString path = root + "/.swordfish-known-actions";
    struct stat info;
    if (lstat(path.text(), &info) != 0)
    {
        if (errno == ENOENT) return true;
        error = path + ": " + strerror(errno);
        return false;
    }
    if (!S_ISREG(info.st_mode))
    {
        error = path + ": not a regular file";
        return false;
    }
    std::ifstream input(path.text());
    if (!input)
    {
        error = path + ": cannot read";
        return false;
    }
    std::string id;
    while (std::getline(input, id))
        if (!id.empty()) known.insert(id);
    if (input.bad())
    {
        error = path + ": read failed";
        return false;
    }
    return true;
}
}

CustomActionMenu::CustomActionMenu(CustomActionsUI* ui, bool dynamic, const CustomActionContext& ctx) :
    actions(ui), live(dynamic), snapshot(ctx)
{
    if (!live) ++actions->transientMenus;
}

CustomActionMenu::~CustomActionMenu()
{
    for (unsigned i = 0; i < panes.size(); ++i) { delete panes[i]; }
    if (!live) --actions->transientMenus;
}

bool CustomActionMenu::isShowing() const
{
    for (FXMenuPane* menu : panes)
        if (menu->shown()) return true;
    return false;
}

FXMenuPane* CustomActionMenu::pane(FXWindow* owner, FXuint opts)
{
    FXMenuPane* p = new FXMenuPane(owner, opts);
    panes.push_back(p); return p;
}

FXMenuCaption* CustomActionMenu::append(FXMenuPane* menu, unsigned n, bool filter)
{
    const CustomAction& action = actions->model.items[n];
    FXString text = label(action.name) + "\t\t" + label(action.description);
    if (action.kind == CustomAction::FOLDER)
    {
        FXMenuPane* sub = pane(menu);
        fill(sub, action.id, filter);
        FXMenuCascade* cascade = new FXMenuCascade(menu, text, actions->icon(n), sub);
        cascade->setTarget(this);
        cascade->setSelector(CustomActionsUI::ID_ACTION + n);
        return cascade;
    }
    return new FXMenuCommand(menu, text, actions->icon(n), this, CustomActionsUI::ID_ACTION + n);
}

void CustomActionMenu::fill(FXMenuPane* menu, const FXString& parent, bool filter)
{
    std::vector<unsigned> children = actions->model.children(parent);
    bool haveEntry = false, separator = false;
    for (unsigned c = 0; c < children.size(); ++c)
    {
        unsigned n = children[c];
        const CustomAction& a = actions->model.items[n];
        if (a.kind == CustomAction::SEPARATOR) { if (haveEntry) { separator = true; } continue; }
        if (a.kind == CustomAction::FOLDER && !hasAction(actions->model, a.id)) { continue; }
        if (filter && !actions->model.visible(n, snapshot)) { continue; }
        if (separator) { new FXMenuSeparator(menu); separator = false; }
        haveEntry = true;
        append(menu, n, filter);
    }
}

void CustomActionMenu::appendEditor(FXMenuPane* menu)
{
    if (menu->getFirst()) { new FXMenuSeparator(menu); }
    new FXMenuCommand(menu, _("Edit Custom Actions..."), minirunicon, actions, CustomActionsUI::ID_EDIT);
}

long CustomActionMenu::handle(FXObject* sender, FXSelector sel, void* ptr)
{
    if (FXSELTYPE(sel) == SEL_RIGHTBUTTONRELEASE) { return actions->toolbarButtonRightClick(sender, sel); }
    unsigned n = FXSELID(sel) - CustomActionsUI::ID_ACTION;
    if (n >= actions->model.items.size()) { return 0; }
    CustomActionContext ctx = live ? actions->context() : snapshot;
    if (FXSELTYPE(sel) == SEL_UPDATE)
    {
        sender->handle(this, FXSEL(SEL_COMMAND, actions->model.visible(n, ctx) ?
                       FXWindow::ID_ENABLE : FXWindow::ID_DISABLE), NULL);
        return 1;
    }
    if (FXSELTYPE(sel) == SEL_COMMAND) { actions->run(n, ctx); return 1; }
    return 0;
}

bool CustomActionsUI::writeKnownActions(const CustomActions& actions, FXString& error)
{
    FXString path = storageRoot + "/.swordfish-known-actions";
    struct stat existing;
    int result = lstat(path.text(), &existing);
    if (result == 0 && !S_ISREG(existing.st_mode))
    {
        error = path + ": not a regular file";
        return false;
    }
    if (result != 0 && errno != ENOENT)
    {
        error = path + ": " + strerror(errno);
        return false;
    }
    FXString temporary = path + "." + FXStringVal((FXint)getpid()) + ".tmp";
    int fd = open(temporary.text(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
    {
        error = temporary + ": " + strerror(errno);
        return false;
    }
    int failure = 0;
    for (const CustomAction& action : actions.items)
    {
        if (action.kind != CustomAction::ACTION) continue;
        FXString line = action.id + "\n";
        size_t offset = 0;
        while (offset < static_cast<size_t>(line.length()))
        {
            ssize_t count = write(fd, line.text() + offset, line.length() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { failure = count < 0 ? errno : EIO; break; }
            offset += static_cast<size_t>(count);
        }
        if (failure) break;
    }
    if (!failure && fsync(fd) != 0) failure = errno;
    if (close(fd) != 0 && !failure) failure = errno;
    if (failure)
    {
        unlink(temporary.text());
        error = temporary + ": " + strerror(failure);
        return false;
    }
    if (rename(temporary.text(), path.text()) != 0)
    {
        failure = errno;
        unlink(temporary.text());
        error = path + ": " + strerror(failure);
        return false;
    }
    return true;
}

bool CustomActionsUI::refreshStorage(bool notify)
{
    FXString settingsConflict;
    if (xf_portablesettingschanged(settingsConflict)) return false;

    FXString fingerprintError, fingerprint =
        CustomActions::directoryFingerprint(storageRoot, fingerprintError);
    if (!fingerprintError.empty())
    {
        if (lastStorageError != fingerprintError)
        {
            storageWarning = fingerprintError;
            lastStorageError = fingerprintError;
            owner->getApp()->addChore(this, ID_STORAGE_WARNING);
            appLog(APP_LOG_ERRORS, "custom-actions-fingerprint", fingerprintError);
        }
        return false;
    }
    if (!storageFingerprint.empty() && fingerprint == storageFingerprint) return true;

    CustomActions loaded;
    FXString error, warnings;
    if (!loaded.loadDirectory(storageRoot, error, warnings))
    {
        if (lastStorageError != error)
        {
            storageWarning = error;
            lastStorageError = error;
            owner->getApp()->addChore(this, ID_STORAGE_WARNING);
            appLog(APP_LOG_ERRORS, "custom-actions-load", error);
        }
        return false;
    }
    lastStorageError.clear();
    std::set<std::string> known;
    if (!readKnownActions(storageRoot, known, error))
    {
        appendIconError(warnings, error);
        appLog(APP_LOG_ERRORS, "custom-actions-known-list", error);
    }
    FXString added;
    if (notify)
    {
        for (const CustomAction& action : loaded.items)
        {
            if (action.kind != CustomAction::ACTION || known.count(action.id.text())) continue;
            if (!added.empty()) added += "\n";
            added += "- " + action.name;
        }
    }
    storageWarning = warnings;
    if (!added.empty())
    {
        newActionsWarning = added;
        FXString saveError;
        if (!writeKnownActions(loaded, saveError))
        {
            appendIconError(storageWarning, saveError);
            appLog(APP_LOG_ERRORS, "custom-actions-known-list", saveError);
        }
    }
    else if (!error.empty())
    {
        FXString saveError;
        if (!writeKnownActions(loaded, saveError))
            appendIconError(storageWarning, saveError);
    }
    bool changed = loaded.items != model.items;
    model = loaded;
    fingerprintError.clear();
    storageFingerprint = CustomActions::directoryFingerprint(storageRoot, fingerprintError);
    if (!fingerprintError.empty())
    {
        if (lastStorageError != fingerprintError)
        {
            storageWarning = fingerprintError;
            lastStorageError = fingerprintError;
            owner->getApp()->addChore(this, ID_STORAGE_WARNING);
            appLog(APP_LOG_ERRORS, "custom-actions-fingerprint", fingerprintError);
        }
    }
    if (changed) rebuild();
    if (!storageWarning.empty()) owner->getApp()->addChore(this, ID_STORAGE_WARNING);
    if (!newActionsWarning.empty()) owner->getApp()->addChore(this, ID_NEW_ACTIONS);
    return true;
}

bool CustomActionsUI::saveStorage(CustomActions& saved, FXString& error, FXString& warnings)
{
    FXString settingsConflict;
    if (xf_portablesettingschanged(settingsConflict))
    {
        error = _("Portable settings changed outside this window. Restart Swordfish before saving custom actions.") +
                FXString("\n\n") + settingsConflict;
        return false;
    }

    FXString currentFingerprint = CustomActions::directoryFingerprint(storageRoot, error);
    if (!error.empty()) return false;
    if (currentFingerprint != storageFingerprint)
    {
        error = _("Custom actions changed on disk while the editor was open. Your edits were not saved; close and reopen the editor to load the latest version.");
        return false;
    }
    if (!saved.saveDirectory(storageRoot, error, warnings, storageFingerprint)) return false;
    CustomActions reloaded;
    FXString loadWarnings;
    if (!reloaded.loadDirectory(storageRoot, error, loadWarnings))
    {
        error = _("Actions were written, but could not be reloaded: ") + error;
        return false;
    }
    FXString knownError;
    if (!writeKnownActions(reloaded, knownError))
    {
        appendIconError(warnings, knownError);
        appLog(APP_LOG_ERRORS, "custom-actions-known-list", knownError);
    }
    saved = reloaded;
    model = reloaded;
    FXString fingerprintError;
    storageFingerprint = CustomActions::directoryFingerprint(storageRoot, fingerprintError);
    if (!fingerprintError.empty())
    {
        error = fingerprintError;
        return false;
    }
    appendIconError(warnings, loadWarnings);
    rebuild();
    return true;
}

CustomActionsUI::CustomActionsUI(XFileExplorer* window, FXDockSite* dock, FXMenuPane* tools, FXMenuBar* bar) : owner(window)
{
    storageRoot = CustomActions::storageDirectory();
    if (xf_mkpath(storageRoot.text(), 0700) != 0)
    {
        storageWarning = _("Cannot create custom actions folder: ") + storageRoot + ": " + strerror(errno);
    }
    else
    {
        FXString error;
        struct stat marker;
        int markerStatus = lstat((storageRoot + "/.swordfish-storage").text(), &marker);
        bool markerAbsent = markerStatus != 0 && errno == ENOENT;
        bool initialized = markerStatus == 0 && S_ISREG(marker.st_mode);
        if (markerStatus == 0 && !initialized)
            appendIconError(storageWarning, storageRoot + "/.swordfish-storage: not a regular file");
        else if (markerStatus != 0 && !markerAbsent)
            appendIconError(storageWarning, storageRoot + "/.swordfish-storage: " + strerror(errno));
        if (!model.loadDirectory(storageRoot, error, storageWarning))
        {
            storageWarning = error;
            appLog(APP_LOG_ERRORS, "custom-actions-load", error);
        }
        else if (!initialized && markerAbsent && !xf_portablesettingschanged(error))
        {
            CustomActions legacy;
            FXString legacyError;
            if (!legacy.load(owner->getApp()->reg(), legacyError))
            {
                appendIconError(storageWarning,
                    _("Legacy custom actions were left untouched because they could not be loaded: ") + legacyError);
            }
            else
            {
                std::map<std::string, FXString> ids;
                for (CustomAction& action : legacy.items)
                {
                    FXString oldId = action.id;
                    ids[oldId.text()] = model.newId();
                }
                for (CustomAction& action : legacy.items)
                {
                    FXString oldId = action.id;
                    action.id = ids[oldId.text()];
                    if (!action.parent.empty()) action.parent = ids[action.parent.text()];
                    action.storagePath.clear();
                    if (action.icon.left(16) == "portable-action:")
                    {
                        FXString path = customActionIconPath(action.icon);
                        if (!path.empty()) action.icon = path;
                        else
                        {
                            appendIconError(storageWarning,
                                action.name + _(": its old portable icon could not be found and was omitted."));
                            action.icon.clear();
                        }
                    }
                    else if (!action.icon.empty() && action.icon.left(6) != "theme:" &&
                             !FXPath::isAbsolute(action.icon))
                        action.icon = FXString::null;
                    if (!action.icon.empty() && action.icon.left(6) != "theme:")
                    {
                        struct stat iconInfo;
                        if (lstat(action.icon.text(), &iconInfo) != 0 || !S_ISREG(iconInfo.st_mode))
                        {
                            appendIconError(storageWarning,
                                action.name + _(": its legacy icon is unavailable and was omitted."));
                            action.icon.clear();
                        }
                    }
                    model.items.push_back(action);
                }
                FXString saveError, saveWarnings;
                if (!model.saveDirectory(storageRoot, saveError, saveWarnings))
                {
                    FXString reloadError, reloadWarnings;
                    if (!model.loadDirectory(storageRoot, reloadError, reloadWarnings))
                        appLog(APP_LOG_ERRORS, "custom-actions-reload", reloadError);
                    appendIconError(storageWarning,
                        _("Legacy custom actions were not migrated; the original settings remain intact: ") +
                        saveError);
                    appLog(APP_LOG_ERRORS, "custom-actions-migration", saveError);
                }
                else
                {
                    CustomActions migrated;
                    FXString reloadError, reloadWarnings;
                    if (!migrated.loadDirectory(storageRoot, reloadError, reloadWarnings))
                    {
                        appendIconError(storageWarning, reloadError);
                        appLog(APP_LOG_ERRORS, "custom-actions-migration", reloadError);
                    }
                    else
                    {
                        model = migrated;
                        FXString knownError;
                        if (!writeKnownActions(model, knownError))
                        {
                            appendIconError(storageWarning, knownError);
                            appLog(APP_LOG_ERRORS, "custom-actions-known-list", knownError);
                        }
                    }
                    appendIconError(storageWarning, saveWarnings);
                    appendIconError(storageWarning, reloadWarnings);
                }
            }
        }
    }
    toolbarShown = owner->getApp()->reg().readUnsignedEntry("CUSTOM ACTIONS UI", "toolbar", 1) != 0;
    toolbar = new CustomActionToolBar(dock);
    toolbar->setTarget(this);
    toolbar->setSelector(ID_TOOLBAR_MENU);
    mainPane = new FXMenuPane(owner);
    mainTitle = new FXMenuTitle(bar, _("Custom &Actions"), NULL, mainPane);
    new FXMenuSeparator(tools);
    new FXMenuCommand(tools, _("Custom Actions..."), minirunicon, this, ID_EDIT);
    new FXMenuCheck(tools, _("Show Custom Toolbar"), this, ID_TOGGLE);
    rebuild();
    if (!storageRoot.empty())
    {
        FXString startupWarning = storageWarning;
        refreshStorage(true);
        appendIconError(storageWarning, startupWarning);
        if (!storageWarning.empty()) owner->getApp()->addChore(this, ID_STORAGE_WARNING);
    }
    owner->getApp()->addTimeout(this, ID_REFRESH, 1500);
    if (!loadError.empty()) { owner->getApp()->addChore(this, ID_LOAD_ERROR); }
    if (!iconWarning.empty()) { owner->getApp()->addChore(this, ID_ICON_WARNING); }
}

CustomActionsUI::~CustomActionsUI()
{
    owner->getApp()->removeChore(this, ID_LOAD_ERROR);
    owner->getApp()->removeChore(this, ID_REBUILD);
    owner->getApp()->removeChore(this, ID_ICON_WARNING);
    owner->getApp()->removeChore(this, ID_STORAGE_WARNING);
    owner->getApp()->removeChore(this, ID_NEW_ACTIONS);
    owner->getApp()->removeTimeout(this, ID_REFRESH);
    delete toolbar;
    delete toolbarMenu;
    delete mainTitle;
    delete mainPane;
    delete mainMenu;
    for (unsigned i = 0; i < icons.size(); ++i) { delete icons[i]; }
    for (unsigned i = 0; i < toolbarIcons.size(); ++i) { delete toolbarIcons[i]; }
}

FXIcon* CustomActionsUI::icon(unsigned n) const
{
    if (icons[n]) { return icons[n]; }
    return model.items[n].kind == CustomAction::FOLDER ? minifoldericon : minirunicon;
}

void CustomActionsUI::rebuild()
{
    CustomActionToolBar* actionToolbar = static_cast<CustomActionToolBar*>(toolbar);
    actionToolbar->reset();
    while (toolbar->getFirst()) { delete toolbar->getFirst(); }
    delete toolbarMenu; toolbarMenu = NULL;
    while (mainPane->getFirst()) { delete mainPane->getFirst(); }
    delete mainMenu; mainMenu = NULL;
    for (unsigned i = 0; i < icons.size(); ++i) { delete icons[i]; }
    icons.assign(model.items.size(), NULL);
    for (unsigned i = 0; i < toolbarIcons.size(); ++i) { delete toolbarIcons[i]; }
    toolbarIcons.clear();
    const FXint toolbarIconSize = xf_toolbariconsize(owner->getApp(), "customtoolbariconsize");
    const FXuint toolbarStyle = owner->getApp()->reg().readUnsignedEntry("SETTINGS", "customtoolbarstyle", 0);
    // Toolbar buttons get their own enlarged icons; menus keep the normal ones.
    auto toolbarIcon = [&](FXIcon* normal, const FXString& path) -> FXIcon*
    {
        if (toolbarIconSize <= 16) { return normal; }
        FXIcon* enlarged = NULL;
        if (!path.empty())
        {
            enlarged = loadActionIcon(owner->getApp(), path, minirunicon->getHeight() * toolbarIconSize / 16);
        }
        if (!enlarged) { enlarged = xf_toolbaricon(owner->getApp(), normal, toolbarIconSize); }
        if (!enlarged) { return normal; }
        toolbarIcons.push_back(enlarged);
        return enlarged;
    };
    std::vector<FXString> themeNames;
    for (unsigned n = 0; n < model.items.size(); ++n)
    {
        const FXString& path = model.items[n].icon;
        if (path.left(6) == "theme:") { themeNames.push_back(path.mid(6, path.length() - 6)); }
    }
    prefetchSystemActionIcons(owner->getApp(), themeNames);
    for (unsigned n = 0; n < model.items.size(); ++n)
    {
        const FXString& path = model.items[n].icon;
        icons[n] = loadActionIcon(owner->getApp(), path, minirunicon->getHeight());
    }
    mainMenu = new CustomActionMenu(this, true, CustomActionContext());
    mainMenu->fill(mainPane, "", false);
    mainMenu->appendEditor(mainPane);
    toolbarMenu = new CustomActionMenu(this, true, CustomActionContext());
    FXMenuPane* overflowMenu = toolbarMenu->pane(owner, POPUP_SHRINKWRAP);
    std::vector<unsigned> nodes = model.toolbarItems();
    bool haveEntry = false, separator = false;
    FXButton* configure = new FXButton(toolbar, _("\tEdit Custom Actions"), toolbarIcon(miniprefsicon, ""),
                                      this, ID_EDIT, BUTTON_TOOLBAR | LAYOUT_RIGHT);
    FXMenuButton* overflow = new FXMenuButton(toolbar, ">>", NULL, overflowMenu,
                                             MENUBUTTON_DOWN | MENUBUTTON_NOARROWS | BUTTON_TOOLBAR | LAYOUT_RIGHT);
    overflow->setTipText(_("More Custom Actions"));
    overflow->setTarget(this);
    overflow->setSelector(ID_TOOLBAR_MENU);
    actionToolbar->setControls(configure, overflow);
    for (unsigned i = 0; i < nodes.size(); ++i)
    {
        unsigned n = nodes[i];
        const CustomAction& a = model.items[n];
        if (a.kind == CustomAction::SEPARATOR) { if (haveEntry) { separator = true; } continue; }
        if (a.kind == CustomAction::FOLDER && !hasAction(model, a.id)) { continue; }
        if (separator)
        {
            FXFrame* divider = new FXFrame(toolbar, FRAME_SUNKEN | LAYOUT_CENTER_Y |
                                          LAYOUT_FIX_WIDTH | LAYOUT_FIX_HEIGHT, 0, 0, 2, toolbarIconSize + 6);
            actionToolbar->append(divider, new FXMenuSeparator(overflowMenu), true);
            separator = false;
        }
        haveEntry = true;
        FXMenuCaption* overflowItem = toolbarMenu->append(overflowMenu, n, false);
        FXString toolbarLabel = label(a.name);
        FXString text = (toolbarStyle == 1 ? FXString() : toolbarLabel) + "\t" + label(a.description);
        FXIcon* buttonIcon = toolbarStyle == 2 ? NULL : toolbarIcon(icon(n), icons[n] ? a.icon : FXString());
        if (a.kind == CustomAction::FOLDER)
        {
            FXMenuPane* menu = toolbarMenu->pane(owner);
            toolbarMenu->fill(menu, a.id, false);
            FXMenuButton* button = new FXMenuButton(toolbar, text, buttonIcon, menu,
                                  MENUBUTTON_DOWN | BUTTON_TOOLBAR | ICON_BEFORE_TEXT);
            button->setTarget(toolbarMenu); button->setSelector(ID_ACTION + n);
            button->setTipText(toolbarLabel);
            actionToolbar->append(button, overflowItem);
        }
        else
        {
            FXButton* button = new FXButton(toolbar, text, buttonIcon, toolbarMenu, ID_ACTION + n,
                                            BUTTON_TOOLBAR | ICON_BEFORE_TEXT);
            button->setTipText(toolbarLabel);
            actionToolbar->append(button, overflowItem);
        }
    }
    toolbar->recalc();
    if (toolbarShown) { toolbar->show(); } else { toolbar->hide(); }
    if (owner->id())
    {
        xf_windowcolors(toolbar);
        toolbar->create();
        mainPane->create();
        for (FXWindow* w = toolbar->getFirst(); w; w = w->getNext())
        {
            FXMenuButton* b = dynamic_cast<FXMenuButton*>(w);
            if (b && b->getMenu()) { b->getMenu()->create(); }
        }
        owner->recalc();
    }
}

CustomActionContext CustomActionsUI::context(FilePanel* panel) const
{
    CustomActionContext ctx;
    if (!panel) { panel = owner->getCurrentPanel(); }
    ctx.directory = panel->getDirectory();
    ctx.local = !RemoteDirectory::isServer(ctx.directory);
    FilePanel* other = panel == owner->getLeftPanel() ? owner->getRightPanel() : owner->getLeftPanel();
    FXString opposite = other->getDirectory();
    if (!RemoteDirectory::isServer(opposite)) { ctx.opposite = opposite; }
    FileList* list = panel->getList();
    for (int i = 0; i < list->getNumItems(); ++i)
    {
        if (!list->isItemSelected(i) || list->getItemFilename(i) == "..") { continue; }
        ctx.paths.push_back(list->getItemPathname(i));
        ctx.directories.push_back(list->isItemDirectory(i));
    }
    return ctx;
}

CustomActionMenu* CustomActionsUI::appendContextMenu(FXMenuPane* menu, FilePanel* panel)
{
    CustomActionContext ctx = context(panel);
    CustomActionMenu* presentation = new CustomActionMenu(this, false, ctx);
    FXMenuPane* sub = presentation->pane(owner);
    presentation->fill(sub, "", true);
    presentation->appendEditor(sub);
    new FXMenuSeparator(menu);
    new FXMenuCascade(menu, _("Custom Actions"), minirunicon, sub);
    return presentation;
}

long CustomActionsUI::handle(FXObject* sender, FXSelector sel, void* ptr)
{
    if (FXSELTYPE(sel) == SEL_TIMEOUT && FXSELID(sel) == ID_REFRESH)
    {
        bool menuOpen = (mainPane && mainPane->shown()) || transientMenus ||
                        (mainMenu && mainMenu->isShowing()) ||
                        (toolbarMenu && toolbarMenu->isShowing());
        if (!editorOpen && !menuOpen) refreshStorage(true);
        owner->getApp()->addTimeout(this, ID_REFRESH, 1500);
        return 1;
    }
    if (FXSELTYPE(sel) == SEL_UPDATE && FXSELID(sel) == ID_TOGGLE)
    {
        sender->handle(this, FXSEL(SEL_COMMAND, toolbarShown ? FXWindow::ID_CHECK : FXWindow::ID_UNCHECK), NULL);
        return 1;
    }
    if (FXSELTYPE(sel) == SEL_CHORE && FXSELID(sel) == ID_LOAD_ERROR)
    {
        MessageBox::error(owner, BOX_OK, _("Custom Actions Not Loaded"),
            _("%s\n\nThe saved entries have been left untouched. Saving in the custom action editor replaces them."),
            loadError.text());
        return 1;
    }
    if (FXSELTYPE(sel) == SEL_CHORE && FXSELID(sel) == ID_ICON_WARNING)
    {
        showIconWarning(owner, iconWarning);
        return 1;
    }
    if (FXSELTYPE(sel) == SEL_CHORE && FXSELID(sel) == ID_STORAGE_WARNING)
    {
        if (!storageWarning.empty())
            MessageBox::warning(owner, BOX_OK, _("Custom Actions Need Attention"), "%s", storageWarning.text());
        storageWarning.clear();
        return 1;
    }
    if (FXSELTYPE(sel) == SEL_CHORE && FXSELID(sel) == ID_NEW_ACTIONS)
    {
        if (!newActionsWarning.empty())
            MessageBox::information(owner, BOX_OK, _("New Custom Actions"),
                _("These action folders were found and are now available:\n\n%s"),
                newActionsWarning.text());
        newActionsWarning.clear();
        return 1;
    }
    if (FXSELTYPE(sel) == SEL_CHORE && FXSELID(sel) == ID_REBUILD)
    {
        rebuild();
        return 1;
    }
    if (FXSELTYPE(sel) == SEL_RIGHTBUTTONRELEASE)
    {
        if (FXSELID(sel) == ID_TOOLBAR_MENU || FXSELID(sel) == ID_EDIT)
        {
            showToolbarMenu();
            return 1;
        }
        return 0;
    }
    if (FXSELTYPE(sel) == SEL_UPDATE && FXSELID(sel) >= ID_STYLE_BOTH && FXSELID(sel) <= ID_SIZE_32)
    {
        FXuint id = FXSELID(sel);
        FXbool checked = id <= ID_STYLE_TITLES ?
            owner->getApp()->reg().readUnsignedEntry("SETTINGS", "customtoolbarstyle", 0) == id - ID_STYLE_BOTH :
            xf_toolbariconsize(owner->getApp(), "customtoolbariconsize") == (FXint)(16 + (id - ID_SIZE_16) * 8);
        sender->handle(this, FXSEL(SEL_COMMAND, checked ? FXWindow::ID_CHECK : FXWindow::ID_UNCHECK), NULL);
        return 1;
    }
    if (FXSELTYPE(sel) != SEL_COMMAND) { return 0; }
    if (FXSELID(sel) >= ID_STYLE_BOTH && FXSELID(sel) <= ID_SIZE_32)
    {
        FXuint id = FXSELID(sel);
        if (id <= ID_STYLE_TITLES)
        {
            owner->getApp()->reg().writeUnsignedEntry("SETTINGS", "customtoolbarstyle", id - ID_STYLE_BOTH);
        }
        else
        {
            owner->getApp()->reg().writeIntEntry("SETTINGS", "customtoolbariconsize", 16 + (id - ID_SIZE_16) * 8);
        }
        owner->getApp()->reg().write();
        // The clicked button may be one being rebuilt, so rebuild after its event finishes
        owner->getApp()->addChore(this, ID_REBUILD);
        return 1;
    }
    if (FXSELID(sel) == ID_EDIT)
    {
        refreshStorage(true);
        editorOpen = true;
        if (!loadError.empty() && MessageBox::question(owner, BOX_OK_CANCEL, _("Invalid Saved Custom Actions"),
            _("The saved custom actions are invalid and were not loaded.\nSaving this editor will replace them. Continue?")) != BOX_CLICKED_OK)
        {
            editorOpen = false;
            return 1;
        }
        ActionEditor editor(owner, *this);
        if (editor.execute(PLACEMENT_OWNER)) { loadError.clear(); rebuild(); }
        editorOpen = false;
        return 1;
    }
    if (FXSELID(sel) == ID_TOGGLE)
    {
        toolbarShown = !toolbarShown;
        owner->getApp()->reg().writeUnsignedEntry("CUSTOM ACTIONS UI", "toolbar", toolbarShown);
        if (toolbarShown) { toolbar->show(); } else { toolbar->hide(); }
        owner->recalc();
        return 1;
    }
    return 0;
}

void CustomActionsUI::showToolbarMenu()
{
    FXMenuPane menu(owner);
    new FXMenuRadio(&menu, _("Icons and Titles"), this, ID_STYLE_BOTH);
    new FXMenuRadio(&menu, _("Icons Only"), this, ID_STYLE_ICONS);
    new FXMenuRadio(&menu, _("Titles Only"), this, ID_STYLE_TITLES);
    new FXMenuSeparator(&menu);
    new FXMenuRadio(&menu, _("Small Icons (16 px)"), this, ID_SIZE_16);
    new FXMenuRadio(&menu, _("Medium Icons (24 px)"), this, ID_SIZE_24);
    new FXMenuRadio(&menu, _("Large Icons (32 px)"), this, ID_SIZE_32);
    new FXMenuSeparator(&menu);
    new FXMenuCommand(&menu, _("Edit Custom Actions..."), minirunicon, this, ID_EDIT);
    new FXMenuCheck(&menu, _("Show Custom Toolbar"), this, ID_TOGGLE);
    FXint x, y;
    FXuint state;
    owner->getRoot()->getCursorPosition(x, y, state);
    xf_windowcolors(&menu);
    menu.create();
    menu.popup(NULL, x, y);
    owner->getApp()->runModalWhileShown(&menu);
}

long CustomActionsUI::toolbarButtonRightClick(FXObject* sender, FXSelector sel)
{
    FXWindow* window = dynamic_cast<FXWindow*>(sender);
    if (FXSELTYPE(sel) != SEL_RIGHTBUTTONRELEASE || !window || window->getParent() != toolbar) { return 0; }
    showToolbarMenu();
    return 1;
}

void CustomActionsUI::run(unsigned n, const CustomActionContext& ctx)
{
    if (n >= model.items.size()) { return; }
    CustomAction a = model.items[n];
    if (!model.matches(a, ctx)) { return; }
    FXString command, directory, error;
    if (!CustomActions::prepare(a, ctx, command, directory, error))
    { MessageBox::error(owner, BOX_OK, _("Cannot Run Custom Action"), "%s", error.text()); return; }
    if (a.confirm && MessageBox::question(owner, BOX_OK_CANCEL, _("Confirm Custom Action"),
        _("%s\n\nDirectory: %s\nSelected items: %u\n\n%s"), a.name.text(), directory.text(),
        (unsigned)ctx.paths.size(), command.text()) != BOX_CLICKED_OK) { return; }
    FXString script = "cd " + xf_quote(directory) + " && /bin/sh -c " + xf_quote(command) +
        "; result=$?; printf '\\nCustom action exit status: %s\\n' \"$result\";";
    if (a.terminal) { script += " printf 'Press Enter to close...'; read reply;"; }
    script += " exit \"$result\"";
    if (a.terminal)
    {
        FXString terminal = xf_terminalprogram(owner->getApp());
        if (terminal.empty() || !xf_existcommand(terminal))
        {
            MessageBox::error(owner, BOX_OK, _("Terminal Unavailable"),
                              _("Choose an available terminal in Preferences before running this action."));
            return;
        }
        command = xf_terminalcommand(terminal, directory, script);
    }
    else { command = "/bin/sh -c " + xf_quote(script) + " </dev/null"; }
    appLog(APP_LOG_UI, "custom-action", "id=" + a.id + " name=" + a.name);
    CommandWindow* output = new CommandWindow(owner, _("Custom Action: ") + a.name,
                                               xf_hostcommand(command), 18, 85, !a.showOutput);
    output->create();
}
