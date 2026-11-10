#include "config.h"
#include <fx.h>
#include <fxkeys.h>
#include <FXPNGIcon.h>
#include <FXJPGIcon.h>
#include "SystemIcons.h"
#include "xfeutils.h"
#include "CustomActionsUI.h"
#include "XFileExplorer.h"
#include "FileList.h"
#include "CommandWindow.h"
#include "Preferences.h"
#include "DirHistBox.h"
#include "FileDialog.h"
#include "icons.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

extern FXMainWindow* mainWindow;
extern FXString execpath, homedir, xdgdatahome, xdgconfighome;

template<typename T> static void collect(FXWindow* window, std::vector<T*>& result)
{
    if (T* match = dynamic_cast<T*>(window)) { result.push_back(match); }
    for (FXWindow* child = window->getFirst(); child; child = child->getNext()) { collect(child, result); }
}

template<typename T> static std::vector<T*> widgets(FXWindow* window)
{
    std::vector<T*> result; collect(window, result); return result;
}

static void pump(FXApp& app, int milliseconds)
{
    FXTime until = FXThread::time() + milliseconds * 1000000LL;
    while (FXThread::time() < until) { if (!app.runOneEvent(false)) { usleep(1000); } }
}

static void mouseClick(FXWindow* window, int button, int x = 2, int y = 2)
{
    FXString command = "xdotool mousemove --window " + FXStringVal((FXulong)window->id()) +
                       " " + FXStringVal(x) + " " + FXStringVal(y);
    assert(system(command.text()) == 0);
    // Let the window manager process pointer-enter focus before pressing.
    pump(*window->getApp(), 100);
    command = "xdotool click " + FXStringVal(button);
    assert(system(command.text()) == 0);
}

static FXTopWindow* dialog(FXApp& app, const char* title)
{
    std::vector<FXTopWindow*> windows = widgets<FXTopWindow>(app.getRootWindow());
    for (unsigned i = 0; i < windows.size(); ++i)
    { if (windows[i]->shown() && windows[i]->getTitle() == title) { return windows[i]; } }
    return NULL;
}

static void click(FXWindow* owner, const char* text)
{
    FXString wanted = text;
    wanted.substitute("&", "");
    std::vector<FXButton*> buttons = widgets<FXButton>(owner);
    for (unsigned i = 0; i < buttons.size(); ++i)
    {
        FXString actual = buttons[i]->getText(); actual.substitute("&", "");
        FXString tip = buttons[i]->getTipText(); tip.substitute("&", "");
        if (actual != wanted && tip != wanted) { continue; }
        buttons[i]->getTarget()->handle(buttons[i], FXSEL(SEL_COMMAND, buttons[i]->getSelector()), NULL);
        return;
    }
    fprintf(stderr, "Missing button: %s\n", text); assert(false);
}

class LegendAutomation : public FXObject
{
    FXApp& app;
public:
    explicit LegendAutomation(FXApp& application) : app(application) {}
    long handle(FXObject*, FXSelector, void*)
    {
        FXTopWindow* window = dialog(app, "Parameter Legend"); assert(window);
        std::vector<FXText*> texts = widgets<FXText>(window); assert(texts.size() == 1);
        assert(texts[0]->getText().find("%w") >= 0 && texts[0]->getText().find(".hidden") >= 0);
        assert(texts[0]->getContentHeight() > texts[0]->getViewportHeight());
        texts[0]->setPosition(0, -texts[0]->getContentHeight());
        assert(texts[0]->getYPosition() < 0);
        click(window, "Close"); return 1;
    }
};

class BrowseAutomation : public FXObject
{
    FXApp& app;
    const char* title;
    FXString path;
public:
    BrowseAutomation(FXApp& application, const char* caption, const FXString& filename) :
        app(application), title(caption), path(filename) {}
    long handle(FXObject*, FXSelector, void*)
    {
        FileDialog* chooser = dynamic_cast<FileDialog*>(dialog(app, title)); assert(chooser);
        chooser->setFilename(path);
        chooser->handle(NULL, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
        return 1;
    }
};

class CheckAutomation : public FXObject
{
    FXApp& app;
    const char* title;
public:
    CheckAutomation(FXApp& application, const char* caption) : app(application), title(caption) {}
    long handle(FXObject*, FXSelector, void*)
    {
        FXTopWindow* result = dialog(app, title); assert(result);
        click(result, "OK");
        return 1;
    }
};

class ImageChooserAutomation : public FXObject
{
    FXApp& app;
    FXString filename;
    FXString path;
    bool accept;
public:
    ImageChooserAutomation(FXApp& application, const FXString& image, bool choose) :
        app(application), filename(FXPath::name(image)), path(image), accept(choose) {}
    long handle(FXObject*, FXSelector, void*)
    {
        FileDialog* chooser = dynamic_cast<FileDialog*>(dialog(app, "Select an Icon Image File"));
        assert(chooser && chooser->getSelectMode() == SELECT_FILE_EXISTING);
        assert(chooser->getDecorations() & DECOR_MAXIMIZE);
        pump(app, 1000);
        chooser->setDirectory(FXPath::directory(path));
        pump(app, 100);
        std::vector<FileList*> lists = widgets<FileList>(chooser); assert(lists.size() == 1);
        FileList* list = lists[0];
        for (const char* name : {"large.png", "wide.jpg", "small.xpm", "wide.svg"})
        {
            bool present = false;
            for (int i = 0; i < list->getNumItems(); ++i)
                if (list->getItemFilename(i) == name) { present = true; }
            assert(present);
        }
        int row = -1;
        for (int i = 0; i < list->getNumItems(); ++i)
        {
            if (list->getItemFilename(i) == filename) { row = i; break; }
        }
        assert(row >= 0);
        int otherRow = row == 0 ? 1 : 0;
        list->setCurrentItem(otherRow, true);
        list->selectItem(otherRow, true);
        list->makeItemVisible(row);
        pump(app, 100);
        bool clicked = false;
        for (int y = 8; y < list->getHeight() && !clicked; y += 4)
            for (int x = 8; x < list->getWidth() && !clicked; x += 8)
                if (list->getItemAt(x, y) == row && list->hitItem(row, x, y))
                {
                    mouseClick(list, 1, x, y);
                    clicked = true;
                }
        assert(clicked);
        pump(app, 200);
        assert(chooser->getFilename() == path);
        pump(app, 200);
        FXLabel* preview = NULL;
        for (FXLabel* label : widgets<FXLabel>(chooser))
        {
            FXIcon* icon = label->getIcon();
            if (icon && label->getTarget() == chooser)
            {
                assert(icon->getWidth() <= 128 && icon->getHeight() <= 128);
                assert(icon->getWidth() == icon->getHeight() * 2);
                int previewX, previewY, listX, listY;
                label->translateCoordinatesTo(previewX, previewY, chooser, 0, 0);
                list->translateCoordinatesTo(listX, listY, chooser, 0, 0);
                assert(previewX + label->getWidth() <= listX);
                assert(!preview);
                preview = label;
            }
        }
        assert(preview);
        chooser->setFilename(FXPath::directory(path) + "/not-image.png");
        pump(app, 200);
        assert(!preview->getIcon() && preview->getText().contains("Preview unavailable"));
        CheckAutomation invalid(app, "Cannot Load Icon");
        app.addTimeout(&invalid, 1, 10);
        click(chooser, "OK");
        assert(chooser->shown());
        chooser->setCurrentPattern(1);
        assert(chooser->getPattern() == "*");
        chooser->setDirectory(FXSystem::getHomeDirectory());
        pump(app, 200);
        assert(!preview->getIcon());
        chooser->setFilename(path);
        pump(app, 200);
        assert(chooser->getFilename() == path);
        assert(preview->getIcon());
        if (FXString(getenv("SWORDFISH_TEST_WM")) == "1")
        {
            chooser->maximize();
            pump(app, 200);
            assert(chooser->isMaximized());
            chooser->restore();
            pump(app, 200);
        }
        click(chooser, accept ? "OK" : "Cancel");
        return 1;
    }
};

class IconChooserAutomation : public FXObject
{
    FXApp& app;
    FXString image;
    int route;
public:
    enum { CANCEL, SYSTEM, NONE, IMAGE, IMAGE_CANCEL, MISSING };
    IconChooserAutomation(FXApp& application, int choice, const FXString& filename) :
        app(application), image(filename), route(choice) {}
    long handle(FXObject*, FXSelector, void*)
    {
        FXTopWindow* chooser = dialog(app, "Choose an Icon"); assert(chooser);
        std::vector<FXComboBox*> modes = widgets<FXComboBox>(chooser);
        assert(modes.size() == 1 && modes[0]->getNumItems() == 15);
        assert(chooser->getDecorations() & DECOR_MAXIMIZE);
        int imageMode = modes[0]->findItem("Image Files");
        int noIconMode = modes[0]->findItem("No Icon");
        assert(imageMode >= 0 && noIconMode >= 0);
        if (route == CANCEL) { click(chooser, "Cancel"); return 1; }
        if (route == NONE)
        {
            modes[0]->setCurrentItem(noIconMode);
            modes[0]->getTarget()->handle(modes[0], FXSEL(SEL_COMMAND, modes[0]->getSelector()), NULL);
            click(chooser, "Select"); return 1;
        }
        if (route == IMAGE || route == IMAGE_CANCEL)
        {
            if (route == IMAGE_CANCEL)
            {
                modes[0]->setCurrentItem(noIconMode);
                modes[0]->getTarget()->handle(modes[0], FXSEL(SEL_COMMAND, modes[0]->getSelector()), NULL);
            }
            ImageChooserAutomation imageChooser(app, image, route == IMAGE);
            app.addTimeout(&imageChooser, 1, 10);
            modes[0]->setCurrentItem(imageMode);
            modes[0]->getTarget()->handle(modes[0], FXSEL(SEL_COMMAND, modes[0]->getSelector()), NULL);
            if (route == IMAGE_CANCEL)
            {
                assert(chooser->shown() && modes[0]->getCurrentItem() == noIconMode);
                assert(!widgets<FXIconList>(chooser)[0]->isEnabled());
                modes[0]->setCurrentItem(0);
                modes[0]->getTarget()->handle(modes[0], FXSEL(SEL_COMMAND, modes[0]->getSelector()), NULL);
                assert(widgets<FXIconList>(chooser)[0]->isEnabled());
                click(chooser, "Cancel");
            }
            else
            {
                assert(!chooser->shown());
                assert(!dialog(app, "Select an Icon Image File"));
                assert(dialog(app, "Custom Action Properties"));
            }
            return 1;
        }
        FXTextField* search = NULL;
        // FXComboBox also owns a text field; the search field targets the dialog.
        for (FXTextField* field : widgets<FXTextField>(chooser))
            if (field->getTarget() == chooser) { assert(!search); search = field; }
        assert(search);
        std::vector<FXIconList*> grids = widgets<FXIconList>(chooser);
        assert(grids.size() == 1);
        FXIconList* list = grids[0];
        assert(list->getListStyle() & ICONLIST_BIG_ICONS);
        if (FXString(getenv("SWORDFISH_TEST_WM")) == "1")
        {
            chooser->maximize();
            pump(app, 200);
            assert(chooser->isMaximized());
            chooser->restore();
        }
        for (const char* category : {"Action Icons", "Application Icons", "Device Icons",
                                     "Uncategorized Icons"})
        {
            int mode = modes[0]->findItem(category);
            assert(mode >= 0);
            modes[0]->setCurrentItem(mode);
            modes[0]->getTarget()->handle(modes[0], FXSEL(SEL_COMMAND, modes[0]->getSelector()), NULL);
            search->setText("swordfish-fixture");
            search->getTarget()->handle(search, FXSEL(SEL_CHANGED, search->getSelector()), NULL);
            assert(list->getNumItems() > 0);
            const std::vector<SystemActionIcon>& catalog = systemActionIcons(&app);
            for (int row = 0; row < list->getNumItems(); ++row)
            {
                bool found = false;
                for (const SystemActionIcon& icon : catalog)
                    if (icon.name == list->getItemText(row))
                    {
                        assert((int)icon.category == mode - 1);
                        found = true; break;
                    }
                assert(found);
            }
        }
        modes[0]->setCurrentItem(0);
        modes[0]->getTarget()->handle(modes[0], FXSEL(SEL_COMMAND, modes[0]->getSelector()), NULL);
        search->setText("swordfish-fixture");
        search->getTarget()->handle(search, FXSEL(SEL_CHANGED, search->getSelector()), NULL);
        assert(list->getNumItems() == 40);
        chooser->resize(600, 540);
        pump(app, 100);
        int narrowRows = list->getNumRows();
        chooser->resize(1100, 700);
        pump(app, 100);
        assert(list->getNumRows() < narrowRows);
        click(chooser, "Next"); assert(list->getNumItems() > 0 && list->getNumItems() <= 40);
        click(chooser, "Previous"); assert(list->getNumItems() == 40);
        search->setText("no-such-installed-icon-729103");
        search->getTarget()->handle(search, FXSEL(SEL_CHANGED, search->getSelector()), NULL);
        assert(list->getNumItems() == 0);
        search->setText(route == MISSING ? "swordfish-fixture-missing" : "SWORDFISH-FIXTURE-APPLICATION");
        search->getTarget()->handle(search, FXSEL(SEL_CHANGED, search->getSelector()), NULL);
        assert(list->getNumItems() == 1);
        list->setCurrentItem(0); list->selectItem(0);
        if (route == MISSING)
        {
            CheckAutomation error(app, "Cannot Load Icon");
            app.addTimeout(&error, 1, 10); click(chooser, "Select");
            assert(chooser->shown()); click(chooser, "Cancel");
        }
        else
        {
            pump(app, 60);
            FXIcon* icon = list->getItemBigIcon(0);
            assert(icon && icon != minirunicon && icon->getWidth() <= 48 && icon->getHeight() <= 48);
            assert(icon->getWidth() == icon->getHeight() * 2);
            click(chooser, "Select");
        }
        return 1;
    }
};

class EditorAutomation : public FXObject
{
    FXApp& app;
    int step = 0;
    FXString iconPath;
public:
    EditorAutomation(FXApp& application, const FXString& image) : app(application), iconPath(image) {}
    long handle(FXObject*, FXSelector, void*)
    {
        int current = step++;
        bool opensProperties = current == 0 || current == 2 || current == 5;
        if (opensProperties) { app.addTimeout(this, 1, 30); }
        FXTopWindow* editor = dialog(app, "Custom Actions");
        FXTopWindow* properties = dialog(app, "Custom Action Properties");
        if (current == 0) { assert(editor); click(editor, "Add Folder..."); }
        else if (current == 1)
        {
            assert(properties);
            widgets<FXTextField>(properties)[0]->setText("Added folder");
            FXTextField* icon = widgets<FXTextField>(properties)[2];
            icon->setText(iconPath);
            icon->getTarget()->handle(icon, FXSEL(SEL_CHANGED, icon->getSelector()), NULL);
            widgets<FXCheckButton>(properties)[0]->setCheck(true);
            properties->handle(NULL, FXSEL(SEL_COMMAND, FXDialogBox::ID_ACCEPT), NULL);
        }
        else if (current == 2) { assert(editor); click(editor, "Add Action..."); }
        else if (current == 3)
        {
            assert(properties);
            std::vector<FXTextField*> fields = widgets<FXTextField>(properties);
            // name, description, command, working directory, icon, file pattern
            assert(fields.size() >= 6);
            fields[0]->setText("Added action"); fields[1]->setText("Editor regression action");
            fields[4]->setText(iconPath);
            fields[4]->getTarget()->handle(fields[4], FXSEL(SEL_CHANGED, fields[4]->getSelector()), NULL);
            bool preview = false, browseCommand = false, browseDirectory = false;
            for (FXButton* button : widgets<FXButton>(properties))
            {
                if (button->getTipText() == "Choose an icon")
                {
                    assert(button->getIcon() && button->getIcon()->getWidth() <= 48 &&
                           button->getIcon()->getHeight() <= 48);
                    preview = true;
                }
                if (button->getTipText() == "Choose a program or script (replaces the command)") { browseCommand = true; }
                if (button->getTipText() == "Choose a working directory") { browseDirectory = true; }
            }
            assert(preview && browseCommand && browseDirectory);
            assert(widgets<FXText>(properties).empty());
            for (FXCheckButton* check : widgets<FXCheckButton>(properties))
            {
                assert(check->getText() != "Advanced: full shell command");
                if (check->getText() == "Show on custom toolbar")
                {
                    FXGroupBox* group = dynamic_cast<FXGroupBox*>(check->getParent());
                    assert(group && group->getText() == "Toolbar");
                }
            }
            fields[2]->setText("/usr/bin/printf '%%s'");
            CheckAutomation replace(app, "Replace Action Icon");
            app.addTimeout(&replace, 1, 10); click(properties, "Use Program Icon");
            assert(fields[4]->getText() == "theme:swordfish-fixture-application");
            fields[4]->setText(iconPath);
            fields[2]->setText("printf");
            assert(fields[4]->getText() == iconPath);
            for (int route : {IconChooserAutomation::CANCEL, IconChooserAutomation::SYSTEM,
                              IconChooserAutomation::MISSING, IconChooserAutomation::IMAGE_CANCEL,
                              IconChooserAutomation::NONE, IconChooserAutomation::IMAGE})
            {
                FXString before = fields[4]->getText();
                IconChooserAutomation chooser(app, route, iconPath);
                app.addTimeout(&chooser, 1, 10);
                click(properties, "Choose an icon");
                assert(!dialog(app, "Choose an Icon"));
                assert(!dialog(app, "Select an Icon Image File"));
                assert(dialog(app, "Custom Action Properties") == properties);
                if (route == IconChooserAutomation::SYSTEM)
                    assert(fields[4]->getText() == "theme:swordfish-fixture-application");
                else if (route == IconChooserAutomation::NONE) { assert(fields[4]->getText().empty()); }
                else if (route == IconChooserAutomation::IMAGE) { assert(fields[4]->getText() == iconPath); }
                else { assert(fields[4]->getText() == before); }
            }
            BrowseAutomation chooseProgram(app, "Select a Program or Script", "/usr/bin/printf");
            app.addTimeout(&chooseProgram, 1, 10);
            click(properties, "Choose a program or script (replaces the command)");
            assert(fields[2]->getText() == "/usr/bin/printf" || fields[2]->getText() == "'/usr/bin/printf'");
            assert(widgets<FXTabBook>(properties).size() == 1);
            std::vector<FXTabItem*> tabs = widgets<FXTabItem>(properties);
            assert(tabs.size() == 2 && tabs[0]->getText() == "Basic" && tabs[1]->getText() == "Appearance Conditions");
            fields[2]->setText("printf '%%s\\n' ");
            fields[2]->setCursorPos(fields[2]->getText().length());
            widgets<FXComboBox>(properties)[0]->setCurrentItem(8);
            click(properties, "Insert");
            assert(fields[2]->getText() == "printf '%%s\\n' %p");
            CheckAutomation passed(app, "Checks Passed");
            app.addTimeout(&passed, 1, 20);
            click(properties, "Test Action");
            fields[2]->setText("printf value )");
            CheckAutomation failed(app, "Action Check Failed");
            app.addTimeout(&failed, 1, 20);
            click(properties, "Test Action");
            fields[2]->setText("printf '%%s\\n' %p");
            BrowseAutomation chooseDirectory(app, "Select Working Directory", FXPath::directory(iconPath));
            app.addTimeout(&chooseDirectory, 1, 10);
            click(properties, "Choose a working directory");
            assert(fields[3]->getText() == FXPath::directory(iconPath));
            fields[3]->setText("%o");
            LegendAutomation legend(app);
            app.addTimeout(&legend, 1, 10);
            click(properties, "Parameter Legend...");
            std::vector<FXCheckButton*> checks = widgets<FXCheckButton>(properties);
            for (unsigned i = 0; i < checks.size(); ++i)
            {
                if (checks[i]->getText().find("without selection") >= 0) { checks[i]->setCheck(true); }
                if (checks[i]->getText() == "Show on custom toolbar") { checks[i]->setCheck(true); }
                if (checks[i]->getText().find("Show command output") == 0) { checks[i]->setCheck(true); }
            }
            click(properties, "Apply");
        }
        else if (current == 4) { assert(editor); click(editor, "Add Separator"); }
        else if (current == 5) { assert(editor); click(editor, "Edit..."); }
        else if (current == 6)
        {
            assert(properties); widgets<FXCheckButton>(properties)[0]->setCheck(true); click(properties, "Apply");
        }
        else if (current == 7)
        { assert(editor); editor->handle(NULL, FXSEL(SEL_COMMAND, FXDialogBox::ID_ACCEPT), NULL); }
        if (current < 7 && !opensProperties) { app.addTimeout(this, 1, 30); }
        return 1;
    }
};

class DismissDialog : public FXObject
{
    FXApp& app;
    const char* title;
    const char* button;
public:
    DismissDialog(FXApp& application, const char* caption, const char* text) :
        app(application), title(caption), button(text) {}
    long handle(FXObject*, FXSelector, void*)
    {
        FXTopWindow* window = dialog(app, title);
        assert(window); click(window, button); return 1;
    }
};

class DismissContextMenu : public FXObject
{
    FXApp& app;
    FilePanel* panel;
public:
    DismissContextMenu(FXApp& application, FilePanel* p) : app(application), panel(p) {}
    long handle(FXObject*, FXSelector, void*)
    {
        std::vector<FXMenuPane*> menus = widgets<FXMenuPane>(app.getRootWindow());
        for (unsigned i = 0; i < menus.size(); ++i)
        {
            if (!menus[i]->shown() || menus[i]->getOwner() != panel) { continue; }
            assert(panel->getNumSelectedItems() == 2);
            menus[i]->hide(); return 1;
        }
        assert(false); return 0;
    }
};

class ToolbarSizePreferenceAutomation : public FXObject
{
    PreferencesBox& preferences;
    FXint initial;
    FXint choice;
    bool accept;
    bool integrationReadable;
public:
    ToolbarSizePreferenceAutomation(PreferencesBox& dialog, FXint current, FXint selected, bool save,
                                    bool readable = true) :
        preferences(dialog), initial(current), choice(selected), accept(save), integrationReadable(readable) {}
    long handle(FXObject*, FXSelector, void*)
    {
        FXGroupBox* toolbarIcons = NULL;
        FXGroupBox* integration = NULL;
        for (FXGroupBox* group : widgets<FXGroupBox>(&preferences))
        {
            if (group->getText() == "Tool Bar Icon Size") { toolbarIcons = group; }
            if (group->getText() == "Optional System Integration") { integration = group; }
        }
        assert(toolbarIcons && integration);
        std::vector<FXTabBook*> books = widgets<FXTabBook>(&preferences);
        assert(books.size() == 1);
        FXint page = 0;
        bool appearanceSelected = false;
        for (FXWindow* child = books[0]->getFirst(); child; child = child->getNext())
        {
            if (FXTabItem* tab = dynamic_cast<FXTabItem*>(child))
            {
                FXString label = tab->getText();
                label.substitute("&", "");
                if (label == "Appearance")
                {
                    books[0]->setCurrent(page);
                    appearanceSelected = true;
                }
                ++page;
            }
        }
        assert(appearanceSelected);
        std::vector<FXRadioButton*> sizes = widgets<FXRadioButton>(toolbarIcons);
        assert(sizes.size() == 3 && widgets<FXListBox>(toolbarIcons).empty());
        for (FXint i = 0; i < 3; ++i) { assert(sizes[i]->getCheck() == (i == initial)); }
        sizes[choice]->setCheck(true, true);
        preferences.getApp()->forceRefresh();
        pump(*preferences.getApp(), 50);
        for (FXint i = 0; i < 3; ++i)
        {
            assert(sizes[i]->getCheck() == (i == choice));
            assert(sizes[i]->getWidth() >= sizes[i]->getDefaultWidth());
            assert(sizes[i]->getX() + sizes[i]->getWidth() <= sizes[i]->getParent()->getWidth());
        }
        std::vector<FXCheckButton*> integrationChoices = widgets<FXCheckButton>(integration);
        assert(integrationChoices.size() == 3);
        for (FXCheckButton* button : integrationChoices)
        {
            assert(button->isEnabled() == integrationReadable);
            assert(button->getCheck());
        }
        if (!integrationReadable)
        {
            bool diagnostic = false;
            for (FXLabel* label : widgets<FXLabel>(integration))
                if (label->getText().contains("Other preferences can still be saved.")) { diagnostic = true; }
            assert(diagnostic);
        }
        if (accept)
            preferences.onCmdAccept(NULL, 0, NULL);
        else
            preferences.onCmdCancel(NULL, 0, NULL);
        return 1;
    }
};

class RemoveIntegrationPreferenceAutomation : public FXObject
{
    PreferencesBox& preferences;
    bool accept;
public:
    RemoveIntegrationPreferenceAutomation(PreferencesBox& dialog, bool save) :
        preferences(dialog), accept(save) {}
    long handle(FXObject*, FXSelector, void*)
    {
        FXGroupBox* integration = NULL;
        for (FXGroupBox* group : widgets<FXGroupBox>(&preferences))
            if (group->getText() == "Optional System Integration") { integration = group; }
        assert(integration);
        std::vector<FXCheckButton*> choices = widgets<FXCheckButton>(integration);
        assert(choices.size() == 3);
        for (FXCheckButton* choice : choices)
        {
            assert(choice->isEnabled() && choice->getCheck());
            choice->setCheck(false);
        }
        if (accept)
        {
            DismissDialog confirm(*preferences.getApp(), "Confirm Command Integration", "Yes");
            preferences.getApp()->addTimeout(&confirm, 1, 30);
            preferences.onCmdAccept(NULL, 0, NULL);
        }
        else
        {
            preferences.onCmdCancel(NULL, 0, NULL);
        }
        return 1;
    }
};

class AddressModePreferenceAutomation : public FXObject
{
    PreferencesBox& preferences;
    FXint initial;
    FXint choice;
    bool accept;
public:
    AddressModePreferenceAutomation(PreferencesBox& dialog, FXint current, FXint selected, bool save) :
        preferences(dialog), initial(current), choice(selected), accept(save) {}
    long handle(FXObject*, FXSelector, void*)
    {
        FXGroupBox* group = NULL;
        for (FXGroupBox* candidate : widgets<FXGroupBox>(&preferences))
            if (candidate->getText() == "Default address bar mode") { group = candidate; }
        assert(group);
        std::vector<FXRadioButton*> modes = widgets<FXRadioButton>(group);
        assert(modes.size() == 2);
        assert(modes[0]->getText() == "Button mode (breadcrumbs) " && modes[1]->getText() == "Edit mode (text) ");
        assert(modes[0]->shown() && modes[1]->shown());
        assert(modes[0]->getWidth() >= modes[0]->getDefaultWidth() &&
               modes[1]->getWidth() >= modes[1]->getDefaultWidth());
        assert(modes[0]->getCheck() == (initial == 1) && modes[1]->getCheck() == (initial == 0));
        FXRadioButton* selected = modes[choice == 1 ? 0 : 1];
        selected->setCheck(true, true);
        preferences.getApp()->forceRefresh();
        pump(*preferences.getApp(), 100);
        assert(modes[0]->getCheck() == (choice == 1) && modes[1]->getCheck() == (choice == 0));
        if (accept)
        {
            DismissDialog restart(*preferences.getApp(), "Restart", "Cancel");
            if (choice != initial) { preferences.getApp()->addTimeout(&restart, 1, 30); }
            preferences.onCmdAccept(NULL, 0, NULL);
        }
        else
        {
            preferences.onCmdCancel(NULL, 0, NULL);
        }
        return 1;
    }
};

class PaneHistoryAutomation : public FXObject
{
    FXApp& app;
    FilePanel* panel;
    FXArrowButton* button;
    FXString expected;
    bool alignRight;
public:
    bool handled = false;
    PaneHistoryAutomation(FXApp& application, FilePanel* p, FXArrowButton* arrow, const FXString& path, bool right) :
        app(application), panel(p), button(arrow), expected(path), alignRight(right) {}
    long handle(FXObject*, FXSelector, void*)
    {
        for (FXMenuPane* menu : widgets<FXMenuPane>(app.getRootWindow()))
        {
            if (!menu->shown() || menu->getOwner() != panel) { continue; }
            std::vector<FXMenuCommand*> commands = widgets<FXMenuCommand>(menu);
            assert(!commands.empty() && commands[0]->getText() == expected);
            FXint bx, by, mx, my;
            button->translateCoordinatesTo(bx, by, app.getRootWindow(), 0, button->getHeight());
            menu->translateCoordinatesTo(mx, my, app.getRootWindow(), 0, 0);
            assert(mx >= 0 && mx + menu->getWidth() <= app.getRootWindow()->getWidth());
            if (alignRight && bx + button->getWidth() >= menu->getWidth())
                assert(abs(mx + menu->getWidth() - bx - button->getWidth()) <= 2);
            if (!alignRight && bx + menu->getWidth() <= app.getRootWindow()->getWidth())
                assert(abs(mx - bx) <= 2);
            FXMenuCommand* selected = commands[0];
            menu->popdown();
            selected->getTarget()->handle(selected, FXSEL(SEL_COMMAND, selected->getSelector()), NULL);
            handled = true;
            return 1;
        }
        assert(false); return 0;
    }
};

class ToolbarHistoryAutomation : public FXObject
{
    FXApp& app;
    FXArrowButton* button;
    FXString expected;
public:
    bool handled = false;
    ToolbarHistoryAutomation(FXApp& application, FXArrowButton* arrow, const FXString& path) :
        app(application), button(arrow), expected(path) {}
    long handle(FXObject*, FXSelector, void*)
    {
        for (DirHistBox* box : widgets<DirHistBox>(app.getRootWindow()))
        {
            if (!box->shown() || box->getOwner() != button) { continue; }
            std::vector<FXList*> lists = widgets<FXList>(box);
            assert(lists.size() == 1 && lists[0]->getNumItems() > 0 && lists[0]->getItemText(0) == expected);
            lists[0]->setCurrentItem(0);
            box->onCmdClicked(lists[0], FXSEL(SEL_CLICKED, DirHistBox::ID_LIST), NULL);
            handled = true;
            return 1;
        }
        assert(false); return 0;
    }
};

static std::vector<FXArrowButton*> paneHistoryButtons(FilePanel* panel)
{
    std::vector<FXArrowButton*> result;
    for (FXArrowButton* button : widgets<FXArrowButton>(panel))
        if (button->getTarget() == panel && FXSELID(button->getSelector()) == FilePanel::ID_PANE_HISTORY)
            result.push_back(button);
    assert(result.size() == 1);
    for (FXArrowButton* button : result)
    {
        assert(button->shown() && button->getArrowSize() >= 10 && button->getWidth() >= 22);
        assert((button->getFrameStyle() & (FRAME_RAISED | FRAME_THICK)) == (FRAME_RAISED | FRAME_THICK));
        assert(button->getTipText() == "Folder History");
    }
    return result;
}

static void checkPaneHistory(FXApp& app, FilePanel* panel, FXArrowButton* button,
                             const FXString& expected, bool alignRight)
{
    PaneHistoryAutomation choose(app, panel, button, expected, alignRight);
    app.addTimeout(&choose, 1, 50);
    button->getTarget()->handle(button, FXSEL(SEL_COMMAND, button->getSelector()), NULL);
    pump(app, 100);
    assert(choose.handled && panel->getDirectory() == expected);
}

static void checkToolbarHistory(FXApp& app, FilePanel* panel, FXArrowButton* button, const FXString& expected)
{
    // The window manager can steal focus and close the list first; retry the pick
    for (int attempt = 0; attempt < 6 && panel->getDirectory() != expected; ++attempt)
    {
        ToolbarHistoryAutomation choose(app, button, expected);
        app.addTimeout(&choose, 1, 50);
        button->getTarget()->handle(button, FXSEL(SEL_COMMAND, button->getSelector()), NULL);
        pump(app, 300);
        assert(choose.handled);
    }
    assert(panel->getDirectory() == expected);
}

static void checkAddressBarMode(FilePanel* panel, bool breadcrumbs)
{
    FXTextField* editor = NULL;
    for (FXTextField* field : widgets<FXTextField>(panel))
        if (field->getTarget() == panel && FXSELID(field->getSelector()) == FilePanel::ID_ENTER_PATH)
            { assert(!editor); editor = field; }
    std::vector<PathLinker*> linkers = widgets<PathLinker>(panel);
    assert(editor && linkers.size() == 1);
    assert(editor->shown() == !breadcrumbs && linkers[0]->shown() == breadcrumbs);
    std::vector<FXArrowButton*> arrows = paneHistoryButtons(panel);
    assert(arrows[0]->getX() < editor->getX());
}

static CustomAction node(const char* id, const char* name, int kind, const char* parent = "")
{
    CustomAction a;
    a.id = id; a.name = name; a.kind = kind; a.parent = parent; a.toolbar = true;
    if (kind == CustomAction::ACTION) { a.folders = false; }
    a.command = "printf '%%s\\0' %F > %p/observed";
    return a;
}

static void select(FilePanel* panel)
{
    FileList* list = panel->getList();
    list->killSelection();
    int count = 0;
    for (int i = 0; i < list->getNumItems(); ++i)
    {
        if (list->getItemFilename(i) == "one 'quoted'.txt" || list->getItemFilename(i) == "two $HOME.txt")
        { list->selectItem(i); ++count; }
    }
    assert(count == 2);
}

static std::string read(const FXString& path)
{
    std::ifstream input(path.text(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

static void finishCommand(FXApp& app, CommandWindow* output)
{
    for (int waited = 0; waited < 100 && app.hasChore(output, CommandWindow::ID_WATCHPROCESS); ++waited)
    {
        app.removeChore(output, CommandWindow::ID_WATCHPROCESS);
        output->onWatchProcess(NULL, 0, NULL);
        pump(app, 20);
    }
    assert(!app.hasChore(output, CommandWindow::ID_WATCHPROCESS));
}

static FXTreeItem* findActionItem(FXTreeItem* item)
{
    for (; item; item = item->getNext())
    {
        if (item->getText().find("Record") == 0) { return item; }
        FXTreeItem* child = findActionItem(item->getFirst());
        if (child) { return child; }
    }
    return NULL;
}

class UnsavedAutomation : public FXObject
{
    FXApp& app;
    bool save;
public:
    UnsavedAutomation(FXApp& application, bool persist) : app(application), save(persist) {}
    long handle(FXObject*, FXSelector, void*)
    {
        FXTopWindow* editor = dialog(app, "Custom Actions"); assert(editor);
        FXTreeList* tree = widgets<FXTreeList>(editor)[0];
        FXTreeItem* source = findActionItem(tree->getFirstItem()); assert(source);
        tree->setCurrentItem(source);
        if (!save)
        {
            class Properties : public FXObject
            {
                FXApp& app;
            public:
                explicit Properties(FXApp& a) : app(a) {}
                long handle(FXObject*, FXSelector, void*)
                {
                    FXTopWindow* properties = dialog(app, "Custom Action Properties"); assert(properties);
                    FXTextField* name = widgets<FXTextField>(properties)[0];
                    assert(widgets<FXTextField>(properties)[2]->getText() == "/usr/bin/printf '%%s\\n' %F");
                    FXString original = name->getText(); name->setText(original + " edited");
                    DismissDialog keep(app, "Unsaved Custom Action Changes", "Keep editing");
                    app.addTimeout(&keep, 1, 10); click(properties, "Cancel");
                    assert(properties->shown() && name->getText() == original + " edited");
                    name->setText(original);
                    // Reverting every changed value must remove the warning.
                    properties->handle(NULL, FXSEL(SEL_CLOSE, 0), NULL); return 1;
                }
            } properties(app);
            app.addTimeout(&properties, 1, 10); click(editor, "Edit...");
        }
        click(editor, "Duplicate Action");
        assert(tree->getCurrentItem() && tree->getCurrentItem()->getText().find("Record (copy)") == 0);
        if (save) { click(editor, "Save"); return 1; }
        for (int route = 0; route < 3; ++route)
        {
            DismissDialog keep(app, "Unsaved Custom Action Changes", "Keep editing");
            app.addTimeout(&keep, 1, 10);
            FXEvent escape = {}; escape.code = KEY_Escape;
            if (route == 0) { click(editor, "Cancel"); }
            else if (route == 1) { editor->handle(NULL, FXSEL(SEL_KEYPRESS, 0), &escape); }
            else { editor->handle(NULL, FXSEL(SEL_CLOSE, 0), NULL); }
            assert(editor->shown() && tree->getCurrentItem()->getText().find("Record (copy)") == 0);
        }
        DismissDialog discard(app, "Unsaved Custom Action Changes", "Discard");
        app.addTimeout(&discard, 1, 10); click(editor, "Cancel");
        return 1;
    }
};

int main(int argc, char** argv)
{
    std::setvbuf(stdout, NULL, _IOLBF, 0);
    assert(argc == 4);
    FXString fixture = FXPath::absolute(argv[1]);
    FXString prefix = FXPath::absolute(argv[2]), iconpath = FXPath::absolute(argv[3]);
    FXApp app("SwordfishCustomActionsTest", "SwordfishCustomActionsTest");
    app.init(argc, argv);
    homedir = FXSystem::getHomeDirectory(); xdgconfighome = homedir + "/config"; xdgdatahome = homedir + "/data";
    execpath = prefix + "/bin/swordfish";
    app.reg().writeStringEntry("SETTINGS", "iconpath", iconpath.text());
    app.reg().writeUnsignedEntry("OPTIONS", "folder_size_mode", 0);
    app.reg().writeStringEntry("OPTIONS", "generaltoolbar_items",
        (FXString("XFileExplorer_") + FXStringVal((FXuint)XFileExplorer::ID_DIR_BACK_HIST) + "," +
         FXString("XFileExplorer_") + FXStringVal((FXuint)XFileExplorer::ID_DIR_BACK)).text());
    app.reg().writeUnsignedEntry("OPTIONS", "toolbar_icon_size", 32);
    app.reg().writeUnsignedEntry("OPTIONS", "generaltoolbar_items_icon_size", 32);
    app.reg().writeUnsignedEntry("OPTIONS", "generaltoolbar_items_display_mode", 2);
    app.reg().writeUnsignedEntry("CUSTOM ACTIONS UI", "icon_size", 32);
    app.reg().writeUnsignedEntry("CUSTOM ACTIONS UI", "display_mode", 2);
    CustomActions model;
    model.items.push_back(node("utilities", "Utilities", CustomAction::FOLDER));
    model.items.push_back(node("nested", "Nested", CustomAction::FOLDER, "utilities"));
    model.items.push_back(node("record", "Record", CustomAction::ACTION, "nested"));
    {
        std::vector<FXColor> pixels(2048 * 1024, FXRGB(220, 80, 20));
        FXPNGIcon large(&app);
        large.setData(pixels.data(), IMAGE_KEEP, 2048, 1024);
        FXFileStream stream;
        assert(stream.open(fixture + "/large.png", FXStreamSave));
        assert(large.savePixels(stream));
        stream.close();
        model.items[0].icon = fixture + "/large.png";
        model.items[2].icon = "theme:swordfish-fixture-application";
    }
    FXString host = fixture + "/host-data", extraHost = fixture + "/host-extra", bundle = fixture + "/bundle-data";
    assert(xf_mkpath((host + "/icons/Fixture/48x48/apps").text(), 0700) == 0);
    assert(xf_mkpath((host + "/icons/Fixture/16x16/apps").text(), 0700) == 0);
    assert(xf_mkpath((host + "/icons/Fixture/scalable/apps").text(), 0700) == 0);
    assert(xf_mkpath((host + "/icons/FixtureParent/48x48/apps").text(), 0700) == 0);
    assert(xf_mkpath((host + "/icons/FixtureOther/48x48/apps").text(), 0700) == 0);
    assert(xf_mkpath((host + "/icons/hicolor/48x48/apps").text(), 0700) == 0);
    assert(xf_mkpath((extraHost + "/icons/Fixture/48x48/apps").text(), 0700) == 0);
    assert(xf_mkpath((host + "/pixmaps").text(), 0700) == 0);
    assert(xf_mkpath((bundle + "/icons/Fixture/48x48/apps").text(), 0700) == 0);
    assert(xf_mkpath((host + "/applications").text(), 0700) == 0);
    {
        std::ofstream desktop((host + "/applications/swordfish-fixture.desktop").text());
        desktop << "[Desktop Entry]\nType=Application\nName=Fixture printf\nExec=/usr/bin/printf %F\n"
                   "Icon=swordfish-fixture-application\n";
    }
    for (const char* theme : {"Fixture", "FixtureParent", "FixtureOther", "hicolor"})
    {
        std::ofstream index((host + "/icons/" + theme + "/index.theme").text());
        index << "[Icon Theme]\nName=" << theme << "\nDirectories=48x48/apps\n";
        if (FXString(theme) == "Fixture") { index << "Inherits=FixtureParent\n"; }
    }
    assert(FXFile::copy(fixture + "/large.png", host + "/icons/Fixture/48x48/apps/swordfish-fixture-application.png"));
    assert(FXFile::copy(fixture + "/large.png", host + "/icons/FixtureParent/48x48/apps/swordfish-fixture-application.png"));
    assert(FXFile::copy(fixture + "/large.png", extraHost + "/icons/Fixture/48x48/apps/swordfish-fixture-application.png"));
    assert(FXFile::copy(fixture + "/large.png", extraHost + "/icons/Fixture/48x48/apps/swordfish-fixture-split.png"));
    assert(FXFile::copy(fixture + "/large.png", bundle + "/icons/Fixture/48x48/apps/swordfish-fixture-application.png"));
    assert(FXFile::copy(fixture + "/large.png", bundle + "/icons/Fixture/48x48/apps/swordfish-fixture-bundle-only.png"));
    assert(FXFile::copy(fixture + "/large.png", host + "/icons/FixtureParent/48x48/apps/swordfish-fixture-inherited.png"));
    assert(FXFile::copy(fixture + "/large.png", host + "/icons/FixtureOther/48x48/apps/swordfish-fixture-other.png"));
    assert(FXFile::copy(fixture + "/large.png", host + "/icons/hicolor/48x48/apps/swordfish-fixture-fallback.png"));
    assert(FXFile::copy(fixture + "/large.png", host + "/icons/Fixture/16x16/apps/swordfish-fixture-choice.png"));
    assert(FXFile::copy(fixture + "/large.png", host + "/icons/Fixture/48x48/apps/swordfish-fixture-choice.png"));
    for (int n = 0; n < 45; ++n)
    {
        FXString path = host + "/icons/Fixture/48x48/apps/swordfish-fixture-extra-" + FXStringVal(n) + ".png";
        assert(symlink((fixture + "/large.png").text(), path.text()) == 0);
    }
    assert(symlink("missing-file", (host + "/icons/Fixture/48x48/apps/swordfish-fixture-missing.png").text()) == 0);
    for (const char* directory : {"actions", "hardware", "misc"})
    {
        FXString folder = host + "/icons/Fixture/48x48/" + directory;
        assert(xf_mkpath(folder.text(), 0700) == 0);
        assert(FXFile::copy(fixture + "/large.png", folder + "/swordfish-fixture-" + directory + ".png"));
    }
    {
        std::ofstream index((host + "/icons/Fixture/index.theme").text(), std::ios::app);
        index << "\n[48x48/hardware]\nContext=Devices\n";
    }
    {
        std::ofstream index((bundle + "/icons/Fixture/index.theme").text());
        index << "[Icon Theme]\nName=Bundle decoy\nDirectories=48x48/apps\n";
        std::ofstream svg((fixture + "/wide.svg").text());
        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\">"
               "<rect width=\"200\" height=\"100\" fill=\"red\"/></svg>";
        std::ofstream xpm((fixture + "/small.xpm").text());
        xpm << "/* XPM */\nstatic char *icon[] = {\"2 1 1 1\", \"a c #ff0000\", \"aa\"};\n";
        std::ofstream invalid((fixture + "/not-image.png").text());
        invalid << "This is not a PNG image.\n";
        std::vector<FXColor> pixels(200 * 100, FXRGB(220, 80, 20));
        FXJPGIcon jpeg(&app); jpeg.setData(pixels.data(), IMAGE_KEEP, 200, 100);
        FXFileStream stream; assert(stream.open(fixture + "/wide.jpg", FXStreamSave));
        assert(jpeg.savePixels(stream)); stream.close();
    }
    assert(FXFile::copy(fixture + "/wide.svg", host + "/icons/Fixture/scalable/apps/swordfish-fixture-choice.svg"));
    assert(FXFile::copy(fixture + "/wide.svg", host + "/icons/Fixture/scalable/apps/swordfish-fixture-svg.svg"));
    assert(FXFile::copy(fixture + "/small.xpm", host + "/pixmaps/swordfish-fixture-pixmap.xpm"));
    setenv("SWORDFISH_HOST_XDG_DATA_HOME", host.text(), 1);
    setenv("SWORDFISH_HOST_XDG_DATA_DIRS", extraHost.text(), 1);
    setenv("XDG_DATA_HOME", bundle.text(), 1);
    setenv("XDG_DATA_DIRS", bundle.text(), 1);
    setenv("XDG_CACHE_HOME", (fixture + "/cache").text(), 1);
    // Force the host theme preference while retaining the application's own resources.
    app.reg().writeStringEntry("SETTINGS", "iconpath", "/unused/Fixture-system");
    std::vector<FXString> names = {
        "swordfish-fixture-application", "swordfish-fixture-inherited", "swordfish-fixture-fallback",
        "swordfish-fixture-other", "swordfish-fixture-split", "swordfish-fixture-choice",
        "swordfish-fixture-svg", "swordfish-fixture-pixmap", "swordfish-fixture-misc",
        "swordfish-fixture-missing", "swordfish-fixture-bundle-only", "not-installed-anywhere-729103"
    };
    for (int n = 0; n < 45; ++n)
        names.push_back("swordfish-fixture-extra-" + FXStringVal(n));
    names.push_back(names[0]);
    prefetchSystemActionIcons(&app, names);
    assert(resolveSystemActionIcon(&app, names[0]) == host + "/icons/Fixture/48x48/apps/swordfish-fixture-application.png");
    assert(resolveSystemActionIcon(&app, names[1]).contains("FixtureParent"));
    assert(resolveSystemActionIcon(&app, names[2]).contains("hicolor"));
    assert(resolveSystemActionIcon(&app, names[3]).contains("FixtureOther"));
    assert(resolveSystemActionIcon(&app, names[4]).left(extraHost.length()) == extraHost);
    assert(resolveSystemActionIcon(&app, names[5]) == host + "/icons/Fixture/48x48/apps/swordfish-fixture-choice.png");
    assert(resolveSystemActionIcon(&app, names[6]).right(4) == ".svg");
    assert(resolveSystemActionIcon(&app, names[7]) == host + "/pixmaps/swordfish-fixture-pixmap.xpm");
    assert(resolveSystemActionIcon(&app, names[8]).contains("/misc/"));
    assert(resolveSystemActionIcon(&app, names[9]).contains("/swordfish-fixture-missing.png"));
    assert(resolveSystemActionIcon(&app, names[10]).empty());
    assert(resolveSystemActionIcon(&app, names[11]).empty());
    app.reg().writeStringEntry("SETTINGS", "iconpath", "/unused/FixtureParent-system");
    assert(resolveSystemActionIcon(&app, names[0]).contains("FixtureParent"));
    app.reg().writeStringEntry("SETTINGS", "iconpath", "/unused/Fixture-system");
    setenv("SWORDFISH_HOST_XDG_DATA_HOME", extraHost.text(), 1);
    setenv("SWORDFISH_HOST_XDG_DATA_DIRS", host.text(), 1);
    assert(resolveSystemActionIcon(&app, names[0]).left(extraHost.length()) == extraHost);
    setenv("SWORDFISH_HOST_XDG_DATA_HOME", host.text(), 1);
    setenv("SWORDFISH_HOST_XDG_DATA_DIRS", extraHost.text(), 1);
    FXString gtkConfig = fixture + "/host-config/gtk-3.0";
    assert(xf_mkpath(gtkConfig.text(), 0700) == 0);
    setenv("SWORDFISH_HOST_XDG_CONFIG_HOME", (fixture + "/host-config").text(), 1);
    app.reg().writeStringEntry("SETTINGS", "iconpath", "/unused/default-theme");
    {
        std::ofstream settings((gtkConfig + "/settings.ini").text());
        settings << "[Settings]\ngtk-icon-theme-name=FixtureParent\n";
    }
    assert(resolveSystemActionIcon(&app, names[0]).contains("FixtureParent"));
    {
        std::ofstream settings((gtkConfig + "/settings.ini").text());
        settings << "[Settings]\ngtk-icon-theme-name=Fixture\n";
    }
    assert(resolveSystemActionIcon(&app, names[0]) == host + "/icons/Fixture/48x48/apps/swordfish-fixture-application.png");
    app.reg().writeStringEntry("SETTINGS", "iconpath", "/unused/Fixture-system");
    prefetchSystemActionIcons(&app, names);
    std::vector<FXString> resolved;
    for (const FXString& name : names) { resolved.push_back(resolveSystemActionIcon(&app, name)); }
    const FXString added = "swordfish-fixture-added-after-lookup";
    assert(resolveSystemActionIcon(&app, added).empty());
    assert(FXFile::copy(fixture + "/large.png", host + "/icons/Fixture/48x48/apps/" + added + ".png"));
    assert(resolveSystemActionIcon(&app, added).empty());
    const std::vector<SystemActionIcon>& catalog = systemActionIcons(&app);
    bool foundAdded = false;
    for (const SystemActionIcon& item : catalog)
    {
        if (item.name == added) { foundAdded = true; }
        for (size_t i = 0; i < names.size(); ++i)
            if (item.name == names[i]) { assert(item.path == resolved[i]); }
    }
    assert(foundAdded && !resolveSystemActionIcon(&app, added).empty());
    for (size_t i = 0; i < names.size(); ++i)
        assert(resolveSystemActionIcon(&app, names[i]) == resolved[i]);
    assert(!loadCustomActionIcon(&app, "theme:not-installed-anywhere-729103", 32));
    assert(!loadCustomActionIcon(&app, "theme:../large.png", 32));
    if (!findIconConverter().empty())
    {
        FXIcon* image = loadCustomActionIcon(&app, "theme:swordfish-fixture-svg", 48);
        assert(image && image->getWidth() == 2 * image->getHeight());
        delete image;
    }
    for (const char* filename : {"wide.jpg", "small.xpm", "wide.svg"})
    {
        if (FXString(filename) == "wide.svg" && findIconConverter().empty()) { continue; }
        FXIcon* image = loadCustomActionIcon(&app, fixture + "/" + filename, 48);
        assert(image && image->getWidth() <= 48 && image->getHeight() <= 48);
        assert(image->getWidth() == 2 * image->getHeight());
        delete image;
    }
    {
        const char* environment = getenv("PATH"); FXString oldpath = environment ? environment : "";
        FXString oldexec = execpath;
        execpath = fixture + "/no-converter/swordfish"; setenv("PATH", "", 1);
        assert(!loadCustomActionIcon(&app, fixture + "/wide.svg", 48));
        execpath = oldexec; setenv("PATH", oldpath.text(), 1);
    }
    {
        CustomActions named;
        named.items.push_back(node("named-icon", "Named icon", CustomAction::ACTION));
        named.items[0].icon = "theme:swordfish-fixture-application";
        FXRegistry settings; named.save(settings);
        CustomActions reread; FXString error; assert(reread.load(settings, error));
        assert(reread.items.size() == 1 && reread.items[0].icon == named.items[0].icon);
    }
    app.reg().writeStringEntry("SETTINGS", "iconpath", iconpath.text());
    puts("PASS: targeted named icons match the lazy catalog, host roots, theme precedence and file selection; JPEG/XPM/SVG preserve bounds");
    model.items.push_back(node("separator", "Separator", CustomAction::SEPARATOR));
    CustomAction background = node("background", "Background", CustomAction::ACTION);
    background.background = true; background.files = false; background.folders = false;
    background.single = false; background.multiple = false; background.command = "printf '%%s\\n' %p";
    background.icon = "theme:swordfish-fixture-inherited";
    model.items.push_back(background);
    CustomAction unmatched = node("unmatched", "Unmatched", CustomAction::ACTION);
    unmatched.patterns = "*.png"; unmatched.toolbar = false;
    model.items.push_back(unmatched);
    FXString error; assert(model.validate(error)); model.save(app.reg());
    FXuint status;
    if (!loadAppIcons(&app, &status))
    {
        fprintf(stderr, "Icon load failed: status=%u requested=%s actual=%s\n", status, iconpath.text(),
                app.reg().readStringEntry("SETTINGS", "iconpath", ""));
        assert(false);
    }
    vector_FXString paths; paths.push_back(fixture); paths.push_back(fixture);
    XFileExplorer* window = new XFileExplorer(&app, paths, 3);
    mainWindow = window;
    app.create(); window->show(); pump(app, 1000);
    {
        FXToolBar* general = NULL;
        FXButton* zoom = NULL;
        for (FXToolBar* bar : widgets<FXToolBar>(window))
        {
            for (FXWindow* child = bar->getFirst(); child; child = child->getNext())
            {
                if (dynamic_cast<FXArrowButton*>(child) &&
                    FXSELID(child->getSelector()) == XFileExplorer::ID_DIR_BACK_HIST)
                    general = bar;
                if (FXButton* button = dynamic_cast<FXButton*>(child))
                {
                    assert(button->getTipText() != "More toolbar commands");
                    if (FXSELID(button->getSelector()) == FilePanel::ID_ZOOM_IN) { zoom = button; }
                }
            }
        }
        assert(general && zoom && zoom->shown() && zoom->getIcon());
        FXWindow* first = general->getFirst();
        while (first && !dynamic_cast<FXToolBarGrip*>(first)) { first = first->getNext(); }
        assert(first && first->getNext() &&
               FXSELID(first->getNext()->getSelector()) == XFileExplorer::ID_DIR_BACK);
        FXButton* back = dynamic_cast<FXButton*>(first->getNext());
        assert(back && back->getIcon() == minidirbackicon);
        assert(zoom->getIcon() == minizoominicon);
        for (FXMenuTitle* title : widgets<FXMenuTitle>(window))
            for (FXWindow* item = title->getMenu()->getFirst(); item; item = item->getNext())
                if (FXMenuCommand* command = dynamic_cast<FXMenuCommand*>(item))
                    assert(command->getText() != "Customize Main Toolbars...");
        mouseClick(back, 3);
        pump(app, 300);
        for (FXMenuPane* menu : widgets<FXMenuPane>(app.getRootWindow())) { assert(!menu->shown()); }
        app.reg().writeIntEntry("SETTINGS", "file_zoom_step", 1);
        zoom->getTarget()->handle(zoom, FXSEL(SEL_COMMAND, zoom->getSelector()), NULL);
        assert(app.reg().readIntEntry("SETTINGS", "file_zoom_step", -1) == 2);
        const int zoom64 = window->getLeftPanel()->getList()->getViewIconSize();
        for (int click = 0; click < 10; ++click)
            zoom->getTarget()->handle(zoom, FXSEL(SEL_COMMAND, FilePanel::ID_ZOOM_OUT), NULL);
        assert(app.reg().readIntEntry("SETTINGS", "file_zoom_step", 0) == -3);
        assert(app.reg().readUnsignedEntry("SETTINGS", "bigthumb_size", 0) == 32);
        assert(window->getLeftPanel()->getList()->getViewIconSize() * 4 <= zoom64 + 4);
        zoom->getTarget()->handle(zoom, FXSEL(SEL_COMMAND, FilePanel::ID_ZOOM_RESET), NULL);
        assert(app.reg().readIntEntry("SETTINGS", "file_zoom_step", -1) == 1);
        puts("PASS: three extra zoom-out levels stop at 16 px and Reset Zoom restores the default");
        puts("PASS: original built-in toolbar order/icons and Zoom In restored; stale customization ignored, no toolbar editor, presentation menus or overflow");
    }
    CustomActionsUI* actions = window->getCustomActions();
    assert(actions && actions->model.items.size() == 6);
    for (unsigned n : {0u, 2u})
    {
        assert(actions->icon(n) != minirunicon);
        assert(actions->icon(n)->getWidth() <= minirunicon->getHeight());
        assert(actions->icon(n)->getHeight() <= minirunicon->getHeight());
        assert(actions->icon(n)->getWidth() == 2 * actions->icon(n)->getHeight());
    }
    puts("PASS: oversized action and folder PNGs fit toolbar/menu bounds and preserve proportions");
    FilePanel* left = window->getLeftPanel(); FilePanel* right = window->getRightPanel();
    left->setDirectory(fixture); right->setDirectory(fixture + "/other");
    {
        std::vector<FXArrowButton*> leftHistory = paneHistoryButtons(left);
        std::vector<FXArrowButton*> rightHistory = paneHistoryButtons(right);
        FXArrowButton* toolbarBack = NULL;
        FXArrowButton* toolbarForward = NULL;
        for (FXArrowButton* button : widgets<FXArrowButton>(window))
        {
            if (button->getTarget() != window) { continue; }
            if (FXSELID(button->getSelector()) == XFileExplorer::ID_DIR_BACK_HIST) { toolbarBack = button; }
            if (FXSELID(button->getSelector()) == XFileExplorer::ID_DIR_FORWARD_HIST) { toolbarForward = button; }
        }
        assert(toolbarBack && toolbarForward);
        for (FXArrowButton* button : {toolbarBack, toolbarForward})
        {
            assert(button->shown() && button->getArrowSize() >= 10 && button->getWidth() >= 22);
            assert((button->getFrameStyle() & (FRAME_RAISED | FRAME_THICK)) == (FRAME_RAISED | FRAME_THICK));
        }
        {
            FXuint dpi = app.reg().readUnsignedEntry("SETTINGS", "screenres", 100);
            FXHorizontalFrame frame(app.getRootWindow(), FRAME_NONE);
            app.reg().writeUnsignedEntry("SETTINGS", "screenres", 60);
            FXArrowButton* normal = xf_historybutton(&frame, NULL, 0, "History");
            app.reg().writeUnsignedEntry("SETTINGS", "screenres", 200);
            FXArrowButton* larger = xf_historybutton(&frame, NULL, 0, "History");
            assert(normal->getArrowSize() == 10 && larger->getArrowSize() == 20);
            assert(larger->getDefaultWidth() > normal->getDefaultWidth());
            app.reg().writeUnsignedEntry("SETTINGS", "screenres", dpi);
        }

        left->backhistRemoveAllItems(); left->forwardhistRemoveAllItems();
        right->backhistRemoveAllItems(); right->forwardhistRemoveAllItems();
        for (FilePanel* panel : {left, right})
            for (FXArrowButton* button : paneHistoryButtons(panel))
            {
                panel->onUpdPaneNavigation(button, FXSEL(SEL_UPDATE, FilePanel::ID_PANE_HISTORY), NULL);
                assert(!button->isEnabled());
            }

        assert(left->navigatePane(fixture + "/other"));
        for (FXArrowButton* button : leftHistory)
        {
            left->onUpdPaneNavigation(button, FXSEL(SEL_UPDATE, FilePanel::ID_PANE_HISTORY), NULL);
            assert(button->isEnabled());
        }
        right->setActive();
        checkPaneHistory(app, left, leftHistory[0], fixture, false);
        assert(window->getCurrentPanel() == left && right->getDirectory() == fixture + "/other");

        assert(right->navigatePane(fixture));
        left->setActive();
        checkPaneHistory(app, right, rightHistory[0], fixture + "/other", false);
        assert(window->getCurrentPanel() == right && left->getDirectory() == fixture);

        left->onCmdTogglePathMode(NULL, 0, NULL);
        pump(app, 100);
        checkAddressBarMode(left, true);
        checkAddressBarMode(right, true);
        assert(left->navigatePane(fixture + "/other"));
        checkPaneHistory(app, left, leftHistory[0], fixture, false);
        left->onCmdTogglePathMode(NULL, 0, NULL);
        pump(app, 100);
        checkAddressBarMode(left, false);
        checkAddressBarMode(right, false);

        assert(left->navigatePane(fixture + "/other"));
        window->onUpdDirBackHist(toolbarBack, 0, NULL);
        assert(toolbarBack->isEnabled());
        checkToolbarHistory(app, left, toolbarBack, fixture);
        assert(left->getDirectory() == fixture && right->getDirectory() == fixture + "/other");
        window->onUpdDirForwardHist(toolbarForward, 0, NULL);
        assert(toolbarForward->isEnabled());
        checkToolbarHistory(app, left, toolbarForward, fixture + "/other");
        assert(left->getDirectory() == fixture + "/other");

        left->setDirectory(fixture); left->updatePath();
        right->setDirectory(fixture + "/other"); right->updatePath();
        left->backhistRemoveAllItems(); left->forwardhistRemoveAllItems();
        right->backhistRemoveAllItems(); right->forwardhistRemoveAllItems();
        puts("PASS: larger DPI-aware toolbar and pane history buttons; both panes, modes, popup placement and navigation");
    }
    left->setActive(); select(left);
    CustomActionContext ctx = actions->context();
    assert(ctx.directory == fixture && ctx.opposite == fixture + "/other" && ctx.paths.size() == 2);
    right->setActive();
    assert(actions->context().directory == fixture + "/other");
    left->setActive();
    select(left);
    assert(left->getNumSelectedItems() == 2);
    puts("PASS: native active/opposite pane context follows the active pane");
    right->setActive(); select(left);
    DismissContextMenu dismissPopup(app, left);
    app.addTimeout(&dismissPopup, 1, 30);
    FXEvent menuKey = {}; menuKey.code = KEY_Menu;
    left->onCmdPopupMenu(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_POPUP_MENU), &menuKey);
    assert(left->getNumSelectedItems() == 2 && window->getCurrentPanel() == left);
    puts("PASS: actual file-panel popup preserves multi-selection when activating an inactive pane");

    {
        FXMenuPane menu(window);
        CustomActionMenu* presentation = actions->appendContextMenu(&menu, left);
        menu.create();
        std::vector<FXMenuCascade*> cascades = widgets<FXMenuCascade>(&menu);
        assert(cascades.size() == 1 && cascades[0]->getText() == "Custom Actions");
        FXPopup* root = cascades[0]->getMenu();
        assert(root->numChildren() == 3);
        assert(dynamic_cast<FXMenuCommand*>(root->getLast())->getText() == "Edit Custom Actions...");
        FXMenuCascade* utilities = dynamic_cast<FXMenuCascade*>(root->getFirst()); assert(utilities);
        FXMenuCascade* nested = dynamic_cast<FXMenuCascade*>(utilities->getMenu()->getFirst()); assert(nested);
        FXMenuCommand* record = dynamic_cast<FXMenuCommand*>(nested->getMenu()->getFirst()); assert(record);
        assert(record->getText() == "Record");
        assert(left->getNumSelectedItems() == 2);
        delete presentation;
    }
    left->getList()->killSelection();
    {
        FXMenuPane menu(window);
        CustomActionMenu* presentation = actions->appendContextMenu(&menu, left);
        FXMenuCascade* cascade = dynamic_cast<FXMenuCascade*>(menu.getLast()); assert(cascade);
        FXPopup* popup = cascade->getMenu();
        assert(popup->numChildren() == 3 && dynamic_cast<FXMenuCommand*>(popup->getFirst())->getText() == "Background");
        assert(dynamic_cast<FXMenuCommand*>(popup->getLast())->getText() == "Edit Custom Actions...");
        delete presentation;
    }
    {
        std::vector<CustomAction> saved = actions->model.items;
        actions->model.items.clear();
        FXMenuPane menu(window);
        CustomActionMenu* presentation = actions->appendContextMenu(&menu, left);
        FXMenuCascade* cascade = dynamic_cast<FXMenuCascade*>(menu.getLast()); assert(cascade);
        assert(cascade->getMenu()->numChildren() == 1 &&
               dynamic_cast<FXMenuCommand*>(cascade->getMenu()->getFirst())->getText() == "Edit Custom Actions...");
        delete presentation;
        actions->model.items = saved;
    }
    puts("PASS: context cascades filter applicability and always include Edit, even with no configured actions");

    {
        std::vector<FXMenuTitle*> titles = widgets<FXMenuTitle>(window);
        FXPopup* customMenu = NULL;
        for (unsigned i = 0; i < titles.size(); ++i)
        { if (titles[i]->getText() == "Custom Actions") { customMenu = titles[i]->getMenu(); } }
        assert(customMenu && customMenu->numChildren() == 6);
        assert(dynamic_cast<FXMenuCommand*>(customMenu->getLast())->getText() == "Edit Custom Actions...");
        FXMenuCascade* utilities = dynamic_cast<FXMenuCascade*>(customMenu->getFirst()); assert(utilities);
        utilities->getTarget()->handle(utilities, FXSEL(SEL_UPDATE, utilities->getSelector()), NULL);
        assert(!utilities->isEnabled());
        FXMenuCommand* unmatched = dynamic_cast<FXMenuCommand*>(customMenu->getFirst()->getNext()->getNext()->getNext());
        assert(unmatched && unmatched->getText() == "Unmatched");
        unmatched->getTarget()->handle(unmatched, FXSEL(SEL_UPDATE, unmatched->getSelector()), NULL);
        assert(!unmatched->isEnabled());
    }
    puts("PASS: top-level Custom Actions menu retains all configured groups/actions, disables inapplicable entries, and includes Edit");

    std::vector<FXToolBar*> bars = widgets<FXToolBar>(window);
    assert(bars.size() >= 6);
    FXToolBar* custom = NULL;
    for (unsigned i = 0; i < bars.size(); ++i)
    {
        std::vector<FXLabel*> labels = widgets<FXLabel>(bars[i]);
        for (unsigned n = 0; n < labels.size(); ++n) { assert(labels[n]->getText() != "Custom:"); }
        std::vector<FXMenuButton*> buttons = widgets<FXMenuButton>(bars[i]);
        for (unsigned n = 0; n < buttons.size(); ++n)
        { if (buttons[n]->getText() == "Utilities") { custom = bars[i]; } }
    }
    assert(custom);
    std::vector<FXMenuButton*> menuButtons = widgets<FXMenuButton>(custom);
    std::vector<FXMenuButton*> folders;
    for (unsigned i = 0; i < menuButtons.size(); ++i)
    { if (menuButtons[i]->getText().find("Utilities") == 0) { folders.push_back(menuButtons[i]); } }
    assert(folders.size() == 1);
    folders[0]->getTarget()->handle(folders[0], FXSEL(SEL_UPDATE, folders[0]->getSelector()), NULL);
    assert(!folders[0]->isEnabled());
    select(left);
    folders[0]->setFocus();
    assert(left->getNumSelectedItems() == 2);
    folders[0]->getTarget()->handle(folders[0], FXSEL(SEL_UPDATE, folders[0]->getSelector()), NULL);
    assert(folders[0]->isEnabled());
    actions->handle(NULL, FXSEL(SEL_COMMAND, CustomActionsUI::ID_TOGGLE), NULL); assert(!custom->shown());
    actions->handle(NULL, FXSEL(SEL_COMMAND, CustomActionsUI::ID_TOGGLE), NULL); assert(custom->shown());
    custom->layout();
    FXButton* editorButton = NULL;
    for (FXButton* button : widgets<FXButton>(custom))
    { if (button->getTipText() == "Edit Custom Actions") { editorButton = button; } }
    assert(editorButton && editorButton->getX() > folders[0]->getX());
    assert(editorButton->getText().empty() && editorButton->getIcon() &&
           editorButton->getIcon() != minirunicon);
    assert(editorButton->getIcon() == miniprefsicon);
    assert(editorButton->getTarget() == actions && editorButton->getSelector() == CustomActionsUI::ID_EDIT);
    assert(editorButton->getX() + editorButton->getWidth() >= custom->getWidth() - 8);
    mouseClick(editorButton, 3);
    pump(app, 300);
    for (FXMenuPane* menu : widgets<FXMenuPane>(app.getRootWindow())) { assert(!menu->shown()); }
    DismissDialog editFromToolbar(app, "Custom Actions", "Cancel");
    app.addTimeout(&editFromToolbar, 1, 200);
    click(custom, "Edit Custom Actions");
    assert(left->getNumSelectedItems() == 2);
    puts("PASS: custom toolbar keeps labeled actions, live applicability, show/hide and rightmost Settings editor; ignores old size/display overrides and has no presentation menu");
    ctx = actions->context();
    actions->run(2, ctx);
    pump(app, 600);
    std::string expected;
    for (unsigned n = 0; n < ctx.paths.size(); ++n) { expected += ctx.paths[n].text(); expected += '\0'; }
    assert(read(fixture + "/observed") == expected);
    std::vector<CommandWindow*> outputs = widgets<CommandWindow>(app.getRootWindow());
    assert(outputs.empty());
    puts("PASS: normal successful action launches silently and destroys its hidden output window");
    actions->model.items[2].showOutput = true;
    actions->run(2, ctx); pump(app, 600);
    outputs = widgets<CommandWindow>(app.getRootWindow());
    assert(outputs.size() == 1);
    finishCommand(app, outputs[0]);
    std::vector<FXText*> texts = widgets<FXText>(outputs[0]);
    if (texts.size() == 1 && texts[0]->getText().find("Custom action exit status: 0") < 0)
    { fprintf(stderr, "Captured command output:\n%s\n", texts[0]->getText().text()); }
    assert(texts.size() == 1 && texts[0]->getText().find("Custom action exit status: 0") >= 0);
    outputs[0]->onCmdClose(NULL, 0, NULL);
    actions->model.items[2].showOutput = false;
    actions->model.items[2].command = "command-that-does-not-exist-for-swordfish";
    actions->run(2, ctx);
    outputs = widgets<CommandWindow>(app.getRootWindow());
    assert(outputs.size() == 1 && !outputs[0]->shown());
    usleep(200000);
    window->onSigHarvest(NULL, 0, NULL);
    pump(app, 600);
    outputs = widgets<CommandWindow>(app.getRootWindow()); assert(outputs.size() == 1);
    finishCommand(app, outputs[0]);
    assert(outputs[0]->shown());
    texts = widgets<FXText>(outputs[0]);
    assert(texts[0]->getText().find("Custom action exit status: 127") >= 0);
    outputs[0]->onCmdClose(NULL, 0, NULL);
    actions->model.items[2].command = "exit 7";
    actions->run(2, ctx); pump(app, 600);
    outputs = widgets<CommandWindow>(app.getRootWindow());
    assert(outputs.size() == 1 && outputs[0]->shown());
    assert(widgets<FXText>(outputs[0])[0]->getText().find("Custom action exit status: 7") >= 0);
    outputs[0]->onCmdClose(NULL, 0, NULL);
    actions->model.items[2].command = model.items[2].command;
    assert(left->getNumSelectedItems() == 2);
    puts("PASS: host-safe execution receives exact quoted multi-selection; command errors and exit status visible; selection retained");

    actions->model.items[2].confirm = true;
    DismissDialog cancelConfirmation(app, "Confirm Custom Action", "&Cancel");
    app.addTimeout(&cancelConfirmation, 1, 30);
    actions->run(2, ctx);
    assert(widgets<CommandWindow>(app.getRootWindow()).empty());
    actions->model.items[2].confirm = false;
    actions->model.items[2].terminal = true;
    actions->model.items[2].showOutput = true;
    app.reg().writeStringEntry("PROGS", "xterm", (fixture + "/../fake-terminal").text());
    actions->run(2, ctx); pump(app, 300);
    outputs = widgets<CommandWindow>(app.getRootWindow()); assert(outputs.size() == 1);
    finishCommand(app, outputs[0]);
    texts = widgets<FXText>(outputs[0]);
    assert(texts[0]->getText().find("Custom action exit status: 0") >= 0 &&
           texts[0]->getText().find("Press Enter to close") >= 0);
    outputs[0]->onCmdClose(NULL, 0, NULL);
    actions->model.items[2].terminal = false;
    actions->model.items[2].showOutput = false;
    puts("PASS: confirmation cancellation prevents execution; configured-terminal argument path runs and retains visible completion status");

    {
        CustomAction legacy = actions->model.items[2];
        FXString executable = fixture + "/program '$(literal) name";
        FXString cwd = fixture + "/cwd '$(literal) name";
        assert(mkdir(cwd.text(), 0700) == 0);
        {
            std::ofstream script(executable.text());
            script << "#!/bin/sh\nprintf '%s\\n' \"$PWD\"\nprintf '%s\\0' \"$@\"\n";
        }
        assert(chmod(executable.text(), 0700) == 0);
        CustomAction& action = actions->model.items[2];
        action.programMode = true; action.program = executable; action.workingDirectory = cwd;
        action.parameters = "%F %c > %p/program-observed";
        actions->run(2, ctx); pump(app, 600);
        std::string expected = cwd.text(); expected += '\n';
        for (const FXString& path : ctx.paths) { expected += path.text(); expected += '\0'; }
        expected += '2'; expected += '\0';
        assert(read(fixture + "/program-observed") == expected);
        assert(widgets<CommandWindow>(app.getRootWindow()).empty());
        DismissDialog missingProgram(app, "Cannot Run Custom Action", "&OK");
        action.program = fixture + "/missing-program";
        app.addTimeout(&missingProgram, 1, 20); actions->run(2, ctx);
        assert(widgets<CommandWindow>(app.getRootWindow()).empty());
        action.program = executable; action.workingDirectory = fixture + "/missing-cwd";
        DismissDialog missingDirectory(app, "Cannot Run Custom Action", "&OK");
        app.addTimeout(&missingDirectory, 1, 20); actions->run(2, ctx);
        assert(widgets<CommandWindow>(app.getRootWindow()).empty());
        actions->model.items[2] = legacy;
        assert(unlink(executable.text()) == 0 && rmdir(cwd.text()) == 0);
    }
    puts("PASS: program-mode execution safely handles hostile executable/cwd paths and parameters; missing executable/cwd errors are visible");

    EditorAutomation automation(app, fixture + "/large.png");
    app.addTimeout(&automation, 1, 30);
    actions->handle(NULL, FXSEL(SEL_COMMAND, CustomActionsUI::ID_EDIT), NULL);
    assert(actions->model.items.size() == 9);
    const CustomAction* addedFolder = NULL; const CustomAction* addedAction = NULL;
    for (unsigned i = 0; i < actions->model.items.size(); ++i)
    {
        if (actions->model.items[i].name == "Added folder") { addedFolder = &actions->model.items[i]; }
        if (actions->model.items[i].name == "Added action") { addedAction = &actions->model.items[i]; }
    }
    assert(addedFolder && addedAction && addedFolder->toolbar && addedAction->toolbar && addedAction->background &&
           addedAction->showOutput && addedFolder->icon == fixture + "/large.png" && addedAction->icon == addedFolder->icon &&
           addedAction->parent == addedFolder->id && !addedAction->programMode && addedAction->program.empty() &&
           addedAction->parameters.empty() && addedAction->workingDirectory == "%o" &&
           addedAction->command == "printf '%%s\\n' %p");
    CustomActions persisted; assert(persisted.load(app.reg(), error) && persisted.items.size() == 9);
    assert(app.reg().write());
    FXRegistry disk("SwordfishCustomActionsTest", "SwordfishCustomActionsTest");
    assert(disk.read()); CustomActions reread; assert(reread.load(disk, error) && reread.items.size() == 9);
    puts("PASS: single command/cwd fields and Browse controls, no preview or mode switch, toolbar option on Appearance Conditions, scrolling legend, Basic/Appearance tabs, icon bounds, toolbar/output flags, Save and disk reload");
    puts("PASS: maximizable categorized system-icon grid, search/paging/select; normal image chooser with live left preview, invalid-image errors, cancel and No Icon routes");
    DismissDialog cancelEditor(app, "Custom Actions", "Cancel");
    app.addTimeout(&cancelEditor, 1, 30);
    actions->handle(NULL, FXSEL(SEL_COMMAND, CustomActionsUI::ID_EDIT), NULL);
    assert(actions->model.items.size() == 9);
    assert(reread.load(app.reg(), error) && reread.items.size() == 9);
    puts("PASS: editor Cancel leaves persisted actions unchanged");
    {
        const FXString appRun = FXPath::directory(prefix) + "/AppRun";
        const FXString integrationIcons = prefix + "/share/icons/hicolor/scalable/apps";
        assert(xf_mkpath(integrationIcons.text(), 0700) == 0);
        assert(FXFile::copy(fixture + "/wide.svg", integrationIcons + "/swordfish.svg"));
        {
            std::ofstream launcher(appRun.text());
            launcher << "#!/bin/sh\nexit 0\n";
            assert(launcher.good());
        }
        assert(chmod(appRun.text(), 0700) == 0);
        const char* previousPortableValue = getenv("SWORDFISH_PORTABLE");
        const bool hadPortable = previousPortableValue != NULL;
        const FXString previousPortable = previousPortableValue ? previousPortableValue : "";
        assert(getenv("SWORDFISH_HOST_PATH"));
        const FXString previousHostPath = getenv("SWORDFISH_HOST_PATH");
        assert(setenv("SWORDFISH_HOST_PATH", (homedir + "/.local/bin:/usr/bin:/bin").text(), 1) == 0);
        assert(setenv("SWORDFISH_PORTABLE", "1", 1) == 0);
        SystemIntegration::Selection integration;
        integration.desktopShortcut = integration.applicationsMenu = integration.commandSymlink = true;
        integration.commandPath = (homedir + "/.local/bin/swordfish").text();
        std::string integrationError, integrationWarning;
        assert(SystemIntegration::applySelection(integration, integrationError, integrationWarning));
        if (hadPortable)
            assert(setenv("SWORDFISH_PORTABLE", previousPortable.text(), 1) == 0);
        else
            assert(unsetenv("SWORDFISH_PORTABLE") == 0);
        const FXString desktopPath = homedir + "/Desktop/Swordfish.desktop";
        const FXString menuPath = host + "/applications/swordfish.desktop";
        const FXString statePath = fixture + "/host-config/swordfish/system-integration.state";
        auto fileState = [](const FXString& path)
        {
            struct stat info;
            assert(lstat(path.text(), &info) == 0);
            return info;
        };
        auto unchanged = [&](const FXString& path, const struct stat& initial)
        {
            struct stat current = fileState(path);
            assert(current.st_dev == initial.st_dev && current.st_ino == initial.st_ino &&
                   current.st_mode == initial.st_mode && current.st_size == initial.st_size &&
                   current.st_mtim.tv_sec == initial.st_mtim.tv_sec &&
                   current.st_mtim.tv_nsec == initial.st_mtim.tv_nsec &&
                   current.st_ctim.tv_sec == initial.st_ctim.tv_sec &&
                   current.st_ctim.tv_nsec == initial.st_ctim.tv_nsec);
        };
        const struct stat originalDesktopState = fileState(desktopPath);
        const struct stat originalMenuState = fileState(menuPath);
        const struct stat originalIntegrationState = fileState(statePath);
        std::ifstream desktopFile(desktopPath.text());
        const std::string originalDesktop((std::istreambuf_iterator<char>(desktopFile)),
                                           std::istreambuf_iterator<char>());
        PreferencesBox preferences(window);
        FXGroupBox* toolbarIcons = NULL;
        for (FXGroupBox* group : widgets<FXGroupBox>(&preferences))
            if (group->getText() == "Tool Bar Icon Size") { toolbarIcons = group; }
        assert(toolbarIcons);
        std::vector<FXRadioButton*> sizes = widgets<FXRadioButton>(toolbarIcons);
        assert(sizes.size() == 3 && widgets<FXListBox>(toolbarIcons).empty());
        assert(sizes[0]->getText() == "16 px" && sizes[1]->getText() == "24 px" &&
               sizes[2]->getText() == "32 px");
        std::vector<FXTabItem*> tabs = widgets<FXTabItem>(&preferences);
        bool customTab = false;
        for (FXTabItem* tab : tabs) { if (tab->getText() == "Custom Actions") { customTab = true; } }
        assert(customTab);
        DismissDialog cancelPreferencesEditor(app, "Custom Actions", "Cancel");
        app.addTimeout(&cancelPreferencesEditor, 1, 30);
        click(&preferences, "Edit Custom Actions...");
        assert(actions->model.items.size() == 9);

        auto checkToolbarIcons = [&](FXint size)
        {
            const FXint side = minirunicon->getHeight() * size / 16;
            FXIcon* folderIcon = NULL;
            for (FXMenuButton* button : widgets<FXMenuButton>(custom))
                if (button->getText() == "Utilities") { folderIcon = button->getIcon(); }
            FXIcon* actionIcon = NULL;
            FXIcon* editorIcon = NULL;
            for (FXButton* button : widgets<FXButton>(custom))
            {
                if (button->getText() == "Background") { actionIcon = button->getIcon(); }
                if (button->getTipText() == "Edit Custom Actions") { editorIcon = button->getIcon(); }
            }
            assert(folderIcon && actionIcon && editorIcon);
            if (folderIcon->getWidth() != side || folderIcon->getHeight() != side / 2)
                fprintf(stderr, "Folder icon at size %d: %dx%d, expected %dx%d\n", size,
                        folderIcon->getWidth(), folderIcon->getHeight(), side, side / 2);
            assert(folderIcon->getWidth() == side && folderIcon->getHeight() == side / 2);
            assert(actionIcon->getWidth() == side && actionIcon->getHeight() == side / 2);
            assert(editorIcon->getWidth() == miniprefsicon->getWidth() * size / 16);
            assert(editorIcon->getHeight() == miniprefsicon->getHeight() * size / 16);
            assert(actions->icon(0)->getWidth() == minirunicon->getHeight());
            assert(actions->icon(0)->getHeight() == minirunicon->getHeight() / 2);
            bool originalBack = false;
            for (FXButton* button : widgets<FXButton>(window))
                if (button->getTarget() == window && FXSELID(button->getSelector()) == XFileExplorer::ID_DIR_BACK)
                    originalBack = button->getIcon() == minidirbackicon;
            assert(originalBack);
        };
        checkToolbarIcons(16);

        ToolbarSizePreferenceAutomation cancelSize(preferences, 0, 2, false);
        app.addTimeout(&cancelSize, 1, 30);
        assert(preferences.execute(PLACEMENT_OWNER) == 0);
        assert(xf_toolbariconsize(&app, "customtoolbariconsize") == 16);
        checkToolbarIcons(16);

        for (FXint choice : {1, 2, 0})
        {
            const FXint previous = choice == 1 ? 0 : choice == 2 ? 1 : 2;
            const FXint size = 16 + choice * 8;
            ToolbarSizePreferenceAutomation changeSize(preferences, previous, choice, true);
            app.addTimeout(&changeSize, 1, 30);
            assert(preferences.execute(PLACEMENT_OWNER) == 1);
            assert(xf_toolbariconsize(&app, "customtoolbariconsize") == size);
            FXRegistry settingsOnDisk("SwordfishCustomActionsTest", "SwordfishCustomActionsTest");
            assert(settingsOnDisk.read());
            assert(settingsOnDisk.readIntEntry("SETTINGS", "customtoolbariconsize", 0) == size);
            checkToolbarIcons(size);
            unchanged(desktopPath, originalDesktopState);
            unchanged(menuPath, originalMenuState);
            unchanged(statePath, originalIntegrationState);
        }
        {
            std::ofstream modified(desktopPath.text(), std::ios::app);
            modified << "\n# Desktop environment metadata\n";
            assert(modified.good());
        }
        const struct stat modifiedDesktopState = fileState(desktopPath);
        // A launcher the desktop has reformatted is still recognised as Swordfish's own
        ToolbarSizePreferenceAutomation sizeWithModifiedLauncher(preferences, 0, 1, true);
        app.addTimeout(&sizeWithModifiedLauncher, 1, 30);
        assert(preferences.execute(PLACEMENT_OWNER) == 1);
        assert(xf_toolbariconsize(&app, "customtoolbariconsize") == 24);
        checkToolbarIcons(24);
        unchanged(desktopPath, modifiedDesktopState);
        unchanged(menuPath, originalMenuState);
        unchanged(statePath, originalIntegrationState);
        {
            std::ofstream unrelated(desktopPath.text());
            unrelated << "[Desktop Entry]\nType=Application\nName=Other Program\nExec=other\n";
            assert(unrelated.good());
        }
        const struct stat unrelatedDesktopState = fileState(desktopPath);
        ToolbarSizePreferenceAutomation sizeWithUnrelatedLauncher(preferences, 1, 0, true, false);
        app.addTimeout(&sizeWithUnrelatedLauncher, 1, 30);
        assert(preferences.execute(PLACEMENT_OWNER) == 1);
        assert(xf_toolbariconsize(&app, "customtoolbariconsize") == 16);
        checkToolbarIcons(16);
        unchanged(desktopPath, unrelatedDesktopState);
        unchanged(menuPath, originalMenuState);
        unchanged(statePath, originalIntegrationState);
        {
            std::ofstream restored(desktopPath.text());
            restored << originalDesktop;
            assert(restored.good());
        }
        puts("PASS: 16/24/32 px radio buttons are exclusive; toolbar saves leave integrated launchers and ownership state untouched, a desktop-reformatted launcher stays recognised and an unrelated one locks only the integration controls");

        assert(!app.reg().readUnsignedEntry("SETTINGS", "show_pathlinker", false));
        checkAddressBarMode(left, false);
        checkAddressBarMode(right, false);
        AddressModePreferenceAutomation cancelAddress(preferences, 0, 1, false);
        app.addTimeout(&cancelAddress, 1, 30);
        assert(preferences.execute(PLACEMENT_OWNER) == 0);
        assert(!app.reg().readUnsignedEntry("SETTINGS", "show_pathlinker", false));

        AddressModePreferenceAutomation saveAddress(preferences, 0, 1, true);
        app.addTimeout(&saveAddress, 1, 30);
        assert(preferences.execute(PLACEMENT_OWNER) == 1);
        assert(app.reg().readUnsignedEntry("SETTINGS", "show_pathlinker", false));
        FXRegistry settingsOnDisk("SwordfishCustomActionsTest", "SwordfishCustomActionsTest");
        assert(settingsOnDisk.read());
        assert(settingsOnDisk.readUnsignedEntry("SETTINGS", "show_pathlinker", false));
        checkAddressBarMode(left, true);
        checkAddressBarMode(right, true);

        AddressModePreferenceAutomation cancelText(preferences, 1, 0, false);
        app.addTimeout(&cancelText, 1, 30);
        assert(preferences.execute(PLACEMENT_OWNER) == 0);
        assert(app.reg().readUnsignedEntry("SETTINGS", "show_pathlinker", false));

        AddressModePreferenceAutomation saveText(preferences, 1, 0, true);
        app.addTimeout(&saveText, 1, 30);
        assert(preferences.execute(PLACEMENT_OWNER) == 1);
        assert(!app.reg().readUnsignedEntry("SETTINGS", "show_pathlinker", true));
        FXRegistry textSettingsOnDisk("SwordfishCustomActionsTest", "SwordfishCustomActionsTest");
        assert(textSettingsOnDisk.read());
        assert(!textSettingsOnDisk.readUnsignedEntry("SETTINGS", "show_pathlinker", true));
        checkAddressBarMode(left, false);
        checkAddressBarMode(right, false);

        left->onCmdTogglePathMode(NULL, 0, NULL);
        assert(app.reg().readUnsignedEntry("SETTINGS", "show_pathlinker", false));
        checkAddressBarMode(left, true);
        checkAddressBarMode(right, true);
        AddressModePreferenceAutomation switchedBreadcrumbs(preferences, 1, 1, false);
        app.addTimeout(&switchedBreadcrumbs, 1, 30);
        assert(preferences.execute(PLACEMENT_OWNER) == 0);
        left->onCmdTogglePathMode(NULL, 0, NULL);
        assert(!app.reg().readUnsignedEntry("SETTINGS", "show_pathlinker", true));
        checkAddressBarMode(left, false);
        checkAddressBarMode(right, false);
        AddressModePreferenceAutomation switchedAddress(preferences, 0, 0, false);
        app.addTimeout(&switchedAddress, 1, 30);
        assert(preferences.execute(PLACEMENT_OWNER) == 0);
        assert(!app.reg().readUnsignedEntry("SETTINGS", "show_pathlinker", true));
        puts("PASS: named address mode choices, cancel, saved preference, panel refresh and toolbar synchronization");
        RemoveIntegrationPreferenceAutomation cancelRemoval(preferences, false);
        app.addTimeout(&cancelRemoval, 1, 30);
        assert(preferences.execute(PLACEMENT_OWNER) == 0);
        SystemIntegration::Selection reloadedIntegration;
        assert(SystemIntegration::readSelection(reloadedIntegration, integrationError));
        assert(reloadedIntegration.desktopShortcut && reloadedIntegration.applicationsMenu &&
               reloadedIntegration.commandSymlink);
        RemoveIntegrationPreferenceAutomation removeIntegration(preferences, true);
        app.addTimeout(&removeIntegration, 1, 30);
        assert(preferences.execute(PLACEMENT_OWNER) == 1);
        assert(SystemIntegration::readSelection(reloadedIntegration, integrationError));
        assert(!reloadedIntegration.desktopShortcut && !reloadedIntegration.applicationsMenu &&
               !reloadedIntegration.commandSymlink);
        assert(!FXStat::exists(desktopPath) && !FXStat::exists(menuPath) && !FXStat::exists(statePath));
        assert(setenv("SWORDFISH_HOST_PATH", previousHostPath.text(), 1) == 0);
        puts("PASS: explicit integration changes still confirm and apply; Cancel leaves launchers installed");
    }
    puts("PASS: Preferences Custom Actions editor and custom toolbar sizes apply, persist and cancel without changing menus or main toolbars");
    {
        // An older program/parameters action must open as one command and stay unchanged
        for (CustomAction& action : actions->model.items)
            if (action.id == "record")
            {
                action.programMode = true; action.program = "/usr/bin/printf";
                action.parameters = "'%%s\\n' %F"; action.command = "  printf 'legacy unchanged'  ";
            }
        assert(actions->model.validate(error)); actions->model.save(app.reg());
        std::vector<CustomAction> baseline = actions->model.items;
        UnsavedAutomation dirty(app, false);
        app.addTimeout(&dirty, 1, 10);
        actions->handle(NULL, FXSEL(SEL_COMMAND, CustomActionsUI::ID_EDIT), NULL);
        assert(actions->model.items == baseline);
        CustomActions saved; assert(saved.load(app.reg(), error) && saved.items == baseline);
        UnsavedAutomation duplicate(app, true);
        app.addTimeout(&duplicate, 1, 10);
        actions->handle(NULL, FXSEL(SEL_COMMAND, CustomActionsUI::ID_EDIT), NULL);
        assert(actions->model.items.size() == baseline.size() + 1);
        const CustomAction* original = actions->model.find("record"); assert(original);
        const CustomAction* copy = NULL;
        for (const CustomAction& action : actions->model.items)
            if (action.name == "Record (copy)") { copy = &action; }
        assert(copy && copy->id != original->id);
        CustomAction expected = *original; expected.id = copy->id; expected.name = copy->name;
        assert(*copy == expected);
        FXRegistry disk("SwordfishCustomActionsTest", "SwordfishCustomActionsTest");
        assert(disk.read() && saved.load(disk, error) && saved.items == actions->model.items);
    }
    puts("PASS: Duplicate Action selection/save/reload; properties/editor meaningful dirty comparisons; Cancel/Escape/X Keep editing/Discard");
    delete actions;
    puts("PASS: clean custom toolbar/menu/controller teardown");
    // Like the existing widget suite, exclude legacy application-global teardown.
    std::fflush(stdout);
    std::_Exit(0);
}
