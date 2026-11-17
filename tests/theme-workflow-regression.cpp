#include <fx.h>
#include <FXPNGImage.h>
#include <FXJPGImage.h>
#include <FXGradientBar.h>
#include <fxkeys.h>
#include "FileList.h"
#include "XFileExplorer.h"
#include "Preferences.h"
#include "ThemeEditor.h"
#include "ColorTools.h"
#include "BackgroundEffect.h"
#include "BackgroundImage.h"
#include "FileDialog.h"
#include "FontDialog.h"
#include "InputDialog.h"
#include "MessageBox.h"
#include "SearchWindow.h"
#include "icons.h"
#include <cassert>
#include <algorithm>
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

static FXListBox* findListBoxWithItem(FXWindow* parent, const FXString& text)
{
    FXListBox* list = dynamic_cast<FXListBox*>(parent);
    if (list && list->findItem(text) >= 0) return list;
    for (FXWindow* child = parent->getFirst(); child; child = child->getNext())
        if (FXListBox* result = findListBoxWithItem(child, text)) return result;
    return NULL;
}

static void selectChooserFiles(FileDialog* chooser, const FXString& first, const FXString& second)
{
    assert(chooser->getSelectMode() == SELECT_FILE_MULTIPLE);
    chooser->setFilename(first);
    FileList* list = dynamic_cast<FileList*>(findClass(chooser, "FileList"));
    assert(list);
    FXint firstIndex = list->findItem(FXPath::name(first));
    FXint secondIndex = list->findItem(FXPath::name(second));
    assert(firstIndex >= 0 && secondIndex >= 0 && firstIndex != secondIndex);
    list->selectItem(firstIndex);
    list->selectItem(secondIndex);
    assert(list->isItemSelected(firstIndex) && list->isItemSelected(secondIndex));
    std::unique_ptr<FXString[]> selected(chooser->getFilenames());
    assert(selected && selected[0] == first && selected[1] == second && selected[2].empty());
}

static void selectChooserFile(FileDialog* chooser, const FXString& path)
{
    assert(chooser->getSelectMode() == SELECT_FILE_MULTIPLE);
    chooser->setFilename(path);
    FileList* list = dynamic_cast<FileList*>(findClass(chooser, "FileList"));
    assert(list);
    FXint index = list->findItem(FXPath::name(path));
    assert(index >= 0);
    list->selectItem(index);
    std::unique_ptr<FXString[]> selected(chooser->getFilenames());
    assert(selected && selected[0] == FXPath::absolute(path) && selected[1].empty());
}

static void chooseFolderInDialog(FileDialog* chooser, const FXString& path)
{
    assert(chooser->getSelectMode() == SELECT_FILE_DIRECTORY);
    chooser->setFilename(path);
    assert(chooser->getFilename() == FXPath::absolute(path));
    chooser->handle(chooser, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
}

static FXLabel* findLabel(FXWindow* parent, const FXString& text)
{
    FXLabel* label = dynamic_cast<FXLabel*>(parent);
    if (label && !dynamic_cast<FXButton*>(parent) && label->getText() == text) return label;
    for (FXWindow* child = parent->getFirst(); child; child = child->getNext())
        if (FXLabel* result = findLabel(child, text)) return result;
    return NULL;
}

// Clicks a gallery tile: opens a folder or applies a graphic.
static void activateItem(FXIconList* gallery, const FXString& text)
{
    FXint index = gallery->findItem(text);
    assert(index >= 0);
    gallery->setCurrentItem(index);
    gallery->getTarget()->handle(gallery, FXSEL(SEL_COMMAND, gallery->getSelector()), (void*)(FXival)index);
}

static void clickItem(FXApp& app, FXIconList* gallery, const FXString& text)
{
    FXint index = gallery->findItem(text);
    assert(index >= 0);
    gallery->makeItemVisible(index);
    pump(app);
    FXEvent event = {};
    FXint x = gallery->getXPosition() + (index % gallery->getNumCols()) * gallery->getItemSpace() +
              gallery->getItemSpace() / 2;
    FXint y = gallery->getYPosition() + (index / gallery->getNumCols()) * gallery->getItemHeight() + 8;
    assert(x >= 0 && x < gallery->getWidth() && y >= 0 && y < gallery->getHeight());
    assert(gallery->getItemAt(x, y) == index);
    event.win_x = event.click_x = x;
    event.win_y = event.click_y = y;
    gallery->translateCoordinatesTo(event.root_x, event.root_y, app.getRootWindow(), x, y);
    event.rootclick_x = event.root_x;
    event.rootclick_y = event.root_y;
    event.code = event.click_button = 1;
    event.click_count = 1;
    gallery->handle(gallery, FXSEL(SEL_LEFTBUTTONPRESS, 0), &event);
    gallery->handle(gallery, FXSEL(SEL_LEFTBUTTONRELEASE, 0), &event);
    pump(app);
}

static void pressKey(FXIconList* gallery, FXuint code)
{
    FXEvent event;
    event.type = SEL_KEYPRESS;
    event.code = code;
    event.state = 0;
    gallery->handle(gallery, FXSEL(SEL_KEYPRESS, 0), &event);
}

static FXbool galleryFillsWidth(FXIconList* gallery)
{
    return gallery->getItemSpace() >= 108 &&
           gallery->getWidth() - gallery->getNumCols() * gallery->getItemSpace() < 40;
}

static FXbool itemShowsPixel(FXIconList* list, FXint index, FXColor color)
{
    Display* display = static_cast<Display*>(list->getApp()->getDisplay());
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(display, list->id(), &attributes) || attributes.map_state != IsViewable)
        return false;
    XImage* image = XGetImage(display, list->id(), 0, 0, list->getWidth(), list->getHeight(), AllPlanes, ZPixmap);
    if (!image) return false;
    auto channel = [](unsigned long pixel, unsigned long mask) -> FXuint
    {
        unsigned int shift = 0;
        while (!(mask & 1)) { mask >>= 1; ++shift; }
        return ((pixel >> shift) & mask) * 255 / mask;
    };
    FXbool found = false;
    for (FXint y = 0; y < list->getHeight() && !found; ++y)
        for (FXint x = 0; x < list->getWidth() && !found; ++x)
            if (list->getItemAt(x, y) == index)
            {
                unsigned long pixel = XGetPixel(image, x, y);
                found = channel(pixel, image->red_mask) == FXREDVAL(color) &&
                        channel(pixel, image->green_mask) == FXGREENVAL(color) &&
                        channel(pixel, image->blue_mask) == FXBLUEVAL(color);
            }
    XDestroyImage(image);
    return found;
}

static FXbool windowShowsPixel(FXWindow* window, FXColor color)
{
    Display* display = static_cast<Display*>(window->getApp()->getDisplay());
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(display, window->id(), &attributes) || attributes.map_state != IsViewable)
        return false;
    XImage* image = XGetImage(display, window->id(), 0, 0, window->getWidth(), window->getHeight(), AllPlanes, ZPixmap);
    if (!image) return false;
    auto channel = [](unsigned long pixel, unsigned long mask) -> FXuint
    {
        unsigned int shift = 0;
        while (!(mask & 1)) { mask >>= 1; ++shift; }
        return ((pixel >> shift) & mask) * 255 / mask;
    };
    FXbool found = false;
    for (FXint y = 0; y < window->getHeight() && !found; ++y)
        for (FXint x = 0; x < window->getWidth() && !found; ++x)
        {
            unsigned long pixel = XGetPixel(image, x, y);
            found = channel(pixel, image->red_mask) == FXREDVAL(color) &&
                    channel(pixel, image->green_mask) == FXGREENVAL(color) &&
                    channel(pixel, image->blue_mask) == FXBLUEVAL(color);
        }
    XDestroyImage(image);
    return found;
}

// A full theme apply repaints the whole window; slow Xvfb hosts may need more than one pump.
static FXbool showsPixelSoon(FXApp& app, FXWindow* window, FXColor color)
{
    for (FXint attempt = 0; attempt < 10; ++attempt)
    {
        if (windowShowsPixel(window, color)) return true;
        pump(app);
    }
    return windowShowsPixel(window, color);
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

static void solidPng(FXApp& app, const FXString& path, FXColor color)
{
    FXColor* pixels = NULL;
    assert(FXMALLOC(&pixels, FXColor, 4));
    for (FXint i = 0; i < 4; ++i) pixels[i] = color;
    FXPNGImage image(&app);
    image.setData(pixels, IMAGE_KEEP | IMAGE_OWNED, 2, 2);
    FXFileStream output;
    assert(output.open(path, FXStreamSave));
    assert(image.savePixels(output));
}

static void solidJpeg(FXApp& app, const FXString& path, FXColor color)
{
    FXColor* pixels = NULL;
    assert(FXMALLOC(&pixels, FXColor, 4));
    for (FXint i = 0; i < 4; ++i) pixels[i] = color;
    FXJPGImage image(&app);
    image.setData(pixels, IMAGE_KEEP | IMAGE_OWNED, 2, 2);
    FXFileStream output;
    assert(output.open(path, FXStreamSave));
    assert(image.savePixels(output));
}

static void checkerPng(FXApp& app, const FXString& path)
{
    FXColor* pixels = NULL;
    assert(FXMALLOC(&pixels, FXColor, 4));
    pixels[0] = pixels[3] = FXRGB(0, 0, 0);
    pixels[1] = pixels[2] = FXRGB(255, 255, 255);
    FXPNGImage image(&app);
    image.setData(pixels, IMAGE_KEEP | IMAGE_OWNED, 2, 2);
    FXFileStream output;
    assert(output.open(path, FXStreamSave));
    assert(image.savePixels(output));
}

static void patternMaskPng(FXApp& app, const FXString& path)
{
    FXColor* pixels = NULL;
    assert(FXMALLOC(&pixels, FXColor, 4));
    pixels[0] = FXRGB(0, 0, 0);
    pixels[1] = FXRGB(255, 255, 255);
    pixels[2] = FXRGBA(255, 255, 255, 0);
    pixels[3] = FXRGBA(255, 255, 255, 128);
    FXPNGImage image(&app);
    image.setData(pixels, IMAGE_KEEP | IMAGE_OWNED, 2, 2);
    FXFileStream output;
    assert(output.open(path, FXStreamSave));
    assert(image.savePixels(output));
}

static void copyThemeWithName(const FXString& source, const FXString& destination, const FXString& name)
{
    std::ifstream input(source.text());
    std::ofstream output(destination.text());
    assert(input && output);
    std::string line;
    while (std::getline(input, line))
        output << (line.compare(0, 5, "name=") == 0 ? "name=" + std::string(name.text()) : line) << '\n';
    assert(!input.bad() && output.good());
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
        editor.selectElement("main");
        auto selectProperty = [&](const char* key)
        {
            auto item = std::find(editor.propertyKeys.begin(), editor.propertyKeys.end(), key);
            assert(item != editor.propertyKeys.end());
            editor.properties->setCurrentItem(item - editor.propertyKeys.begin());
        };
        ActionTimer fontHelp([&]()
        {
            FontDialog* dialog = dynamic_cast<FontDialog*>(app.getModalWindow());
            assert(dialog);
            FXButton* help = findButton(dialog, "User Manual");
            assert(help);
            checkManual(app, help->getTarget(), help->getSelector(), help, true);
            assert(app.getModalWindow() == dialog);
            dialog->handle(dialog, FXSEL(SEL_COMMAND, DialogBox::ID_CANCEL), NULL);
        });
        selectProperty("font");
        app.addTimeout(&fontHelp, 1, 150);
        editor.onEditProperty(NULL, 0, NULL);
        ActionTimer percentageHelp([&]()
        {
            InputDialog* dialog = dynamic_cast<InputDialog*>(app.getModalWindow());
            assert(dialog && dialog->getTitle() == "Bar shading");
            FXButton* help = findButton(dialog, "User Manual");
            assert(help);
            checkManual(app, help->getTarget(), help->getSelector(), help, true);
            dialog->handle(dialog, FXSEL(SEL_COMMAND, DialogBox::ID_CANCEL), NULL);
        });
        selectProperty("shading");
        app.addTimeout(&percentageHelp, 1, 150);
        editor.onEditProperty(NULL, 0, NULL);
        ActionTimer namingHelp([&]()
        {
            InputDialog* dialog = dynamic_cast<InputDialog*>(app.getModalWindow());
            assert(dialog && dialog->getTitle() == "Save theme");
            FXButton* help = findButton(dialog, "User Manual");
            assert(help);
            checkManual(app, help->getTarget(), help->getSelector(), help, true);
            dialog->handle(dialog, FXSEL(SEL_COMMAND, DialogBox::ID_CANCEL), NULL);
        });
        app.addTimeout(&namingHelp, 1, 150);
        editor.onSave(NULL, FXSEL(SEL_COMMAND, ThemeEditor::ID_SAVE_AS), NULL);
        editor.previewEffect("fill=solid colors=#123456", false);
        ActionTimer decisionHelp([&]()
        {
            FXWindow* dialog = app.getModalWindow();
            assert(dialog && FXString(dialog->getClassName()) == "ThemeDecision");
            FXButton* help = findButton(dialog, "User Manual");
            assert(help);
            checkManual(app, help->getTarget(), help->getSelector(), help, true);
            dialog->handle(dialog, FXSEL(SEL_COMMAND, DialogBox::ID_CANCEL), NULL);
        });
        app.addTimeout(&decisionHelp, 1, 150);
        editor.selectElement("tree");
        assert(editor.area == "main");
        editor.onDiscard(NULL, 0, NULL);
        editor.selectElement("list");
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
                effectTabs->layout();
                pump(app);
                FXWindow* page = effectTabs->childAtIndex(tab * 2 + 1);
                FXIconList* samples = static_cast<FXIconList*>(findClass(page, "ThemeGallery"));
                assert(page->shown() && samples && samples->shown());
                // Top level lists the category folders first, like the file list's thumbnail view.
                FXint firstChoice = samples->findItem(tab == 1 ? "No pattern" : "No texture");
                assert(firstChoice > 0 && samples->findItem(tab == 1 ? "Fabric" : "Wood") >= 0);
                for (FXint item = 0; item < samples->getNumItems(); ++item)
                    assert((samples->getItemData(item) != NULL) == (item < firstChoice));
                assert(samples->getNumCols() > 1 && galleryFillsWidth(samples));
                screenshot(dialog, tab == 1 ? "background-pattern-folders" : "background-texture-folders");
                if (tab == 1)
                {
                    FXint columns = samples->getNumCols();
                    dialog->resize(dialog->getWidth() + 360, dialog->getHeight());
                    pump(app);
                    assert(samples->getNumCols() > columns && galleryFillsWidth(samples));
                    dialog->resize(dialog->getWidth() - 360, dialog->getHeight());
                    pump(app);
                    assert(samples->getNumCols() == columns);
                    // Arrow keys only move the cursor; Enter opens and Backspace goes back up.
                    samples->setCurrentItem(0);
                    pressKey(samples, KEY_Right);
                    assert(samples->getCurrentItem() == 1 && samples->findItem("Horizontal stripes") >= 0);
                    pressKey(samples, KEY_Return);
                    assert(samples->getItemText(0) == ".." && samples->findItem("Horizontal stripes") < 0);
                    pressKey(samples, KEY_BackSpace);
                    assert(samples->findItem("Horizontal stripes") >= 0 && samples->getItemText(0) != "..");
                }
                activateItem(samples, tab == 1 ? "Fabric" : "Wood");
                pump(app);
                assert(samples->getItemText(0) == ".." && samples->getItemData(0) && !samples->isItemSelected(0));
                FXint bundledIndex = samples->findItem(tab == 1 ? "Gingham-Check" : "Oak-Veneer-01");
                assert(bundledIndex > 0 && samples->getItemBigIcon(bundledIndex) &&
                       samples->getItemBigIcon(bundledIndex)->getData());
                FXint sampleIndex = bundledIndex;
                FXint otherIndex = 0;
                samples->setCurrentItem(otherIndex);
                samples->selectItem(otherIndex);
                samples->makeItemVisible(sampleIndex);
                pump(app);
                FXIcon* visibleSample = samples->getItemBigIcon(sampleIndex);
                assert(visibleSample && visibleSample->getData() &&
                       itemShowsPixel(samples, sampleIndex, visibleSample->getData()[0]));
                screenshot(dialog, tabShots[tab - 1]);
                FXint checked = 0;
                for (FXint item = 0; item < samples->getNumItems(); ++item)
                    if (samples->getItemBigIcon(item))
                    {
                        assert(samples->getItemBigIcon(item)->getData());
                        ++checked;
                    }
                assert(checked >= 6);
                if (tab == 2)
                {
                    // Thumbnails show the PNG's true colours, not a theme or greyscale tint.
                    FXFileStream stream;
                    FXPNGImage source(&app, NULL, IMAGE_KEEP);
                    assert(stream.open(FXPath::directory(execpath) + "/../share/swordfish/textures/Wood/Oak-Veneer-01.png",
                                       FXStreamLoad) && source.loadPixels(stream));
                    double expected[3] = {0, 0, 0}, actual[3] = {0, 0, 0};
                    FXint sourceCount = source.getWidth() * source.getHeight();
                    for (FXint i = 0; i < sourceCount; ++i)
                    {
                        expected[0] += FXREDVAL(source.getData()[i]) / static_cast<double>(sourceCount);
                        expected[1] += FXGREENVAL(source.getData()[i]) / static_cast<double>(sourceCount);
                        expected[2] += FXBLUEVAL(source.getData()[i]) / static_cast<double>(sourceCount);
                    }
                    FXIcon* oak = samples->getItemBigIcon(bundledIndex);
                    FXint oakCount = oak->getWidth() * oak->getHeight();
                    for (FXint i = 0; i < oakCount; ++i)
                    {
                        actual[0] += FXREDVAL(oak->getData()[i]) / static_cast<double>(oakCount);
                        actual[1] += FXGREENVAL(oak->getData()[i]) / static_cast<double>(oakCount);
                        actual[2] += FXBLUEVAL(oak->getData()[i]) / static_cast<double>(oakCount);
                    }
                    for (FXint channel = 0; channel < 3; ++channel)
                        assert(fabs(expected[channel] - actual[channel]) < 4);
                    assert(actual[0] - actual[2] > 40);
                }
            }
            ActionTimer textureNamingHelp([&]()
            {
                InputDialog* naming = dynamic_cast<InputDialog*>(app.getModalWindow());
                assert(naming && naming->getTitle() == "Texture name");
                FXButton* help = findButton(naming, "User Manual");
                assert(help);
                checkManual(app, help->getTarget(), help->getSelector(), help, true);
                naming->handle(naming, FXSEL(SEL_COMMAND, DialogBox::ID_CANCEL), NULL);
            });
            ActionTimer chooseTexture([&]()
            {
                FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
                assert(chooser && chooser->getTitle() == "Add PNG texture");
                selectChooserFile(chooser, FXPath::directory(execpath) + "/../share/swordfish/textures/brick.png");
                app.addTimeout(&textureNamingHelp, 1, 150);
                chooser->handle(chooser, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
            });
            FXButton* addTexture = findButton(effectTabs->childAtIndex(5), "Add PNG...");
            assert(addTexture);
            app.addTimeout(&chooseTexture, 1, 150);
            addTexture->getTarget()->handle(addTexture, FXSEL(SEL_COMMAND, addTexture->getSelector()), NULL);
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
        FXint savedIndex = -1;
        for (FXint i = NUM_DEFAULT_THEMES; i < static_cast<FXint>(loadedPreferences.Themes.size()); ++i)
            if (loadedPreferences.Themes[i].name == saved.name) savedIndex = i;
        assert(savedIndex >= NUM_DEFAULT_THEMES);
        const Theme& savedReference = loadedPreferences.Themes[savedIndex];
        assert(savedReference.deferred && savedReference.properties.empty() && savedReference.effect.empty());
        Theme loadedTheme;
        assert(loadedPreferences.resolveTheme(savedReference, loadedTheme));
        assert(loadedTheme.encodeProperties() == saved.encodeProperties() && loadedTheme.effect == saved.effect);
        assert(loadedPreferences.Themes[savedIndex].deferred);
        {
            ThemeEditor duplicate(&loadedPreferences, &window, loadedPreferences.Themes, savedIndex, ThemeEditor::DUPLICATE);
            assert(duplicate.getDraft().name == saved.name + " copy");
            assert(duplicate.getDraft().encodeProperties() == saved.encodeProperties());
            assert(loadedPreferences.Themes[savedIndex].deferred);
        }
        loadedPreferences.create();
        loadedPreferences.rememberPreferences();
        loadedPreferences.themesList->setCurrentItem(savedIndex);
        assert(loadedPreferences.onCmdTheme(NULL, 0, NULL));
        assert(loadedPreferences.currTheme.encodeProperties() == saved.encodeProperties());
        assert(loadedPreferences.Themes[savedIndex].deferred);
        loadedPreferences.themesList->setCurrentItem(1);
        assert(loadedPreferences.onCmdTheme(NULL, 0, NULL));
        assert(loadedPreferences.currTheme.name == loadedPreferences.Themes[1].name &&
               loadedPreferences.Themes[savedIndex].properties.empty());
        loadedPreferences.themesList->setCurrentItem(savedIndex);
        assert(loadedPreferences.onCmdTheme(NULL, 0, NULL));
        ActionTimer renameTheme([&] {
            InputDialog* dialog = dynamic_cast<InputDialog*>(app.getModalWindow());
            assert(dialog && dialog->getTitle() == "Rename theme");
            dialog->setText("Renamed regression");
            dialog->handle(dialog, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
        });
        app.addTimeout(&renameTheme, 1, 150);
        assert(loadedPreferences.onCmdThemeRename(NULL, 0, NULL));
        assert(!loadedPreferences.Themes[savedIndex].deferred &&
               loadedPreferences.Themes[savedIndex].encodeProperties() == saved.encodeProperties());
        loadedPreferences.onCmdCancel(NULL, FXSEL(SEL_COMMAND, PreferencesBox::ID_CANCEL), NULL);
        assert(loadedPreferences.Themes[savedIndex].deferred &&
               loadedPreferences.Themes[savedIndex].name == saved.name);
        FXString movedThemeFile = savedThemeFile + ".held";
        assert(rename(savedThemeFile.text(), movedThemeFile.text()) == 0);
        Theme previousTheme = loadedPreferences.currTheme;
        FXString loadError;
        ActionTimer missingTheme([&] {
            MessageBox* message = dynamic_cast<MessageBox*>(app.getModalWindow());
            assert(message && message->getTitle() == "Load theme");
            loadError = message->getText();
            message->handle(message, FXSEL(SEL_COMMAND, MessageBox::ID_CLICKED_OK), NULL);
        });
        loadedPreferences.themesList->setCurrentItem(savedIndex);
        app.addTimeout(&missingTheme, 1, 150);
        assert(!loadedPreferences.onCmdTheme(NULL, 0, NULL));
        assert(loadedPreferences.currTheme == previousTheme &&
               loadedPreferences.themesList->getCurrentItem() != savedIndex);
        assert(!loadError.empty());
        {
            ThemeEditor switching(&loadedPreferences, &window, loadedPreferences.Themes, 0, ThemeEditor::EDIT_CURRENT);
            assert(switching.isReady());
            switching.draft.setProperty("list", "foreground", "#123456");
            Theme pending = switching.draft;
            switching.baseTheme->setCurrentItem(savedIndex);
            app.addTimeout(&missingTheme, 1, 150);
            assert(!switching.start(savedIndex, ThemeEditor::DUPLICATE));
            assert(switching.draft == pending && switching.baseTheme->getCurrentItem() == switching.currentBase);
        }
        FXbool unexpectedEditor = false;
        ActionTimer closeUnexpectedEditor([&] {
            ThemeEditor* fallback = dynamic_cast<ThemeEditor*>(app.getModalWindow());
            assert(fallback);
            unexpectedEditor = true;
            fallback->handle(fallback, FXSEL(SEL_COMMAND, ThemeEditor::ID_CLOSE), NULL);
        });
        ActionTimer editorLoadError([&] {
            MessageBox* message = dynamic_cast<MessageBox*>(app.getModalWindow());
            assert(message && message->getTitle() == "Load theme");
            app.addTimeout(&closeUnexpectedEditor, 1, 150);
            message->handle(message, FXSEL(SEL_COMMAND, MessageBox::ID_CLICKED_OK), NULL);
        });
        loadedPreferences.themesList->setCurrentItem(savedIndex);
        app.addTimeout(&editorLoadError, 1, 150);
        loadedPreferences.onCmdThemeEditor(NULL, FXSEL(SEL_COMMAND, PreferencesBox::ID_THEME_DUPLICATE), NULL);
        app.removeTimeout(&closeUnexpectedEditor, 1);
        assert(!unexpectedEditor);
        copyThemeWithName(movedThemeFile, savedThemeFile, "Externally changed");
        loadedPreferences.themesList->setCurrentItem(savedIndex);
        app.addTimeout(&missingTheme, 1, 150);
        assert(!loadedPreferences.onCmdTheme(NULL, 0, NULL));
        assert(loadError.contains("name no longer matches"));
        assert(unlink(savedThemeFile.text()) == 0);
        assert(symlink(outsideFile.text(), savedThemeFile.text()) == 0);
        loadedPreferences.themesList->setCurrentItem(savedIndex);
        app.addTimeout(&missingTheme, 1, 150);
        assert(!loadedPreferences.onCmdTheme(NULL, 0, NULL));
        assert(loadError.contains("no longer safe"));
        assert(unlink(savedThemeFile.text()) == 0);
        assert(rename(movedThemeFile.text(), savedThemeFile.text()) == 0);

        Theme updated = saved;
        updated.setProperty("list", "foreground", "#123456");
        assert(loadedPreferences.saveEditedTheme(updated, savedIndex));
        Theme updatedFromDisk;
        assert(loadedPreferences.Themes[savedIndex].deferred &&
               loadedPreferences.resolveTheme(loadedPreferences.Themes[savedIndex], updatedFromDisk));
        assert(updatedFromDisk.property("list", "foreground") == "#123456" &&
               Theme::current(&app).property("list", "foreground") == "#123456");
        assert(loadedPreferences.saveEditedTheme(saved, savedIndex));
        ActionTimer replaceTheme([&] {
            MessageBox* message = dynamic_cast<MessageBox*>(app.getModalWindow());
            assert(message && message->getTitle() == "Replace theme");
            message->handle(message, FXSEL(SEL_COMMAND, MessageBox::ID_CLICKED_YES), NULL);
        });
        app.addTimeout(&replaceTheme, 1, 150);
        assert(loadedPreferences.importThemeFile(outsideFile));
        assert(!loadedPreferences.Themes[savedIndex].deferred &&
               loadedPreferences.Themes[savedIndex].assetRoot == outsideFolder);
        loadedPreferences.onCmdCancel(NULL, FXSEL(SEL_COMMAND, PreferencesBox::ID_CANCEL), NULL);
        assert(loadedPreferences.Themes[savedIndex].deferred &&
               loadedPreferences.Themes[savedIndex].assetRoot == FXPath::directory(savedThemeFile));

        FXString blockerFolder = xf_userdatahome() + "/themes/colors/zz-rollback-blocker";
        FXString blockerFile = blockerFolder + "/theme.swordfish-theme";
        assert(xf_mkpath(blockerFolder.text(), 0755) == 0);
        copyThemeWithName(savedThemeFile, blockerFile, "Rollback blocker");
        PreferencesBox rollbackPreferences(&window, app.getBackColor(), app.getForeColor(), app.getBackColor(),
                                           app.getSelbackColor(), FXRGB(255, 0, 0), app.getBaseColor());
        rollbackPreferences.create();
        rollbackPreferences.rememberPreferences();
        FXint rollbackIndex = -1, blockerIndex = -1;
        for (FXint i = NUM_DEFAULT_THEMES; i < static_cast<FXint>(rollbackPreferences.Themes.size()); ++i)
        {
            if (rollbackPreferences.Themes[i].name == saved.name) rollbackIndex = i;
            if (rollbackPreferences.Themes[i].name == "Rollback blocker") blockerIndex = i;
        }
        assert(rollbackIndex >= NUM_DEFAULT_THEMES && blockerIndex >= NUM_DEFAULT_THEMES);
        FXString heldBlocker = blockerFile + ".held";
        assert(rename(blockerFile.text(), heldBlocker.text()) == 0);
        Theme attempted = saved;
        attempted.setProperty("list", "foreground", "#123456");
        Theme activeBeforeFailure = Theme::current(&app);
        ActionTimer failedSave([&] {
            MessageBox* message = dynamic_cast<MessageBox*>(app.getModalWindow());
            assert(message && message->getTitle() == "Save theme");
            message->handle(message, FXSEL(SEL_COMMAND, MessageBox::ID_CLICKED_OK), NULL);
        });
        app.addTimeout(&failedSave, 1, 150);
        assert(!rollbackPreferences.saveEditedTheme(attempted, rollbackIndex));
        Theme restored;
        assert(rollbackPreferences.resolveTheme(rollbackPreferences.Themes[rollbackIndex], restored));
        assert(restored.encodeProperties() == saved.encodeProperties() &&
               Theme::current(&app) == activeBeforeFailure);
        assert(rename(heldBlocker.text(), blockerFile.text()) == 0);
        ActionTimer renameSavedTheme([&] {
            InputDialog* dialog = dynamic_cast<InputDialog*>(app.getModalWindow());
            assert(dialog && dialog->getTitle() == "Rename theme");
            dialog->setText("Renamed deferred fixture");
            dialog->handle(dialog, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
        });
        rollbackPreferences.themesList->setCurrentItem(blockerIndex);
        app.addTimeout(&renameSavedTheme, 1, 150);
        assert(rollbackPreferences.onCmdThemeRename(NULL, 0, NULL));
        assert(!rollbackPreferences.Themes[blockerIndex].deferred &&
               rollbackPreferences.saveCustomThemes(rollbackPreferences.Themes));
        FXString renamedFile = xf_userdatahome() + "/themes/colors/Renamed deferred fixture/theme.swordfish-theme";
        assert(xf_existfile(renamedFile) && !xf_existfile(blockerFile));
        PreferencesBox renamedPreferences(&window, app.getBackColor(), app.getForeColor(), app.getBackColor(),
                                          app.getSelbackColor(), FXRGB(255, 0, 0), app.getBaseColor());
        FXint renamedIndex = -1;
        for (FXint i = NUM_DEFAULT_THEMES; i < static_cast<FXint>(renamedPreferences.Themes.size()); ++i)
            if (renamedPreferences.Themes[i].name == "Renamed deferred fixture") renamedIndex = i;
        assert(renamedIndex >= NUM_DEFAULT_THEMES && renamedPreferences.Themes[renamedIndex].deferred);
        Theme renamedFromDisk;
        assert(renamedPreferences.resolveTheme(renamedPreferences.Themes[renamedIndex], renamedFromDisk) &&
               renamedFromDisk.encodeProperties() == saved.encodeProperties());
        vector_Theme withoutFixture = renamedPreferences.Themes;
        withoutFixture.erase(withoutFixture.begin() + renamedIndex);
        assert(renamedPreferences.saveCustomThemes(withoutFixture) && !xf_existfile(renamedFile));
        puts("PASS: deferred themes load on demand; switching, overwrite, import, rename, stale files and rollback are safe");
        vector_Theme withoutCustom = loadedPreferences.Themes;
        withoutCustom.erase(withoutCustom.begin() + NUM_DEFAULT_THEMES, withoutCustom.end());
        assert(loadedPreferences.saveCustomThemes(withoutCustom));
        assert(!xf_existfile(savedThemeFile));
        assert(xf_existfile(unknownFile) && xf_existfile(duplicateFile) && xf_existfile(outsideFile));
        assert(xf_mkpath(FXPath::directory(savedThemeFile).text(), 0755) == 0);
        assert(FXFile::copyFiles(duplicateFile, savedThemeFile, false));
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

        FXString data = FXPath::absolute(xf_userdatahome());
        FXString globalTextures = data + "/textures";
        FXString redSource = data + "/source-red";
        FXString blueSource = data + "/source-blue";
        assert(xf_mkpath(globalTextures.text(), 0755) == 0);
        assert(xf_mkpath((redSource + "/textures").text(), 0755) == 0);
        assert(xf_mkpath((blueSource + "/textures").text(), 0755) == 0);
        solidPng(app, globalTextures + "/shared.png", FXRGB(0, 255, 0));
        solidPng(app, redSource + "/textures/shared.png", FXRGB(255, 0, 0));
        solidPng(app, blueSource + "/textures/shared.png", FXRGB(0, 0, 255));
        solidPng(app, redSource + "/textures/paper.png", FXRGB(255, 0, 0));
        assert(BackgroundEffect::texturePath("paper", redSource) == redSource + "/textures/paper.png");

        FXString occupied = data + "/themes/colors/Red pack";
        assert(xf_mkpath(occupied.text(), 0755) == 0);
        {
            std::ofstream keep((occupied + "/keep.txt").text());
            keep << "unrecognized content";
            assert(keep.good());
        }
        Theme red = Theme::current(&app);
        red.name = "Red pack";
        red.assetRoot = redSource;
        red.effect = "fill=solid colors=#000000 texture=shared strength=100 texture_colors=original rows=off";
        assert(preferences.saveEditedTheme(red, -1));
        assert(red.assetRoot == occupied + "-2");
        assert(xf_existfile(occupied + "/keep.txt"));
        assert(red.assetRoot[0] == '/');
        assert(BackgroundEffect::texturePath("shared", red.assetRoot) == red.assetRoot + "/textures/shared.png");
        assert(Theme::current(&app).assetRoot == red.assetRoot);

        Theme blue = red;
        blue.name = "Blue pack";
        blue.assetRoot = blueSource;
        assert(preferences.saveEditedTheme(blue, -1));
        assert(red != blue);
        assert(Theme::current(&app).assetRoot == blue.assetRoot);
        assert(BackgroundEffect::texturePath("shared") == globalTextures + "/shared.png");
        std::unique_ptr<FXIcon> global(BackgroundEffect::preview(&window, blue.effect, FXRGB(0, 0, 0), 8));
        assert(global->getData()[0] == FXRGB(0, 255, 0));

        auto liveBackground = [&]() -> FXColor
        {
            FXImage image(&app, NULL, IMAGE_KEEP, 8, 8);
            image.create();
            {
                FXDCWindow dc(&image);
                files->fillBackground(dc, 0, 0, 8, 8);
            }
            image.restore();
            assert(image.getData());
            return image.getData()[0];
        };
        window.applyTheme(red);
        assert(liveBackground() == FXRGB(255, 0, 0));
        window.applyTheme(blue);
        assert(liveBackground() == FXRGB(0, 0, 255));

        FXString copied = data + "/themes/colors/Copied folder";
        assert(xf_mkpath((copied + "/textures").text(), 0755) == 0);
        copyThemeWithName(red.assetRoot + "/theme.swordfish-theme", copied + "/shared.swordfish-theme",
                          "Copied pack");
        assert(FXFile::copyFiles(red.assetRoot + "/textures/shared.png", copied + "/textures/shared.png", false));
        PreferencesBox imported(&window, app.getBackColor(), app.getForeColor(), app.getBackColor(),
                                app.getSelbackColor(), FXRGB(255, 0, 0), app.getBaseColor());
        imported.create();
        imported.rememberPreferences();
        FXbool foundRed = false, foundBlue = false, foundCopy = false;
        for (const Theme& theme : imported.Themes)
        {
            if (theme.name == red.name) foundRed = theme.assetRoot == red.assetRoot;
            if (theme.name == blue.name) foundBlue = theme.assetRoot == blue.assetRoot;
            if (theme.name == "Copied pack") foundCopy = theme.assetRoot == copied;
        }
        assert(foundRed && foundBlue && foundCopy);
        assert(imported.saveCustomThemes(imported.Themes));
        assert(xf_existfile(copied + "/shared.swordfish-theme"));

        FXString external = data + "/external-pack";
        assert(xf_mkpath((external + "/textures").text(), 0755) == 0);
        copyThemeWithName(red.assetRoot + "/theme.swordfish-theme", external + "/exported.swordfish-theme",
                          "Imported pack");
        assert(FXFile::copyFiles(red.assetRoot + "/textures/shared.png", external + "/textures/shared.png", false));
        assert(imported.importThemeFile(external + "/exported.swordfish-theme"));
        assert(imported.currTheme.assetRoot == external);
        imported.onCmdAccept(NULL, FXSEL(SEL_COMMAND, PreferencesBox::ID_APPLY), NULL);
        Theme active = Theme::current(&app);
        assert(active.assetRoot == data + "/themes/colors/Imported pack");
        assert(BackgroundEffect::texturePath("shared", active.assetRoot) == active.assetRoot + "/textures/shared.png");
        assert(liveBackground() == FXRGB(255, 0, 0));
        global.reset(BackgroundEffect::preview(&window, blue.effect, FXRGB(0, 0, 0), 8));
        assert(global->getData()[0] == FXRGB(0, 255, 0));

        FXString portable = data + "/relocated-portable";
        FXString relocated = portable + "/themes/colors/Imported pack";
        assert(xf_mkpath((relocated + "/textures").text(), 0755) == 0);
        assert(FXFile::copyFiles(active.assetRoot + "/theme.swordfish-theme",
                                 relocated + "/theme.swordfish-theme", false));
        assert(FXFile::copyFiles(active.assetRoot + "/textures/shared.png", relocated + "/textures/shared.png", false));
        const char* previousPortable = getenv("SWORDFISH_PORTABLE_DATA");
        FXString previousValue = previousPortable ? previousPortable : "";
        assert(setenv("SWORDFISH_PORTABLE_DATA", portable.text(), 1) == 0);
        Theme moved = Theme::current(&app);
        assert(moved.assetRoot == relocated);
        assert(BackgroundEffect::texturePath("shared", moved.assetRoot) == relocated + "/textures/shared.png");
        if (previousPortable) assert(setenv("SWORDFISH_PORTABLE_DATA", previousValue.text(), 1) == 0);
        else assert(unsetenv("SWORDFISH_PORTABLE_DATA") == 0);

        FXString categoryName = "Natural Stone/River 1";
        FXString categoryReference = BackgroundEffect::textureReference(categoryName);
        assert(categoryReference == "Natural%20Stone/River%201");
        FXString decodedCategory;
        assert(BackgroundEffect::textureRelativePath(categoryReference, decodedCategory) &&
               decodedCategory == categoryName);
        for (const char* invalid : {"../tile", "/absolute", "a//tile", "a/%2E%2E", "a/%2Ftile",
                                    "a/%5Ctile", "a/%00", "a/%0Atile", "a/%zz"})
            assert(!BackgroundEffect::validTextureName(invalid));
        FXString unicode = FXString("Colors/caf") + "\xC3\xA9";
        assert(BackgroundEffect::textureRelativePath(BackgroundEffect::textureReference(unicode), decodedCategory));
        assert(decodedCategory == unicode);
        checkerPng(app, redSource + "/textures/checker.png");
        FXString textureError;
        assert(BackgroundEffect::importTexture(&app, redSource + "/textures/checker.png",
                                                categoryReference, false, textureError));
        assert(xf_existfile(globalTextures + "/Natural Stone/River 1.png"));
        FXString secondReference = BackgroundEffect::textureReference("Paint/River 1");
        assert(BackgroundEffect::importTexture(&app, blueSource + "/textures/shared.png",
                                                secondReference, false, textureError));
        assert(BackgroundEffect::texturePath(categoryReference) == globalTextures + "/Natural Stone/River 1.png");
        assert(BackgroundEffect::texturePath(secondReference) == globalTextures + "/Paint/River 1.png");
        assert(xf_mkpath((globalTextures + "/Uppercase").text(), 0755) == 0);
        solidPng(app, globalTextures + "/Uppercase/Tile.PnG", FXRGB(10, 20, 30));
        assert(BackgroundEffect::texturePath("Uppercase/Tile") == globalTextures + "/Uppercase/Tile.PnG");
        assert(symlink(outsideFolder.text(), (globalTextures + "/linked-category").text()) == 0);
        textureError.clear();
        assert(!BackgroundEffect::importTexture(&app, redSource + "/textures/checker.png",
                                                 "linked-category/escape", false, textureError));
        assert(!textureError.empty() && !xf_existfile(outsideFolder + "/escape.png"));

        Theme categorized = Theme::current(&app);
        categorized.name = "Categorized pack";
        categorized.effect = "fill=solid colors=#808080 texture=" + categoryReference +
                             " strength=100 texture_colors=original rows=off";
        assert(imported.saveEditedTheme(categorized, -1));
        assert(xf_existfile(categorized.assetRoot + "/textures/Natural Stone/River 1.png"));
        assert(BackgroundEffect::texturePath(categoryReference, categorized.assetRoot) ==
               categorized.assetRoot + "/textures/Natural Stone/River 1.png");
        assert(liveBackground() == FXRGB(0, 0, 0));
        PreferencesBox reloadedCategories(&window, app.getBackColor(), app.getForeColor(), app.getBackColor(),
                                          app.getSelbackColor(), FXRGB(255, 0, 0), app.getBaseColor());
        FXbool foundCategorized = false;
        for (const Theme& theme : reloadedCategories.Themes)
            if (theme.name == categorized.name)
            {
                Theme loaded;
                assert(theme.deferred && reloadedCategories.resolveTheme(theme, loaded));
                foundCategorized = loaded.assetRoot == categorized.assetRoot &&
                                   BackgroundEffect::parse(loaded.effect).texture == categoryReference;
            }
        assert(foundCategorized);

        FXString exportParent = data + "/exports";
        assert(xf_mkpath(exportParent.text(), 0755) == 0);
        ActionTimer chooseExport([&] {
            FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
            assert(chooser && chooser->getTitle() == "Choose where to export the theme folder");
            chooser->setFilename(exportParent);
            chooser->handle(chooser, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
        });
        app.addTimeout(&chooseExport, 1, 150);
        imported.onCmdThemeExport(NULL, 0, NULL);
        FXString exported = exportParent + "/Categorized pack";
        assert(xf_existfile(exported + "/theme.swordfish-theme"));
        assert(xf_existfile(exported + "/textures/Natural Stone/River 1.png"));
        assert(BackgroundEffect::texturePath(categoryReference, exported) ==
               exported + "/textures/Natural Stone/River 1.png");

        editor.draft = editor.applied = editor.saved = categorized;
        editor.selectElement("list");
        editor.show(PLACEMENT_SCREEN);
        pump(app);
        ActionTimer categoryDialog([&] {
            FXWindow* dialog = app.getModalWindow();
            assert(dialog);
            FXTabBook* pages = static_cast<FXTabBook*>(findClass(dialog, "FXTabBook"));
            assert(pages);
            pages->setCurrent(2, true);
            pump(app);
            FXWindow* page = pages->childAtIndex(5);
            FXIconList* gallery = static_cast<FXIconList*>(findClass(page, "ThemeGallery"));
            assert(page->shown() && gallery && gallery->shown());
            // Opens in the applied texture's folder, with the folder named and the target category preset.
            assert(gallery->getNumItems() == 2 && gallery->getItemText(0) == "..");
            assert(gallery->getItemText(1) == "River 1" && gallery->getCurrentItem() == 1);
            assert(findLabel(page, "Textures / Natural Stone"));
            FXButton* up = findButton(page, "Up");
            assert(up && up->isEnabled());
            up->getTarget()->handle(up, FXSEL(SEL_COMMAND, up->getSelector()), NULL);
            assert(gallery->findItem("Natural Stone") >= 0 && gallery->findItem("River 1") < 0 && !up->isEnabled());
            assert(findLabel(page, "Textures"));
            activateItem(gallery, "Natural Stone");
            pump(app);
            assert(gallery->getNumItems() == 2 && up->isEnabled());
            FXIcon* sample = gallery->getItemBigIcon(1);
            assert(sample && sample->getData());
            gallery->setCurrentItem(0);
            gallery->selectItem(0);
            pump(app);
            screenshot(page, "categorized-textures");
            assert(itemShowsPixel(gallery, 1, sample->getData()[0]));
            gallery->setCurrentItem(1);
            gallery->getTarget()->handle(gallery, FXSEL(SEL_COMMAND, gallery->getSelector()), NULL);
            assert(BackgroundEffect::parse(editor.getDraft().effect).texture == categoryReference);
            FXString batchTextureOne = redSource + "/textures/Batch One.png";
            FXString batchTextureTwo = redSource + "/textures/Batch Two.png";
            solidPng(app, batchTextureOne, FXRGB(30, 80, 130));
            solidPng(app, batchTextureTwo, FXRGB(130, 80, 30));
            ActionTimer chooseBatchTexture([&] {
                FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
                assert(chooser && chooser->getTitle() == "Add PNG texture");
                selectChooserFiles(chooser, batchTextureOne, batchTextureTwo);
                chooser->handle(chooser, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
            });
            FXButton* addTexture = findButton(page, "Add PNG...");
            assert(addTexture);
            app.addTimeout(&chooseBatchTexture, 1, 150);
            addTexture->getTarget()->handle(addTexture, FXSEL(SEL_COMMAND, addTexture->getSelector()), NULL);
            FXString batchTextureReference = BackgroundEffect::textureReference("Natural Stone/Batch One");
            assert(xf_existfile(globalTextures + "/Natural Stone/Batch One.png") &&
                   xf_existfile(globalTextures + "/Natural Stone/Batch Two.png") &&
                   BackgroundEffect::parse(editor.getDraft().effect).texture == batchTextureReference);
            FXString batchTextureThree = redSource + "/textures/Batch Three.png";
            solidPng(app, batchTextureThree, FXRGB(30, 130, 80));
            ActionTimer confirmPartialTextureImport([&] {
                MessageBox* message = dynamic_cast<MessageBox*>(app.getModalWindow());
                assert(message && message->getTitle() == "Texture");
                message->handle(message, FXSEL(SEL_COMMAND, MessageBox::ID_CLICKED_OK), NULL);
            });
            ActionTimer choosePartialTextureBatch([&] {
                FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
                assert(chooser && chooser->getTitle() == "Add PNG texture");
                selectChooserFiles(chooser, batchTextureOne, batchTextureThree);
                app.addTimeout(&confirmPartialTextureImport, 1, 150);
                chooser->handle(chooser, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
            });
            app.addTimeout(&choosePartialTextureBatch, 1, 150);
            addTexture->getTarget()->handle(addTexture, FXSEL(SEL_COMMAND, addTexture->getSelector()), NULL);
            assert(xf_existfile(globalTextures + "/Natural Stone/Batch Three.png") &&
                   BackgroundEffect::parse(editor.getDraft().effect).texture ==
                       BackgroundEffect::textureReference("Natural Stone/Batch Three"));
            FXString textureFolderImport = data + "/Batch Texture Pack";
            assert(xf_mkpath((textureFolderImport + "/Oak").text(), 0755) == 0);
            assert(xf_mkpath((textureFolderImport + "/Pine").text(), 0755) == 0);
            solidPng(app, textureFolderImport + "/Oak/Leaves.png", FXRGB(30, 70, 110));
            solidPng(app, textureFolderImport + "/Pine/Needles.png", FXRGB(110, 70, 30));
            solidPng(app, textureFolderImport + "/Root.png", FXRGB(70, 30, 110));
            {
                std::ofstream unsupported((textureFolderImport + "/readme.txt").text());
                unsupported << "not a texture";
                assert(unsupported.good());
            }
            FXButton* addFolder = findButton(page, "Add folder...");
            assert(addFolder);
            ActionTimer confirmFolderReport([&] {
                MessageBox* message = dynamic_cast<MessageBox*>(app.getModalWindow());
                assert(message && message->getTitle() == "Texture");
                message->handle(message, FXSEL(SEL_COMMAND, MessageBox::ID_CLICKED_OK), NULL);
            });
            ActionTimer chooseFolder([&] {
                FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
                assert(chooser && chooser->getTitle() == "Import texture folder");
                app.addTimeout(&confirmFolderReport, 1, 150);
                chooseFolderInDialog(chooser, textureFolderImport);
            });
            app.addTimeout(&chooseFolder, 1, 150);
            addFolder->getTarget()->handle(addFolder, FXSEL(SEL_COMMAND, addFolder->getSelector()), NULL);
            assert(xf_existfile(globalTextures + "/Batch Texture Pack/Oak/Leaves.png") &&
                   xf_existfile(globalTextures + "/Batch Texture Pack/Pine/Needles.png") &&
                   xf_existfile(globalTextures + "/Batch Texture Pack/Root.png") &&
                   BackgroundEffect::parse(editor.getDraft().effect).texture ==
                       BackgroundEffect::textureReference("Batch Texture Pack/Oak/Leaves"));

            FXint conflictAction = 0;
            ActionTimer renameImportedFolder([&] {
                InputDialog* naming = dynamic_cast<InputDialog*>(app.getModalWindow());
                assert(naming && naming->getTitle() == "Rename imported folder");
                naming->setText("Renamed Texture Pack");
                app.addTimeout(&confirmFolderReport, 1, 150);
                naming->handle(naming, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
            });
            ActionTimer chooseAlternateParent([&] {
                InputDialog* destination = dynamic_cast<InputDialog*>(app.getModalWindow());
                assert(destination && destination->getTitle() == "Save under another folder");
                destination->setText("Alternate Parent");
                app.addTimeout(&confirmFolderReport, 1, 150);
                destination->handle(destination, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
            });
            ActionTimer resolveFolderConflict([&] {
                DialogBox* conflict = dynamic_cast<DialogBox*>(app.getModalWindow());
                assert(conflict && conflict->getTitle() == "Folder already exists");
                FXButton* choice = findButton(conflict, conflictAction == 1 ? "Merge" :
                                                       conflictAction == 2 ? "Rename" :
                                                                           "Save under another folder");
                assert(choice);
                if (conflictAction == 1) app.addTimeout(&confirmFolderReport, 1, 150);
                else if (conflictAction == 2) app.addTimeout(&renameImportedFolder, 1, 150);
                else app.addTimeout(&chooseAlternateParent, 1, 150);
                choice->getTarget()->handle(choice, FXSEL(SEL_COMMAND, choice->getSelector()), NULL);
            });
            ActionTimer chooseConflictingFolder([&] {
                FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
                assert(chooser && chooser->getTitle() == "Import texture folder");
                app.addTimeout(&resolveFolderConflict, 1, 150);
                chooseFolderInDialog(chooser, textureFolderImport);
            });
            for (conflictAction = 1; conflictAction <= 3; ++conflictAction)
            {
                app.addTimeout(&chooseConflictingFolder, 1, 150);
                addFolder->getTarget()->handle(addFolder, FXSEL(SEL_COMMAND, addFolder->getSelector()), NULL);
                if (conflictAction == 2)
                    assert(xf_existfile(globalTextures + "/Renamed Texture Pack/Oak/Leaves.png"));
                if (conflictAction == 3)
                    assert(xf_existfile(globalTextures + "/Alternate Parent/Batch Texture Pack/Oak/Leaves.png"));
            }
            dialog->handle(dialog, FXSEL(SEL_COMMAND, DialogBox::ID_CANCEL), NULL);
        });
        app.addTimeout(&categoryDialog, 1, 150);
        editor.properties->setCurrentItem(editor.propertyKeys.size() - 1);
        editor.onEditProperty(NULL, 0, NULL);
        editor.hide();
        puts("PASS: categorized texture import, safe references, theme copies and visible gallery thumbnails");

        FXString patternReference = BackgroundEffect::textureReference("Geometric/Checker Board");
        FXString patternOther = BackgroundEffect::textureReference("Geometric/Other");
        FXString differentCategory = BackgroundEffect::textureReference("Organic/Checker Board");
        FXString globalPatterns = data + "/patterns";
        patternMaskPng(app, redSource + "/textures/mask.png");
        FXString patternError;
        assert(BackgroundEffect::importPattern(&app, redSource + "/textures/mask.png",
                                                patternReference, false, patternError));
        assert(BackgroundEffect::importPattern(&app, redSource + "/textures/checker.png",
                                                patternOther, false, patternError));
        assert(BackgroundEffect::importPattern(&app, redSource + "/textures/checker.png",
                                                differentCategory, false, patternError));
        assert(BackgroundEffect::patternPath(patternReference) == globalPatterns + "/Geometric/Checker Board.png");
        assert(BackgroundEffect::patternPath(differentCategory) == globalPatterns + "/Organic/Checker Board.png");
        patternError.clear();
        assert(!BackgroundEffect::importPattern(&app, redSource + "/textures/checker.png",
                                                 patternReference, false, patternError));
        assert(!patternError.empty());
        assert(symlink(outsideFolder.text(), (globalPatterns + "/linked-category").text()) == 0);
        patternError.clear();
        assert(!BackgroundEffect::importPattern(&app, redSource + "/textures/checker.png",
                                                 "linked-category/escape", false, patternError));
        assert(!patternError.empty() && !xf_existfile(outsideFolder + "/escape.png"));
        assert(BackgroundEffect::patternPath("../escape").empty());
        {
            const unsigned char oversized[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
                                               0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                               0, 0, 4, 1, 0, 0, 0, 2};
            std::ofstream file((data + "/oversized-pattern.png").text(), std::ios::binary);
            file.write(reinterpret_cast<const char*>(oversized), sizeof(oversized));
            file.close();
            patternError.clear();
            assert(!BackgroundEffect::validatePattern(&app, data + "/oversized-pattern.png", patternError));
            assert(!patternError.empty());
        }

        Theme patterned = Theme::current(&app);
        patterned.name = "Pattern pack";
        patterned.assetRoot.clear();
        FXString originalPattern = "fill=pattern colors=#102030,#E0C080 pattern_image=" + patternReference +
                                   " size=4 rows=off";
        assert(BackgroundEffect::parse(originalPattern).patternOriginalColors);
        std::unique_ptr<FXIcon> originalPreview(
            BackgroundEffect::preview(&window, originalPattern, FXRGB(0, 0, 0), 8));
        assert(originalPreview->getData()[0] == FXRGB(0, 0, 0));
        assert(originalPreview->getData()[2] == FXRGB(255, 255, 255));
        patterned.effect = originalPattern + " pattern_colors=theme";
        patterned.properties["gradient.Pattern preset"] =
            "fill=pattern colors=#000000,#FFFFFF pattern_image=" + patternOther + " size=4 rows=off";
        BackgroundEffect roundTrip = BackgroundEffect::parse(patterned.effect);
        assert(roundTrip.patternImage == patternReference && !roundTrip.patternOriginalColors &&
               BackgroundEffect::parse(roundTrip.serialize()).patternImage == patternReference &&
               !BackgroundEffect::parse(roundTrip.serialize()).patternOriginalColors);
        std::unique_ptr<FXIcon> patternPreview(
            BackgroundEffect::preview(&window, patterned.effect, FXRGB(0, 0, 0), 8));
        assert(patternPreview->getData()[0] == FXRGB(16, 32, 48));
        assert(patternPreview->getData()[2] == FXRGB(224, 192, 128));
        assert(patternPreview->getData()[16] == FXRGB(16, 32, 48));
        FXColor half = patternPreview->getData()[18];
        assert(FXREDVAL(half) == 120 && FXGREENVAL(half) == 112 && FXBLUEVAL(half) == 88);
        assert(xf_mkpath((blueSource + "/patterns/Geometric").text(), 0755) == 0);
        solidPng(app, blueSource + "/patterns/Geometric/Checker Board.png", FXRGB(255, 255, 255));
        std::unique_ptr<FXIcon> localPattern(
            BackgroundEffect::preview(&window, patterned.effect, FXRGB(0, 0, 0), 8, blueSource));
        assert(localPattern->getData()[0] == FXRGB(224, 192, 128));
        assert(imported.saveEditedTheme(patterned, -1));
        assert(xf_existfile(patterned.assetRoot + "/patterns/Geometric/Checker Board.png"));
        assert(xf_existfile(patterned.assetRoot + "/patterns/Geometric/Other.png"));
        assert(BackgroundEffect::patternPath(patternReference, patterned.assetRoot) ==
               patterned.assetRoot + "/patterns/Geometric/Checker Board.png");
        assert(liveBackground() == FXRGB(16, 32, 48));
        PreferencesBox reloadedPatterns(&window, app.getBackColor(), app.getForeColor(), app.getBackColor(),
                                        app.getSelbackColor(), FXRGB(255, 0, 0), app.getBaseColor());
        FXbool foundPattern = false;
        for (const Theme& theme : reloadedPatterns.Themes)
            if (theme.name == patterned.name)
            {
                Theme loaded;
                assert(theme.deferred && reloadedPatterns.resolveTheme(theme, loaded));
                foundPattern = loaded.assetRoot == patterned.assetRoot &&
                               BackgroundEffect::parse(loaded.effect).patternImage == patternReference;
            }
        assert(foundPattern);

        ActionTimer choosePatternExport([&] {
            FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
            assert(chooser && chooser->getTitle() == "Choose where to export the theme folder");
            chooser->setFilename(exportParent);
            chooser->handle(chooser, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
        });
        app.addTimeout(&choosePatternExport, 1, 150);
        imported.onCmdThemeExport(NULL, 0, NULL);
        FXString patternExport = exportParent + "/Pattern pack";
        assert(xf_existfile(patternExport + "/patterns/Geometric/Checker Board.png"));
        assert(xf_existfile(patternExport + "/patterns/Geometric/Other.png"));
        copyThemeWithName(patternExport + "/theme.swordfish-theme", patternExport + "/import.swordfish-theme",
                          "Imported pattern pack");
        assert(imported.importThemeFile(patternExport + "/import.swordfish-theme"));
        assert(imported.currTheme.assetRoot == patternExport);
        assert(BackgroundEffect::patternPath(patternReference, imported.currTheme.assetRoot) ==
               patternExport + "/patterns/Geometric/Checker Board.png");

        editor.draft = editor.applied = editor.saved = patterned;
        editor.show(PLACEMENT_SCREEN);
        pump(app);
        ActionTimer patternDialog([&] {
            FXWindow* dialog = app.getModalWindow();
            assert(dialog);
            FXTabBook* pages = static_cast<FXTabBook*>(findClass(dialog, "FXTabBook"));
            assert(pages);
            pages->setCurrent(1, true);
            pages->layout();
            pump(app);
            FXWindow* page = pages->childAtIndex(3);
            FXIconList* gallery = static_cast<FXIconList*>(findClass(page, "ThemeGallery"));
            assert(page->shown() && gallery && gallery->shown());
            assert(findLabel(page, "Patterns / Geometric"));
            assert(gallery->getNumItems() == 3 && gallery->getItemText(0) == ".." &&
                   gallery->getItemText(1) == "Checker Board");
            FXIcon* sample = gallery->getItemBigIcon(1);
            gallery->setCurrentItem(2);
            gallery->selectItem(2);
            pump(app);
            assert(sample && sample->getData() && itemShowsPixel(gallery, 1, sample->getData()[0]));
            activateItem(gallery, "Checker Board");
            assert(BackgroundEffect::parse(editor.getDraft().effect).patternImage == patternReference);
            activateItem(gallery, "..");
            assert(gallery->findItem("Geometric") >= 0 && gallery->findItem("Crosshatch") >= 0);
            FXSpinner* size = static_cast<FXSpinner*>(findClass(page, "FXSpinner"));
            assert(size);
            size->setValue(12);
            size->getTarget()->handle(size, FXSEL(SEL_COMMAND, size->getSelector()), NULL);
            assert(BackgroundEffect::parse(editor.getDraft().effect).patternImage == patternReference);
            activateItem(gallery, "Grid");
            BackgroundEffect shape = BackgroundEffect::parse(editor.getDraft().effect);
            assert(shape.patternImage.empty() && shape.pattern == 5);
            activateItem(gallery, "Geometric");
            ActionTimer namePattern([&] {
                InputDialog* naming = dynamic_cast<InputDialog*>(app.getModalWindow());
                assert(naming && naming->getTitle() == "Pattern name");
                naming->setText("UI Added");
                naming->handle(naming, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
            });
            ActionTimer choosePattern([&] {
                FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
                assert(chooser && chooser->getTitle() == "Add PNG pattern");
                selectChooserFile(chooser, redSource + "/textures/checker.png");
                app.addTimeout(&namePattern, 1, 150);
                chooser->handle(chooser, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
            });
            FXButton* addPattern = findButton(page, "Add PNG...");
            assert(addPattern);
            app.addTimeout(&choosePattern, 1, 150);
            addPattern->getTarget()->handle(addPattern, FXSEL(SEL_COMMAND, addPattern->getSelector()), NULL);
            assert(xf_existfile(globalPatterns + "/Geometric/UI Added.png"));
            assert(BackgroundEffect::parse(editor.getDraft().effect).patternImage ==
                   BackgroundEffect::textureReference("Geometric/UI Added"));
            assert(gallery->getItemText(0) == ".." && gallery->findItem("UI Added") > 0 &&
                   gallery->getCurrentItem() == gallery->findItem("UI Added"));
            FXString batchPatternOne = redSource + "/textures/Batch One.png";
            FXString batchPatternTwo = redSource + "/textures/Batch Two.png";
            solidPng(app, batchPatternOne, FXRGB(40, 90, 140));
            solidPng(app, batchPatternTwo, FXRGB(140, 90, 40));
            ActionTimer chooseBatchPattern([&] {
                FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
                assert(chooser && chooser->getTitle() == "Add PNG pattern");
                selectChooserFiles(chooser, batchPatternOne, batchPatternTwo);
                chooser->handle(chooser, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
            });
            app.addTimeout(&chooseBatchPattern, 1, 150);
            addPattern->getTarget()->handle(addPattern, FXSEL(SEL_COMMAND, addPattern->getSelector()), NULL);
            FXString batchPatternReference = BackgroundEffect::textureReference("Geometric/Batch One");
            assert(xf_existfile(globalPatterns + "/Geometric/Batch One.png") &&
                   xf_existfile(globalPatterns + "/Geometric/Batch Two.png") &&
                   BackgroundEffect::parse(editor.getDraft().effect).patternImage == batchPatternReference);
            FXString patternFolderImport = data + "/Batch Pattern Pack";
            assert(xf_mkpath((patternFolderImport + "/Lines").text(), 0755) == 0);
            solidPng(app, patternFolderImport + "/Lines/Needle.png", FXRGB(30, 90, 150));
            FXButton* addFolder = findButton(page, "Add folder...");
            assert(addFolder);
            ActionTimer choosePatternFolder([&] {
                FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
                assert(chooser && chooser->getTitle() == "Import pattern folder");
                chooseFolderInDialog(chooser, patternFolderImport);
            });
            app.addTimeout(&choosePatternFolder, 1, 150);
            addFolder->getTarget()->handle(addFolder, FXSEL(SEL_COMMAND, addFolder->getSelector()), NULL);
            FXString patternFolderReference = BackgroundEffect::textureReference("Batch Pattern Pack/Lines/Needle");
            assert(xf_existfile(globalPatterns + "/Batch Pattern Pack/Lines/Needle.png") &&
                   BackgroundEffect::parse(editor.getDraft().effect).patternImage == patternFolderReference);
            dialog->handle(dialog, FXSEL(SEL_COMMAND, DialogBox::ID_CANCEL), NULL);
        });
        app.addTimeout(&patternDialog, 1, 150);
        editor.properties->setCurrentItem(editor.propertyKeys.size() - 1);
        editor.onEditProperty(NULL, 0, NULL);
        editor.hide();
        puts("PASS: categorized pattern masks, safe imports, theme sharing and visible gallery previews");

        FXColor strip[] = {FXRGB(255, 0, 0), FXRGB(0, 0, 255)};
        FXImage stripImage(&app, strip, IMAGE_KEEP, 2, 1);
        FXColor pixels[16];
        std::fill(pixels, pixels + 16, FXRGB(255, 255, 255));
        BackgroundImage::composite(pixels, 4, 4, stripImage, "fit");
        assert(pixels[0] == FXRGB(255, 255, 255) && pixels[4] == FXRGB(255, 0, 0));
        assert(pixels[7] == FXRGB(0, 0, 255) && pixels[15] == FXRGB(255, 255, 255));
        std::fill(pixels, pixels + 16, FXRGB(255, 255, 255));
        BackgroundImage::composite(pixels, 4, 4, stripImage, "fill");
        assert(FXREDVAL(pixels[0]) > FXBLUEVAL(pixels[0]) &&
               FXBLUEVAL(pixels[3]) > FXREDVAL(pixels[3]) && pixels[0] == pixels[12]);
        std::fill(pixels, pixels + 16, FXRGB(0, 0, 0));
        BackgroundImage::composite(pixels, 4, 4, stripImage, "tile");
        assert(pixels[0] == pixels[2] && pixels[1] == pixels[3] && pixels[0] == pixels[4]);
        FXColor translucent[] = {FXRGBA(255, 0, 0, 128)};
        FXImage translucentImage(&app, translucent, IMAGE_KEEP, 1, 1);
        BackgroundImage::composite(pixels, 4, 4, translucentImage, "tile");
        assert(pixels[0] == FXRGB(255, 0, 0) && pixels[1] == FXRGB(128, 0, 127));

        FXString globalImages = data + "/images";
        assert(xf_mkpath(globalImages.text(), 0755) == 0);
        FXString imageSource = data + "/image-source.png", jpegSource = data + "/image-source.jpg";
        FXString checkerSource = data + "/image-checker.png";
        FXColor rose = FXRGB(235, 17, 173);
        solidPng(app, imageSource, rose);
        solidJpeg(app, jpegSource, FXRGB(50, 100, 150));
        checkerPng(app, checkerSource);
        FXString imageReference = BackgroundEffect::textureReference("Photographs/Rose.png");
        FXString checkerReference = BackgroundEffect::textureReference("Photographs/Checker.png");
        FXString jpegReference = BackgroundEffect::textureReference("Photographs/Photo.jpg");
        FXString imageError;
        assert(BackgroundImage::importImage(&app, imageSource, imageReference, imageError));
        assert(BackgroundImage::importImage(&app, checkerSource, checkerReference, imageError));
        assert(BackgroundImage::importImage(&app, jpegSource, jpegReference, imageError));
        assert(BackgroundImage::load(&app, BackgroundImage::path(jpegReference), imageError));
        assert(BackgroundImage::path(imageReference) == globalImages + "/Photographs/Rose.png");
        imageError.clear();
        assert(!BackgroundImage::importImage(&app, imageSource, imageReference, imageError) && !imageError.empty());
        assert(!BackgroundImage::validReference("../escape.png") &&
               !BackgroundImage::validReference("Photo.gif") &&
               !BackgroundImage::validReference("a/%2Fescape.png"));
        assert(symlink(outsideFolder.text(), (globalImages + "/linked-category").text()) == 0);
        imageError.clear();
        assert(!BackgroundImage::importImage(&app, imageSource, "linked-category/escape.png", imageError) &&
               !imageError.empty() && !xf_existfile(outsideFolder + "/escape.png"));
        {
            const unsigned char oversized[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
                                               0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                               0, 0, 0x20, 1, 0, 0, 0, 2};
            std::ofstream file((data + "/oversized-image.png").text(), std::ios::binary);
            file.write(reinterpret_cast<const char*>(oversized), sizeof(oversized));
            file.close();
            imageError.clear();
            assert(!BackgroundImage::load(&app, data + "/oversized-image.png", imageError) && !imageError.empty());
        }
        Theme decodedImages;
        imageError.clear();
        assert(!decodedImages.decodeProperties("list.image_mode=stretch", imageError));
        assert(!decodedImages.decodeProperties("menus.background_image=Photo.png", imageError));
        assert(!decodedImages.decodeProperties("main.image_opacity=101", imageError));
        assert(decodedImages.decodeProperties("main.image_opacity=40", imageError));
        assert(decodedImages.decodeProperties("tree.background_image=" + imageReference +
                                              "&tree.image_mode=tile&tabs.background_image=" + checkerReference,
                                              imageError));
        assert(decodedImages.property("main", "background_image") == imageReference &&
               decodedImages.property("main", "image_mode") == "tile" &&
               decodedImages.property("tree", "background_image").empty() &&
               decodedImages.property("tabs", "background_image").empty());

        window.applyTheme(patterned);
        FXColor liveBeforeImage = liveBackground();
        editor.draft = editor.applied = editor.saved = patterned;
        editor.draft.setProperty("main", "foreground", "#FFFFFF");
        editor.applied = editor.saved = editor.draft;
        editor.selectElement("main");
        editor.refreshProperties();
        selectProperty("background_image");
        editor.refreshPreview();
        editor.show(PLACEMENT_SCREEN);
        pump(app);
        ActionTimer cancelImage([&] {
            FXWindow* dialog = app.getModalWindow();
            assert(dialog && FXString(dialog->getClassName()) == "BackgroundImageEditor");
            FXButton* help = findButton(dialog, "User Manual");
            assert(help);
            checkManual(app, help->getTarget(), help->getSelector(), help, true);
            FXListBox* placement = findListBoxWithItem(dialog, "Fill (crop to cover)");
            FXIconList* gallery = static_cast<FXIconList*>(findClass(dialog, "ThemeGallery"));
            assert(placement && gallery && findLabel(dialog, "Images"));
            assert(gallery->findItem("No image") > gallery->findItem("Photographs") && gallery->findItem("Photographs") >= 0);
            screenshot(dialog, "background-image-folders");
            activateItem(gallery, "Photographs");
            pump(app);
            assert(findLabel(dialog, "Images / Photographs") && gallery->findItem("No image") < 0);
            screenshot(dialog, "background-image");
            FXint roseIndex = -1, checkerIndex = -1;
            for (FXint i = 0; i < gallery->getNumItems(); ++i)
            {
                if (gallery->getItemText(i) == "Rose.png") roseIndex = i;
                if (gallery->getItemText(i) == "Checker.png") checkerIndex = i;
            }
            assert(roseIndex > 0 && checkerIndex > 0);
            FXIcon* sample = gallery->getItemBigIcon(roseIndex);
            gallery->setCurrentItem(0);
            gallery->selectItem(0);
            pump(app);
            assert(sample && sample->getData() && itemShowsPixel(gallery, roseIndex, sample->getData()[0]));
            placement->setCurrentItem(1);
            gallery->setCurrentItem(roseIndex);
            gallery->getTarget()->handle(gallery, FXSEL(SEL_COMMAND, gallery->getSelector()), NULL);
            pump(app);
            assert(editor.getDraft().property("main", "background_image") == imageReference);
            assert(editor.getDraft().property("main", "image_mode") == "fill");
            FXSpinner* opacity = dynamic_cast<FXSpinner*>(findClass(dialog, "FXSpinner"));
            assert(opacity && opacity->getValue() == 100);
            opacity->setValue(40);
            opacity->getTarget()->handle(opacity, FXSEL(SEL_COMMAND, opacity->getSelector()), NULL);
            assert(editor.getDraft().property("main", "image_opacity") == "40");
            opacity->setValue(100);
            opacity->getTarget()->handle(opacity, FXSEL(SEL_COMMAND, opacity->getSelector()), NULL);
            assert(editor.getDraft().property("main", "image_opacity").empty());
            dialog->hide();
            pump(app);
            assert(windowShowsPixel(findClass(&editor, "ThemeMockup"), rose));
            dialog->show();
            pump(app);
            assert(liveBackground() == liveBeforeImage);
            placement->setCurrentItem(2);
            placement->getTarget()->handle(placement, FXSEL(SEL_COMMAND, placement->getSelector()), NULL);
            gallery->setCurrentItem(checkerIndex);
            gallery->getTarget()->handle(gallery, FXSEL(SEL_COMMAND, gallery->getSelector()), NULL);
            editor.onReadability(NULL, 0, NULL);
            assert(editor.readability->getText().contains("1.00:1"));

            ActionTimer nameImage([&] {
                InputDialog* naming = dynamic_cast<InputDialog*>(app.getModalWindow());
                assert(naming && naming->getTitle() == "Image name");
                naming->setText("UI Added");
                naming->handle(naming, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
            });
            ActionTimer chooseImage([&] {
                FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
                assert(chooser && chooser->getTitle() == "Add background image");
                selectChooserFile(chooser, jpegSource);
                app.addTimeout(&nameImage, 1, 150);
                chooser->handle(chooser, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
            });
            FXButton* add = findButton(dialog, "Add image...");
            assert(add);
            app.addTimeout(&chooseImage, 1, 150);
            add->getTarget()->handle(add, FXSEL(SEL_COMMAND, add->getSelector()), NULL);
            assert(xf_existfile(globalImages + "/Photographs/UI Added.jpg"));
            FXString batchImageOne = data + "/Batch One.png";
            FXString batchImageTwo = data + "/Batch Two.jpg";
            assert(FXFile::copyFiles(imageSource, batchImageOne, false));
            assert(FXFile::copyFiles(jpegSource, batchImageTwo, false));
            ActionTimer chooseBatchImages([&] {
                FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
                assert(chooser && chooser->getTitle() == "Add background image");
                selectChooserFiles(chooser, batchImageOne, batchImageTwo);
                chooser->handle(chooser, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
            });
            app.addTimeout(&chooseBatchImages, 1, 150);
            add->getTarget()->handle(add, FXSEL(SEL_COMMAND, add->getSelector()), NULL);
            assert(xf_existfile(globalImages + "/Photographs/Batch One.png") &&
                   xf_existfile(globalImages + "/Photographs/Batch Two.jpg") &&
                   editor.getDraft().property("main", "background_image") ==
                       BackgroundEffect::textureReference("Photographs/Batch One.png"));
            FXString imageFolderImport = data + "/Batch Background Pack";
            assert(xf_mkpath((imageFolderImport + "/Oak").text(), 0755) == 0);
            assert(xf_mkpath((imageFolderImport + "/Pine").text(), 0755) == 0);
            assert(FXFile::copyFiles(imageSource, imageFolderImport + "/Oak/Scene.png", false));
            assert(FXFile::copyFiles(jpegSource, imageFolderImport + "/Pine/View.jpg", false));
            FXButton* addFolder = findButton(dialog, "Add folder...");
            assert(addFolder);
            ActionTimer chooseImageFolder([&] {
                FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
                assert(chooser && chooser->getTitle() == "Import background-image folder");
                chooseFolderInDialog(chooser, imageFolderImport);
            });
            app.addTimeout(&chooseImageFolder, 1, 150);
            addFolder->getTarget()->handle(addFolder, FXSEL(SEL_COMMAND, addFolder->getSelector()), NULL);
            assert(xf_existfile(globalImages + "/Batch Background Pack/Oak/Scene.png") &&
                   xf_existfile(globalImages + "/Batch Background Pack/Pine/View.jpg") &&
                   editor.getDraft().property("main", "background_image") ==
                       BackgroundEffect::textureReference("Batch Background Pack/Oak/Scene.png"));
            dialog->handle(dialog, FXSEL(SEL_COMMAND, DialogBox::ID_CANCEL), NULL);
        });
        app.addTimeout(&cancelImage, 1, 150);
        editor.onEditProperty(NULL, 0, NULL);
        assert(editor.getDraft().property("main", "background_image").empty());
        assert(liveBackground() == liveBeforeImage);
        ActionTimer applyImage([&] {
            FXWindow* dialog = app.getModalWindow();
            FXListBox* placement = findListBoxWithItem(dialog, "Tile (original size)");
            FXIconList* gallery = static_cast<FXIconList*>(findClass(dialog, "ThemeGallery"));
            assert(placement && gallery);
            activateItem(gallery, "Photographs");
            placement->setCurrentItem(2);
            activateItem(gallery, "Rose.png");
            dialog->handle(dialog, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
        });
        app.addTimeout(&applyImage, 1, 150);
        editor.onEditProperty(NULL, 0, NULL);
        assert(editor.getApplied().property("main", "background_image") == imageReference);
        assert(editor.getApplied().property("main", "image_mode") == "tile");
        assert(liveBackground() == liveBeforeImage);
        editor.onUndo(NULL, 0, NULL);
        assert(editor.getDraft().property("main", "background_image").empty());
        editor.onRedo(NULL, 0, NULL);
        assert(editor.getDraft().property("main", "background_image") == imageReference);
        editor.hide();
        puts("PASS: image compositing, categorized PNG/JPEG gallery, preview, contrast and rollback");

        Theme imageTheme = editor.getDraft();
        imageTheme.name = "Image pack";
        assert(imported.saveEditedTheme(imageTheme, -1));
        assert(imageTheme.assetRoot[0] == '/');
        assert(BackgroundImage::path(imageReference, imageTheme.assetRoot) ==
               imageTheme.assetRoot + "/images/Photographs/Rose.png");
        assert(xf_mkpath((redSource + "/images/Photographs").text(), 0755) == 0);
        solidPng(app, redSource + "/images/Photographs/Rose.png", FXRGB(255, 0, 0));
        Theme localImage = imageTheme;
        localImage.assetRoot = redSource;
        assert(BackgroundImage::path(imageReference, localImage.assetRoot) ==
               redSource + "/images/Photographs/Rose.png");
        window.applyTheme(localImage);
        assert(liveBackground() == FXRGB(255, 0, 0));
        window.applyTheme(imageTheme);
        pump(app);
        assert(liveBackground() == rose);
        FXWindow* sidebar = findClass(&window, "SidebarTree");
        FXWindow* toolbar = findClass(&window, "ThemeToolBar");
        assert(sidebar && toolbar && showsPixelSoon(app, sidebar, rose) && showsPixelSoon(app, toolbar, rose));
        assert(preferences.getBackColor() == DialogColors::background);
        FXWindow* activeTab = NULL;
        FXWindow* inactiveTab = NULL;
        for (FXWindow* child = leftTabs->getFirst(); child; child = child->getNext())
        {
            if (FXString(child->getClassName()) != "TabFrame" || !child->shown()) continue;
            if (leftTabs->allowsBackgroundImage(child)) inactiveTab = child;
            else activeTab = child;
        }
        assert(activeTab && inactiveTab && !windowShowsPixel(activeTab, rose) && windowShowsPixel(inactiveTab, rose));
        leftTabs->setCurrentTab(leftTabs->getCurrentTab() == 0 ? 1 : 0);
        pump(app);
        assert(windowShowsPixel(activeTab, rose) && !windowShowsPixel(inactiveTab, rose));
        FXWindow* tree = window.getDirPanel()->getList();
        FXTabBook* sidebarTabs = dynamic_cast<FXTabBook*>(tree->getParent()->getParent());
        assert(sidebarTabs);
        sidebarTabs->setCurrent(1, true);
        sidebarTabs->layout();
        pump(app);
        assert(windowShowsPixel(tree, rose));
        {
            ThemeEditor treeEditor(&preferences, &window, preferences.Themes, 0, ThemeEditor::EDIT_CURRENT);
            treeEditor.draft = treeEditor.applied = treeEditor.saved = patterned;
            treeEditor.draft.setProperty("main", "background_image", imageReference);
            treeEditor.draft.setProperty("main", "image_mode", "tile");
            treeEditor.create();
            treeEditor.zoom->setCurrentItem(0);
            treeEditor.onPreview(NULL, 0, NULL);
            treeEditor.show(PLACEMENT_SCREEN);
            pump(app);
            assert(windowShowsPixel(findClass(&treeEditor, "ThemeMockup"), rose));
            treeEditor.hide();
        }
        FXint previousHeight = tree->getHeight();
        window.resize(window.getWidth() - 50, window.getHeight() - 60);
        pump(app);
        assert(tree->getHeight() != previousHeight && windowShowsPixel(tree, rose));
        window.applyTheme(patterned);
        pump(app);
        assert(liveBackground() == liveBeforeImage && !windowShowsPixel(tree, rose));
        window.applyTheme(imageTheme);
        pump(app);
        assert(liveBackground() == rose && windowShowsPixel(tree, rose));
        {
            // One image spans the window: each pane shows the part under its own position
            FXString rampSource = data + "/image-ramp.png";
            FXColor* ramp = NULL;
            assert(FXMALLOC(&ramp, FXColor, 4096));
            for (FXint x = 0; x < 4096; ++x) ramp[x] = FXRGB(x & 255, x >> 8, 0);
            {
                FXPNGImage image(&app);
                image.setData(ramp, IMAGE_KEEP | IMAGE_OWNED, 4096, 1);
                FXFileStream output;
                assert(output.open(rampSource, FXStreamSave) && image.savePixels(output));
            }
            FXString rampReference = BackgroundEffect::textureReference("Photographs/Ramp.png");
            assert(BackgroundImage::importImage(&app, rampSource, rampReference, imageError));
            Theme rampTheme = imageTheme;
            rampTheme.setProperty("main", "background_image", rampReference);
            rampTheme.setProperty("main", "image_mode", "tile");
            window.applyTheme(rampTheme);
            pump(app);
            auto paneBackground = [&](IconList* list) -> FXColor
            {
                FXImage image(&app, NULL, IMAGE_KEEP, 1, 1);
                image.create();
                {
                    FXDCWindow dc(&image);
                    list->fillBackground(dc, 0, 0, 1, 1);
                }
                image.restore();
                return image.getData()[0];
            };
            FXint leftX, rightX, unused;
            IconList* leftList = window.getLeftPanel()->getList();
            IconList* rightList = window.getRightPanel()->getList();
            leftList->translateCoordinatesTo(leftX, unused, &window, 0, 0);
            rightList->translateCoordinatesTo(rightX, unused, &window, 0, 0);
            assert(leftX != rightX);
            assert(paneBackground(leftList) == FXRGB(leftX & 255, leftX >> 8, 0));
            assert(paneBackground(rightList) == FXRGB(rightX & 255, rightX >> 8, 0));
            rampTheme.setProperty("main", "background_image", imageReference);
            rampTheme.setProperty("main", "image_opacity", "50");
            window.applyTheme(rampTheme);
            pump(app);
            auto half = [](FXuint base, FXuint image) { return (image * 128 + base * 127 + 127) / 255; };
            assert(liveBackground() == FXRGB(half(FXREDVAL(liveBeforeImage), FXREDVAL(rose)),
                                             half(FXGREENVAL(liveBeforeImage), FXGREENVAL(rose)),
                                             half(FXBLUEVAL(liveBeforeImage), FXBLUEVAL(rose))));
            window.applyTheme(imageTheme);
            pump(app);
            assert(liveBackground() == rose);
        }

        PreferencesBox reloadedImages(&window, app.getBackColor(), app.getForeColor(), app.getBackColor(),
                                      app.getSelbackColor(), FXRGB(255, 0, 0), app.getBaseColor());
        FXbool foundImages = false;
        for (const Theme& theme : reloadedImages.Themes)
            if (theme.name == imageTheme.name)
            {
                Theme loaded;
                assert(theme.deferred && reloadedImages.resolveTheme(theme, loaded));
                foundImages = loaded.assetRoot == imageTheme.assetRoot &&
                              loaded.property("main", "background_image") == imageReference &&
                              loaded.property("main", "image_mode") == "tile";
            }
        assert(foundImages);
        ActionTimer chooseImageExport([&] {
            FileDialog* chooser = dynamic_cast<FileDialog*>(app.getModalWindow());
            assert(chooser && chooser->getTitle() == "Choose where to export the theme folder");
            chooser->setFilename(exportParent);
            chooser->handle(chooser, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
        });
        app.addTimeout(&chooseImageExport, 1, 150);
        imported.onCmdThemeExport(NULL, 0, NULL);
        FXString imageExport = exportParent + "/Image pack";
        assert(xf_existfile(imageExport + "/images/Photographs/Rose.png"));
        copyThemeWithName(imageExport + "/theme.swordfish-theme", imageExport + "/import.swordfish-theme",
                          "Imported image pack");
        assert(imported.importThemeFile(imageExport + "/import.swordfish-theme"));
        imported.onCmdAccept(NULL, FXSEL(SEL_COMMAND, PreferencesBox::ID_APPLY), NULL);
        Theme importedImage = Theme::current(&app);
        assert(importedImage.assetRoot == data + "/themes/colors/Imported image pack");
        assert(BackgroundImage::path(imageReference, importedImage.assetRoot) ==
               importedImage.assetRoot + "/images/Photographs/Rose.png");
        assert(liveBackground() == rose);
        FXString movedImageFolder = portable + "/themes/colors/Imported image pack";
        assert(xf_mkpath((movedImageFolder + "/images/Photographs").text(), 0755) == 0);
        assert(FXFile::copyFiles(importedImage.assetRoot + "/theme.swordfish-theme",
                                 movedImageFolder + "/theme.swordfish-theme", false));
        assert(FXFile::copyFiles(importedImage.assetRoot + "/images/Photographs/Rose.png",
                                 movedImageFolder + "/images/Photographs/Rose.png", false));
        const char* oldPortable = getenv("SWORDFISH_PORTABLE_DATA");
        FXbool hadPortable = oldPortable != NULL;
        FXString oldPortableValue = oldPortable ? oldPortable : "";
        assert(setenv("SWORDFISH_PORTABLE_DATA", portable.text(), 1) == 0);
        Theme relocatedImage = Theme::current(&app);
        assert(relocatedImage.assetRoot == movedImageFolder);
        assert(BackgroundImage::path(imageReference, relocatedImage.assetRoot) ==
               movedImageFolder + "/images/Photographs/Rose.png");
        if (hadPortable) assert(setenv("SWORDFISH_PORTABLE_DATA", oldPortableValue.text(), 1) == 0);
        else assert(unsetenv("SWORDFISH_PORTABLE_DATA") == 0);
        puts("PASS: live list, sidebar, tree, tabs and toolbar images; resize, persistence and sharing");

        Theme removalTheme = imageTheme;
        removalTheme.effect = "fill=pattern colors=#122334,#778899 pattern_image=Fabric/Gingham-Check"
                              " texture=Wood/Oak-Veneer-01 strength=100 rows=off";
        editor.draft = editor.applied = editor.saved = removalTheme;
        editor.selectElement("main");
        editor.refreshProperties();
        selectProperty("background_image");
        editor.refreshPreview();
        editor.show(PLACEMENT_SCREEN);
        pump(app);
        assert(windowShowsPixel(findClass(&editor, "ThemeMockup"), rose));
        FXbool acceptImageRemoval = false;
        ActionTimer removeImage([&] {
            FXWindow* dialog = app.getModalWindow();
            FXIconList* gallery = static_cast<FXIconList*>(findClass(dialog, "ThemeGallery"));
            FXButton* up = findButton(dialog, "Up");
            FXSpinner* opacity = dynamic_cast<FXSpinner*>(findClass(dialog, "FXSpinner"));
            assert(gallery && up && opacity);
            opacity->setValue(40);
            opacity->getTarget()->handle(opacity, FXSEL(SEL_COMMAND, opacity->getSelector()), NULL);
            up->getTarget()->handle(up, FXSEL(SEL_COMMAND, up->getSelector()), NULL);
            clickItem(app, gallery, "No image");
            assert(editor.getDraft().property("main", "background_image").empty());
            assert(editor.getDraft().property("main", "image_mode").empty());
            assert(editor.getDraft().property("main", "image_opacity").empty());
            dialog->hide();
            pump(app);
            assert(!windowShowsPixel(findClass(&editor, "ThemeMockup"), rose));
            assert(liveBackground() == rose);
            dialog->show();
            dialog->handle(dialog, FXSEL(SEL_COMMAND, acceptImageRemoval ? DialogBox::ID_ACCEPT :
                                                                          DialogBox::ID_CANCEL), NULL);
        });
        app.addTimeout(&removeImage, 1, 150);
        editor.onEditProperty(NULL, 0, NULL);
        assert(editor.getDraft().property("main", "background_image") == imageReference);
        acceptImageRemoval = true;
        app.addTimeout(&removeImage, 1, 150);
        editor.onEditProperty(NULL, 0, NULL);
        assert(editor.getApplied().property("main", "background_image").empty());
        editor.onUndo(NULL, 0, NULL);
        assert(editor.getDraft().property("main", "background_image") == imageReference);
        editor.onRedo(NULL, 0, NULL);
        assert(editor.getDraft().property("main", "background_image").empty());

        editor.selectElement("list");
        editor.refreshProperties();
        selectProperty("effect");
        FXbool acceptEffectRemoval = false;
        ActionTimer removeEffects([&] {
            FXWindow* dialog = app.getModalWindow();
            FXTabBook* tabs = static_cast<FXTabBook*>(findClass(dialog, "FXTabBook"));
            assert(tabs);
            tabs->setCurrent(1, true);
            tabs->layout();
            pump(app);
            FXWindow* patternPage = tabs->childAtIndex(3);
            FXIconList* patterns = static_cast<FXIconList*>(findClass(patternPage, "ThemeGallery"));
            FXButton* patternUp = findButton(patternPage, "Up");
            assert(patterns && patternUp);
            patternUp->getTarget()->handle(patternUp, FXSEL(SEL_COMMAND, patternUp->getSelector()), NULL);
            clickItem(app, patterns, "No pattern");
            BackgroundEffect effect = BackgroundEffect::parse(editor.getDraft().effect);
            assert(effect.patternImage.empty() && effect.pattern == 0 && effect.fill == "solid");
            assert(effect.texture == "Wood/Oak-Veneer-01");
            FXListBox* fill = findListBoxWithItem(dialog, "Gradient");
            assert(fill);
            fill->setCurrentItem(2);
            fill->getTarget()->handle(fill, FXSEL(SEL_COMMAND, fill->getSelector()), NULL);
            effect = BackgroundEffect::parse(editor.getDraft().effect);
            assert(effect.fill == "pattern" && effect.pattern > 0);
            assert(patterns->getItemText(patterns->getCurrentItem()) == "Horizontal stripes");
            clickItem(app, patterns, "No pattern");
            fill->setCurrentItem(1);
            fill->getTarget()->handle(fill, FXSEL(SEL_COMMAND, fill->getSelector()), NULL);
            clickItem(app, patterns, "No pattern");
            effect = BackgroundEffect::parse(editor.getDraft().effect);
            assert(effect.fill == "gradient" && effect.texture == "Wood/Oak-Veneer-01");
            clickItem(app, patterns, "Horizontal stripes");
            effect = BackgroundEffect::parse(editor.getDraft().effect);
            assert(effect.fill == "pattern" && effect.pattern > 0);
            patterns->setCurrentItem(patterns->findItem("No pattern"));
            pressKey(patterns, KEY_Return);
            assert(BackgroundEffect::parse(editor.getDraft().effect).fill == "solid");
            clickItem(app, patterns, "Horizontal stripes");
            tabs->setCurrent(2, true);
            tabs->layout();
            pump(app);
            FXWindow* texturePage = tabs->childAtIndex(5);
            FXIconList* textures = static_cast<FXIconList*>(findClass(texturePage, "ThemeGallery"));
            FXButton* textureUp = findButton(texturePage, "Up");
            assert(textures && textureUp);
            textureUp->getTarget()->handle(textureUp, FXSEL(SEL_COMMAND, textureUp->getSelector()), NULL);
            clickItem(app, textures, "No texture");
            effect = BackgroundEffect::parse(editor.getDraft().effect);
            assert(effect.texture.empty() && effect.fill == "pattern" && effect.pattern > 0);

            tabs->setCurrent(1, true);
            tabs->layout();
            pump(app);
            clickItem(app, patterns, "No pattern");
            effect = BackgroundEffect::parse(editor.getDraft().effect);
            assert(effect.patternImage.empty() && effect.pattern == 0 && effect.fill != "pattern");
            dialog->handle(dialog, FXSEL(SEL_COMMAND, acceptEffectRemoval ? DialogBox::ID_ACCEPT :
                                                                           DialogBox::ID_CANCEL), NULL);
        });
        app.addTimeout(&removeEffects, 1, 150);
        editor.onEditProperty(NULL, 0, NULL);
        assert(editor.getDraft().effect == removalTheme.effect);
        acceptEffectRemoval = true;
        app.addTimeout(&removeEffects, 1, 150);
        editor.onEditProperty(NULL, 0, NULL);
        BackgroundEffect removed = BackgroundEffect::parse(editor.getApplied().effect);
        assert(removed.texture.empty() && removed.patternImage.empty() && removed.fill != "pattern");
        Theme persistedRemoval = editor.getApplied();
        persistedRemoval.name = "No graphics regression";
        assert(imported.saveEditedTheme(persistedRemoval, -1));
        PreferencesBox reloadedRemoval(&window, app.getBackColor(), app.getForeColor(), app.getBackColor(),
                                       app.getSelbackColor(), FXRGB(255, 0, 0), app.getBaseColor());
        FXbool foundRemoval = false;
        for (const Theme& theme : reloadedRemoval.Themes)
            if (theme.name == persistedRemoval.name)
            {
                Theme loaded;
                assert(reloadedRemoval.resolveTheme(theme, loaded));
                BackgroundEffect effect = BackgroundEffect::parse(loaded.effect);
                foundRemoval = loaded.property("main", "background_image").empty() &&
                               effect.texture.empty() && effect.patternImage.empty() && effect.fill != "pattern";
                window.applyTheme(loaded);
                assert(liveBackground() == FXRGB(0x12, 0x23, 0x34));
            }
        assert(foundRemoval);
        puts("PASS: removing image, texture and pattern through gallery clicks");

        FXString savedImage = imageTheme.assetRoot + "/images/Photographs/Rose.png";
        assert(unlink(savedImage.text()) == 0);
        assert(symlink(outsideFile.text(), savedImage.text()) == 0);
        assert(!imported.saveCustomThemes(imported.Themes));
        assert(xf_existfile(outsideFile));
        assert(unlink(savedImage.text()) == 0);
        assert(FXFile::copyFiles(globalImages + "/Photographs/Rose.png", savedImage, false));

        assert(unlink((copied + "/textures/shared.png").text()) == 0);
        assert(symlink(outsideFile.text(), (copied + "/textures/shared.png").text()) == 0);
        assert(!imported.saveCustomThemes(imported.Themes));
        assert(xf_existfile(outsideFile));
        puts("PASS: theme-local absolute texture paths, collision isolation, copied folders and portable relocation");
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
