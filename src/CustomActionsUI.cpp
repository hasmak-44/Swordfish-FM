#include "config.h"
#include "i18n.h"
#include "CustomActionsUI.h"
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
#include <stdint.h>
#include <fxkeys.h>
#include <algorithm>

namespace
{
enum { ENTRY_SAVE = FXDialogBox::ID_LAST, ENTRY_BROWSE, ENTRY_INSERT, ENTRY_COMMAND, ENTRY_ICON,
       ENTRY_CHANGED, ENTRY_MODE, ENTRY_PROGRAM, ENTRY_DIRECTORY, ENTRY_LEGEND, ENTRY_TEST, ENTRY_SUGGEST };

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
    "With Folders enabled and no selection, file/name/URI/extension parameters\n"
    "refer to the current folder. %d/%D still mean the current directory; %c remains 0.\n"
    "Without Folders, file parameters require selected files; %p and %d remain usable.\n\n"
    "Working directory accepts %p, %o, %d, %f and %%; it is one literal path,\n"
    "not shell code. Leave it empty to use the active pane directory.\n"
    "Program is one executable name or path; do not add arguments or shell quotes.\n"
    "Use Parameters for arguments. Full command mode retains legacy shell syntax.\n"
    "Use an external script for nested shell substitutions or here-documents.");

FXIcon* loadActionIcon(FXApp* app, const FXString& path, FXint size)
{
    return loadCustomActionIcon(app, path, size);
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
        if (current.empty() || current.left(6) == "theme:") { setDirectory(FXSystem::getHomeDirectory()); }
        else { setFilename(current); }
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
    enum { SEARCH = FXDialogBox::ID_LAST, MODE, PREVIOUS, NEXT, LOAD, SELECT, ACCEPT };
    enum { PAGE_SIZE = 40 };
    enum { ALL_ICONS, FIRST_CATEGORY, IMAGE_FILES = FIRST_CATEGORY + SystemActionIcon::UNCATEGORIZED + 1,
           NO_ICON };
    const std::vector<SystemActionIcon>& catalog;
    std::vector<unsigned> matches;
    std::vector<FXIcon*> images;
    FXTextField* search;
    FXComboBox* mode;
    FXIconList* list;
    FXLabel* status;
    FXButton *previous, *next;
    unsigned page = 0, loading = 0;
    FXString selection;
    int lastMode = ALL_ICONS;

    void clearImages()
    {
        getApp()->removeTimeout(this, LOAD);
        list->clearItems();
        for (size_t i = 0; i < images.size(); ++i) { delete images[i]; }
        images.clear();
    }
    void populate()
    {
        clearImages();
        unsigned end = FXMIN((unsigned)matches.size(), page + PAGE_SIZE);
        for (unsigned i = page; i < end; ++i)
        {
            const SystemActionIcon& item = catalog[matches[i]];
            list->appendItem(item.name, minirunicon, minirunicon);
            if (selection == "theme:" + item.name) { list->setCurrentItem(i - page); list->selectItem(i - page); }
        }
        images.resize(end - page, NULL);
        status->setText(matches.empty() ? _("No matching icons") :
            FXStringVal((FXint)page + 1) + "–" + FXStringVal((FXint)end) + " / " + FXStringVal((FXint)matches.size()));
        if (page > 0) { previous->enable(); } else { previous->disable(); }
        if (end < matches.size()) { next->enable(); } else { next->disable(); }
        loading = 0;
        if (end > page) { getApp()->addTimeout(this, LOAD, 10); }
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
        page = 0; populate();
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
        new FXLabel(body, _("Search installed host icons:"), NULL, JUSTIFY_LEFT);
        search = new FXTextField(body, 35, this, SEARCH, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        list = new FXIconList(body, this, SELECT,
            ICONLIST_BROWSESELECT | ICONLIST_BIG_ICONS | ICONLIST_COLUMNS |
            LAYOUT_FILL_X | LAYOUT_FILL_Y | FRAME_SUNKEN);
        list->setItemSpace(120);
        FXHorizontalFrame* pages = new FXHorizontalFrame(body, LAYOUT_FILL_X);
        previous = new FXButton(pages, _("Previous"), NULL, this, PREVIOUS, BUTTON_NORMAL);
        next = new FXButton(pages, _("Next"), NULL, this, NEXT, BUTTON_NORMAL);
        status = new FXLabel(pages, "", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        new FXLabel(body, _("System icons are stored by name and follow the host theme.\n"
                            "Image files keep their absolute path. SVG requires rsvg-convert."),
                    NULL, JUSTIFY_LEFT);
        filter();
    }
    ~ActionIconChooser() { clearImages(); }
    const FXString& value() const { return selection; }
    long handle(FXObject* sender, FXSelector sel, void* ptr)
    {
        FXuint id = FXSELID(sel), type = FXSELTYPE(sel);
        if ((type == SEL_CHANGED || type == SEL_COMMAND) && id == SEARCH) { filter(); return 1; }
        if (type == SEL_TIMEOUT && id == LOAD)
        {
            if (loading < images.size())
            {
                unsigned row = loading++;
                images[row] = loadActionIcon(getApp(), catalog[matches[page + row]].path, 48);
                if (images[row]) { list->setItemBigIcon(row, images[row]); }
                if (loading < images.size()) { getApp()->addTimeout(this, LOAD, 10); }
            }
            return 1;
        }
        if (type == SEL_DOUBLECLICKED && id == SELECT)
            return handle(sender, FXSEL(SEL_COMMAND, ACCEPT), ptr);
        if (type != SEL_COMMAND) { return FXDialogBox::handle(sender, sel, ptr); }
        switch (id)
        {
        case MODE:
            if (mode->getCurrentItem() == IMAGE_FILES) { browseImage(); return 1; }
            if (mode->getCurrentItem() == NO_ICON)
            {
                list->disable(); search->disable(); previous->disable(); next->disable();
            }
            else
            {
                list->enable(); search->enable(); filter();
            }
            lastMode = mode->getCurrentItem();
            return 1;
        case PREVIOUS: if (page >= PAGE_SIZE) { page -= PAGE_SIZE; populate(); } return 1;
        case NEXT: if (page + PAGE_SIZE < matches.size()) { page += PAGE_SIZE; populate(); } return 1;
        case ACCEPT:
            if (mode->getCurrentItem() == NO_ICON) { selection.clear(); }
            else
            {
                int row = list->getCurrentItem();
                if (row < 0 || page + row >= matches.size()) { return 1; }
                FXString value = "theme:" + catalog[matches[page + row]].name;
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
    FXTextField *program = NULL, *parameters = NULL, *workingDirectory = NULL;
    FXCheckButton* legacyMode = NULL;
    FXText* preview = NULL;
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
            a.command = command->getText(); a.patterns = patterns->getText().trim();
            a.programMode = !legacyMode->getCheck();
            a.program = program->getText(); a.parameters = parameters->getText();
            a.workingDirectory = workingDirectory->getText();
            a.files = files->getCheck(); a.folders = folders->getCheck();
            a.single = single->getCheck(); a.multiple = multiple->getCheck(); a.background = background->getCheck();
            a.terminal = terminal->getCheck(); a.confirm = confirm->getCheck(); a.showOutput = showOutput->getCheck();
        }
        return a;
    }
    void suggestIcon()
    {
        if (!program || program->getText().trim().empty()) { return; }
        FXString executable = program->getText().trim();
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
    void updatePreview()
    {
        if (!preview) { return; }
        bool legacy = legacyMode->getCheck();
        if (legacy) { command->enable(); program->disable(); parameters->disable(); }
        else { command->disable(); program->enable(); parameters->enable(); }
        FXWindow* programRow = program->getParent();
        FXWindow* commandRow = command->getParent();
        if (legacy)
        {
            programRow->hide(); programRow->getPrev()->hide();
            parameters->hide(); parameters->getPrev()->hide();
            commandRow->show(); commandRow->getPrev()->show();
        }
        else
        {
            programRow->show(); programRow->getPrev()->show();
            parameters->show(); parameters->getPrev()->show();
            commandRow->hide(); commandRow->getPrev()->hide();
        }
        programRow->getParent()->recalc();
        recalc();
        CustomAction action = model.items[index];
        action.programMode = !legacy;
        action.program = program->getText(); action.parameters = parameters->getText();
        action.command = command->getText(); action.workingDirectory = workingDirectory->getText();
        if (folders) { action.folders = folders->getCheck(); }
        FXString expanded, directory, error;
        if (CustomActions::prepare(action, previewContext, expanded, directory, error, false))
        { preview->setText(_("Directory: ") + directory + "\n" + expanded); }
        else { preview->setText(_("Preview unavailable: ") + error); }
    }
    void updateIcon()
    {
        FXIcon* replacement = loadActionIcon(getApp(), icon->getText().trim(), 48);
        iconPreview->setIcon(replacement ? replacement : minirunicon);
        icon->setTipText(!icon->getText().trim().empty() && !replacement ?
            _("Icon unavailable on this host. Choose another icon or No Icon.") :
            _("theme:NAME for an installed host icon, or an absolute image filename."));
        delete previewIcon; previewIcon = replacement;
    }
public:
    ActionProperties(FXWindow* owner, CustomActions& actions, unsigned n, const CustomActionContext& ctx) :
        FXDialogBox(owner, _("Custom Action Properties"),
                    DECOR_TITLE | DECOR_BORDER | DECOR_RESIZE | DECOR_CLOSE, 0, 0, 720, 660),
        model(actions), index(n), previewContext(ctx)
    {
        const CustomAction& a = model.items[n];
        FXHorizontalFrame* buttons = new FXHorizontalFrame(this, LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
        new FXButton(buttons, _("Cancel"), NULL, this, ID_CANCEL, BUTTON_NORMAL | LAYOUT_RIGHT);
        new FXButton(buttons, _("Apply"), NULL, this, ENTRY_SAVE, BUTTON_DEFAULT | BUTTON_INITIAL | LAYOUT_RIGHT);
        FXTabBook* tabs = new FXTabBook(this, NULL, 0, LAYOUT_FILL_X | LAYOUT_FILL_Y);
        new FXTabItem(tabs, _("Basic"));
        FXScrollWindow* scroll = new FXScrollWindow(tabs, LAYOUT_FILL_X | LAYOUT_FILL_Y | FRAME_THICK | FRAME_RAISED);
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
            new FXLabel(form, _("Program:"), NULL, JUSTIFY_LEFT);
            FXHorizontalFrame* programrow = new FXHorizontalFrame(form, LAYOUT_FILL_X, 0, 0, 0, 0, 0, 0, 0, 0);
            program = new FXTextField(programrow, 24, this, ENTRY_CHANGED, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
            program->setText(a.program);
            program->setTipText(_("One bare PATH executable or full executable path, without shell quotes or arguments."));
            new FXButton(programrow, _("Browse...\tChoose an executable"), minifiledialogicon, this, ENTRY_PROGRAM, BUTTON_NORMAL);
            new FXLabel(form, _("Parameters:"), NULL, JUSTIFY_LEFT);
            parameters = new FXTextField(form, 24, this, ENTRY_CHANGED, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
            parameters->setText(a.parameters);
            new FXLabel(form, _("Full command:"), NULL, JUSTIFY_LEFT);
            FXHorizontalFrame* commandrow = new FXHorizontalFrame(form, LAYOUT_FILL_X, 0, 0, 0, 0, 0, 0, 0, 0);
            command = new FXTextField(commandrow, 24, this, ENTRY_CHANGED, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
            command->setText(a.command);
            new FXButton(commandrow, _("Browse...\tChoose a program or script (replaces the command)"),
                         minifiledialogicon, this, ENTRY_COMMAND, BUTTON_NORMAL);
            new FXLabel(form, _("Working directory:"), NULL, JUSTIFY_LEFT);
            FXHorizontalFrame* directoryrow = new FXHorizontalFrame(form, LAYOUT_FILL_X, 0, 0, 0, 0, 0, 0, 0, 0);
            workingDirectory = new FXTextField(directoryrow, 24, this, ENTRY_CHANGED, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
            workingDirectory->setText(a.workingDirectory);
            workingDirectory->setTipText(_("Empty uses the active folder. One literal path; %p, %o, %d, %f and %% are supported."));
            new FXButton(directoryrow, _("Browse...\tChoose a working directory"), minifoldericon, this, ENTRY_DIRECTORY, BUTTON_NORMAL);
            legacyMode = new FXCheckButton(body, _("Advanced: full shell command"), this, ENTRY_MODE);
            legacyMode->setCheck(!a.programMode);
            new FXLabel(body, _("Enter the executable in Program and its arguments in Parameters.\n"
                               "Advanced mode keeps an existing full command unchanged."),
                        NULL, JUSTIFY_LEFT);
            new FXLabel(body, _("Command preview (active pane context; does not execute):"), NULL, JUSTIFY_LEFT);
            preview = new FXText(body, NULL, 0, TEXT_READONLY | FRAME_SUNKEN | LAYOUT_FILL_X);
            preview->setVisibleRows(3); preview->setVisibleColumns(60);
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
        toolbar = new FXCheckButton(body, _("Show on custom toolbar"));
        toolbar->setCheck(a.toolbar);
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
            placeholders = new FXComboBox(row, 45, NULL, 0, COMBOBOX_STATIC | FRAME_SUNKEN | LAYOUT_FILL_X);
            const char* labels[] = {
                N_("%f  First selected path"), N_("%F  All selected paths"), N_("%n  First selected filename"),
                N_("%N  All selected filenames"), N_("%d  First selected parent directory"),
                N_("%D  All selected parent directories"), N_("%u  First selected file URI"), N_("%U  All selected file URIs"),
                N_("%p  Current pane directory"), N_("%o  Opposite pane directory"), N_("%%  Literal percent"),
                N_("%c  Selection count"), N_("%w  First name without extension"), N_("%W  All names without extension"),
                N_("%x  First extension"), N_("%X  All extensions")
            };
            for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) { placeholders->appendItem(_(labels[i])); }
            placeholders->setNumVisible(11);
            placeholders->setCurrentItem(0);
            new FXButton(row, _("Insert"), NULL, this, ENTRY_INSERT, BUTTON_NORMAL);
            new FXButton(row, _("Parameter Legend..."), NULL, this, ENTRY_LEGEND, BUTTON_NORMAL);
            new FXButton(row, _("Test Action"), NULL, this, ENTRY_TEST, BUTTON_NORMAL);
            new FXLabel(help, _("Test checks syntax, placeholders, program and directory without running the action.\n"
                                 "It cannot verify an application's supported options or guarantee runtime success."),
                        NULL, JUSTIFY_LEFT);
            new FXLabel(help, _("%f / %F  First / all selected paths    %n / %N  First / all filenames\n"
                               "%d / %D  Selected parent directories    %u / %U  Selected file URIs\n"
                               "%p  Current directory    %o  Opposite pane directory    %%  Literal percent\n\n"
                               "Filenames are quoted automatically. Use %F/%N/%D/%U as separate,\n"
                               "unquoted arguments. Use a script for complex shell substitutions."),
                        NULL, JUSTIFY_LEFT);
            new FXTabItem(tabs, _("Appearance Conditions"));
            FXScrollWindow* appearance = new FXScrollWindow(tabs, LAYOUT_FILL_X | LAYOUT_FILL_Y | FRAME_THICK | FRAME_RAISED);
            FXVerticalFrame* conditions = new FXVerticalFrame(appearance, LAYOUT_FILL_X | LAYOUT_FILL_Y);
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
            folders->setTarget(this); folders->setSelector(ENTRY_CHANGED);
            FXHorizontalFrame* counts = new FXHorizontalFrame(conditions, LAYOUT_FILL_X);
            single = new FXCheckButton(counts, _("Single selection")); single->setCheck(a.single);
            multiple = new FXCheckButton(counts, _("Multiple selection")); multiple->setCheck(a.multiple);
            new FXLabel(conditions, _("Every selected file must match a pattern. Selection counts apply to selected items.\n"
                                     "Folders also enables the current folder when nothing is selected:\n"
                                     "%f/%F/%n/%N/%u/%U then refer to that folder; %d/%D remain the current directory.\n"
                                     "Unrestricted range: * (no additional range restriction)."),
                        NULL, JUSTIFY_LEFT);
        }
        else if (a.kind == CustomAction::SEPARATOR)
        {
            new FXLabel(body, _("Separators are displayed between visible entries, never at the edges."));
        }
        updatePreview();
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
        if ((FXSELTYPE(sel) == SEL_CHANGED || FXSELTYPE(sel) == SEL_COMMAND) &&
            (FXSELID(sel) == ENTRY_CHANGED || FXSELID(sel) == ENTRY_MODE))
        { updatePreview(); return 1; }
        if ((FXSELTYPE(sel) == SEL_CHANGED || FXSELTYPE(sel) == SEL_COMMAND) && FXSELID(sel) == ENTRY_ICON)
        { updateIcon(); return 1; }
        if (FXSELTYPE(sel) != SEL_COMMAND) { return FXDialogBox::handle(sender, sel, ptr); }
        switch (FXSELID(sel))
        {
        case ENTRY_SUGGEST: suggestIcon(); return 1;
        case ENTRY_TEST:
        {
            CustomAction action = model.items[index];
            action.programMode = !legacyMode->getCheck();
            action.program = program->getText(); action.parameters = parameters->getText();
            action.command = command->getText(); action.workingDirectory = workingDirectory->getText();
            action.folders = folders->getCheck();
            FXString error;
            if (CustomActions::check(action, previewContext, error))
                MessageBox::information(this, BOX_OK, _("Checks Passed"),
                    _("Checks passed: syntax, placeholders, program (Program mode), and working directory.\n"
                      "The action was not run. Application options and runtime success are not verified."));
            else
                MessageBox::error(this, BOX_OK, _("Action Check Failed"), "%s", error.text());
            return 1;
        }
        case ENTRY_INSERT:
        {
            const char* tokens[] = { "%f", "%F", "%n", "%N", "%d", "%D", "%u", "%U", "%p", "%o", "%%",
                                     "%c", "%w", "%W", "%x", "%X" };
            int choice = placeholders->getCurrentItem();
            if (choice < 0 || choice >= (int)(sizeof(tokens) / sizeof(tokens[0]))) { return 1; }
            FXString token = tokens[choice];
            FXTextField* destination = legacyMode->getCheck() ? command : parameters;
            FXString value = destination->getText();
            int position = destination->getCursorPos();
            value.insert(position, token);
            destination->setText(value); destination->setCursorPos(position + token.length()); destination->setFocus();
            updatePreview();
            return 1;
        }
        case ENTRY_BROWSE:
        {
            ActionIconChooser dialog(this, icon->getText().trim());
            if (dialog.execute(PLACEMENT_OWNER)) { icon->setText(dialog.value()); updateIcon(); }
            return 1;
        }
        case ENTRY_COMMAND:
        case ENTRY_PROGRAM:
        {
            FileDialog dialog(this, _("Select a Program or Script"));
            dialog.setSelectMode(SELECTFILE_EXISTING);
            if (dialog.execute())
            {
                FXTextField* field = FXSELID(sel) == ENTRY_PROGRAM ? program : command;
                field->setText(FXSELID(sel) == ENTRY_PROGRAM ? dialog.getFilename() : xf_quote(dialog.getFilename()));
                legacyMode->setCheck(FXSELID(sel) != ENTRY_PROGRAM);
                field->setCursorPos(field->getText().length()); field->setFocus(); updatePreview();
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
                workingDirectory->setText(path); updatePreview();
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
protected:
    void draw(const FXTreeList* list, FXDC& dc, FXint x, FXint y, FXint w, FXint h) const
    {
        dc.setForeground(isSelected() ? list->getSelBackColor() : list->getBackColor());
        dc.fillRectangle(x, y, w, h);
        FXIcon* image = isExpanded() ? getOpenIcon() : getClosedIcon();
        const FXint textX = x + 40;
        if (image) { dc.drawIcon(image, x + 4, y + (h - image->getHeight()) / 2); }
        FXFont* font = list->getFont();
        dc.setFont(font);
        dc.setForeground(isSelected() ? list->getSelTextColor() : list->getTextColor());
        dc.drawText(textX, y + 4 + font->getFontAscent(), getText());
        dc.drawText(textX, y + 5 + font->getFontHeight() + font->getFontAscent(), description);
        if (hasFocus()) { dc.drawFocusRectangle(x + 1, y + 1, w - 2, h - 2); }
    }
    FXint hitItem(const FXTreeList* list, FXint x, FXint y) const
    {
        return x >= 0 && y >= 0 && x < getWidth(list) && y < getHeight(list) ? 2 : 0;
    }
public:
    ActionTreeItem(const FXString& name, const FXString& detail, FXIcon* image, unsigned n) :
        FXTreeItem(name, image, image, (void*)(uintptr_t)(n + 1)), description(detail) {}
    FXint getHeight(const FXTreeList* list) const { return FXMAX(42, list->getFont()->getFontHeight() * 2 + 10); }
    FXint getWidth(const FXTreeList* list) const
    {
        FXFont* font = list->getFont();
        return 48 + FXMAX(font->getTextWidth(getText()), font->getTextWidth(description));
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
            FXIcon* nodeIcon = loadActionIcon(getApp(), a.icon, 32);
            if (nodeIcon) { itemIcons.push_back(nodeIcon); }
            else { nodeIcon = a.kind == CustomAction::FOLDER ? minifoldericon : minirunicon; }
            FXString detail = a.description;
            detail.substitute("\n", " "); detail.substitute("\r", " "); detail.substitute("\t", " ");
            if (a.kind == CustomAction::FOLDER && detail.empty()) { detail = _("Cascading folder"); }
            FXTreeItem* item = tree->appendItem(parent, new ActionTreeItem(text, detail, nodeIcon, n));
            populate(item, a.id, select);
            tree->expandTree(item);
            if (a.id == select) { tree->setCurrentItem(item); }
        }
    }
    void refresh(const FXString& select)
    {
        tree->clearItems();
        for (unsigned i = 0; i < itemIcons.size(); ++i) { delete itemIcons[i]; }
        itemIcons.clear();
        populate(NULL, "", select);
        if (!tree->getCurrentItem() && tree->getFirstItem()) { tree->setCurrentItem(tree->getFirstItem()); }
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
        tree = new FXTreeList(body, this, EDIT, TREELIST_SHOWS_LINES | TREELIST_SHOWS_BOXES |
                             TREELIST_BROWSESELECT | LAYOUT_FILL_X | LAYOUT_FILL_Y | FRAME_SUNKEN);
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
            if (item.kind == CustomAction::ACTION) { item.programMode = true; }
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
                _("Remove '%s' and all its children?"), a->name.text()) == BOX_CLICKED_OK)
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
            FXSettings previous(getApp()->reg());
            working.save(getApp()->reg());
            if (!getApp()->reg().write())
            {
                static_cast<FXSettings&>(getApp()->reg()) = previous;
                MessageBox::error(this, BOX_OK, _("Cannot Save Custom Actions"),
                                  _("The personal Swordfish settings could not be written. Changes have not been applied."));
                return 1;
            }
            actions.model = working;
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
}

CustomActionMenu::CustomActionMenu(CustomActionsUI* ui, bool dynamic, const CustomActionContext& ctx) :
    actions(ui), live(dynamic), snapshot(ctx) {}

CustomActionMenu::~CustomActionMenu()
{
    for (unsigned i = 0; i < panes.size(); ++i) { delete panes[i]; }
}

FXMenuPane* CustomActionMenu::pane(FXWindow* owner)
{
    FXMenuPane* p = new FXMenuPane(owner);
    panes.push_back(p); return p;
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
        if (filter && !actions->model.visible(n, snapshot)) { continue; }
        if (separator) { new FXMenuSeparator(menu); separator = false; }
        haveEntry = true;
        FXString text = label(a.name) + "\t\t" + label(a.description);
        if (a.kind == CustomAction::FOLDER)
        {
            FXMenuPane* sub = pane(menu);
            fill(sub, a.id, filter);
            FXMenuCascade* cascade = new FXMenuCascade(menu, text, actions->icon(n), sub);
            cascade->setTarget(this); cascade->setSelector(CustomActionsUI::ID_ACTION + n);
        }
        else { new FXMenuCommand(menu, text, actions->icon(n), this, CustomActionsUI::ID_ACTION + n); }
    }
}

void CustomActionMenu::appendEditor(FXMenuPane* menu)
{
    if (menu->getFirst()) { new FXMenuSeparator(menu); }
    new FXMenuCommand(menu, _("Edit Custom Actions..."), minirunicon, actions, CustomActionsUI::ID_EDIT);
}

long CustomActionMenu::handle(FXObject* sender, FXSelector sel, void* ptr)
{
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

CustomActionsUI::CustomActionsUI(XFileExplorer* window, FXDockSite* dock, FXMenuPane* tools, FXMenuBar* bar) : owner(window)
{
    model.load(owner->getApp()->reg(), loadError);
    toolbarShown = owner->getApp()->reg().readUnsignedEntry("CUSTOM ACTIONS UI", "toolbar", 1) != 0;
    toolbar = new FXToolBar(dock, FRAME_NONE | LAYOUT_SIDE_TOP | LAYOUT_DOCK_NEXT | LAYOUT_FILL_X);
    mainPane = new FXMenuPane(owner);
    mainTitle = new FXMenuTitle(bar, _("Custom &Actions"), NULL, mainPane);
    new FXMenuSeparator(tools);
    new FXMenuCommand(tools, _("Custom Actions..."), minirunicon, this, ID_EDIT);
    new FXMenuCheck(tools, _("Show Custom Toolbar"), this, ID_TOGGLE);
    rebuild();
    if (!loadError.empty()) { owner->getApp()->addChore(this, ID_LOAD_ERROR); }
}

CustomActionsUI::~CustomActionsUI()
{
    owner->getApp()->removeChore(this, ID_LOAD_ERROR);
    delete toolbar;
    delete toolbarMenu;
    delete mainTitle;
    delete mainPane;
    delete mainMenu;
    for (unsigned i = 0; i < icons.size(); ++i) { delete icons[i]; }
}

FXIcon* CustomActionsUI::icon(unsigned n) const
{
    if (icons[n]) { return icons[n]; }
    return model.items[n].kind == CustomAction::FOLDER ? minifoldericon : minirunicon;
}

void CustomActionsUI::rebuild()
{
    while (toolbar->getFirst()) { delete toolbar->getFirst(); }
    delete toolbarMenu; toolbarMenu = NULL;
    while (mainPane->getFirst()) { delete mainPane->getFirst(); }
    delete mainMenu; mainMenu = NULL;
    for (unsigned i = 0; i < icons.size(); ++i) { delete icons[i]; }
    icons.assign(model.items.size(), NULL);
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
    std::vector<unsigned> nodes = model.toolbarItems();
    bool haveEntry = false, separator = false;
    new FXButton(toolbar, _("\tEdit Custom Actions"), miniprefsicon, this, ID_EDIT,
                 BUTTON_TOOLBAR | LAYOUT_RIGHT);
    for (unsigned i = 0; i < nodes.size(); ++i)
    {
        unsigned n = nodes[i];
        const CustomAction& a = model.items[n];
        if (a.kind == CustomAction::SEPARATOR) { if (haveEntry) { separator = true; } continue; }
        if (separator)
        {
            new FXFrame(toolbar, FRAME_SUNKEN | LAYOUT_CENTER_Y |
                        LAYOUT_FIX_WIDTH | LAYOUT_FIX_HEIGHT, 0, 0, 2, 22);
            separator = false;
        }
        haveEntry = true;
        FXString text = label(a.name) + "\t" + label(a.description);
        FXString toolbarLabel = label(a.name);
        FXIcon* toolbarIcon = icon(n);
        if (a.kind == CustomAction::FOLDER)
        {
            FXMenuPane* menu = toolbarMenu->pane(owner);
            toolbarMenu->fill(menu, a.id, false);
            FXMenuButton* button = new FXMenuButton(toolbar, text, toolbarIcon, menu,
                                  MENUBUTTON_DOWN | BUTTON_TOOLBAR | ICON_BEFORE_TEXT);
            button->setTarget(toolbarMenu); button->setSelector(ID_ACTION + n);
            button->setTipText(toolbarLabel);
        }
        else
        {
            FXButton* button = new FXButton(toolbar, text, toolbarIcon, toolbarMenu, ID_ACTION + n,
                                            BUTTON_TOOLBAR | ICON_BEFORE_TEXT);
            button->setTipText(toolbarLabel);
        }
    }
    if (toolbarShown) { toolbar->show(); } else { toolbar->hide(); }
    if (owner->id())
    {
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
    if (FXSELTYPE(sel) != SEL_COMMAND) { return 0; }
    if (FXSELID(sel) == ID_EDIT)
    {
        if (!loadError.empty() && MessageBox::question(owner, BOX_OK_CANCEL, _("Invalid Saved Custom Actions"),
            _("The saved custom actions are invalid and were not loaded.\nSaving this editor will replace them. Continue?")) != BOX_CLICKED_OK)
        { return 1; }
        ActionEditor editor(owner, *this);
        if (editor.execute(PLACEMENT_OWNER)) { loadError.clear(); rebuild(); }
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
