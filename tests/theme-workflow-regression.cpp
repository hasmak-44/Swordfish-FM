#include <fx.h>
#include <FXPNGImage.h>
#include <FXGradientBar.h>
#include "FileList.h"
#include "XFileExplorer.h"
#include "Preferences.h"
#include "ThemeEditor.h"
#include "ColorTools.h"
#include "BackgroundEffect.h"
#include "FileDialog.h"
#include "InputDialog.h"
#include "SearchWindow.h"
#include "icons.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <functional>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

extern FXMainWindow* mainWindow;
extern FXString execpath, homedir, xdgconfighome, xdgdatahome;

class PumpTimer : public FXObject
{
    FXApp& app;
public:
    explicit PumpTimer(FXApp& application) : app(application) {}
    long handle(FXObject*, FXSelector, void*) override { app.stopModal(1); return 1; }
};

class ActionTimer : public FXObject
{
    std::function<void()> action;
public:
    explicit ActionTimer(std::function<void()> callback) : action(callback) {}
    long handle(FXObject*, FXSelector, void*) override { action(); return 1; }
};

class TabGroupNameResponder : public FXObject
{
    FXApp& app;
public:
    explicit TabGroupNameResponder(FXApp& application) : app(application) {}
    long handle(FXObject*, FXSelector, void*) override
    {
        InputDialog* dialog = dynamic_cast<InputDialog*>(app.getModalWindow());
        if (!dialog || dialog->getTitle() != "Bookmark All Tabs as Group")
        {
            app.addTimeout(this, 1, 20);
            return 1;
        }
        dialog->setText("Regression tab group");
        dialog->handle(this, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
        return 1;
    }
};

static void pump(FXApp& app)
{
    PumpTimer timer(app);
    app.addTimeout(&timer, 1, 350);
    app.runModal();
    app.flush(true);
}

static FXWindow* findClass(FXWindow* parent, const FXString& name)
{
    if (parent->getClassName() == name) return parent;
    for (FXWindow* child = parent->getFirst(); child; child = child->getNext())
        if (FXWindow* result = findClass(child, name)) return result;
    return NULL;
}

static FXButton* findButton(FXWindow* parent, const FXString& text)
{
    FXButton* button = dynamic_cast<FXButton*>(parent);
    if (button && button->getText() == text) return button;
    for (FXWindow* child = parent->getFirst(); child; child = child->getNext())
        if (FXButton* result = findButton(child, text)) return result;
    return NULL;
}

static void checkManual(FXApp& app, FXObject* target, FXSelector selector, FXObject* sender, bool theme)
{
    const char* oldPath = getenv("SWORDFISH_HOST_PATH");
    const bool hadPath = oldPath != NULL;
    FXString previous = oldPath ? oldPath : "";
    FXString path = FXString(getenv("SWORDFISH_TEST_MANUAL_LAUNCHER")) + ":/usr/bin:/bin";
    assert(setenv("SWORDFISH_HOST_PATH", path.text(), 1) == 0);
    assert(target->handle(sender, FXSEL(SEL_COMMAND, selector), NULL));
    pump(app);
    if (hadPath) assert(setenv("SWORDFISH_HOST_PATH", previous.text(), 1) == 0);
    else assert(unsetenv("SWORDFISH_HOST_PATH") == 0);
    static int calls = 0;
    std::ifstream input(getenv("SWORDFISH_TEST_MANUAL_CAPTURE"));
    std::string line, last;
    int lines = 0;
    while (std::getline(input, line)) { last = line; ++lines; }
    assert(lines == ++calls);
    std::string suffix = theme ? "/share/swordfish/manual/index.html#theme-editor" :
                                 "/share/swordfish/manual/index.html";
    assert(last.size() >= suffix.size() && last.compare(last.size() - suffix.size(), suffix.size(), suffix) == 0);
}

static void screenshot(FXWindow* window, const char* name)
{
    const char* directory = getenv("SWORDFISH_THEME_SCREENSHOTS");
    if (!directory) return;
    window->getApp()->flush(true);
    Display* display = static_cast<Display*>(window->getApp()->getDisplay());
    int x, y;
    Window child;
    XTranslateCoordinates(display, window->id(), window->getRoot()->id(), 0, 0, &x, &y, &child);
    XImage* screen = XGetImage(display, window->getRoot()->id(), x, y,
                              window->getWidth(), window->getHeight(), AllPlanes, ZPixmap);
    assert(screen);
    FXColor* pixels = NULL;
    assert(FXMALLOC(&pixels, FXColor, window->getWidth() * window->getHeight()));
    auto channel = [](unsigned long pixel, unsigned long mask) -> FXuint
    {
        unsigned int shift = 0;
        while (!(mask & 1)) { mask >>= 1; ++shift; }
        return ((pixel >> shift) & mask) * 255 / mask;
    };
    for (FXint row = 0; row < window->getHeight(); ++row)
        for (FXint column = 0; column < window->getWidth(); ++column)
        {
            unsigned long pixel = XGetPixel(screen, column, row);
            pixels[row * window->getWidth() + column] =
                FXRGB(channel(pixel, screen->red_mask), channel(pixel, screen->green_mask), channel(pixel, screen->blue_mask));
        }
    XDestroyImage(screen);
    FXPNGImage image(window->getApp());
    image.setData(pixels, IMAGE_KEEP | IMAGE_OWNED, window->getWidth(), window->getHeight());
    FXFileStream output;
    assert(output.open(FXString(directory) + "/" + name + ".png", FXStreamSave));
    assert(image.savePixels(output));
}

class ThemeWorkflowRegression
{
public:
    static void run(FXApp& app, XFileExplorer& window)
    {
        PreferencesBox preferences(&window, app.getBackColor(), app.getForeColor(), app.getBackColor(),
                                   app.getSelbackColor(), FXRGB(255, 0, 0), app.getBaseColor());
        preferences.create();
        preferences.rememberPreferences();
        preferences.show(PLACEMENT_SCREEN);
        pump(app);
        assert(preferences.Themes.size() >= NUM_DEFAULT_THEMES);
        const char* builtinNames[] = {
            "Arctic Aurora", "Autumn Leaves", "Carbon Fiber", "Desert Canyon", "Desert Dusk", "Forest Mist",
            "Graphite Copper", "Indigo Weave", "Linen Sage", "Midnight Tide", "Ocean Depth", "Porcelain Blue",
            "Red-Green Accessible Dark", "Red-Green Accessible Light", "Rose Quartz", "Warm Parchment"};
        for (const char* name : builtinNames)
        {
            bool found = false;
            for (size_t i = 1; i < NUM_DEFAULT_THEMES; ++i)
                if (preferences.Themes[i].name == name) found = true;
            assert(found);
        }
        for (const char* removed : {"Swordfish", "GNOME", "KDE", "XFCE", "Gray On Blue"})
            for (size_t i = 1; i < NUM_DEFAULT_THEMES; ++i)
                assert(preferences.Themes[i].name != removed);
        FXint oceanIndex = -1;
        for (FXint i = 1; i < NUM_DEFAULT_THEMES; ++i)
            if (preferences.Themes[i].name == "Ocean Depth") oceanIndex = i;
        assert(oceanIndex >= 0);
        preferences.themesList->setCurrentItem(oceanIndex);
        assert(preferences.onCmdThemeRemove(NULL, 0, NULL) == 0);
        assert(preferences.Themes[oceanIndex].name == "Ocean Depth");
        preferences.themesList->setCurrentItem(0);
        Theme ocean = Theme::current(&app);
        assert(ocean.color[0] == FXRGB(15, 46, 54));
        assert(ocean.color[2] == FXRGB(12, 37, 44));
        assert(ocean.color[4] == FXRGB(191, 67, 42));
        assert(ocean.effect == "fill=gradient colors=#135563,#0A2A33,#05141A direction=vertical");
        assert(preferences.getBackColor() == DialogColors::background);
        assert(preferences.settingsFilter->getBackColor() == DialogColors::field);
        for (const auto& category : preferences.categories)
            assert(category.icon && category.selectedIcon);
        preferences.settingsFilter->setText("copied");
        preferences.onFilter(NULL, 0, NULL);
        preferences.settingsFilter->setText("copy suffix");
        preferences.onFilter(NULL, 0, NULL);
        assert(preferences.currentCategory == PreferencesBox::FILE_OPERATIONS);
        preferences.settingsFilter->setText("no such setting 7391");
        preferences.onFilter(NULL, 0, NULL);
        for (const auto& category : preferences.categories) assert(!category.button->shown());
        preferences.settingsFilter->setText("");
        preferences.onFilter(NULL, 0, NULL);
        preferences.selectCategory(PreferencesBox::APPEARANCE);
        pump(app);
        assert(preferences.categories[PreferencesBox::APPEARANCE].button->getBackColor() == DialogColors::accent);
        screenshot(&preferences, "preferences");
        FXButton* category = preferences.categories[PreferencesBox::APPEARANCE].button;
        {
            FXDCWindow dc(category);
            assert(themeLuminance(dc.readPixel(6, 6)) < 0.15);
        }
        puts("PASS: fixed Preferences palette, category icons and setting-label filtering");

        assert((preferences.getDecorations() & (DECOR_RESIZE | DECOR_MAXIMIZE)) == (DECOR_RESIZE | DECOR_MAXIMIZE));
        assert(dynamic_cast<FXScrollWindow*>(preferences.getFirst()) && !preferences.getFirst()->getNext());
        FXCheckButton* toggle = NULL;
        std::vector<FXButton*> rowButtons;
        std::vector<FXWindow*> pending = {&preferences};
        while (!pending.empty())
        {
            FXWindow* item = pending.back();
            pending.pop_back();
            if (!toggle) toggle = dynamic_cast<FXCheckButton*>(item);
            FXButton* button = dynamic_cast<FXButton*>(item);
            if (button && button->getText() == "OK" && rowButtons.empty())
                for (FXWindow* sibling = button->getParent()->getFirst(); sibling; sibling = sibling->getNext())
                    if (dynamic_cast<FXButton*>(sibling)) rowButtons.push_back((FXButton*)sibling);
            for (FXWindow* child = item->getFirst(); child; child = child->getNext()) pending.push_back(child);
        }
        assert(toggle && toggle->getBackColor() == DialogColors::background && toggle->getDefaultHeight() >= 25);
        assert(rowButtons.size() == 3);
        for (FXButton* button : rowButtons)
        {
            assert(button->getBackColor() == DialogColors::field);
            assert(button->getDefaultWidth() == rowButtons[0]->getDefaultWidth());
        }
        puts("PASS: fixed dialogs are resizable/maximizable/scrollable with large toggles and uniform field buttons");

        FileList* files = window.getCurrentPanel()->getList();
        FXColor before = files->getBackColor();
        FXString effectBefore = app.reg().readStringEntry("SETTINGS", "list_background_effect", "");
        ThemeEditor editor(&preferences, &window, preferences.Themes, 0, ThemeEditor::EDIT_CURRENT);
        editor.create();
        editor.show(PLACEMENT_SCREEN);
        editor.zoom->setCurrentItem(0);
        editor.onPreview(NULL, 0, NULL);
        pump(app);
        FXButton* editorManual = findButton(&editor, "User Manual");
        assert(editorManual);
        checkManual(app, editorManual->getTarget(), editorManual->getSelector(), editorManual, true);
        checkManual(app, &window, XFileExplorer::ID_USER_MANUAL, &window, false);
        ActionTimer editorPickerHelp([&]()
        {
            ColorDialog* picker = dynamic_cast<ColorDialog*>(app.getModalWindow());
            assert(picker);
            FXButton* help = findButton(picker, "User Manual");
            assert(help);
            checkManual(app, help->getTarget(), help->getSelector(), help, true);
            picker->handle(picker, FXSEL(SEL_COMMAND, FXDialogBox::ID_CANCEL), NULL);
        });
        app.addTimeout(&editorPickerHelp, 1, 150);
        editor.onEditProperty(NULL, 0, NULL);
        assert(findClass(&editor, "ThemeMockup"));
        assert(!findClass(&editor, "FilePanel") && !findClass(&editor, "FileList"));
        assert(editor.getBackColor() == DialogColors::background);
        editor.onColor(NULL, 0, reinterpret_cast<void*>(static_cast<FXuval>(FXRGB(20, 45, 70))));
        assert(files->getBackColor() == before);
        editor.onApply(NULL, 0, NULL);
        assert(files->getBackColor() == before);
        assert(app.reg().readStringEntry("SETTINGS", "list_background_effect", "") == effectBefore);
        Theme applied = editor.getApplied();
        editor.onColor(NULL, 0, reinterpret_cast<void*>(static_cast<FXuval>(FXRGB(70, 45, 20))));
        editor.onDiscard(NULL, 0, NULL);
        assert(editor.getDraft() == applied);
        puts("PASS: safe mockup contains no operational file panels; Apply and discard leave live lists/settings unchanged");

        editor.previewEffect("fill=gradient colors=#000000,#FFFFFF direction=horizontal", true);
        editor.previewEffect("fill=gradient colors=#000000,#FF0000,#FFFFFF positions=0,0.3,1 direction=horizontal", true);
        editor.onApply(NULL, 0, NULL);
        editor.onUndo(NULL, 0, NULL);
        assert(editor.getDraft().effect.contains("#000000,#FFFFFF"));
        editor.onUndo(NULL, 0, NULL);
        assert(editor.getDraft().effect == applied.effect);
        editor.onRedo(NULL, 0, NULL);
        assert(editor.getDraft().effect.contains("#000000,#FFFFFF"));
        editor.onRedo(NULL, 0, NULL);
        assert(editor.getDraft().effect.contains("positions=0,0.3,1"));
        assert(files->getBackColor() == before);
        editor.selectElement("tree");
        for (const FXString& key : editor.propertyKeys) assert(key != "effect");
        editor.selectElement("list");
        editor.privacy->setCheck(true);
        editor.onPreview(NULL, 0, NULL);
        pump(app);
        screenshot(&editor, "theme-editor-private");
        editor.privacy->setCheck(false);
        editor.outlines->setCheck(false);
        editor.onPreview(NULL, 0, NULL);
        pump(app);
        screenshot(&editor, "theme-editor");
        {
            FXScrollWindow* column = dynamic_cast<FXScrollWindow*>(editor.properties->getParent()->getParent());
            assert(column);
            FXint width = editor.getWidth(), height = editor.getHeight();
            editor.resize(1000, 560);
            pump(app);
            assert(column->verticalScrollBar()->shown());
            screenshot(&editor, "theme-editor-small");
            editor.resize(width, height);
            pump(app);
        }
        {
            FXWindow* mockup = findClass(&editor, "ThemeMockup");
            Display* display = static_cast<Display*>(app.getDisplay());
            int x, y;
            Window child;
            XTranslateCoordinates(display, files->id(), window.id(), 0, 0, &x, &y, &child);
            double scale = FXMIN(mockup->getWidth() / static_cast<double>(window.getWidth()),
                                 mockup->getHeight() / static_cast<double>(window.getHeight()));
            FXDCWindow dc(mockup);
            FXColor pixel = dc.readPixel((x + files->getWidth() * 0.8) * scale,
                                          (y + files->getHeight() * 0.6) * scale);
            assert(FXREDVAL(pixel) > 200 && FXGREENVAL(pixel) > 80 && FXBLUEVAL(pixel) > 80);
        }
        puts("PASS: separate gradient-stop undo/redo and supported-property filtering");
        ActionTimer background([&]()
        {
            FXWindow* dialog = findClass(app.getRootWindow(), "BackgroundEditor");
            assert(dialog && dialog->getBackColor() == DialogColors::background);
            FXButton* backgroundManual = findButton(dialog, "User Manual");
            assert(backgroundManual);
            checkManual(app, backgroundManual->getTarget(), backgroundManual->getSelector(), backgroundManual, true);
            ActionTimer backgroundPickerHelp([&]()
            {
                ColorDialog* picker = dynamic_cast<ColorDialog*>(app.getModalWindow());
                assert(picker);
                FXButton* help = findButton(picker, "User Manual");
                assert(help);
                checkManual(app, help->getTarget(), help->getSelector(), help, true);
                picker->handle(picker, FXSEL(SEL_COMMAND, FXDialogBox::ID_CANCEL), NULL);
            });
            FXButton* colour = findButton(dialog, "First colour...");
            assert(colour);
            app.addTimeout(&backgroundPickerHelp, 1, 150);
            colour->getTarget()->handle(colour, FXSEL(SEL_COMMAND, colour->getSelector()), NULL);
            FXGradientBar* gradient = static_cast<FXGradientBar*>(findClass(dialog, "FXGradientBar"));
            assert(gradient && gradient->getNumSegments() == 2);
            gradient->moveSegmentMiddle(0, 0.06);
            gradient->getTarget()->handle(gradient, FXSEL(SEL_COMMAND, gradient->getSelector()), NULL);
            BackgroundEffect actual = BackgroundEffect::parse(editor.getDraft().effect);
            assert(actual.midpoints.size() == 2 && std::fabs(actual.midpoints[0] - 0.2) < 0.000001);
            FXString validEffect = editor.getDraft().effect;
            std::vector<FXGradient> invalid(gradient->getNumSegments());
            for (FXint i = 0; i < gradient->getNumSegments(); ++i)
            {
                invalid[i].lower = gradient->getSegmentLower(i);
                invalid[i].middle = gradient->getSegmentMiddle(i);
                invalid[i].upper = gradient->getSegmentUpper(i);
                invalid[i].lowerColor = gradient->getSegmentLowerColor(i);
                invalid[i].upperColor = gradient->getSegmentUpperColor(i);
                invalid[i].blend = GRADIENT_BLEND_LINEAR;
            }
            invalid[0].upper = invalid[0].lower;
            gradient->setGradients(invalid.data(), invalid.size());
            gradient->getTarget()->handle(gradient, FXSEL(SEL_COMMAND, gradient->getSelector()), NULL);
            assert(editor.getDraft().effect == validEffect);
            assert(gradient->getSegmentUpper(0) > gradient->getSegmentLower(0));
            screenshot(dialog, "background-editor");
            FXTabBook* effectTabs = static_cast<FXTabBook*>(findClass(dialog, "FXTabBook"));
            assert(effectTabs && effectTabs->numChildren() >= 6);
            const char* tabShots[] = {"background-pattern", "background-texture"};
            for (FXint tab = 1; tab <= 2; ++tab)
            {
                effectTabs->setCurrent(tab, true);
                pump(app);
                screenshot(dialog, tabShots[tab - 1]);
                FXIconList* samples = static_cast<FXIconList*>(findClass(effectTabs->childAtIndex(tab * 2 + 1),
                                                                         "FXIconList"));
                assert(samples && samples->getNumItems() > 0);
                FXint checked = 0;
                for (FXint item = 0; item < samples->getNumItems(); ++item)
                {
                    // Samples must stay readable on the dark fixed field regardless of the edited theme.
                    FXIcon* icon = samples->getItemBigIcon(item);
                    if (!icon) continue;
                    assert(icon->getData());
                    ++checked;
                    FXint low = 255, high = 0, total = 0, count = icon->getWidth() * icon->getHeight();
                    for (FXint i = 0; i < count; ++i)
                    {
                        FXColor pixel = icon->getData()[i];
                        FXint luma = (FXREDVAL(pixel) * 3 + FXGREENVAL(pixel) * 6 + FXBLUEVAL(pixel)) / 10;
                        low = FXMIN(low, luma);
                        high = FXMAX(high, luma);
                        total += luma;
                    }
                    assert(total / count > 110 && high - low > (tab == 1 ? 40 : 12));
                }
                assert(checked >= 8);
            }
            dialog->handle(dialog, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
        });
        app.addTimeout(&background, 1, 150);
        editor.properties->setCurrentItem(editor.propertyKeys.size() - 1);
        editor.onEditProperty(NULL, 0, NULL);
        assert(files->getBackColor() == before);
        puts("PASS: native gradient midpoint editing and degenerate-segment rejection leave the running app unchanged");

        Theme saved = editor.getApplied();
        saved.name = "Regression theme";
        saved.setProperty("list", "background", "#172D43");
        saved.setProperty("headers", "foreground", "#BFDFFF");
        saved.setProperty("menus", "background", "#253A50");
        FXFontDesc bold;
        app.getNormalFont()->getFontDesc(bold);
        bold.weight = FXFont::Bold;
        FXFont font(&app, bold);
        saved.setProperty("main", "font", font.getFont());
        preferences.copysuffix->setText("unsaved suffix");
        assert(preferences.saveEditedTheme(saved, -1));
        FXString savedThemeFile = xf_userdatahome() + "/themes/colors/Regression theme/theme.swordfish-theme";
        assert(xf_existfile(savedThemeFile));
        {
            FILE* file = fopen(savedThemeFile.text(), "r");
            assert(file);
            char line[512];
            FXbool foundName = false, foundDetails = false;
            while (fgets(line, sizeof(line), file))
            {
                if (FXString(line).trim() == "name=Regression theme") foundName = true;
                if (FXString(line).trim() == "theme_details=" + saved.encodeProperties()) foundDetails = true;
            }
            fclose(file);
            assert(foundName && foundDetails);
        }
        FXString unknownFolder = xf_userdatahome() + "/themes/colors/z-unrecognized";
        FXString duplicateFolder = xf_userdatahome() + "/themes/colors/z-duplicate";
        FXString outsideFolder = xf_userdatahome() + "/outside-theme";
        FXString linkedFolder = xf_userdatahome() + "/themes/colors/z-linked";
        assert(xf_mkpath(unknownFolder.text(), 0755) == 0);
        assert(xf_mkpath(duplicateFolder.text(), 0755) == 0);
        assert(xf_mkpath(outsideFolder.text(), 0755) == 0);
        FXString unknownFile = unknownFolder + "/theme.swordfish-theme";
        FXString duplicateFile = duplicateFolder + "/theme.swordfish-theme";
        {
            std::ofstream output(unknownFile.text());
            output << "not a valid theme\n";
            assert(output.good());
        }
        assert(FXFile::copyFiles(savedThemeFile, duplicateFile, false));
        FXString outsideFile = outsideFolder + "/theme.swordfish-theme";
        assert(FXFile::copyFiles(savedThemeFile, outsideFile, false));
        assert(symlink(outsideFolder.text(), linkedFolder.text()) == 0);
        PreferencesBox loadedPreferences(&window, app.getBackColor(), app.getForeColor(), app.getBackColor(),
                                        app.getSelbackColor(), FXRGB(255, 0, 0), app.getBaseColor());
        bool foundReloadedTheme = false;
        for (const Theme& theme : loadedPreferences.Themes)
            if (theme.name == saved.name && theme.encodeProperties() == saved.encodeProperties())
                foundReloadedTheme = true;
        assert(foundReloadedTheme);
        vector_Theme withoutCustom = loadedPreferences.Themes;
        withoutCustom.erase(withoutCustom.begin() + NUM_DEFAULT_THEMES, withoutCustom.end());
        assert(loadedPreferences.saveCustomThemes(withoutCustom));
        assert(!xf_existfile(savedThemeFile));
        assert(xf_existfile(unknownFile) && xf_existfile(duplicateFile) && xf_existfile(outsideFile));
        FXRegistry disk(app.getAppName(), app.getVendorName());
        assert(disk.read());
        assert(disk.readStringEntry("SETTINGS", "theme_name", "") == saved.name);
        assert(disk.readStringEntry("SETTINGS", "theme_details", "") == saved.encodeProperties());
        assert(files->getBackColor() == FXRGB(23, 45, 67));
        assert(app.getNormalFont()->getFont() == font.getFont());
        Theme reloaded = Theme::current(&app);
        assert(reloaded.property("list", "background") == "#172D43");
        assert(reloaded.effect == saved.effect);
        assert(preferences.copysuffix->getText() == "unsaved suffix");
        preferences.onCmdCancel(NULL, FXSEL(SEL_COMMAND, PreferencesBox::ID_CANCEL), NULL);
        assert(app.reg().readStringEntry("SETTINGS", "theme_name", "") == saved.name);
        assert(files->getBackColor() == FXRGB(23, 45, 67));
        assert(preferences.copysuffix->getText() != "unsaved suffix");
        preferences.show(PLACEMENT_SCREEN);
        preferences.copysuffix->setText("applied suffix");
        preferences.onCmdAccept(NULL, FXSEL(SEL_COMMAND, PreferencesBox::ID_APPLY), NULL);
        assert(preferences.shown());
        assert(FXString(app.reg().readStringEntry("OPTIONS", "copysuffix", "")) == "applied suffix");
        preferences.copysuffix->setText("discarded suffix");
        preferences.onCmdCancel(NULL, FXSEL(SEL_COMMAND, PreferencesBox::ID_CANCEL), NULL);
        assert(preferences.copysuffix->getText() == "applied suffix");
        puts("PASS: explicit editor Save persists/activates every property and survives cancelling unrelated Preferences edits");
        FXMenuPane late(&window);
        FXMenuCommand* command = new FXMenuCommand(&late, "Late menu");
        xf_windowcolors(&late);
        late.create();
        assert(late.getBackColor() == FXRGB(37, 58, 80));
        assert(command->getFont()->getFont() == font.getFont());
        editor.hide();
        preferences.hide();
        pump(app);

        FileDialog chooser(&window, "Fixed file chooser");
        chooser.create();
        assert(chooser.getBackColor() == DialogColors::background);
        FXWindow* list = findClass(&chooser, "FileList");
        assert(list && list->getBackColor() == DialogColors::field);
        assert(!static_cast<FileList*>(list)->hasBackgroundEffect());
        FXMenuPane childMenu(&chooser);
        new FXMenuCommand(&childMenu, "Fixed late menu");
        xf_windowcolors(&childMenu);
        childMenu.create();
        assert(childMenu.getBackColor() == DialogColors::background);
        FXWindow* tabStrip = findClass(&window, "TabButtons");
        assert(tabStrip);
        FXWindow* tabFrame = NULL;
        for (FXWindow* child = tabStrip->getFirst(); child; child = child->getNext())
        {
            if (FXString(child->getClassName()) == FXString("TabFrame") && child->shown())
            {
                tabFrame = child;
                break;
            }
        }
        assert(tabFrame);
        FXDCWindow tabDC(tabFrame);
        assert(tabDC.readPixel(0, 0) == tabFrame->getBackColor());
        assert(tabDC.readPixel(3, 0) == app.getBorderColor());
        screenshot(&window, "theme-active");

        ColorDialog picker(&window, "Native colour picker");
        picker.setRGBA(FXRGBA(32, 96, 160, 180));
        picker.create();
        picker.show(PLACEMENT_SCREEN);
        pump(app);
        screenshot(&picker, "colour-picker");
        {
            FXTabBook* modes = static_cast<FXTabBook*>(findClass(&picker, "FXTabBook"));
            assert(modes && modes->numChildren() == 4);
            for (FXWindow* tab = modes->getFirst(); tab; tab = tab->getNext()->getNext())
                assert(tab->shown() && !static_cast<FXTabItem*>(tab)->getText().empty());
        }
        ColorTools* tools = static_cast<ColorTools*>(findClass(&picker, "ColorTools"));
        assert(tools);
        tools->codes[0]->setText("#12");
        tools->onCode(tools->codes[0], 0, NULL);
        assert(!tools->validInput());
        tools->codes[0]->setText("cmyk(100%, 0%, 0%, 0%)");
        tools->onCode(tools->codes[0], 0, NULL);
        assert(tools->validInput());
        assert(picker.getRGBA() == FXRGB(0, 255, 255));
        Theme paletteTheme = Theme::current(&app);
        tools->setTheme(&paletteTheme);
        tools->scope->setCurrentItem(1);
        tools->onPalette(NULL, FXSEL(SEL_COMMAND, ColorTools::ID_SCOPE), NULL);
        tools->paletteName->setText("Draft colours");
        tools->colorName->setText("Named cyan");
        tools->onPaletteEdit(NULL, FXSEL(SEL_COMMAND, ColorTools::ID_PALETTE_ADD), NULL);
        tools->onPaletteEdit(NULL, FXSEL(SEL_COMMAND, ColorTools::ID_PALETTE_SAVE), NULL);
        assert(paletteTheme.properties["palette.Draft colours"].contains("Named cyan"));
        assert(!Theme::current(&app).properties.count("palette.Draft colours"));
        tools->scope->setCurrentItem(0);
        tools->onPalette(NULL, FXSEL(SEL_COMMAND, ColorTools::ID_SCOPE), NULL);
        tools->paletteName->setText("Shared colours");
        tools->onPaletteEdit(NULL, FXSEL(SEL_COMMAND, ColorTools::ID_PALETTE_ADD), NULL);
        tools->onPaletteEdit(NULL, FXSEL(SEL_COMMAND, ColorTools::ID_PALETTE_SAVE), NULL);
        FXRegistry shared(app.getAppName(), app.getVendorName());
        assert(shared.read() && shared.readIntEntry("COLOR_PALETTES", "count", 0) == 1);
        assert(FXString(shared.readStringEntry("COLOR_PALETTES", "0.name", "")) == "Shared colours");
        assert(!paletteTheme.properties.count("palette.Shared colours"));
        picker.hide();
        puts("PASS: file chooser stays fixed and effect-free; native picker rejects incomplete values and accepts format autodetection");

        FXString fixture = window.getLeftPanel()->getDirectory();
        TabButtons* leftTabs = window.getLeftPanel()->getTabButtons();
        TabButtons* rightTabs = window.getRightPanel()->getTabButtons();
        leftTabs->clearTabs();
        leftTabs->addTab(fixture);
        window.getLeftPanel()->getList()->setViewMode(FileList::VIEW_ICONS);
        leftTabs->addTab(homedir);
        window.getLeftPanel()->getList()->setViewMode(FileList::VIEW_DETAILS);
        rightTabs->clearTabs();
        rightTabs->addTab(homedir);
        window.getRightPanel()->getList()->setViewMode(FileList::VIEW_DETAILS);
        rightTabs->addTab(fixture);
        window.getRightPanel()->getList()->setViewMode(FileList::VIEW_ICONS);
        window.onCmdHorzVertPanels(&window, FXSEL(SEL_COMMAND, XFileExplorer::ID_VERT_PANELS), NULL);
        window.onCmdShowPanels(&window, FXSEL(SEL_COMMAND, XFileExplorer::ID_SHOW_FOLDERS_TWO_PANELS), NULL);
        TabGroupNameResponder groupName(app);
        app.addTimeout(&groupName, 1, 20);
        assert(window.onCmdAddTabGroup(NULL, 0, NULL) == 1);
        assert(window.getNumTabGroups() == 1);
        FXString groupData = app.reg().readStringEntry("TAB GROUPS", "group0", "");
        FXString groupId = groupData.rafter(';');
        FXString groupLayout = "TAB GROUP " + groupId;
        assert(app.reg().readUnsignedEntry(groupLayout.text(), "panel_view", 99) == 3);
        assert(app.reg().readUnsignedEntry(groupLayout.text(), "vert_panels", 0) == 1);
        for (const char* key : {"twopanels_lpanel_pct", "treepanel_tree_pct",
                                "treetwopanels_tree_pct", "treetwopanels_lpanel_pct"})
            assert(app.reg().existingEntry(groupLayout.text(), key));
        window.onCmdShowPanels(&window, FXSEL(SEL_COMMAND, XFileExplorer::ID_SHOW_ONE_PANEL), NULL);
        window.onCmdHorzVertPanels(&window, FXSEL(SEL_COMMAND, XFileExplorer::ID_HORZ_PANELS), NULL);
        assert(window.openTabGroup(0));
        pump(app);
        assert(window.getDirPanel()->shown() && window.getRightPanel()->shown());
        FXSplitter* panelSplitter = dynamic_cast<FXSplitter*>(window.getLeftPanel()->getParent());
        assert(panelSplitter && !(panelSplitter->getSplitterStyle() & SPLITTER_VERTICAL));
        assert(leftTabs->getNumActiveTabs() == 2 && rightTabs->getNumActiveTabs() == 2);
        assert(leftTabs->getCurrentTab() == 1 && rightTabs->getCurrentTab() == 1);
        assert(leftTabs->getPath(0) == fixture && leftTabs->getPath(1) == homedir);
        assert(rightTabs->getPath(0) == homedir && rightTabs->getPath(1) == fixture);
        leftTabs->setCurrentTab(0);
        assert(window.getLeftPanel()->getList()->getViewMode() == FileList::VIEW_ICONS);
        leftTabs->setCurrentTab(1);
        assert(window.getLeftPanel()->getList()->getViewMode() == FileList::VIEW_DETAILS);
        rightTabs->setCurrentTab(0);
        assert(window.getRightPanel()->getList()->getViewMode() == FileList::VIEW_DETAILS);
        rightTabs->setCurrentTab(1);
        assert(window.getRightPanel()->getList()->getViewMode() == FileList::VIEW_ICONS);
        puts("PASS: tab groups restore panel layout/orientation and every tab's view type");
    }
};

int main(int argc, char** argv)
{
    std::setvbuf(stdout, NULL, _IOLBF, 0);
    assert(argc == 3);
    FXString prefix = argv[1], fixture = argv[2];
    FXApp app("SwordfishThemeRegression", "SwordfishThemeRegression");
    app.init(argc, argv);
    app.setBaseColor(FXRGB(15, 46, 54));
    app.setBorderColor(FXRGB(7, 21, 25));
    app.setBackColor(FXRGB(12, 37, 44));
    app.setForeColor(FXRGB(226, 244, 246));
    app.setSelbackColor(FXRGB(191, 67, 42));
    app.setSelforeColor(FXRGB(255, 255, 255));
    homedir = FXSystem::getHomeDirectory();
    xdgconfighome = homedir + "/config";
    xdgdatahome = homedir + "/data";
    execpath = prefix + "/bin/swordfish";
    app.reg().writeStringEntry("SETTINGS", "iconpath", (prefix + "/share/swordfish/icons/default-theme").text());
    app.reg().writeBoolEntry("OPTIONS", "automount", false);
    app.reg().writeBoolEntry("OPTIONS", "welcome_completed", true);
    FXuint status;
    assert(loadAppIcons(&app, &status));
    app.create();
    vector_FXString paths;
    paths.push_back(fixture);
    XFileExplorer* window = new XFileExplorer(&app, paths, 3);
    mainWindow = window;
    window->create();
    window->resize(1380, 880);
    window->show(PLACEMENT_SCREEN);
    window->getCurrentPanel()->setDirectory(fixture);
    window->getCurrentPanel()->getList()->onCmdRefresh(NULL, 0, NULL);
    pump(app);

    FXColor color;
    FXString error;
    assert(ColorTools::parseColor("#1234", color, error) && color == FXRGBA(17, 34, 51, 68));
    assert(ColorTools::parseColor("rgba(10,20,30,50%)", color, error) && color == FXRGBA(10, 20, 30, 128));
    assert(ColorTools::parseColor("hsb(120,100%,100%)", color, error) && color == FXRGB(0, 255, 0));
    assert(ColorTools::parseColor("hsv(360,100%,100%)", color, error) && color == FXRGB(255, 0, 0));
    for (const char* invalid : {"#12", "rgb(256,0,0)", "rgba(0,0,0,2)", "hsv(nan,0,0)", "cmyk(0,0,0,-1)"})
        assert(!ColorTools::parseColor(invalid, color, error) && !error.empty());
    assert(std::fabs(themeContrast(FXRGB(0, 0, 0), FXRGB(255, 255, 255)) - 21) < 0.000001);
    Theme metadata = Theme::current(&app), decoded;
    metadata.setProperty("list", "foreground", "#BFDFFF80");
    metadata.properties["palette.Ocean blue"] = "#123456FF Deep blue|#ABCDEFAA Light blue";
    assert(decoded.decodeProperties(metadata.encodeProperties(), error));
    assert(decoded.properties == metadata.properties);
    assert(!decoded.decodeProperties("main.opacity=bogus", error));
    assert(!decoded.decodeProperties("list.foreground=%00", error));
    assert(!decoded.decodeProperties("main.font=bad\nfont", error));
    std::unique_ptr<FXIcon> gradient(BackgroundEffect::preview(window,
        "fill=gradient colors=#FF000000,#00FF00,#0000FF positions=0,0.25,1 direction=horizontal",
        FXRGB(255, 255, 255), 101));
    assert(gradient->getData()[0] == FXRGB(255, 255, 255));
    assert(gradient->getData()[25] == FXRGB(0, 255, 0));
    assert(gradient->getData()[100] == FXRGB(0, 0, 255));
    gradient.reset(BackgroundEffect::preview(window,
        "fill=gradient colors=#000000,#FFFFFF direction=horizontal midpoints=0.25",
        FXRGB(0, 0, 0), 101));
    assert(gradient->getData()[25] == FXRGB(128, 128, 128));
    puts("PASS: strict colour formats, WCAG contrast, theme metadata round trip, positioned RGBA gradient pixels");
    ThemeWorkflowRegression::run(app, *window);
    puts("PASS: theme workflow regression");
    std::_Exit(0);
}
