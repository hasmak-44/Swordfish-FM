#include <fx.h>
#include "FileList.h"
#include "DirList.h"
#include "Places.h"
#include "XFileExplorer.h"
#include "Preview.h"
#include "RemoteDirectory.h"
#include "DialogBox.h"
#include "TabButtons.h"
#include "File.h"
#include "MessageBox.h"
#include "Properties.h"
#include "HistInputDialog.h"
#include "FileDict.h"
#include "icons.h"
#include <X11/Xlib.h>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <ctime>
#include <fstream>
#include <iterator>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <mntent.h>

extern FXMainWindow* mainWindow;
extern FXString execpath, homedir, xdgdatahome, xdgconfighome;

static FXString fixture;
static bool delayChildren;
static int childOpens;
static Display* otherClient;
static bool checkPointer;
static int pointerChecks;
static FXString measuredDirectory;
static int stats, lstats, accesses, nestedReads;
static bool measuring;
static bool busyFlushed;

static FXApp* testApp;
static Window observedWindow;
static bool busyCursor;
static DirPanel* networkPanel;
static bool expanding;
static int remoteFilesystemCalls;
static FXString simulatedMount;
static bool simulateStillMounted;
static FXString simulatedGvfsDirectory;
static FXString simulatedMountTable;
static FXString failedUnlink;

extern "C" int unlink(const char* path) noexcept
{
    if (!failedUnlink.empty() && failedUnlink == path)
    {
        errno = EACCES;
        return -1;
    }
    typedef int (*Unlink)(const char*);
    static Unlink realUnlink = reinterpret_cast<Unlink>(dlsym(RTLD_NEXT, "unlink"));
    return realUnlink(path);
}

extern "C" FILE* fopen(const char* path, const char* mode)
{
    typedef FILE* (*Open)(const char*, const char*);
    static Open realOpen = reinterpret_cast<Open>(dlsym(RTLD_NEXT, "fopen"));
    return realOpen(!simulatedMountTable.empty() && std::strcmp(path, MTAB_PATH) == 0 ?
                    simulatedMountTable.text() : path, mode);
}

extern "C" FILE* setmntent(const char* path, const char* mode)
{
    typedef FILE* (*Open)(const char*, const char*);
    static Open realOpen = reinterpret_cast<Open>(dlsym(RTLD_NEXT, "setmntent"));
    return realOpen(!simulatedMountTable.empty() && std::strcmp(path, MTAB_PATH) == 0 ?
                    simulatedMountTable.text() : path, mode);
}

static void checkRemote(const char* path)
{
    const char* prefix = "/run/user/1000/gvfs/smb-share:test";
    if (std::strncmp(path, prefix, std::strlen(prefix)) == 0)
        ++remoteFilesystemCalls;
}

extern "C" int XDefineCursor(Display* display, Window window, Cursor cursor)
{
    typedef int (*DefineCursor)(Display*, Window, Cursor);
    static DefineCursor realDefine = reinterpret_cast<DefineCursor>(dlsym(RTLD_NEXT, "XDefineCursor"));
    if (testApp && window == observedWindow)
    {
        busyCursor = cursor == testApp->getWaitCursor()->id();
    }
    return realDefine(display, window, cursor);
}

extern "C" int XFlush(Display* display)
{
    typedef int (*Flush)(Display*);
    static Flush realFlush = reinterpret_cast<Flush>(dlsym(RTLD_NEXT, "XFlush"));
    if ((measuring || expanding) && busyCursor) busyFlushed = true;
    return realFlush(display);
}

static void checkBusy(const char* path)
{
    if (measuring && std::strncmp(path, measuredDirectory.text(), measuredDirectory.length()) == 0)
    {
        assert(busyCursor && busyFlushed);
    }
}

static bool measuredChild(const char* path)
{
    return measuring && std::strncmp(path, measuredDirectory.text(), measuredDirectory.length()) == 0 &&
           path[measuredDirectory.length()] == '/' &&
           std::strchr(path + measuredDirectory.length() + 1, '/') == NULL;
}

static bool hasLabel(FXWindow* window, const FXString& text)
{
    FXLabel* label = dynamic_cast<FXLabel*>(window);
    if (label && label->getText() == text) return true;
    for (FXWindow* child = window->getFirst(); child; child = child->getNext())
        if (hasLabel(child, text)) return true;
    return false;
}

extern "C" int stat(const char* path, struct stat* info) noexcept
{
    checkRemote(path);
    typedef int (*Stat)(const char*, struct stat*);
    static Stat realStat = reinterpret_cast<Stat>(dlsym(RTLD_NEXT, "stat"));
    checkBusy(path);
    if (measuredChild(path)) { ++stats; usleep(1000); }
    return realStat(path, info);
}

extern "C" int lstat(const char* path, struct stat* info) noexcept
{
    if (simulateStillMounted && simulatedMount == path)
    {
        std::memset(info, 0, sizeof(*info));
        info->st_mode = S_IFDIR | 0700;
        return 0;
    }
    checkRemote(path);
    typedef int (*Stat)(const char*, struct stat*);
    static Stat realStat = reinterpret_cast<Stat>(dlsym(RTLD_NEXT, "lstat"));
    checkBusy(path);
    if (measuredChild(path)) { ++lstats; usleep(1000); }
    return realStat(path, info);
}

extern "C" int access(const char* path, int mode) noexcept
{
    checkRemote(path);
    typedef int (*Access)(const char*, int);
    static Access realAccess = reinterpret_cast<Access>(dlsym(RTLD_NEXT, "access"));
    checkBusy(path);
    if (measuredChild(path)) { ++accesses; usleep(1000); }
    return realAccess(path, mode);
}

class TestItem : public FileItem
{
public:
    using FileItem::FileItem;
    int mark() const { return treeMark; }
};

class TestList : public FileList
{
public:
    using FileList::FileList;
    using FileList::listItems;
    IconItem* createItem(const FXString& text, FXIcon* big, FXIcon* mini, void* ptr) override
    {
        return new TestItem(getApp(), text, big, mini, ptr);
    }
    int mark(int index) const { return static_cast<TestItem*>(getItem(index))->mark(); }
};

class TestDirectoryItem : public DirItem
{
public:
    explicit TestDirectoryItem(const FXString& name) : DirItem(name)
    {
        state |= FOLDER;
    }
};

struct SidebarPaint
{
    FXString text;
    FXColor textColor = 0;
    FXint textX = 0, textY = 0;
    const FXIcon* icon = NULL;
    FXint iconX = 0, iconY = 0;
    bool focused = false, filled = false;
    FXint focusX = 0, focusY = 0, focusW = 0, focusH = 0;
    FXint fillX = 0, fillY = 0, fillW = 0, fillH = 0;
    FXColor fillColor = 0;
};

class SidebarRecordingDC : public FXDC
{
    SidebarPaint& paint;
    FXColor foreground = 0;
public:
    SidebarRecordingDC(FXApp* app, SidebarPaint& result) : FXDC(app), paint(result) {}
    void setForeground(FXColor color) override { foreground = color; }
    void drawIcon(const FXIcon* icon, FXint x, FXint y) override
    {
        paint.icon = icon; paint.iconX = x; paint.iconY = y;
    }
    void drawText(FXint x, FXint y, const FXString& text) override
    {
        paint.text = text; paint.textColor = foreground; paint.textX = x; paint.textY = y;
    }
    void drawText(FXint x, FXint y, const FXchar* text, FXuint length) override
    {
        drawText(x, y, FXString(text, length));
    }
    void drawFocusRectangle(FXint x, FXint y, FXint w, FXint h) override
    {
        paint.focused = true; paint.focusX = x; paint.focusY = y; paint.focusW = w; paint.focusH = h;
    }
    void fillRectangle(FXint x, FXint y, FXint w, FXint h) override
    {
        if (!paint.filled)
        {
            paint.filled = true; paint.fillX = x; paint.fillY = y;
            paint.fillW = w; paint.fillH = h; paint.fillColor = foreground;
        }
    }
    void drawRectangle(FXint, FXint, FXint, FXint) override {}
    void fillPolygon(const FXPoint*, FXuint) override {}
};

static SidebarPaint recordSidebarPaint(const SidebarItem& item, const SidebarTree* tree)
{
    SidebarPaint result;
    SidebarRecordingDC dc(tree->getApp(), result);
    item.draw(tree, dc, 20, 10, item.getWidth(tree), item.getHeight(tree));
    return result;
}

static FXdouble textContrast(FXColor foreground, FXColor background)
{
    auto luminance = [](FXColor color)
    {
        auto linear = [](FXuint component)
        {
            FXdouble value = component / 255.0;
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(FXREDVAL(color)) + 0.7152 * linear(FXGREENVAL(color)) +
               0.0722 * linear(FXBLUEVAL(color));
    };
    FXdouble fg = luminance(foreground), bg = luminance(background);
    return (FXMAX(fg, bg) + 0.05) / (FXMIN(fg, bg) + 0.05);
}

static void checkSidebarDeviceText(const SidebarItem& item, const SidebarTree* tree, bool muted)
{
    const FXColor foreground = item.isSelected() ? tree->getSelTextColor() : tree->getTextColor();
    const FXColor background = item.isSelected() ? tree->getSelBackColor() : tree->getBackColor();
    const SidebarPaint paint = recordSidebarPaint(item, tree);
    const FXString suffix = " (unmounted)";
    if (muted && paint.textColor != foreground)
        assert(paint.text + suffix == item.getText());
    else
        assert(paint.text == item.getText());
    if (!muted || textContrast(foreground, background) < 4.5)
        assert(paint.textColor == foreground);
    else
        assert(textContrast(paint.textColor, background) >= 4.5);
    if (muted && textContrast(foreground, background) >= 7.0) assert(paint.textColor != foreground);
}

static void checkSidebarDeviceRendering(SidebarTree* tree)
{
    const FXColor oldBack = tree->getBackColor(), oldText = tree->getTextColor();
    const FXColor oldSelBack = tree->getSelBackColor(), oldSelText = tree->getSelTextColor();
    struct Theme { FXColor back, text, selectedBack, selectedText; };
    const Theme themes[] = {
        { FXRGB(255, 255, 255), FXRGB(0, 0, 0), FXRGB(30, 70, 130), FXRGB(255, 255, 255) },
        { FXRGB(32, 32, 32), FXRGB(255, 255, 255), FXRGB(235, 235, 235), FXRGB(0, 0, 0) },
        { FXRGB(255, 255, 255), FXRGB(0, 0, 0), FXRGB(82, 129, 148), FXRGB(255, 255, 255) }
    };
    SidebarItem mounted("Device (unmounted)", miniremovabledeviceicon, SidebarItem::DEVICE, "/dev/test");
    SidebarItem unmounted("Device (unmounted)", miniremovabledeviceicon, SidebarItem::DEVICE, "/dev/test");
    mounted.deviceAction = SidebarItem::UNMOUNT_DEVICE;
    unmounted.deviceAction = SidebarItem::MOUNT_DEVICE;
    mounted.canEject = unmounted.canEject = true;
    mounted.setFocus(true); unmounted.setFocus(true);
    for (const Theme& theme : themes)
    {
        tree->setBackColor(theme.back); tree->setTextColor(theme.text);
        tree->setSelBackColor(theme.selectedBack); tree->setSelTextColor(theme.selectedText);
        for (bool selected : {false, true})
        {
            mounted.setSelected(selected); unmounted.setSelected(selected);
            SidebarPaint normal = recordSidebarPaint(mounted, tree);
            SidebarPaint muted = recordSidebarPaint(unmounted, tree);
            const FXColor background = selected ? theme.selectedBack : theme.back;
            const FXColor foreground = selected ? theme.selectedText : theme.text;
            const bool dimmed = muted.textColor != foreground;
            assert(normal.text == mounted.getText());
            assert(muted.text == (dimmed ? FXString("Device") : normal.text));
            assert(normal.textColor == foreground && muted.icon == normal.icon);
            assert(muted.textX == normal.textX && muted.textY == normal.textY);
            assert(muted.iconX == normal.iconX && muted.iconY == normal.iconY);
            assert(muted.focused && normal.focused && muted.focusX == normal.focusX &&
                   muted.focusY == normal.focusY && muted.focusH == normal.focusH);
            assert(dimmed ? muted.focusW < normal.focusW : muted.focusW == normal.focusW);
            if (selected)
                assert(muted.filled && normal.filled && muted.fillColor == normal.fillColor &&
                       muted.fillX == normal.fillX && muted.fillY == normal.fillY &&
                       muted.fillH == normal.fillH &&
                       (dimmed ? muted.fillW < normal.fillW : muted.fillW == normal.fillW));
            if (textContrast(foreground, background) >= 4.5)
                assert(textContrast(muted.textColor, background) >= 4.5);
            else
                assert(muted.textColor == foreground);
            if (textContrast(foreground, background) >= 7.0) assert(muted.textColor != foreground);
        }
    }
    unmounted.setEnabled(false); mounted.setEnabled(false);
    assert(recordSidebarPaint(unmounted, tree).textColor == recordSidebarPaint(mounted, tree).textColor);
    unmounted.setEnabled(true); unmounted.kind = SidebarItem::PLACE;
    assert(recordSidebarPaint(unmounted, tree).textColor == tree->getSelTextColor());
    tree->setBackColor(oldBack); tree->setTextColor(oldText);
    tree->setSelBackColor(oldSelBack); tree->setSelTextColor(oldSelText);
}

extern "C" DIR* opendir(const char* path)
{
    checkRemote(path);
    typedef DIR* (*OpenDirectory)(const char*);
    static OpenDirectory realOpen = reinterpret_cast<OpenDirectory>(dlsym(RTLD_NEXT, "opendir"));
    assert(realOpen);
    FXString gvfsRoot = "/run/user/" + FXStringVal(getuid()) + "/gvfs";
    if (!simulatedGvfsDirectory.empty() && gvfsRoot == path)
        return realOpen(simulatedGvfsDirectory.text());
    if (expanding) assert(busyCursor && busyFlushed);
    if (measuring && std::strncmp(path, measuredDirectory.text(), measuredDirectory.length()) == 0 &&
        path[measuredDirectory.length()] == '/' && path[measuredDirectory.length() + 1])
    {
        ++nestedReads;
    }
    if (delayChildren && std::strncmp(path, fixture.text(), fixture.length()) == 0 &&
        path[fixture.length()] == '/' && path[fixture.length() + 1] != '\0')
    {
        ++childOpens;
        usleep(1500000);
    }
    if (checkPointer)
    {
        int result = XGrabPointer(otherClient, DefaultRootWindow(otherClient), False,
                                  ButtonPressMask, GrabModeAsync, GrabModeAsync,
                                  None, None, CurrentTime);
        assert(result == GrabSuccess);
        XUngrabPointer(otherClient, CurrentTime);
        XSync(otherClient, False);
        ++pointerChecks;
    }
    return realOpen(path);
}

static int find(FileList* list, const char* name)
{
    for (int i = 0; i < list->getNumItems(); ++i)
    {
        if (list->getItemFilename(i) == name) return i;
    }
    return -1;
}

static SidebarTree* findSidebar(FXWindow* parent)
{
    SidebarTree* tree = dynamic_cast<SidebarTree*>(parent);
    if (tree) return tree;
    for (FXWindow* child = parent->getFirst(); child; child = child->getNext())
    {
        if ((tree = findSidebar(child))) return tree;
    }
    return NULL;
}

static MountsList* findMounts(FXWindow* parent)
{
    MountsList* mounts = dynamic_cast<MountsList*>(parent);
    if (mounts) return mounts;
    for (FXWindow* child = parent->getFirst(); child; child = child->getNext())
    {
        if ((mounts = findMounts(child))) return mounts;
    }
    return NULL;
}

static int pump(FXApp& app, int milliseconds)
{
    const FXTime until = FXThread::time() + milliseconds * 1000000LL;
    int iterations = 0;
    while (FXThread::time() < until)
    {
        int status;
        pid_t child;
        while ((child = waitpid(-1, &status, WNOHANG)) > 0)
            // A stopped network helper is reaped after the panel forgot its pid.
            if (!RemoteDirectory::harvest(child, status))
            {
                assert(networkPanel);
                networkPanel->handleNetworkExit(child, status);
            }
        const FXTime start = FXThread::time();
        app.runOneEvent(false);
        if (FXThread::time() - start >= 150000000LL)
            fprintf(stderr, "UI event stalled for %lld ms\n", (long long)((FXThread::time() - start) / 1000000));
        assert(FXThread::time() - start < 150000000LL);
        ++iterations;
        usleep(1000);
    }
    return iterations;
}

class RemoteObserver : public FXObject
{
public:
    int notifications = 0;
    long handle(FXObject*, FXSelector, void*) override { ++notifications; return 1; }
};

class AuthenticationResponder : public FXObject
{
    static void fields(FXWindow* parent, std::vector<FXTextField*>& result)
    {
        if (FXTextField* field = dynamic_cast<FXTextField*>(parent)) result.push_back(field);
        for (FXWindow* child = parent->getFirst(); child; child = child->getNext()) fields(child, result);
    }
public:
    bool cancel = false;
    int prompts = 0;
    long handle(FXObject*, FXSelector, void*) override
    {
        DialogBox* dialog = dynamic_cast<DialogBox*>(testApp->getModalWindow());
        if (!dialog || dialog->getTitle() != "Server authentication")
        {
            testApp->addTimeout(this, 1, 20);
            return 1;
        }
        ++prompts;
        std::vector<FXTextField*> inputs;
        fields(dialog, inputs);
        assert(inputs.size() == 3);
        inputs[1]->setText("test-user");
        inputs[2]->setText("");
        dialog->handle(this, FXSEL(SEL_COMMAND, cancel ? DialogBox::ID_CANCEL : DialogBox::ID_ACCEPT), NULL);
        return 1;
    }
};

class TrashConfirmation : public FXObject
{
public:
    bool cancel = false;
    bool expectError = false;
    int answered = 0;
    int errors = 0;
    long handle(FXObject*, FXSelector, void*) override
    {
        MessageBox* dialog = dynamic_cast<MessageBox*>(testApp->getModalWindow());
        if (!dialog)
        {
            testApp->addTimeout(this, 1, 20);
            return 1;
        }
        if (dialog->getTitle() == "Error")
        {
            assert(expectError);
            ++errors;
            dialog->handle(this, FXSEL(SEL_COMMAND, MessageBox::ID_CLICKED_CANCEL), NULL);
            return 1;
        }
        assert(dialog->getTitle() == "Empty trash can");
        ++answered;
        dialog->handle(this, FXSEL(SEL_COMMAND,
                       cancel ? MessageBox::ID_CLICKED_CANCEL : MessageBox::ID_CLICKED_OK), NULL);
        if (expectError) testApp->addTimeout(this, 1, 20);
        return 1;
    }
};

class ConnectCanceller : public FXObject
{
public:
    int prompts = 0;
    FXString message;
    long handle(FXObject*, FXSelector, void*) override
    {
        DialogBox* dialog = dynamic_cast<DialogBox*>(testApp->getModalWindow());
        if (!dialog || dialog->getTitle() != "Connect to Server")
        {
            testApp->addTimeout(this, 1, 50);
            return 1;
        }
        ++prompts;
        FXWindow* contents = dialog->getFirst();
        for (FXWindow* child = contents ? contents->getFirst() : NULL; child; child = child->getNext())
        {
            if (FXLabel* label = dynamic_cast<FXLabel*>(child)) message += label->getText() + "\n";
        }
        dialog->handle(this, FXSEL(SEL_COMMAND, DialogBox::ID_CANCEL), NULL);
        return 1;
    }
};

class MountMessageResponder : public FXObject
{
public:
    int errors = 0;
    int successes = 0;
    int alreadyMounted = 0;
    FXString message;
    FXString title;
    long handle(FXObject*, FXSelector, void*) override
    {
        MessageBox* dialog = dynamic_cast<MessageBox*>(testApp->getModalWindow());
        if (!dialog)
        {
            testApp->addTimeout(this, 1, 20);
            return 1;
        }
        if (dialog->getTitle() == "Error") ++errors;
        if (dialog->getTitle() == "Success") ++successes;
        if (dialog->getTitle() == "Already mounted") ++alreadyMounted;
        message = dialog->getText();
        title = dialog->getTitle();
        dialog->handle(this, FXSEL(SEL_COMMAND, MessageBox::ID_CLICKED_OK), NULL);
        return 1;
    }
};

// Records a context menu while it is open, then runs the entry named by choose
class PopupMenuResponder : public FXObject
{
public:
    FXString labels;
    FXString choose;
    long handle(FXObject*, FXSelector, void*) override
    {
        FXMenuPane* menu = dynamic_cast<FXMenuPane*>(testApp->getModalWindow());
        if (!menu)
        {
            testApp->addTimeout(this, 1, 20);
            return 1;
        }
        FXMenuCommand* chosen = NULL;
        labels.clear();
        collect(menu, chosen);
        menu->popdown();
        if (chosen) chosen->getTarget()->handle(chosen, FXSEL(SEL_COMMAND, chosen->getSelector()), NULL);
        return 1;
    }
    void collect(FXWindow* pane, FXMenuCommand*& chosen)
    {
        for (FXWindow* child = pane->getFirst(); child; child = child->getNext())
        {
            if (FXMenuCascade* cascade = dynamic_cast<FXMenuCascade*>(child))
            {
                labels += cascade->getText() + "[";
                collect(cascade->getMenu(), chosen);
                labels += "]";
            }
            else if (FXMenuCommand* command = dynamic_cast<FXMenuCommand*>(child))
            {
                labels += command->getText() + ";";
                if (command->getText() == choose) chosen = command;
            }
        }
    }
};

// Answers the Open With dialog with a command, optionally ticking "Remember"
class OpenWithResponder : public FXObject
{
public:
    FXString command;
    FXbool remember = false;
    FXbool showedType = false;
    long handle(FXObject*, FXSelector, void*) override;
};

static FXWindow* findText(FXWindow* parent, const FXString& prefix);

long OpenWithResponder::handle(FXObject*, FXSelector, void*)
{
    HistInputDialog* dialog = dynamic_cast<HistInputDialog*>(testApp->getModalWindow());
    if (!dialog)
    {
        testApp->addTimeout(this, 1, 20);
        return 1;
    }
    showedType = findText(dialog, "Open Song One.mp3 (audio/mpeg) with:") != NULL;
    dialog->setText(command);
    if (remember) dialog->handle(this, FXSEL(SEL_COMMAND, DialogBox::ID_TOGGLE_OPTION), NULL);
    dialog->handle(this, FXSEL(SEL_COMMAND, DialogBox::ID_ACCEPT), NULL);
    return 1;
}

static FXString popupMedia(FilePanel* panel, const FXString& choose)
{
    PopupMenuResponder responder;
    responder.choose = choose;
    testApp->addTimeout(&responder, 1, 20);
    FXEvent event;
    panel->onCmdPopupMenu(NULL, 0, &event);
    testApp->removeTimeout(&responder, 1);
    return responder.labels;
}

static DialogBox* downloadWindow()
{
    for (FXWindow* child = testApp->getRootWindow()->getFirst(); child; child = child->getNext())
    {
        DialogBox* dialog = dynamic_cast<DialogBox*>(child);
        if (dialog && dialog->getTitle() == "Media Download") return dialog;
    }
    return NULL;
}

static FXWindow* findText(FXWindow* parent, const FXString& prefix)
{
    FXLabel* label = dynamic_cast<FXLabel*>(parent);
    if (label && label->getText().left(prefix.length()) == prefix) return label;
    for (FXWindow* child = parent->getFirst(); child; child = child->getNext())
        if (FXWindow* found = findText(child, prefix)) return found;
    return NULL;
}

static FXButton* projectLink(FXWindow* parent)
{
    FXButton* button = dynamic_cast<FXButton*>(parent);
    if (button && button->getText() == "https://github.com/hasmak-44/Swordfish-FM") return button;
    for (FXWindow* child = parent->getFirst(); child; child = child->getNext())
        if ((button = projectLink(child))) return button;
    return NULL;
}

class AboutLinkResponder : public FXObject
{
public:
    bool clicked = false;
    long handle(FXObject*, FXSelector, void*) override
    {
        MessageBox* dialog = dynamic_cast<MessageBox*>(testApp->getModalWindow());
        if (!dialog || dialog->getTitle() != "About SwordFish")
        {
            testApp->addTimeout(this, 1, 20);
            return 1;
        }
        FXButton* link = projectLink(dialog);
        assert(link && link->getSelector() == XFileExplorer::ID_PROJECT_WEBSITE);
        assert(dialog->getText().contains("Hassanein Makhlouf (hasmak-44)"));
        assert(dialog->getText().contains("with AI-assisted development"));
        assert(dialog->getText().contains("https://github.com/hasmak-44/Swordfish-FM/issues"));
        link->getTarget()->handle(link, FXSEL(SEL_COMMAND, link->getSelector()), NULL);
        clicked = true;
        dialog->handle(this, FXSEL(SEL_COMMAND, MessageBox::ID_CLICKED_OK), NULL);
        return 1;
    }
};

int main(int argc, char** argv)
{
    std::setvbuf(stdout, NULL, _IOLBF, 0);
    assert(argc == 4);
    fixture = argv[1];
    FXString executable = FXPath::absolute(argv[2]);
    FXString icons = argv[3];
    FXApp app("SwordfishExpansionTest", "SwordfishExpansionTest");
    testApp = &app;
    app.init(argc, argv);
    homedir = FXSystem::getHomeDirectory();
    xdgconfighome = homedir + "/config";
    xdgdatahome = homedir + "/data";
    execpath = executable;
    const FXString hashFixture = fixture + "/mount-hash-regression";
    FXulong previousHash = 0;
    int previouslyClamped = 0;
    for (int n = 0; n < 100; ++n)
    {
        const std::string contents = "/dev/sdb1 /media/user/USB-" + std::to_string(n) +
                                     " vfat rw,nosuid,nodev 0 0\n";
        {
            std::ofstream output(hashFixture.text());
            assert(output);
            output << contents;
        }
        FXulong expected = 7873;
        for (unsigned char c : contents) expected = expected * 33 + c;
        if (expected & (FXulong(1) << (sizeof(FXulong) * 8 - 1))) ++previouslyClamped;
        const FXulong actual = xf_hashfile(hashFixture.text());
        assert(actual == expected && actual != previousHash);
        previousHash = actual;
    }
    assert(previouslyClamped > 1);
    assert(unlink(hashFixture.text()) == 0);
    assert(xf_keepmount("/media/user/USB", "/dev/sdb1"));
    assert(xf_keepmount("/run/media/user/USB", "/dev/sdb1"));
    assert(!xf_keepmount("/run/user/1000/gvfs", "gvfsd-fuse"));
    assert(!xf_keepmount("/run/other", "tmpfs"));
    assert(!xf_keepmount("/run/mediator", "tmpfs"));
    FXString commandOutput = "old output";
    assert(xf_hostcommandoutput("printf 'localized headings without a device column'", commandOutput));
    assert(commandOutput == "localized headings without a device column");
    assert(!xf_hostcommandoutput("sh -c 'printf service-unavailable; exit 1'", commandOutput));
    assert(commandOutput == "service-unavailable");
    puts("PASS: mount hashes preserve high bits, /run/media is visible, host query uses exit status");
    const char* hostPathEnvironment = getenv("SWORDFISH_HOST_PATH");
    const FXString savedHostPath = hostPathEnvironment ? hostPathEnvironment : "";
    setenv("SWORDFISH_HOST_PATH", "/usr/bin:/bin", 1);
    vector_FXString actualVolumes;
    assert(xf_unmountedvolumes(actualVolumes));
    if (hostPathEnvironment) setenv("SWORDFISH_HOST_PATH", savedHostPath.text(), 1);
    else unsetenv("SWORDFISH_HOST_PATH");
    puts("PASS: production volume inventory command succeeds against real host lsblk (read-only)");
    app.reg().writeStringEntry("SETTINGS", "iconpath", icons.text());
    // Background folder sizes read child folders by design; these checks cover the listing itself
    app.reg().writeUnsignedEntry("OPTIONS", "folder_size_mode", 0);
    FXuint status;
    assert(loadAppIcons(&app, &status));
    FXMainWindow* shell = new FXMainWindow(&app, "Folder expansion regression", NULL, NULL,
                                          DECOR_ALL, 0, 0, 900, 600);
    mainWindow = shell;
    FXHorizontalFrame* frame = new FXHorizontalFrame(shell, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    FXuint columns[] = {FileList::ID_COL_NAME, FileList::ID_COL_SIZE, FileList::ID_COL_TYPE};
    TestList* list = new TestList(shell, frame, columns, 3, NULL, 0, false, 100, 100,
                                  LAYOUT_FILL_X | LAYOUT_FILL_Y);
    SidebarTree* sidebar = new SidebarTree(frame, NULL, 0, LAYOUT_FILL_Y);
    DirList* tree = new DirList(shell, frame, NULL, 0);
    app.create();
    shell->show();
    checkSidebarDeviceRendering(sidebar);
    puts("PASS: unmounted device text follows light, dark and selected theme colors with readable contrast");
    simulatedMountTable = fixture + "/mount-table-regression";
    auto mountTable = [&](bool attached)
    {
        std::ofstream output(simulatedMountTable.text());
        assert(output);
        output << "/dev/root / ext4 rw 0 0\n";
        if (attached)
            output << "/dev/sdb1 /run/media/test/USB vfat rw 0 0\n";
    };
    mountTable(false);
    MountsList* deviceModel = new MountsList(shell, frame, NULL, 0);
    assert(deviceModel->findItem("USB") == -1);
    mountTable(true);
    assert(xf_mountroot("/run/media/test/USB/folder") == "/run/media/test/USB");
    assert(xf_mountroot("/run/media/test/USB-other").empty());
    assert(xf_mountroot(ROOTDIR).empty());
    deviceModel->onMountsRefresh(NULL, 0, NULL);
    assert(deviceModel->findItem("USB") >= 0);
    assert(deviceModel->getItemPathname("USB") == "/run/media/test/USB");
    mountTable(false);
    deviceModel->onMountsRefresh(NULL, 0, NULL);
    assert(deviceModel->findItem("USB") == -1);
    const FXString volumesFixture = fixture + "/unmounted-volumes";
    {
        std::ofstream output(volumesFixture.text());
        output << "PATH=\"/dev/sdb1\" LABEL=\"\" FSTYPE=\"\" MOUNTPOINT=\"\" HOTPLUG=\"1\" TYPE=\"part\"\n"
               << "PATH=\"/dev/test-internal1\" LABEL=\"Internal\\x20data\" FSTYPE=\"ntfs\" MOUNTPOINT=\"\" HOTPLUG=\"0\" TYPE=\"part\"\n"
               << "PATH=\"/dev/test-usb1\" LABEL=\"USB\\x20data\" FSTYPE=\"ext4\" MOUNTPOINT=\"\" HOTPLUG=\"1\" TYPE=\"part\"\n"
               << "PATH=\"/dev/test-blank1\" LABEL=\"\" FSTYPE=\"\" MOUNTPOINT=\"\" HOTPLUG=\"0\" TYPE=\"part\"\n"
               << "PATH=\"/dev/test-swap1\" LABEL=\"Swap\" FSTYPE=\"swap\" MOUNTPOINT=\"\" HOTPLUG=\"0\" TYPE=\"part\"\n";
    }
    setenv("SWORDFISH_TEST_VOLUMES", volumesFixture.text(), 1);
    deviceModel->invalidate();
    deviceModel->onMountsRefresh(NULL, 0, NULL);
    assert(deviceModel->getItemPathname("USB") == "/dev/sdb1");
    assert(deviceModel->getItemPathname("Internal data") == "/dev/test-internal1");
    assert(deviceModel->getItemPathname("USB data") == "/dev/test-usb1");
    assert(deviceModel->findItem("test-blank1") == -1 && deviceModel->findItem("Swap") == -1);
    assert(xf_hotplugdevice("/dev/test-usb1") && !xf_hotplugdevice("/dev/test-internal1"));
    unsetenv("SWORDFISH_TEST_VOLUMES");
    deviceModel->invalidate();
    deviceModel->onMountsRefresh(NULL, 0, NULL);
    assert(deviceModel->findItem("USB") == -1 && deviceModel->findItem("Internal data") == -1);
    assert(unlink(volumesFixture.text()) == 0);
    delete deviceModel;
    assert(unlink(simulatedMountTable.text()) == 0);
    simulatedMountTable.clear();
    puts("PASS: /run/media device appears on mount and disappears after external unmount");
    puts("PASS: unmounted internal and USB filesystems remain remountable; missing FSTYPE retains a known volume; unplugged devices disappear");
    observedWindow = list->id();
    otherClient = XOpenDisplay(NULL);
    assert(otherClient);
    list->setViewMode(FileList::VIEW_DETAILS);
    delayChildren = true;
    auto start = std::chrono::steady_clock::now();
    list->setDirectory(fixture);
    list->scan(true);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    assert(childOpens == 0 && elapsed < 1000);
    delayChildren = false;
    puts("PASS: listing and refresh never open collapsed children, even with delayed directory reads");

    int folder = find(list, "folder");
    assert(folder >= 0 && list->mark(folder) == 1);
    int link = find(list, "link-folder");
    int broken = find(list, "broken-link");
    assert(link >= 0 && list->isItemDirectory(link) &&
           static_cast<FileItem*>(list->getItem(link))->isSymlink());
    assert(broken >= 0 && !list->isItemDirectory(broken) &&
           static_cast<FileItem*>(list->getItem(broken))->isSymlink());
    expanding = true;
    list->toggleExpand(folder, 1);
    expanding = false;
    assert(!busyCursor);
    assert(find(list, "folder/nested.txt") >= 0);
    list->toggleExpand(find(list, "folder"), 0);
    assert(find(list, "folder/nested.txt") < 0);
    int empty = find(list, "empty");
    assert(empty >= 0 && list->mark(empty) == 1);
    list->toggleExpand(empty, 1);
    assert(list->mark(find(list, "empty")) == 0);
    list->listItems(false);
    assert(list->mark(find(list, "empty")) == 0);
    list->scan(true);
    assert(list->mark(find(list, "empty")) == 1);
    puts("PASS: explicit expansion, collapse, lazy empty-folder detection, and refresh");

    measuredDirectory = fixture + "/large";
    stats = lstats = accesses = nestedReads = 0;
    measuring = true;
    start = std::chrono::steady_clock::now();
    list->setDirectory(measuredDirectory);
    elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    measuring = false;
    assert(list->getNumItems() == 794 && nestedReads == 0 && !busyCursor);
    printf("793 folders, simulated 1ms metadata latency: %ldms, stat=%d lstat=%d access=%d child-reads=%d\n",
           elapsed, stats, lstats, accesses, nestedReads);
    assert(lstats == 794 && stats == 0 && accesses == 794);
    puts("PASS: busy cursor active and flushed before first metadata read, balanced after listing");
    list->setDirectory(fixture);

    SidebarItem* item = new SidebarItem("fixture", minifoldericon, SidebarItem::DIRECTORY, fixture);
    sidebar->appendItem(NULL, item);
    sidebar->grab();
    app.flush(true);
    assert(sidebar->grabbed());
    checkPointer = true;
    busyFlushed = false;
    expanding = true;
    sidebar->onExpandItem(NULL, 0, item);
    expanding = false;
    checkPointer = false;
    assert(!sidebar->grabbed() && pointerChecks > 0 && item->loaded && !busyCursor);
    puts("PASS: Places expansion releases and flushes pointer grab before directory reads");

    tree->setDirectory(fixture, true);
    DirItem* current = static_cast<DirItem*>(tree->getCurrentItem());
    assert(current && current->isDirectory());
    tree->grab();
    app.flush(true);
    assert(tree->grabbed());
    pointerChecks = 0;
    checkPointer = true;
    busyFlushed = false;
    expanding = true;
    tree->onExpanded(NULL, 0, current);
    expanding = false;
    checkPointer = false;
    assert(!tree->grabbed() && pointerChecks > 0 && !busyCursor);
    puts("PASS: Tree expansion releases pointer so another X11 client can receive input");

    FXString remote = "/run/user/1000/gvfs/smb-share:test/large";
    start = std::chrono::steady_clock::now();
    list->setDirectory(remote);
    elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    assert(elapsed < 150 && busyCursor && RemoteDirectory::loading(remote));
    assert(list->getNumItems() == 1);
    // Bound first-batch readiness without assuming a fixed child-process startup time.
    const FXTime firstBatchDeadline = FXThread::time() + 2000000000LL;
    while (list->getNumItems() == 1 && FXThread::time() < firstBatchDeadline) pump(app, 20);
    if (!(list->getNumItems() > 1 && list->getNumItems() < 794 && busyCursor))
        fprintf(stderr, "Remote batch: items=%d busy=%d error=%s\n", list->getNumItems(),
                busyCursor, RemoteDirectory::error(remote).text());
    assert(list->getNumItems() > 1 && list->getNumItems() < 794 && busyCursor);
    list->selectItem(find(list, "folder-0"));
    assert(pump(app, 1600) > 100);
    assert(list->getNumItems() == 794 && !busyCursor && !RemoteDirectory::loading(remote));
    assert(list->isItemSelected(find(list, "folder-0")));
    assert(remoteFilesystemCalls == 0);
    puts("PASS: 793 remote folders arrive progressively, UI stays responsive, selection survives, no per-entry FUSE calls");
    list->setAllowRefresh(false);
    list->onRemoteChanged(NULL, 0, NULL);
    assert(busyCursor);
    list->setAllowRefresh(true);
    list->onCmdRefreshTimer(NULL, 0, NULL);
    assert(!busyCursor && list->getNumItems() == 794);
    puts("PASS: remote UI updates honor interaction-time refresh suspension");
    list->selectItem(find(list, "folder-792"));
    list->onCmdRefresh(NULL, 0, NULL);
    pump(app, 420);
    assert(busyCursor && list->getNumItems() == 794);
    assert(list->isItemSelected(find(list, "folder-792")));
    pump(app, 1400);
    assert(!busyCursor && list->isItemSelected(find(list, "folder-792")));
    puts("PASS: remote refresh preserves existing rows and selection while new batches arrive");
    list->toggleExpand(find(list, "folder-0"), 1);
    assert(busyCursor);
    pump(app, 500);
    assert(find(list, "folder-0/subfolder") >= 0 && !busyCursor);
    assert(list->isItemDirectory(find(list, "folder-0/link-folder")));
    assert(!list->isItemDirectory(find(list, "folder-0/broken-link")));
    list->toggleExpand(find(list, "folder-1"), 1);
    assert(busyCursor);
    list->toggleExpand(find(list, "folder-1"), 0);
    assert(!RemoteDirectory::loading(remote + "/folder-1") && !busyCursor);
    list->onCmdRefresh(NULL, 0, NULL);
    assert(busyCursor);
    list->setDirectory(fixture);
    pump(app, 250);
    assert(!busyCursor && list->getDirectory() == fixture);
    puts("PASS: asynchronous Details expansion, symlinks, collapse cancellation, refresh, and navigation cancellation");

    SidebarItem* remoteItem = new SidebarItem("remote", minifoldericon, SidebarItem::DEVICE, remote);
    remoteItem->setHasItems(true);
    sidebar->appendItem(NULL, remoteItem);
    sidebar->expandTree(remoteItem, false);
    sidebar->onExpandItem(NULL, 0, remoteItem);
    assert(RemoteDirectory::loading(remote));
    pump(app, 1800);
    assert(remoteItem->getNumChildren() == 793 && !busyCursor);
    sidebar->collapseTree(remoteItem, false);
    sidebar->onCollapseItem(NULL, 0, remoteItem);
    assert(!RemoteDirectory::loading(sidebar));
    puts("PASS: Places remote expansion uses native metadata batches and balances busy cursor");

    DirList* remoteTree = new DirList(shell, frame, NULL, 0);
    remoteTree->create();
    app.removeTimeout(remoteTree, DirList::ID_REFRESH_TIMER);
    FXTreeItem* parent = remoteTree->getFirstItem();
    assert(parent);
    for (const char* component : {"run", "user", "1000", "gvfs", "smb-share:test", "large"})
    {
        FXTreeItem* next = parent->getFirst();
        while (next && next->getText() != component) next = next->getNext();
        if (!next) next = remoteTree->appendItem(parent, new TestDirectoryItem(component));
        parent->setExpanded(true);
        parent = next;
    }
    parent->setExpanded(true);
    remoteTree->onExpanded(NULL, 0, parent);
    assert(RemoteDirectory::loading(remoteTree) && busyCursor);
    pump(app, 1700);
    assert(parent->getNumChildren() == 793 && !busyCursor);
    remoteTree->setDirectory(remote + "/folder-792/subfolder", false);
    pump(app, 600);
    assert(remoteTree->getDirectory() == remote + "/folder-792/subfolder");
    assert(!busyCursor && remoteFilesystemCalls == 0);
    RemoteDirectory::release(remoteTree);
    puts("PASS: legacy Tree batches and asynchronous deep-path synchronization without per-entry filesystem calls");

    RemoteObserver observer;
    FXString eofPath = remote + "/eof-before-exit";
    RemoteDirectory::entries(&app, eofPath, &observer, 1);
    pump(app, 300);
    assert(RemoteDirectory::loading(eofPath));
    pump(app, 500);
    assert(!RemoteDirectory::loading(eofPath) && RemoteDirectory::error(eofPath).empty());
    RemoteDirectory::release(&observer);
    RemoteObserver second;
    RemoteDirectory::entries(&app, remote, &observer, 1);
    RemoteDirectory::entries(&app, remote, &second, 1);
    int firstNotifications = observer.notifications;
    RemoteDirectory::release(&observer);
    pump(app, 1700);
    assert(observer.notifications == firstNotifications);
    assert(second.notifications > 0 && !RemoteDirectory::loading(remote));
    RemoteDirectory::release(&second);
    puts("PASS: EOF/exit ordering and shared requests survive one view closing");
    for (const char* suffix : {"/failure", "/invalid"})
    {
        FXString path = remote + suffix;
        RemoteDirectory::entries(&app, path, &observer, 1);
        pump(app, 500);
        assert(!RemoteDirectory::loading(path) && !RemoteDirectory::error(path).empty());
        assert(observer.notifications > 0);
        RemoteDirectory::release(&observer);
    }
    RemoteDirectory::entries(&app, remote, &observer, 1);
    int notifications = observer.notifications;
    RemoteDirectory::release(&observer);
    pump(app, 250);
    assert(observer.notifications == notifications);
    puts("PASS: helper failure, malformed protocol, and released-observer callbacks");

    AuthenticationResponder authentication;
    for (const char* uri : {"smb://auth.test", "smb://cancel.test"})
    {
        authentication.cancel = strcmp(uri, "smb://cancel.test") == 0;
        app.addTimeout(&authentication, 1, 20);
        RemoteDirectory::entries(&app, uri, &observer, 1);
        for (int waited = 0; waited < 20 && RemoteDirectory::loading(uri); ++waited) pump(app, 100);
        assert(!RemoteDirectory::loading(uri));
        if (authentication.cancel)
            assert(RemoteDirectory::error(uri) == "Share listing cancelled.");
        else
        {
            assert(RemoteDirectory::error(uri).empty());
            assert(RemoteDirectory::entries(&app, uri, &observer, 1).size() == 2);
        }
        RemoteDirectory::release(&observer);
    }
    assert(authentication.prompts == 2);
    puts("PASS: share authentication occurs only on request, accepts named users with blank passwords, cancels cleanly");

    vector_FXString paths;
    paths.push_back(fixture);
    const FXString trashData = fixture + "/trash-data";
    setenv("SWORDFISH_HOST_XDG_DATA_HOME", trashData.text(), 1);
    mainWindow = NULL;
    XFileExplorer* window = new XFileExplorer(&app, paths, 3);
    mainWindow = window;
    window->create();
    window->show();
    {
        const FXString homeTrash = trashData + "/Trash";
        const FXString volume = fixture + "/trash-volume";
        const FXString sharedVolume = fixture + "/trash-shared-volume";
        const FXString unsafeVolume = fixture + "/trash-unsafe-volume";
        const FXString volumeTrash = volume + "/.Trash-" + FXStringVal((FXuint)getuid());
        const FXString sharedTrash = sharedVolume + "/.Trash/" + FXStringVal((FXuint)getuid());
        const FXString oldPortable = fixture + "/old-portable/data/Trash";
        for (const FXString& root : {homeTrash, volumeTrash, sharedTrash, oldPortable})
        {
            assert(xf_mkpath((root + "/files").text(), 0700) == 0);
            assert(xf_mkpath((root + "/info").text(), 0700) == 0);
        }
        assert(chmod((sharedVolume + "/.Trash").text(), 01777) == 0);
        assert(xf_mkpath(unsafeVolume.text(), 0700) == 0);
        assert(symlink(oldPortable.text(),
                       (unsafeVolume + "/.Trash-" + FXStringVal((FXuint)getuid())).text()) == 0);
        simulatedMountTable = fixture + "/trash-mount-table";
        auto writeTrashMounts = [&](bool mounted)
        {
            std::ofstream output(simulatedMountTable.text());
            output << "/dev/root / ext4 rw 0 0\n";
            if (mounted)
            {
                for (const FXString& mount : {volume, sharedVolume, unsafeVolume})
                    output << "/dev/fixture " << mount.text() << " ext4 rw 0 0\n";
                output << "/dev/fixture " << volume.text() << " ext4 rw 0 0\n";
            }
        };
        writeTrashMounts(true);
        const FXString name = "same name%.txt";
        const FXString homeOriginal = fixture + "/home restored%.txt";
        const FXString volumeOriginal = volume + "/original folder/restored%.txt";
        const FXString sharedOriginal = sharedVolume + "/restored-folder";
        assert(xf_mkpath((volume + "/original folder").text(), 0700) == 0);
        auto put = [](const FXString& path, const FXString& data)
        {
            std::ofstream output(path.text());
            assert(output);
            output << data.text();
        };
        auto metadata = [&](const FXString& root, const FXString& file, const FXString& original)
        {
            put(root + "/info/" + file + ".trashinfo",
                "[Trash Info]\nDeletionDate=2026-11-10T08:00:00\nPath=" + FXURL::encode(original) + "\n");
        };
        put(homeTrash + "/files/" + name, "home contents");
        put(volumeTrash + "/files/" + name, "volume contents");
        assert(xf_mkpath((sharedTrash + "/files/.hidden-folder").text(), 0700) == 0);
        put(sharedTrash + "/files/.hidden-folder/child.txt", "nested contents");
        put(oldPortable + "/files/keep.txt", "legacy portable contents");
        metadata(homeTrash, name, homeOriginal);
        metadata(volumeTrash, name, "original folder/restored%.txt");
        metadata(sharedTrash, ".hidden-folder", "restored-folder");
        const std::vector<TrashLocation> locations = xf_trashlocations();
        assert(locations.size() == 3);
        TrashLocation location;
        assert(xf_trashlocation(volumeTrash + "/files/" + name, location));
        FXString original, deleted, infofile;
        assert(xf_readtrashinfo(volumeTrash + "/files/" + name, location, original, deleted, infofile));
        assert(original == volumeOriginal && deleted == "2026-11-10T08:00:00");
        assert(xf_trashlocation(sharedTrash + "/files/.hidden-folder/child.txt", location));
        assert(xf_readtrashinfo(sharedTrash + "/files/.hidden-folder/child.txt",
                                location, original, deleted, infofile));
        assert(original == sharedOriginal + "/child.txt");
        metadata(sharedTrash, "bad", "../outside");
        assert(!xf_readtrashinfo(sharedTrash + "/files/bad", location, original, deleted, infofile));
        put(sharedTrash + "/info/bad.trashinfo", "[Trash Info]\nPath=bad%00path\n");
        assert(!xf_readtrashinfo(sharedTrash + "/files/bad", location, original, deleted, infofile));
        const FXString freshTrash = fixture + "/fresh-trash";
        const FXString freshPath = freshTrash + "/files/fresh.txt";
        assert(xf_create_trashinfo(homeOriginal, freshPath, freshTrash + "/files", freshTrash + "/info") == 0);
        TrashLocation freshLocation = {freshTrash, FXString::null};
        assert(xf_readtrashinfo(freshPath, freshLocation, original, deleted, infofile));
        assert(original == homeOriginal && !deleted.empty());
        FilePanel* panel = window->getCurrentPanel();
        panel->setDirectory(homeTrash + "/files");
        FileList* files = panel->getList();
        files->setAllowRefresh(false);
        assert(files->isInTrash());
        auto pathIndex = [&](const FXString& path)
        {
            for (int i = 0; i < files->getNumItems(); ++i)
                if (files->getItemFullPathname(i) == path) return i;
            return -1;
        };
        assert(files->getNumItems() == 4);
        int volumeIndex = pathIndex(volumeTrash + "/files/" + name);
        assert(volumeIndex >= 0);
        assert(files->getItemPathname(volumeIndex) == volumeTrash + "/files/" + name);
        assert(files->getItemText(volumeIndex).contains(volumeOriginal));
        assert(pathIndex(homeTrash + "/files/" + name) >= 0);
        assert(pathIndex(sharedTrash + "/files/.hidden-folder") >= 0);
        files->selectItem(volumeIndex);
        files->onCmdRefresh(NULL, 0, NULL);
        assert(files->isItemSelected(pathIndex(volumeTrash + "/files/" + name)));
        panel->onCmdCopyCut(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_COPY_CLIPBOARD), NULL);
        FXString clipboard;
        assert(shell->getDNDData(FROM_CLIPBOARD, app.registerDragType("text/uri-list"), clipboard));
        assert(FXURL::decode(FXURL::fileFromURL(clipboard)) == volumeTrash + "/files/" + name);
        panel->onCmdProperties(NULL, 0, NULL);
        PropertiesBox* properties = NULL;
        for (FXWindow* child = app.getRootWindow()->getFirst(); child; child = child->getNext())
        {
            properties = dynamic_cast<PropertiesBox*>(child);
            if (properties) break;
        }
        assert(properties && hasLabel(properties, volumeOriginal));
        delete properties;
        puts("PASS: clipboard and Properties use the selected volume item's physical path, not its same-named Home Trash counterpart");
        writeTrashMounts(false);
        files->onCmdRefresh(NULL, 0, NULL);
        assert(files->getNumItems() == 2);
        writeTrashMounts(true);
        files->onCmdRefresh(NULL, 0, NULL);
        assert(files->getNumItems() == 4);
        puts("PASS: combined Trash includes Home and both volume layouts, duplicate names, hidden folders, metadata and unmount refresh; symlink roots and legacy portable Trash excluded");
        app.reg().writeUnsignedEntry("OPTIONS", "confirm_trash", false);
        for (const FXString& path : {homeTrash + "/files/" + name, volumeTrash + "/files/" + name,
                                    sharedTrash + "/files/.hidden-folder"})
        {
            files->killSelection();
            int index = pathIndex(path);
            assert(index > 0);
            files->selectItem(index);
            panel->onCmdFileRestore(NULL, 0, NULL);
            assert(!FXStat::exists(path));
        }
        assert(FXStat::exists(homeOriginal) && FXStat::exists(volumeOriginal));
        assert(FXStat::exists(sharedOriginal + "/child.txt"));
        assert(!FXStat::exists(homeTrash + "/info/" + name + ".trashinfo"));
        assert(!FXStat::exists(volumeTrash + "/info/" + name + ".trashinfo"));
        puts("PASS: actual Restore restores duplicate files and a nested folder to their correct Home/volume locations, decoding percent escapes once");
        for (const FXString& root : {homeTrash, volumeTrash, sharedTrash})
        {
            put(root + "/files/empty-me.txt", "disposable");
            metadata(root, "empty-me.txt", fixture + "/unused.txt");
        }
        TrashConfirmation answer;
        answer.cancel = true;
        app.addTimeout(&answer, 1, 20);
        window->onCmdEmptyTrash(NULL, 0, NULL);
        assert(answer.answered == 1);
        for (const FXString& root : {homeTrash, volumeTrash, sharedTrash})
            assert(FXStat::exists(root + "/files/empty-me.txt") &&
                   FXStat::exists(root + "/info/empty-me.txt.trashinfo"));
        answer.cancel = false;
        answer.expectError = true;
        failedUnlink = volumeTrash + "/files/empty-me.txt";
        app.addTimeout(&answer, 1, 20);
        assert(!window->onCmdEmptyTrash(NULL, 0, NULL));
        assert(answer.answered == 2 && answer.errors == 1);
        assert(!FXStat::exists(homeTrash + "/files/empty-me.txt"));
        assert(FXStat::exists(failedUnlink));
        assert(FXStat::exists(volumeTrash + "/info/empty-me.txt.trashinfo"));
        assert(FXStat::exists(sharedTrash + "/files/empty-me.txt"));
        failedUnlink.clear();
        answer.expectError = false;
        app.addTimeout(&answer, 1, 20);
        assert(window->onCmdEmptyTrash(NULL, 0, NULL));
        assert(answer.answered == 3);
        for (const FXString& root : {homeTrash, volumeTrash, sharedTrash})
        {
            assert(FXStat::isDirectory(root + "/files") && FXStat::isDirectory(root + "/info"));
            assert(!FXStat::exists(root + "/files/empty-me.txt"));
            assert(!FXStat::exists(root + "/info/empty-me.txt.trashinfo"));
        }
        assert(FXStat::exists(oldPortable + "/files/keep.txt"));
        puts("PASS: actual Empty Trash cancellation preserves all items; partial deletion failure preserves remaining restore metadata; success empties Home and mounted drives without touching old portable Trash");
        files->setAllowRefresh(true);
        panel->setDirectory(fixture);
        simulatedMountTable.clear();
    }
    {
        FilePanel* panel = window->getCurrentPanel();
        FileList* files = panel->getList();
        assert(files->getNumItems() >= 3);
        FXMenuPane menu(panel);
        FXMenuCommand* single = new FXMenuCommand(&menu, "Rename", NULL, panel, FilePanel::ID_CONTEXT_RENAME);
        FXMenuCommand* bulk = new FXMenuCommand(&menu, "Bulk Rename...", NULL, panel, FilePanel::ID_CONTEXT_BULK_RENAME);
        for (int count = 0; count <= 2; ++count)
        {
            files->killSelection();
            for (int i = 0; i < count; ++i) files->selectItem(i + 1);
            panel->handle(single, FXSEL(SEL_UPDATE, FilePanel::ID_CONTEXT_RENAME), NULL);
            panel->handle(bulk, FXSEL(SEL_UPDATE, FilePanel::ID_CONTEXT_BULK_RENAME), NULL);
            assert(single->isEnabled() == (count == 1));
            assert(bulk->isEnabled() == (count > 1 || (count == 1 && files->isItemDirectory(1))));
            if (count != 1)
                panel->handle(single, FXSEL(SEL_COMMAND, FilePanel::ID_CONTEXT_RENAME), NULL);
        }
        for (int i = 1; i < files->getNumItems(); ++i)
        {
            files->killSelection();
            files->selectItem(i);
            panel->handle(bulk, FXSEL(SEL_UPDATE, FilePanel::ID_CONTEXT_BULK_RENAME), NULL);
            assert(bulk->isEnabled() == files->isItemDirectory(i));
        }
        files->killSelection();
        FilePanel* left = window->getLeftPanel();
        FXMenuCommand* tools = new FXMenuCommand(&menu, "Bulk Rename...", NULL, left, FilePanel::ID_CONTEXT_BULK_RENAME);
        window->getRightPanel()->setDirectory(fixture);
        window->getRightPanel()->setActive();
        for (int i = 1; i < window->getRightPanel()->getList()->getNumItems(); ++i)
            if (window->getRightPanel()->getList()->isItemDirectory(i))
            {
                window->getRightPanel()->getList()->selectItem(i);
                break;
            }
        left->handle(tools, FXSEL(SEL_UPDATE, FilePanel::ID_CONTEXT_BULK_RENAME), NULL);
        assert(tools->isEnabled());
        window->getRightPanel()->getList()->killSelection();
        left->handle(tools, FXSEL(SEL_UPDATE, FilePanel::ID_CONTEXT_BULK_RENAME), NULL);
        assert(!tools->isEnabled());
        panel->setActive();
        puts("PASS: Rename needs one item; Bulk Rename needs several items or one folder, and the Tools entry follows the active pane");
    }
    observedWindow = window->getCurrentPanel()->getList()->id();
    SidebarTree* navigation = findSidebar(window->getDirPanel());
    assert(navigation);
    const char* sectionNames[] = {"Places", "Bookmarks", "Devices", "Network"};
    FXTreeItem* section = navigation->getFirstItem();
    for (const char* name : sectionNames)
    {
        assert(section && section->getText() == name);
        section = section->getNext();
    }
    assert(!section);
    DirPanel* panel = window->getDirPanel();
    networkPanel = panel;
    panel->onSidebarRefresh(NULL, 0, NULL);
    {
        MountsList* mounts = findMounts(panel);
        assert(mounts);
        SidebarItem* root = static_cast<SidebarItem*>(navigation->getFirstItem()->getNext()->getNext());
        assert(root && root->key == "mounts");
        auto device = [root](const FXString& path) -> SidebarItem*
        {
            for (FXTreeItem* entry = root->getFirst(); entry; entry = entry->getNext())
                if (static_cast<SidebarItem*>(entry)->path == path) return static_cast<SidebarItem*>(entry);
            return NULL;
        };
        const FXString volumes = fixture + "/sidebar-color-volumes";
        simulatedMountTable = fixture + "/sidebar-color-mtab";
        auto writeVolumes = [&volumes](bool internal, bool usb)
        {
            std::ofstream output(volumes.text());
            assert(output);
            if (internal)
                output << "PATH=\"/dev/test-internal1\" LABEL=\"Internal\" FSTYPE=\"ntfs\" "
                          "MOUNTPOINT=\"\" HOTPLUG=\"0\" TYPE=\"part\"\n";
            if (usb)
                output << "PATH=\"/dev/test-usb1\" LABEL=\"USB\" FSTYPE=\"ext4\" "
                          "MOUNTPOINT=\"\" HOTPLUG=\"1\" TYPE=\"part\"\n";
        };
        auto writeMounts = [](bool mounted)
        {
            std::ofstream output(simulatedMountTable.text());
            assert(output);
            output << "/dev/root / ext4 rw 0 0\n";
            if (mounted)
            {
                output << "/dev/test-internal1 /run/media/test/Internal ntfs rw 0 0\n";
                output << "/dev/test-usb1 /run/media/test/USB ext4 rw 0 0\n";
            }
        };
        auto refresh = [&]()
        {
            mounts->invalidate();
            mounts->onMountsRefresh(NULL, 0, NULL);
            panel->onSidebarRefresh(NULL, 0, NULL);
        };
        setenv("SWORDFISH_TEST_VOLUMES", volumes.text(), 1);
        writeVolumes(true, true); writeMounts(false); refresh();
        SidebarItem* internal = device("/dev/test-internal1");
        SidebarItem* usb = device("/dev/test-usb1");
        assert(internal && usb && internal->isEnabled() && usb->isEnabled());
        assert(internal->deviceAction == SidebarItem::MOUNT_DEVICE && !internal->canEject);
        assert(usb->deviceAction == SidebarItem::MOUNT_DEVICE && usb->canEject);
        assert(internal->getText() == "Internal (unmounted)" && usb->getText() == "USB (unmounted)");
        checkSidebarDeviceText(*internal, navigation, true);
        checkSidebarDeviceText(*usb, navigation, true);

        writeVolumes(false, false); writeMounts(true); refresh();
        internal = device("/run/media/test/Internal"); usb = device("/run/media/test/USB");
        assert(internal && usb && internal->deviceAction == SidebarItem::UNMOUNT_DEVICE &&
               usb->deviceAction == SidebarItem::UNMOUNT_DEVICE);
        assert(internal->getText() == "Internal" && usb->getText() == "USB");
        checkSidebarDeviceText(*internal, navigation, false);
        checkSidebarDeviceText(*usb, navigation, false);

        writeVolumes(true, true); writeMounts(false); refresh();
        internal = device("/dev/test-internal1"); usb = device("/dev/test-usb1");
        assert(internal && usb && internal->deviceAction == SidebarItem::MOUNT_DEVICE &&
               usb->deviceAction == SidebarItem::MOUNT_DEVICE);
        checkSidebarDeviceText(*internal, navigation, true);
        checkSidebarDeviceText(*usb, navigation, true);
        assert(access(getenv("SWORDFISH_TEST_MOUNT_LOG"), F_OK) != 0);
        setenv("SWORDFISH_TEST_EJECT_VOLUMES", volumes.text(), 1);
        SidebarDeviceRequest ejectUsb = {usb, SidebarItem::EJECT_DEVICE};
        app.addSignal(SIGCHLD, window, XFileExplorer::ID_HARVEST);
        panel->onSidebarDeviceAction(NULL, 0, &ejectUsb);
        app.removeSignal(SIGCHLD);
        unsetenv("SWORDFISH_TEST_EJECT_VOLUMES");
        std::ifstream ejectLog(getenv("SWORDFISH_TEST_MOUNT_LOG"));
        std::string ejectCommand;
        assert(std::getline(ejectLog, ejectCommand) && ejectCommand == "disk power-off");
        assert(!std::getline(ejectLog, ejectCommand));
        ejectLog.close();
        assert(unlink(getenv("SWORDFISH_TEST_MOUNT_LOG")) == 0);
        refresh();
        assert(device("/dev/test-internal1") && !device("/dev/test-usb1"));
        unsetenv("SWORDFISH_TEST_VOLUMES");
        assert(unlink(volumes.text()) == 0 && unlink(simulatedMountTable.text()) == 0);
        simulatedMountTable.clear();
        refresh();
        puts("PASS: internal and USB rows change color on mount/unmount; USB disappears after eject");
    }
    SidebarItem* network = static_cast<SidebarItem*>(navigation->getLastItem());
    // A restored expanded Network root must not search on its own.
    assert(network->isExpanded() && network->getNumChildren() == 2);
    assert(network->getFirst()->getNext()->getText().find("Click Refresh") == 0);
    pump(app, 300);
    assert(network->getNumChildren() == 2);
    panel->onSidebarActivate(NULL, FXSEL(SEL_COMMAND, DirPanel::ID_SIDEBAR), network);
    assert(network->getFirst()->getText() == "Stop");
    for (int waited = 0; waited < 40 && network->getFirst()->getText() != "Refresh"; ++waited)
    {
        pump(app, 100);
        panel->onSidebarRefresh(NULL, 0, NULL);
    }
    const char* serverNames[] = {"DELL-740", "MX-HP", "MX-RYZEN (this computer)"};
    FXTreeItem* server = network->getFirst()->getNext();
    assert(server->getText() == "3 computer(s) sharing files found. Click a computer to view its shares.");
    server = server->getNext();
    for (const char* name : serverNames)
    {
        assert(server && server->getText() == name);
        assert(static_cast<SidebarItem*>(server)->kind == SidebarItem::SERVER && !server->hasItems());
        server = server->getNext();
    }
    assert(!server);
    SidebarItem* dell = static_cast<SidebarItem*>(network->getFirst()->getNext()->getNext());
    assert(dell->path == "192.168.1.104" &&
           dell->tip == "Address: 192.168.1.104\nWorkgroup: MAKHLOUF\nFound by: WS-Discovery");
    SidebarItem* hp = static_cast<SidebarItem*>(dell->getNext());
    assert(hp->path == "mx-hp.local" &&
           hp->tip == "Address: mx-hp.local (192.168.1.102)\nFound by: GVFS, WS-Discovery");
    // A single click browses the server in the active tab without mounting anything.
    TabButtons* tabs = window->getCurrentPanel()->getTabButtons();
    const FXuint previousTab = tabs->getCurrentTab();
    const FXuint previousCount = tabs->getNumActiveTabs();
    panel->onSidebarActivate(NULL, FXSEL(SEL_COMMAND, DirPanel::ID_SIDEBAR), dell);
    FilePanel* sharePanel = window->getCurrentPanel();
    assert(sharePanel->getDirectory() == "smb://192.168.1.104");
    assert(tabs->getNumActiveTabs() == previousCount && tabs->getCurrentTab() == previousTab);
    panel->onSidebarActivate(NULL, FXSEL(SEL_COMMAND, DirPanel::ID_SIDEBAR), dell);
    assert(tabs->getNumActiveTabs() == previousCount && tabs->getCurrentTab() == previousTab);
    pump(app, 650);
    assert(sharePanel->getList()->getNumItems() == 2);
    assert(sharePanel->getList()->getItemFilename(0) == "5TB");
    assert(sharePanel->getList()->getItemFilename(1) == "Team files");
    assert(sharePanel->getList()->getItemPathname(1) == "smb://192.168.1.104/Team%20files");
    assert(access(getenv("SWORDFISH_TEST_MOUNT_LOG"), F_OK) != 0);
    assert(sharePanel->navigatePane(fixture));
    assert(sharePanel->getDirectory() != "smb://192.168.1.104");
    panel->onSidebarActivate(NULL, FXSEL(SEL_COMMAND, DirPanel::ID_SIDEBAR), dell);
    assert(tabs->getNumActiveTabs() == previousCount && tabs->getCurrentTab() == previousTab);
    pump(app, 450);
    assert(sharePanel->getDirectory() == "smb://192.168.1.104" && sharePanel->getList()->getNumItems() == 2);
    FXButton edit(shell, "Edit");
    sharePanel->onUpdOpen(&edit, FXSEL(SEL_UPDATE, FilePanel::ID_EDIT), NULL);
    assert(!edit.isEnabled());
    sharePanel->getList()->selectItem(1);
    sharePanel->onUpdOpen(&edit, FXSEL(SEL_UPDATE, FilePanel::ID_OPEN), NULL);
    assert(edit.isEnabled());
    sharePanel->onCmdItemClicked(NULL, FXSEL(SEL_CLICKED, FilePanel::ID_FILELIST), (void*)1);
    assert(access(getenv("SWORDFISH_TEST_MOUNT_LOG"), F_OK) != 0);
    sharePanel->onCmdItemDoubleClicked(NULL, FXSEL(SEL_DOUBLECLICKED, FilePanel::ID_FILELIST), (void*)1);
    assert(sharePanel->getDirectory() == fixture + "/large");
    std::ifstream mountedLog(getenv("SWORDFISH_TEST_MOUNT_LOG"));
    std::string mountedUri;
    assert(std::getline(mountedLog, mountedUri) && mountedUri == "smb://192.168.1.104/Team%20files/");
    assert(!std::getline(mountedLog, mountedUri));
    sharePanel->onCmdPaneNavigation(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_PANE_BACK), NULL);
    assert(sharePanel->getDirectory() == "smb://192.168.1.104");
    pump(app, 450);
    sharePanel->onCmdPaneNavigation(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_PANE_FORWARD), NULL);
    assert(sharePanel->getDirectory() == fixture + "/large");
    puts("PASS: server single-click lists shares in the active tab without mounting; share double-click mounts only the chosen share");
    const FXString runtime = fixture + "/runtime";
    const FXString gvfs = runtime + "/gvfs";
    const FXString guestMount = gvfs + "/smb-share:user=guest,share=Team%20files,server=reuse.test";
    const FXString userMount = gvfs + "/smb-share:share=Team%20files,user=alice,server=reuse.test,domain=HOME";
    assert(mkdir(runtime.text(), 0700) == 0 && mkdir(gvfs.text(), 0700) == 0);
    const char* runtimeEnvironment = getenv("XDG_RUNTIME_DIR");
    const bool hadRuntime = runtimeEnvironment != NULL;
    const FXString savedRuntime = runtimeEnvironment ? runtimeEnvironment : "";
    setenv("XDG_RUNTIME_DIR", runtime.text(), 1);
    setenv("SWORDFISH_TEST_GVFS_ROOT", gvfs.text(), 1);
    assert(unlink(getenv("SWORDFISH_TEST_MOUNT_LOG")) == 0);
    FXString reusedPath;
    assert(sharePanel->resolveNetworkPath("smb://reuse.test/Team%20files", reusedPath) == 1);
    assert(reusedPath == guestMount);
    std::ifstream guestLog(getenv("SWORDFISH_TEST_MOUNT_LOG"));
    assert(std::getline(guestLog, mountedUri) && mountedUri == "smb://reuse.test/Team%20files/");
    assert(!std::getline(guestLog, mountedUri));
    // A pre-existing authenticated mount is reused without calling it a guest login.
    assert(rmdir(guestMount.text()) == 0 && mkdir(userMount.text(), 0700) == 0);
    assert(unlink(getenv("SWORDFISH_TEST_MOUNT_LOG")) == 0);
    MountMessageResponder reuseMessage;
    testApp->addTimeout(&reuseMessage, 1, 20);
    assert(sharePanel->resolveNetworkPath("smb://REUSE.TEST/Team%20files/nested", reusedPath) == 1);
    assert(reusedPath == userMount + "/nested" && reuseMessage.alreadyMounted == 1);
    assert(access(getenv("SWORDFISH_TEST_MOUNT_LOG"), F_OK) != 0);
    // An explicit different user must not silently inherit Alice's connection.
    const FXString bobMount = gvfs + "/smb-share:user=bob,share=Team%20files,server=reuse.test,domain=WORKGROUP";
    assert(sharePanel->resolveNetworkPath("smb://bob:password@reuse.test/Team%20files", reusedPath) == 1);
    assert(reusedPath == bobMount);
    std::ifstream bobLog(getenv("SWORDFISH_TEST_MOUNT_LOG"));
    assert(std::getline(bobLog, mountedUri) && mountedUri == "smb://WORKGROUP;bob@reuse.test/Team%20files/");
    assert(!std::getline(bobLog, mountedUri));
    assert(rmdir(bobMount.text()) == 0 && rmdir(userMount.text()) == 0 &&
           rmdir(gvfs.text()) == 0 && rmdir(runtime.text()) == 0);
    unsetenv("SWORDFISH_TEST_GVFS_ROOT");
    if (hadRuntime) setenv("XDG_RUNTIME_DIR", savedRuntime.text(), 1);
    else unsetenv("XDG_RUNTIME_DIR");
    puts("PASS: mounted SMB shares survive login-URI differences; cached user mounts are reused without a second mount");
    // A share whose gio mount never returns must not freeze the window forever.
    ConnectCanceller canceller;
    testApp->addTimeout(&canceller, 1, 50);
    FXString hungPath;
    const time_t hungStart = time(NULL);
    assert(sharePanel->resolveNetworkPath("smb://hang.test/share", hungPath) == -1);
    assert(time(NULL) - hungStart < 30);
    assert(canceller.prompts == 1 && canceller.message.find("did not respond in time") >= 0);
    puts("PASS: a hanging gio mount times out and returns to the credentials dialog");
    panel->onCmdToggleSection(NULL, FXSEL(SEL_COMMAND, DirPanel::ID_TOGGLE_NETWORK), NULL);
    assert(!network->isExpanded());
    panel->onCmdToggleSection(NULL, FXSEL(SEL_COMMAND, DirPanel::ID_TOGGLE_NETWORK), NULL);
    assert(network->isExpanded() && network->getFirst()->getText() == "Stop");
    panel->onCmdRefreshNetwork(NULL, 0, NULL);
    panel->onSidebarRefresh(NULL, 0, NULL);
    assert(network->getFirst()->getText() == "Refresh");
    assert(network->getFirst()->getNext()->getText().find("Search stopped") == 0);
    pump(app, 200);
    puts("PASS: Network section searches only on demand, merges updates, sorts this computer last, ignores bad records, stops");
    {
        // Advertised devices: media servers, FTP/SFTP, printers and routers
        setenv("SWORDFISH_TEST_SERVICES", "1", 1);
        unlink(getenv("SWORDFISH_TEST_MOUNT_LOG"));
        panel->onCmdRefreshNetwork(NULL, 0, NULL);
        panel->onSidebarRefresh(NULL, 0, NULL);
        assert(network->getFirst()->getText() == "Stop");
        for (int waited = 0; waited < 40 && network->getFirst()->getText() != "Refresh"; ++waited)
        {
            pump(app, 100);
            panel->onSidebarRefresh(NULL, 0, NULL);
        }
        unsetenv("SWORDFISH_TEST_SERVICES");
        FXTreeItem* row = network->getFirst()->getNext();
        assert(row->getText() == "3 computer(s) sharing files and 5 other device(s) found. Click one to open it.");
        for (int i = 0; i < 4; ++i) row = row->getNext();
        const char* deviceNames[] = {"Living Room (media server)", "NAS (FTP)", "Office Laser (printer)",
                                     "Home Router (router / access point)", "NAS (SFTP)"};
        SidebarItem* devices[5];
        for (int i = 0; i < 5; ++i)
        {
            assert(row && row->getText() == deviceNames[i]);
            devices[i] = static_cast<SidebarItem*>(row);
            assert(devices[i]->kind == SidebarItem::SERVICE && !row->hasItems());
            row = row->getNext();
        }
        assert(!row);
        assert(devices[2]->path == "http://192.168.1.50/" && devices[2]->key == "printer" &&
               devices[2]->tip == "Address: 192.168.1.50\nOpens the settings page: http://192.168.1.50/\n"
                                  "Found by: Zeroconf, UPnP");
        assert(devices[0]->tip == "Address: 192.168.1.70\nBrowse music, videos and pictures (read-only)\n"
                                  "Found by: UPnP");
        assert(app.reg().readStringEntry("NETWORK MEDIA", "192.168.1.70:8200", "") ==
               FXString("http://192.168.1.70:8200/rootDesc.xml\tLiving Room"));
        puts("PASS: advertised devices are listed with their kind; bad or duplicate SERVICE records are ignored");

        // Printers and routers open their web page in the desktop browser
        app.addSignal(SIGCHLD, window, XFileExplorer::ID_HARVEST);
        panel->onSidebarActivate(NULL, FXSEL(SEL_COMMAND, DirPanel::ID_SIDEBAR), devices[2]);
        for (int waited = 0; waited < 40 && access(getenv("SWORDFISH_TEST_MOUNT_LOG"), F_OK) != 0; ++waited)
            pump(app, 50);
        pump(app, 100);
        std::ifstream printerLog(getenv("SWORDFISH_TEST_MOUNT_LOG"));
        std::string line;
        assert(std::getline(printerLog, line) && line == "website http://192.168.1.50/");
        printerLog.close();
        assert(unlink(getenv("SWORDFISH_TEST_MOUNT_LOG")) == 0);

        // A media server is browsed read-only, like a folder
        FilePanel* mediaPanel = window->getCurrentPanel();
        panel->onSidebarActivate(NULL, FXSEL(SEL_COMMAND, DirPanel::ID_SIDEBAR), devices[0]);
        assert(mediaPanel->getDirectory() == "dlna://192.168.1.70:8200");
        pump(app, 450);
        assert(mediaPanel->getTabButtons()->getLabel(mediaPanel->getTabButtons()->getCurrentTab()) == "Living Room");
        assert(mediaPanel->getList()->getNumItems() == 2);
        assert(mediaPanel->getList()->getItemFilename(0) == "Music");
        mediaPanel->onCmdItemDoubleClicked(NULL, FXSEL(SEL_DOUBLECLICKED, FilePanel::ID_FILELIST), (void*)0);
        assert(mediaPanel->getDirectory() == "dlna://192.168.1.70:8200/Music");
        pump(app, 450);
        FileList* media = mediaPanel->getList();
        assert(media->getNumItems() == 3);
        FXint song = -1, live = -1;
        for (FXint i = 0; i < media->getNumItems(); ++i)
        {
            if (media->getItemFilename(i) == "Song One.mp3") song = i;
            if (media->getItemFilename(i) == "Live 50%.flac") live = i;
        }
        assert(song >= 0 && live >= 0 && !media->isItemDirectory(song));
        assert(media->getItemPathname(live) == "dlna://192.168.1.70:8200/Music/Live%2050%25.flac");
        assert(RemoteDirectory::mediaLink(media->getItemPathname(live)) ==
               "http://192.168.1.70:8200/MediaItems/2.flac");
        assert(RemoteDirectory::displayPath(media->getItemPathname(live)) == "dlna://Living Room/Music/Live 50%.flac");
        FXButton up(shell, "Up");
        mediaPanel->handle(&up, FXSEL(SEL_UPDATE, FilePanel::ID_DIRECTORY_UP), NULL);
        assert(up.isEnabled());
        // Read-only: no editing commands
        media->killSelection();
        media->selectItem(song);
        MountMessageResponder readOnly;
        testApp->addTimeout(&readOnly, 1, 20);
        mediaPanel->handle(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_CONTEXT_RENAME), NULL);
        assert(readOnly.message.find("Media servers are read-only") == 0);
        assert(media->getItemFilename(song) == "Song One.mp3");
        // Double-clicking a file streams it (no player configured: the browser)
        mediaPanel->onCmdItemDoubleClicked(NULL, FXSEL(SEL_DOUBLECLICKED, FilePanel::ID_FILELIST), (void*)(FXival)song);
        for (int waited = 0; waited < 40 && access(getenv("SWORDFISH_TEST_MOUNT_LOG"), F_OK) != 0; ++waited)
            pump(app, 50);
        pump(app, 100);
        std::ifstream streamLog(getenv("SWORDFISH_TEST_MOUNT_LOG"));
        assert(std::getline(streamLog, line) && line == "website http://192.168.1.70:8200/MediaItems/1.mp3");
        streamLog.close();
        assert(unlink(getenv("SWORDFISH_TEST_MOUNT_LOG")) == 0);
        assert(mediaPanel->getDirectory() == "dlna://192.168.1.70:8200/Music");

        // Context menu: play choices and the stream address for the selected file
        extern FXString clipboard;
        const FXString songUrl = "http://192.168.1.70:8200/MediaItems/1.mp3";
        media->killSelection();
        media->selectItem(song);
        FXString menuLabels = popupMedia(mediaPanel, "Copy Stream Address");
        assert(menuLabels == "Play;Play With[Web Browser;Other Application...;]Copy Stream Address;Download...;Refresh;");
        assert(clipboard == songUrl);
        // The desktop's default application for the type comes before the browser
        const FXString scratch = FXPath::directory(getenv("SWORDFISH_TEST_MOUNT_LOG"));
        const char* dataHome = getenv("SWORDFISH_HOST_XDG_DATA_HOME");
        const FXString applications = (dataHome ? FXString(dataHome) : FXString(getenv("HOME")) + "/.local/share") +
                                      "/applications";
        assert(system(("mkdir -p '" + applications + "' '" + FXString(getenv("HOME")) + "/.config'").text()) == 0);
        {
            std::ofstream desktop((applications + "/fake-player.desktop").text());
            desktop << "[Desktop Entry]\nType=Application\nName=Fake Player\nExec=" << scratch.text()
                    << "/fake-player %U\nMimeType=audio/mpeg;audio/flac;\nCategories=AudioVideo;Audio;Player;\n";
            std::ofstream other((applications + "/other-player.desktop").text());
            other << "[Desktop Entry]\nType=Application\nName=Another Player\nExec=" << scratch.text()
                  << "/fake-player --other %U\nMimeType=audio/mpeg;\nCategories=AudioVideo;Video;TV;\n";
            // Declare the type but are not stream players: an editor, and a player for local files only
            std::ofstream editor((applications + "/fake-editor.desktop").text());
            editor << "[Desktop Entry]\nType=Application\nName=Sound Editor\nExec=" << scratch.text()
                   << "/fake-player --edit %U\nMimeType=audio/mpeg;\nCategories=AudioVideo;AudioVideoEditing;\n";
            std::ofstream local((applications + "/local-player.desktop").text());
            local << "[Desktop Entry]\nType=Application\nName=Local Player\nExec=" << scratch.text()
                  << "/fake-player --local %F\nMimeType=audio/mpeg;\nCategories=AudioVideo;Player;\n";
            std::ofstream defaults((FXString(getenv("HOME")) + "/.config/mimeapps.list").text());
            defaults << "[Default Applications]\naudio/mpeg=fake-player.desktop\n";
        }
        auto launched = [&](const char* expected)
        {
            for (int waited = 0; waited < 40 && access(getenv("SWORDFISH_TEST_MOUNT_LOG"), F_OK) != 0; ++waited)
                pump(app, 50);
            pump(app, 100);
            std::ifstream log(getenv("SWORDFISH_TEST_MOUNT_LOG"));
            std::string entry;
            const bool matched = std::getline(log, entry) && entry == expected;
            if (!matched) fprintf(stderr, "launched: got '%s', expected '%s'\n", entry.c_str(), expected);
            log.close();
            unlink(getenv("SWORDFISH_TEST_MOUNT_LOG"));
            return matched;
        };
        // The fake player never maps a window, so it could not end a startup notification
        const FXuint notification = testApp->reg().readUnsignedEntry("OPTIONS", "use_startup_notification", true);
        testApp->reg().writeUnsignedEntry("OPTIONS", "use_startup_notification", false);
        mediaPanel->onCmdItemDoubleClicked(NULL, FXSEL(SEL_DOUBLECLICKED, FilePanel::ID_FILELIST), (void*)(FXival)song);
        assert(launched("player http://192.168.1.70:8200/MediaItems/1.mp3"));
        menuLabels = popupMedia(mediaPanel, "Another Player");
        assert(menuLabels == "Play;Play With[Fake Player;Another Player;Web Browser;Other Application...;]"
                             "Copy Stream Address;Download...;Refresh;");
        assert(launched("player --other http://192.168.1.70:8200/MediaItems/1.mp3"));
        popupMedia(mediaPanel, "Web Browser");
        assert(launched("website http://192.168.1.70:8200/MediaItems/1.mp3"));
        popupMedia(mediaPanel, "Play");
        assert(launched("player http://192.168.1.70:8200/MediaItems/1.mp3"));
        // Other Application... is the regular Open With dialog; Remember makes it the double-click choice
        OpenWithResponder chooser;
        chooser.command = scratch + "/fake-player --chosen";
        testApp->addTimeout(&chooser, 1, 20);
        popupMedia(mediaPanel, "Other Application...");
        testApp->removeTimeout(&chooser, 1);
        assert(chooser.showedType && launched("player --chosen http://192.168.1.70:8200/MediaItems/1.mp3"));
        chooser.remember = true;
        chooser.command = scratch + "/fake-player --remembered";
        testApp->addTimeout(&chooser, 1, 20);
        popupMedia(mediaPanel, "Other Application...");
        testApp->removeTimeout(&chooser, 1);
        assert(launched("player --remembered http://192.168.1.70:8200/MediaItems/1.mp3"));
        mediaPanel->onCmdItemDoubleClicked(NULL, FXSEL(SEL_DOUBLECLICKED, FilePanel::ID_FILELIST), (void*)(FXival)song);
        assert(launched("player --remembered http://192.168.1.70:8200/MediaItems/1.mp3"));
        FileDict::setUserCommand(testApp->reg(), "mp3", "");
        testApp->reg().writeUnsignedEntry("OPTIONS", "use_startup_notification", notification);
        // Folders keep the plain menu
        media->killSelection();
        assert(popupMedia(mediaPanel, "") == "Open;Download...;Refresh;");
        mediaPanel->onCmdDirectoryUp(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_DIRECTORY_UP), NULL);
        assert(mediaPanel->getDirectory() == "dlna://192.168.1.70:8200");
        puts("PASS: media servers browse read-only; files play with the Swordfish player, the desktop default or the browser; Play With and Copy Stream Address work");

        // Downloads run in the background, never overwrite, and report failures once
        const FXString saved = fixture + "/empty/Song One.mp3";
        RemoteDirectory::download(mediaPanel, "http://192.168.1.70:8200/MediaItems/1.mp3", saved);
        assert(RemoteDirectory::downloadsPending() == 1);
        for (int waited = 0; waited < 60 && RemoteDirectory::downloadsPending(); ++waited) pump(app, 50);
        assert(!RemoteDirectory::downloadsPending());
        std::ifstream savedFile(saved.text());
        std::string savedText;
        assert(std::getline(savedFile, savedText) && savedText == "media:http://192.168.1.70:8200/MediaItems/1.mp3");
        MountMessageResponder failure;
        testApp->addTimeout(&failure, 1, 20);
        RemoteDirectory::download(mediaPanel, "http://192.168.1.70:8200/MediaItems/missing.mp3", fixture + "/empty/missing.mp3");
        for (int waited = 0; waited < 60 && (RemoteDirectory::downloadsPending() || failure.title.empty()); ++waited)
            pump(app, 50);
        testApp->removeTimeout(&failure, 1);
        app.removeSignal(SIGCHLD);
        assert(failure.title == "Download failed" &&
               failure.message == "missing.mp3: The media server answered 404 Not Found\n");
        assert(access((fixture + "/empty/missing.mp3").text(), F_OK) != 0);
        assert(unlink(saved.text()) == 0);
        // Slow downloads show progress; Cancel stops the queue and removes the unfinished file
        const FXString slow = fixture + "/empty/slow.mp3";
        RemoteDirectory::download(mediaPanel, "http://192.168.1.70:8200/MediaItems/slow.mp3", slow);
        RemoteDirectory::download(mediaPanel, songUrl, fixture + "/empty/queued.mp3");
        assert(RemoteDirectory::downloadsPending() == 2);
        DialogBox* progress = NULL;
        for (int waited = 0; waited < 60 && !(progress && progress->shown() &&
                                              findText(progress, "512 B of 2")); ++waited)
        {
            pump(app, 50);
            progress = downloadWindow();
        }
        assert(progress && progress->shown());
        assert(findText(progress, "Downloading 1 of 2: slow.mp3"));
        assert(access((slow + ".part").text(), F_OK) == 0);
        FXButton* cancel = dynamic_cast<FXButton*>(findText(progress, "Cancel Downloads"));
        assert(cancel);
        cancel->getTarget()->handle(cancel, FXSEL(SEL_COMMAND, cancel->getSelector()), NULL);
        assert(!RemoteDirectory::downloadsPending() && !progress->shown());
        pump(app, 300);
        assert(access((slow + ".part").text(), F_OK) != 0 && access(slow.text(), F_OK) != 0);
        assert(access((fixture + "/empty/queued.mp3").text(), F_OK) != 0);
        // Hide keeps the download running without the window coming back
        RemoteDirectory::download(mediaPanel, "http://192.168.1.70:8200/MediaItems/slow.mp3", slow);
        for (int waited = 0; waited < 60 && !progress->shown(); ++waited) pump(app, 50);
        assert(progress->shown() && findText(progress, "Downloading 1 of 1: slow.mp3"));
        FXButton* hide = dynamic_cast<FXButton*>(findText(progress, "Hide"));
        assert(hide);
        hide->getTarget()->handle(hide, FXSEL(SEL_COMMAND, hide->getSelector()), NULL);
        pump(app, 800);
        assert(!progress->shown() && RemoteDirectory::downloadsPending() == 1);
        RemoteDirectory::cancelDownloads();
        pump(app, 300);
        assert(!RemoteDirectory::downloadsPending() && access((slow + ".part").text(), F_OK) != 0);
        unlink(getenv("SWORDFISH_TEST_MOUNT_LOG"));
        assert(mediaPanel->navigatePane(fixture));
        pump(app, 300);
        assert(!RemoteDirectory::loading(mediaPanel->getList()));
        puts("PASS: media downloads save local copies in the background, show progress, cancel cleanly and report failures once");
    }
    assert(sharePanel->navigatePane(fixture + "/empty"));
    SidebarItem destination("mounted share", minifoldericon, SidebarItem::DEVICE, fixture + "/large");
    navigation->grab();
    app.flush(true);
    pointerChecks = 0;
    checkPointer = true;
    measuredDirectory = fixture + "/large";
    stats = lstats = accesses = nestedReads = 0;
    busyFlushed = false;
    measuring = true;
    start = std::chrono::steady_clock::now();
    window->getDirPanel()->onSidebarActivate(NULL, FXSEL(SEL_COMMAND, DirPanel::ID_SIDEBAR), &destination);
    elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    measuring = false;
    checkPointer = false;
    assert(!navigation->grabbed() && pointerChecks > 0);
    assert(window->getCurrentPanel()->getDirectory() == fixture + "/large");
    assert(window->getCurrentPanel()->getList()->getNumItems() == 794 && nestedReads == 0);
    assert(!busyCursor);
    puts("PASS: clicking a Devices entry releases desktop pointer before file-pane navigation");
    printf("793-folder Devices navigation, simulated 1ms metadata latency: %ldms, stat=%d lstat=%d access=%d child-reads=%d\n",
           elapsed, stats, lstats, accesses, nestedReads);
    std::ifstream log((homedir + "/.local/state/swordfish/logs/file-operations.log").text());
    assert(log.good());
    std::string recorded((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    assert(recorded.find("event=slow-directory-listing") != std::string::npos);
    assert(recorded.find("entry_metadata_calls=794") != std::string::npos);
    assert(recorded.find("folder_access_calls=794") != std::string::npos);
    puts("PASS: all triangle paths show/reset busy cursor; slow-listing phase timings recorded");

    app.addSignal(SIGCHLD, window, XFileExplorer::ID_HARVEST);
    RemoteDirectory::entries(&app, remote, &observer, 1);
    File* command = new File(window, "Archive-style command regression", MOUNT);
    command->create();
    FXString output;
    int commandStatus = -1;
    assert(command->mount(fixture, "Testing command completion", "sleep 1; printf mounted",
                          &output, &commandStatus) == 1);
    assert(commandStatus == 0 && output == "mounted");
    delete command;

    FXString gvfsBase = "/run/user/" + FXStringVal(getuid()) + "/gvfs/";
    simulatedMount = gvfsBase + "smb-share:server=unmount.test,share=Team%20files";
    assert(xf_gvfsmount(simulatedMount + "/nested") == simulatedMount);
    assert(xf_gvfsmount(fixture).empty());
    assert(xf_gvfsmounturi(simulatedMount) == "smb://unmount.test/Team%20files/");
    assert(xf_gvfsmounturi(gvfsBase + "smb-share:domain=MAKHLOUF,user=Hassanein,server=localhost,share=data") ==
           "smb://MAKHLOUF;Hassanein@localhost/data/");
    assert(xf_gvfsmounturi(gvfsBase + "sftp:host=example.test,user=test,port=2222") ==
           "sftp://test@example.test:2222/");
    assert(xf_gvfsmounturi(gvfsBase + "ftp:host=ftp.example.test") ==
           "ftp://ftp.example.test/");
    assert(xf_gvfsmounturi(gvfsBase + "ftp:host=ftp.example.test,user=test%40example,port=2121/nested") ==
           "ftp://test%40example@ftp.example.test:2121/");
    assert(xf_gvfsmounturi(gvfsBase + "ftp:user=test").empty());
    assert(xf_gvfsmounturi(gvfsBase + "mtp:host=%5Busb%3A001%2C002%5D") ==
           "mtp://%5Busb%3A001%2C002%5D/");
    assert(xf_gvfsmounturi(gvfsBase + "smb-share:share=data").empty());
    const FXString beforeDeviceControls = sharePanel->getDirectory();
    sharePanel->setDirectory(simulatedMount);
    sharePanel->getList()->setFocus();
    FXButton mountControl(shell, "Mount");
    FXButton unmountControl(shell, "Unmount");
    sharePanel->onUpdUnmount(&unmountControl, FXSEL(SEL_UPDATE, FilePanel::ID_UMOUNT), NULL);
    assert(!unmountControl.isEnabled() && sharePanel->mountTarget().empty());
    sharePanel->setDirectory(ROOTDIR);
    sharePanel->onUpdUnmount(&unmountControl, FXSEL(SEL_UPDATE, FilePanel::ID_UMOUNT), NULL);
    assert(!unmountControl.isEnabled());
    sharePanel->setDirectory(FXSystem::getHomeDirectory());
    sharePanel->getList()->killSelection();
    sharePanel->onUpdUnmount(&unmountControl, FXSEL(SEL_UPDATE, FilePanel::ID_UMOUNT), NULL);
    sharePanel->onUpdMount(&mountControl, FXSEL(SEL_UPDATE, FilePanel::ID_MOUNT), NULL);
    assert(!unmountControl.isEnabled() && !mountControl.isEnabled());
    assert(sharePanel->mountTarget().empty());
    assert(xf_protectedmount(ROOTDIR) && xf_protectedmount(FXSystem::getHomeDirectory()));
    assert(xf_protectedmount(FXPath::directory(FXSystem::getHomeDirectory())));
    assert(!xf_protectedmount("/media/test/USB"));
    MountMessageResponder noSelectionMessage;
    app.addTimeout(&noSelectionMessage, 1, 20);
    assert(sharePanel->onCmdMount(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_UMOUNT), NULL) == 1);
    assert(noSelectionMessage.message.contains("Select a device"));
    SidebarItem* unmountedUsb = new SidebarItem("USB data", miniremovabledeviceicon,
                                               SidebarItem::DEVICE, "/dev/test-usb1");
    navigation->appendItem(navigation->getFirstItem(), unmountedUsb);
    navigation->setCurrentItem(unmountedUsb);
    navigation->setFocus();
    assert(panel->focusedDevice() == "/dev/test-usb1");
    sharePanel->onUpdMount(&mountControl, FXSEL(SEL_UPDATE, FilePanel::ID_MOUNT), NULL);
    sharePanel->onUpdUnmount(&unmountControl, FXSEL(SEL_UPDATE, FilePanel::ID_UMOUNT), NULL);
    assert(mountControl.isEnabled() && !unmountControl.isEnabled());
    FXButton ejectControl(shell, "Eject");
    panel->handle(&ejectControl, FXSEL(SEL_UPDATE, DirPanel::ID_EJECT), NULL);
    assert(ejectControl.isEnabled() && unmountedUsb->isEnabled());
    unmountedUsb->path = "/dev/test-internal1";
    panel->handle(&ejectControl, FXSEL(SEL_UPDATE, DirPanel::ID_EJECT), NULL);
    assert(!ejectControl.isEnabled());
    navigation->removeItem(unmountedUsb);
    sharePanel->getList()->setFocus();
    sharePanel->setDirectory(beforeDeviceControls);
    puts("PASS: no selection disables Mount/Unmount, including Home and GVFS; explicit device selection works; root and Home are protected");
    const FXbool mountMessages = app.reg().readUnsignedEntry("OPTIONS", "mount_messages", true);
    app.reg().writeUnsignedEntry("OPTIONS", "mount_messages", false);
    MountMessageResponder mountMessagesResponder;
    File* unmount = new File(window, "Unmount regression", UNMOUNT);
    unmount->create();
    FXString protectedOutput;
    int protectedStatus = 0;
    const FXString forbiddenMarker = fixture + "/unmount-must-not-run";
    assert(unmount->mount(FXSystem::getHomeDirectory(), "Protected Home",
                           "touch " + xf_quote(forbiddenMarker), &protectedOutput, &protectedStatus) == -1);
    assert(protectedStatus == -1 && protectedOutput.contains("cannot be unmounted"));
    assert(access(forbiddenMarker.text(), F_OK) != 0);
    assert(unmount->mount(ROOTDIR, "Protected root",
                           "touch " + xf_quote(forbiddenMarker), &protectedOutput, &protectedStatus) == -1);
    assert(access(forbiddenMarker.text(), F_OK) != 0);
    puts("PASS: invoking Unmount with no selection launches no command; low-level root/Home guards prevent command execution");
    const FXString localMount = fixture + "/local-mount";
    assert(mkdir(localMount.text(), 0700) == 0 && mkdir((localMount + "/sub").text(), 0700) == 0);
    const FXuint beforeLocalTabs = sharePanel->getTabButtons()->getNumActiveTabs();
    window->getRightPanel()->getTabButtons()->addTab(localMount, false);
    sharePanel->getTabButtons()->addTab(localMount + "/sub");
    assert(sharePanel->getDirectory() == localMount + "/sub");
    const char pngHeader[] = "\x89PNG\r\n\x1a\n";
    for (const FXString& image : { localMount + "/sub/photo.png", fixture + "/outside.png" })
    {
        std::ofstream file(image.text(), std::ios::binary);
        file.write(pngHeader, sizeof(pngHeader) - 1);
    }
    Preview::shutdown();
    FXbool previewWaiting = false;
    Preview::request(&app, localMount + "/sub/photo.png", 64, false, &previewWaiting);
    assert(previewWaiting && Preview::pending());
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(unmount->mount(localMount, "Unmount regression", "printf 'ordinary unmount failed'; exit 7") == -1);
    assert(mountMessagesResponder.errors == 1 && mountMessagesResponder.successes == 0);
    assert(mountMessagesResponder.message == "ordinary unmount failed");
    assert(sharePanel->getTabButtons()->getNumActiveTabs() == beforeLocalTabs);
    for (FXuint index = 0; index < sharePanel->getTabButtons()->getNumActiveTabs(); ++index)
        assert(!sharePanel->getTabButtons()->getPath(index).contains("local-mount"));
    for (FXuint index = 0; index < window->getRightPanel()->getTabButtons()->getNumActiveTabs(); ++index)
        assert(!window->getRightPanel()->getTabButtons()->getPath(index).contains("local-mount"));
    assert(!sharePanel->getDirectory().contains("local-mount"));
    assert(!Preview::pending());
    {
        const FXString deep = localMount + "/deep";
        FXString level = deep;
        assert(mkdir(level.text(), 0700) == 0);
        for (int i = 0; i < 40; ++i)
        {
            level += "/d" + FXStringVal(i);
            assert(mkdir(level.text(), 0700) == 0);
            for (int f = 0; f < 50; ++f) { std::ofstream file((level + "/f" + FXStringVal(f)).text()); }
        }
        sharePanel->setDirectory(localMount);
        pump(app, 5);
        const FXTime stopStart = FXThread::time();
        assert(FileList::stopFolderScans(3000));
        assert(FXThread::time() - stopStart < 2000000000LL);
        sharePanel->setDirectory(beforeDeviceControls);
    }
    Preview::request(&app, fixture + "/outside.png", 64, false, &previewWaiting);
    Preview::cancel(localMount);
    assert(Preview::pending());
    Preview::shutdown();
    sharePanel->setDirectory(beforeDeviceControls);
    puts("PASS: local unmount leaves active and background tabs in both panels and cancels previews under the mount before running the command");
    setenv("SWORDFISH_TEST_UNMOUNT_FAIL", "1", 1);
    SidebarItem* explicitShare = new SidebarItem("Selected share", mininetdriveicon,
                                                SidebarItem::DEVICE, simulatedMount);
    navigation->appendItem(navigation->getFirstItem(), explicitShare);
    navigation->setCurrentItem(explicitShare);
    navigation->setFocus();
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(unmount->mount(simulatedMount, "Unmount regression", "exit 99") == -1);
    assert(mountMessagesResponder.errors == 2 && mountMessagesResponder.message.contains("share is busy"));
    unsetenv("SWORDFISH_TEST_UNMOUNT_FAIL");
    navigation->setFocus();
    simulateStillMounted = true;
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(unmount->mount(simulatedMount, "Unmount regression", "exit 99") == -1);
    assert(mountMessagesResponder.errors == 3 && mountMessagesResponder.message.contains("still mounted"));
    simulateStillMounted = false;
    sharePanel->getTabButtons()->addTab(simulatedMount + "/nested");
    FXuint inactiveMountTab = sharePanel->getTabButtons()->getCurrentTab();
    sharePanel->getTabButtons()->addTab(simulatedMount);
    FXuint activeMountTab = sharePanel->getTabButtons()->getCurrentTab();
    setenv("SWORDFISH_TEST_UNMOUNT_FAIL", "1", 1);
    simulateStillMounted = true;
    navigation->setFocus();
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(sharePanel->onCmdMount(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_UMOUNT), NULL) == 0);
    assert(mountMessagesResponder.errors == 4 && mountMessagesResponder.successes == 0);
    assert(sharePanel->getDirectory() == simulatedMount);
    assert(sharePanel->getTabButtons()->getPath(inactiveMountTab) == simulatedMount + "/nested");
    assert(sharePanel->getTabButtons()->getPath(activeMountTab) == simulatedMount);
    assert(mountMessagesResponder.message.contains("Please close all applications using the device and try again."));
    simulateStillMounted = false;
    const FXTime restoredLoadDeadline = FXThread::time() + 3000000000LL;
    while (busyCursor && FXThread::time() < restoredLoadDeadline) pump(app, 20);
    assert(!busyCursor);
    unsetenv("SWORDFISH_TEST_UNMOUNT_FAIL");
    // The failed attempt restored both active and inactive tabs in their original positions.
    app.reg().writeUnsignedEntry("OPTIONS", "mount_messages", true);
    navigation->setFocus();
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(sharePanel->onCmdMount(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_UMOUNT), NULL) == 1);
    assert(mountMessagesResponder.errors == 4 && mountMessagesResponder.successes == 1);
    assert(!busyCursor);
    assert(sharePanel->getDirectory() == fixture + "/large");
    assert(sharePanel->getTabButtons()->getNumActiveTabs() == inactiveMountTab);
    assert(sharePanel->getTabButtons()->getPath(activeMountTab).empty());
    assert(sharePanel->getTabButtons()->getPath(previousTab) == fixture + "/large");
    navigation->removeItem(explicitShare);
    delete unmount;
    app.reg().writeUnsignedEntry("OPTIONS", "mount_messages", mountMessages);
    std::ifstream unmountLog(getenv("SWORDFISH_TEST_MOUNT_LOG"));
    int unmountCommands = 0;
    while (std::getline(unmountLog, mountedUri))
        if (mountedUri == "unmount smb://unmount.test/Team%20files/") ++unmountCommands;
    assert(unmountCommands == 4);
    puts("PASS: GVFS unmount uses host gio, rejects failures and still-mounted results, closes active/inactive tabs");

    app.reg().writeUnsignedEntry("OPTIONS", "mount_messages", false);
    File* ftpUnmount = new File(window, "FTP unmount regression", UNMOUNT);
    ftpUnmount->create();
    assert(ftpUnmount->mount(gvfsBase + "ftp:host=ftp.example.test", "FTP unmount regression", "exit 99") == 1);
    assert(ftpUnmount->mount(gvfsBase + "ftp:host=ftp.example.test,user=test%40example,port=2121/nested",
                             "FTP unmount regression", "exit 99") == 1);
    delete ftpUnmount;
    app.reg().writeUnsignedEntry("OPTIONS", "mount_messages", mountMessages);
    std::ifstream ftpUnmountLog(getenv("SWORDFISH_TEST_MOUNT_LOG"));
    int ftpUnmountCommands = 0;
    while (std::getline(ftpUnmountLog, mountedUri))
        if (mountedUri == "unmount ftp://ftp.example.test/" ||
            mountedUri == "unmount ftp://test%40example@ftp.example.test:2121/") ++ftpUnmountCommands;
    assert(ftpUnmountCommands == 2);
    puts("PASS: FTP unmount invokes host gio with the mount URI, preserving encoded login and custom port");

    app.addTimeout(&mountMessagesResponder, 1, 20);
    FXString alreadyMountedPath;
    assert(sharePanel->resolveNetworkPath("smb://192.168.1.104/Team%20files", alreadyMountedPath) == 1);
    assert(mountMessagesResponder.alreadyMounted == 1);
    assert(mountMessagesResponder.message.contains("already mounted at"));
    assert(mountMessagesResponder.message.contains((fixture + "/large").text()));
    puts("PASS: reconnecting an already-mounted share reports its local path");

    sharePanel->setDirectory(fixture + "/folder");
    sharePanel->updatePath();
    simulatedGvfsDirectory = fixture + "/gvfs";
    assert(mkdir(simulatedGvfsDirectory.text(), 0700) == 0);
    FXString disk4 = simulatedGvfsDirectory + "/smb-share:server=mx-hp.local,share=4tb";
    FXString disk5 = simulatedGvfsDirectory + "/smb-share:server=mx-hp.local,share=5tb";
    assert(mkdir(disk4.text(), 0700) == 0);
    assert(mkdir(disk5.text(), 0700) == 0);
    pump(app, 1500);
    SidebarItem* devices = static_cast<SidebarItem*>(navigation->getFirstItem()->getNext()->getNext());
    auto deviceCount = [&devices](const FXString& name) {
        int count = 0;
        for (FXTreeItem* item = devices->getFirst(); item; item = item->getNext())
            if (static_cast<SidebarItem*>(item)->key == name) ++count;
        return count;
    };
    const FXString label4 = "4tb@" + RemoteDirectory::hostName("mx-hp.local");
    const FXString label5 = "5tb@" + RemoteDirectory::hostName("mx-hp.local");
    assert(deviceCount(label4) == 1 && deviceCount(label5) == 1);
    for (FXTreeItem* entry = devices->getFirst(); entry; entry = entry->getNext())
    {
        SidebarItem* device = static_cast<SidebarItem*>(entry);
        if (device->path == ROOTDIR) assert(device->deviceAction == SidebarItem::NO_ACTION);
        if (device->key == label4 || device->key == label5)
            assert(device->deviceAction == SidebarItem::UNMOUNT_DEVICE);
    }
    FXString mount5 = gvfsBase + FXPath::name(disk5);
    FXuint beforeExternal = sharePanel->getTabButtons()->getNumActiveTabs();
    sharePanel->getTabButtons()->addTab(mount5 + "/nested");
    sharePanel->getTabButtons()->addTab(mount5);
    assert(rmdir(disk5.text()) == 0);
    pump(app, 1500);
    assert(deviceCount(label5) == 1 && deviceCount(label4) == 1);
    for (FXTreeItem* entry = devices->getFirst(); entry; entry = entry->getNext())
    {
        SidebarItem* device = static_cast<SidebarItem*>(entry);
        if (device->key == label5)
        {
            assert(device->getText() == label5 + " (unmounted)");
            assert(device->isEnabled());
            assert(device->deviceAction == SidebarItem::MOUNT_DEVICE && device->canEject);
            assert(panel->disconnectedDevice(device->path));
            checkSidebarDeviceText(*device, navigation, true);
        }
    }
    assert(sharePanel->getTabButtons()->getNumActiveTabs() == beforeExternal);
    assert(sharePanel->getDirectory() == fixture + "/folder");
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        assert(mkdir(disk5.text(), 0700) == 0);
        pump(app, 1500);
        assert(deviceCount(label5) == 1);
        assert(rmdir(disk5.text()) == 0);
        pump(app, 1500);
        assert(deviceCount(label5) == 1);
    }
    for (FXTreeItem* entry = devices->getFirst(); entry; entry = entry->getNext())
    {
        SidebarItem* device = static_cast<SidebarItem*>(entry);
        if (device->key != label5) continue;
        setenv("SWORDFISH_TEST_REMOUNT_DIR", disk5.text(), 1);
        app.addTimeout(&mountMessagesResponder, 1, 20);
        SidebarDeviceRequest remountShare = {device, SidebarItem::MOUNT_DEVICE};
        panel->onSidebarDeviceAction(NULL, 0, &remountShare);
        app.removeTimeout(&mountMessagesResponder, 1);
        unsetenv("SWORDFISH_TEST_REMOUNT_DIR");
        break;
    }
    pump(app, 1500);
    assert(access(disk5.text(), F_OK) == 0 && deviceCount(label5) == 1);
    for (FXTreeItem* entry = devices->getFirst(); entry; entry = entry->getNext())
        if (static_cast<SidebarItem*>(entry)->key == label5)
        {
            SidebarItem* device = static_cast<SidebarItem*>(entry);
            assert(device->deviceAction == SidebarItem::UNMOUNT_DEVICE && device->getText() == label5);
            checkSidebarDeviceText(*device, navigation, false);
        }
    assert(rmdir(disk5.text()) == 0);
    pump(app, 1500);
    puts("PASS: the share go button remounts through the existing connection flow and returns to stop state");
    SidebarItem* retainedShare = NULL;
    SidebarItem* connectedShare = NULL;
    for (FXTreeItem* entry = devices->getFirst(); entry; entry = entry->getNext())
    {
        SidebarItem* device = static_cast<SidebarItem*>(entry);
        if (device->key == label5) retainedShare = device;
        if (device->key == label4) connectedShare = device;
    }
    assert(retainedShare && connectedShare && connectedShare->canEject);
    setenv("SWORDFISH_TEST_UNMOUNT_FAIL", "1", 1);
    app.addTimeout(&mountMessagesResponder, 1, 20);
    const int errorsBeforeEject = mountMessagesResponder.errors;
    SidebarDeviceRequest failedEject = {connectedShare, SidebarItem::EJECT_DEVICE};
    panel->onSidebarDeviceAction(NULL, 0, &failedEject);
    assert(mountMessagesResponder.errors == errorsBeforeEject + 1);
    unsetenv("SWORDFISH_TEST_UNMOUNT_FAIL");
    pump(app, 1500);
    assert(deviceCount(label4) == 1);
    SidebarDeviceRequest forgetShare = {retainedShare, SidebarItem::EJECT_DEVICE};
    panel->onSidebarDeviceAction(NULL, 0, &forgetShare);
    pump(app, 1500);
    assert(deviceCount(label5) == 0 && deviceCount(label4) == 1);
    for (FXTreeItem* entry = devices->getFirst(); entry; entry = entry->getNext())
        if (static_cast<SidebarItem*>(entry)->key == label4) connectedShare = static_cast<SidebarItem*>(entry);
    app.reg().writeUnsignedEntry("OPTIONS", "mount_messages", false);
    setenv("SWORDFISH_TEST_GVFS_UNMOUNT_DIR", disk4.text(), 1);
    SidebarDeviceRequest disconnectAndForget = {connectedShare, SidebarItem::EJECT_DEVICE};
    panel->onSidebarDeviceAction(NULL, 0, &disconnectAndForget);
    unsetenv("SWORDFISH_TEST_GVFS_UNMOUNT_DIR");
    app.reg().writeUnsignedEntry("OPTIONS", "mount_messages", mountMessages);
    pump(app, 1500);
    assert(deviceCount(label4) == 0 && deviceCount(label5) == 0);
    puts("PASS: failed share eject keeps its entry; ejecting a disconnected share forgets only that entry");
    FilePanel* otherPanel = window->getRightPanel();
    assert(otherPanel != sharePanel);
    otherPanel->getTabButtons()->clearTabs();
    otherPanel->getTabButtons()->addTab(mount5);
    otherPanel->getTabButtons()->addTab(mount5 + "/nested");
    otherPanel->getTabButtons()->closeMount(mount5);
    assert(otherPanel->getTabButtons()->getNumActiveTabs() == 1);
    assert(otherPanel->getDirectory() == FXSystem::getHomeDirectory());
    assert(otherPanel->getTabButtons()->getPath(0) == FXSystem::getHomeDirectory());
    assert(rmdir(simulatedGvfsDirectory.text()) == 0);
    simulatedGvfsDirectory.clear();
    puts("PASS: external unmount retains a remountable share, closes tabs, repeated cycles leave no duplicates");

    simulatedMountTable = fixture + "/disk-commands-mtab";
    {
        std::ofstream output(simulatedMountTable.text());
        output << "/dev/root / ext4 rw 0 0\n";
    }
    setenv("SWORDFISH_TEST_DISK_MTAB", simulatedMountTable.text(), 1);
    setenv("SWORDFISH_TEST_DISK_PATH", (fixture + "/folder").text(), 1);
    app.reg().writeUnsignedEntry("OPTIONS", "mount_messages", false);
    SidebarItem* mountableUsb = new SidebarItem("USB data", miniremovabledeviceicon,
                                               SidebarItem::DEVICE, "/dev/test-usb1");
    navigation->appendItem(navigation->getFirstItem(), mountableUsb);
    navigation->setCurrentItem(mountableUsb);
    navigation->setFocus();
    assert(sharePanel->onCmdMount(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_MOUNT), NULL) == 1);
    navigation->removeItem(mountableUsb);
    assert(xf_mountpointof("/dev/test-usb1") == fixture + "/folder");
    TabButtons* leftTabs = window->getLeftPanel()->getTabButtons();
    TabButtons* rightTabs = window->getRightPanel()->getTabButtons();
    leftTabs->addTab(fixture + "/folder");
    rightTabs->addTab(fixture + "/folder", false);
    window->getLeftPanel()->getList()->setHeaderSize(0, 417);
    window->getRightPanel()->getList()->setHeaderSize(0, 619);
    TabButtons::State leftBeforeBusy = leftTabs->saveState();
    TabButtons::State rightBeforeBusy = rightTabs->saveState();
    int busyPipe[2];
    assert(pipe(busyPipe) == 0);
    pid_t busyProcess = fork();
    assert(busyProcess >= 0);
    if (busyProcess == 0)
    {
        close(busyPipe[0]);
        if (chdir((fixture + "/folder").text()) != 0) _exit(1);
        assert(write(busyPipe[1], "x", 1) == 1);
        for (;;) pause();
    }
    close(busyPipe[1]);
    char ready;
    assert(read(busyPipe[0], &ready, 1) == 1);
    close(busyPipe[0]);
    setenv("SWORDFISH_TEST_DISK_BUSY", "1", 1);
    const int previousErrors = mountMessagesResponder.errors;
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(!panel->unmountRemovable(fixture + "/folder", false));
    assert(mountMessagesResponder.errors == previousErrors + 1);
    assert(mountMessagesResponder.message.contains("currently in use"));
    assert(mountMessagesResponder.message.contains(("PID " + FXStringVal(busyProcess)).text()));
    assert(mountMessagesResponder.message.contains("Please close all applications using the device and try again."));
    assert(leftTabs->saveState().paths == leftBeforeBusy.paths);
    assert(rightTabs->saveState().paths == rightBeforeBusy.paths);
    assert(leftTabs->getCurrentTab() == leftBeforeBusy.selected);
    assert(rightTabs->getCurrentTab() == rightBeforeBusy.selected);
    assert(leftTabs->saveState().views == leftBeforeBusy.views);
    assert(rightTabs->saveState().views == rightBeforeBusy.views);
    assert(leftTabs->saveState().columns == leftBeforeBusy.columns);
    assert(rightTabs->saveState().columns == rightBeforeBusy.columns);
    assert(leftTabs->saveState().columnOrder == leftBeforeBusy.columnOrder);
    assert(rightTabs->saveState().columnOrder == rightBeforeBusy.columnOrder);
    assert(kill(busyProcess, SIGTERM) == 0);
    int busyStatus;
    assert(waitpid(busyProcess, &busyStatus, 0) == busyProcess);
    puts("PASS: busy error identifies a real blocking process and failure restores both panels' tab order, selection, views and column widths");
    assert(!xf_mountpointof("/dev/test-usb1").empty());
    unsetenv("SWORDFISH_TEST_DISK_BUSY");
    const FXString busyOnce = fixture + "/disk-busy-once";
    { std::ofstream marker(busyOnce.text()); }
    setenv("SWORDFISH_TEST_DISK_BUSY_ONCE", busyOnce.text(), 1);
    assert(panel->unmountRemovable(fixture + "/folder", false));
    assert(mountMessagesResponder.errors == previousErrors + 1);
    assert(access(busyOnce.text(), F_OK) != 0);
    unsetenv("SWORDFISH_TEST_DISK_BUSY_ONCE");
    assert(xf_mountpointof("/dev/test-usb1").empty());
    assert(panel->openDevice("/dev/test-usb1"));
    puts("PASS: busy USB unmount is rejected and transient busy unmount retries safely");
    // Exercise the visible device-row button, not only the command handler.
    SidebarItem* rowButton = new SidebarItem("USB action test", miniremovabledeviceicon,
                                             SidebarItem::DEVICE, fixture + "/folder");
    rowButton->deviceAction = SidebarItem::UNMOUNT_DEVICE;
    rowButton->canEject = true;
    navigation->appendItem(navigation->getFirstItem(), rowButton);
    navigation->expandTree(navigation->getFirstItem());
    navigation->makeItemVisible(rowButton);
    window->raise();
    window->layout();
    pump(app, 100);
    int buttonY = -1;
    const int buttonX = navigation->deviceButtonLeft() + 12;
    for (int y = 0; y < navigation->getHeight(); ++y)
        if (navigation->deviceButtonAt(buttonX, y) == rowButton) { buttonY = y; break; }
    assert(buttonY >= 0);
    assert(navigation->deviceButtonAt(buttonX - 54, buttonY) == NULL);
    FXEvent buttonEvent = {};
    buttonEvent.win_x = buttonX;
    buttonEvent.win_y = buttonY;
    navigation->onDevicePress(NULL, 0, &buttonEvent);
    buttonEvent.win_x = buttonX - 24;
    navigation->onDeviceRelease(NULL, 0, &buttonEvent);
    assert(!navigation->grabbed() && !xf_mountpointof("/dev/test-usb1").empty());
    buttonEvent.win_x = navigation->deviceButtonLeft(false) + 12;
    assert(navigation->deviceActionAt(buttonEvent.win_x, buttonY) == SidebarItem::UNMOUNT_DEVICE);
    navigation->onDevicePress(NULL, 0, &buttonEvent);
    navigation->onDeviceRelease(NULL, 0, &buttonEvent);
    assert(xf_mountpointof("/dev/test-usb1").empty());
    rowButton->path = "/dev/test-usb1";
    rowButton->deviceAction = SidebarItem::MOUNT_DEVICE;
    assert(navigation->deviceActionAt(buttonEvent.win_x, buttonY) == SidebarItem::MOUNT_DEVICE);
    navigation->onDevicePress(NULL, 0, &buttonEvent);
    navigation->onDeviceRelease(NULL, 0, &buttonEvent);
    assert(xf_mountpointof("/dev/test-usb1") == fixture + "/folder");
    rowButton->path = fixture + "/folder";
    rowButton->deviceAction = SidebarItem::UNMOUNT_DEVICE;
    buttonEvent.win_x = buttonX;
    navigation->onDevicePress(NULL, 0, &buttonEvent);
    assert(navigation->grabbed());
    navigation->onDeviceRelease(NULL, 0, &buttonEvent);
    assert(!navigation->grabbed());
    assert(sharePanel->getDirectory() != fixture + "/folder");
    navigation->removeItem(rowButton);
    assert(xf_mountpointof("/dev/test-usb1").empty());
    std::ifstream diskLog(getenv("SWORDFISH_TEST_MOUNT_LOG"));
    std::vector<std::string> diskCommands;
    while (std::getline(diskLog, mountedUri))
        if (mountedUri.find("disk ") == 0) diskCommands.push_back(mountedUri);
    assert((diskCommands == std::vector<std::string>{
        "disk mount", "disk unmount", "disk unmount", "disk unmount", "disk unmount",
        "disk mount", "disk unmount", "disk mount", "disk unmount", "disk power-off"}));
    {
        std::ofstream output(simulatedMountTable.text());
        output << "/dev/root / ext4 rw 0 0\n"
               << "/dev/test-usb1 " << fixture.text() << "/folder ext4 rw 0 0\n"
               << "/dev/test-usb2 " << fixture.text() << "/empty ext4 rw 0 0\n";
    }
    setenv("SWORDFISH_TEST_DISK_BUSY", "1", 1);
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(!panel->unmountRemovable("/dev/test-usb1", true));
    assert(!xf_mountpointof("/dev/test-usb1").empty() && !xf_mountpointof("/dev/test-usb2").empty());
    unsetenv("SWORDFISH_TEST_DISK_BUSY");
    setenv("SWORDFISH_TEST_DISK_STILL_MOUNTED", "1", 1);
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(!panel->unmountRemovable("/dev/test-usb1", true));
    assert(mountMessagesResponder.message.contains("still mounted"));
    unsetenv("SWORDFISH_TEST_DISK_STILL_MOUNTED");
    assert(panel->unmountRemovable("/dev/test-usb1", true));
    assert(xf_mountpointof("/dev/test-usb1").empty() && xf_mountpointof("/dev/test-usb2").empty());
    std::ifstream allDiskLog(getenv("SWORDFISH_TEST_MOUNT_LOG"));
    int powerOffs = 0;
    while (std::getline(allDiskLog, mountedUri))
        if (mountedUri == "disk power-off") ++powerOffs;
    assert(powerOffs == 2);
    {
        std::ofstream output(simulatedMountTable.text());
        output << "/dev/test-usb2 / ext4 rw 0 0\n"
               << "/dev/test-usb1 " << fixture.text() << "/folder ext4 rw 0 0\n";
    }
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(!panel->unmountRemovable("/dev/test-usb1", true));
    assert(xf_mountpointof("/dev/test-usb1") == fixture + "/folder");
    {
        std::ofstream output(simulatedMountTable.text());
        output << "/dev/root / ext4 rw 0 0\n";
    }
    setenv("SWORDFISH_TEST_POWER_OFF_FAIL", "1", 1);
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(!panel->unmountRemovable("/dev/test-usb1", true));
    unsetenv("SWORDFISH_TEST_POWER_OFF_FAIL");
    puts("PASS: whole-device eject releases all partitions, aborts on busy mounts, and reports power-off failure");
    unsetenv("SWORDFISH_TEST_DISK_MTAB");
    unsetenv("SWORDFISH_TEST_DISK_PATH");
    app.reg().writeUnsignedEntry("OPTIONS", "mount_messages", mountMessages);
    assert(unlink(simulatedMountTable.text()) == 0);
    simulatedMountTable.clear();
    puts("PASS: desktop-service mount, busy-unmount failure after one retry, transient busy retried successfully, remount and safe eject execute the expected commands");
    puts("PASS: distinct stop/go/eject buttons perform their own actions; releasing over another button cancels");

    window->onCmdHelp(NULL, 0, NULL);
    FXButton* helpLink = projectLink(app.getRootWindow());
    assert(helpLink && helpLink->getSelector() == XFileExplorer::ID_PROJECT_WEBSITE);
    helpLink->getTarget()->handle(helpLink, FXSEL(SEL_COMMAND, helpLink->getSelector()), NULL);
    AboutLinkResponder aboutLink;
    app.addTimeout(&aboutLink, 1, 20);
    window->onCmdAbout(NULL, 0, NULL);
    assert(aboutLink.clicked);
    pump(app, 250);
    std::ifstream websiteLog(getenv("SWORDFISH_TEST_MOUNT_LOG"));
    int websites = 0;
    while (std::getline(websiteLog, mountedUri))
        if (mountedUri == "website https://github.com/hasmak-44/Swordfish-FM") ++websites;
    assert(websites == 2);
    puts("PASS: About and Help project links launch the host browser with the repository URL");

    // Exercise the application's real signal handler, not pump()'s manual reaper.
    FXTime until = FXThread::time() + 2200000000LL;
    while (FXThread::time() < until)
    {
        app.runOneEvent(false);
        usleep(1000);
    }
    assert(!RemoteDirectory::loading(remote));
    assert(RemoteDirectory::error(remote).empty());
    RemoteDirectory::refresh(remote);
    until = FXThread::time() + 2200000000LL;
    while (FXThread::time() < until)
    {
        app.runOneEvent(false);
        usleep(1000);
    }
    assert(!RemoteDirectory::loading(remote));
    assert(RemoteDirectory::error(remote).empty());
    RemoteDirectory::release(&observer);
    puts("PASS: archive-style command restores SIGCHLD, harvests intervening exits, and subsequent remote refresh completes");
    FilePanel* layoutPanels[] = {window->getLeftPanel(), window->getRightPanel()};
    for (FilePanel* pane : layoutPanels)
    {
        pane->getTabButtons()->clearTabs();
        pane->getTabButtons()->addTab(fixture);
    }
    for (int cycle = 0; cycle < 6; ++cycle)
    {
        window->onCmdHorzVertPanels(NULL, FXSEL(SEL_COMMAND, cycle % 2 ?
                                   XFileExplorer::ID_HORZ_PANELS : XFileExplorer::ID_VERT_PANELS), NULL);
        window->resize(cycle % 2 ? 1024 : 800, cycle % 2 ? 768 : 600);
        for (FilePanel* pane : layoutPanels) pane->showTabbar(false);
        window->layout();
        for (FilePanel* pane : layoutPanels) pane->showTabbar(true);
        pump(app, 120);
        for (FilePanel* pane : layoutPanels)
        {
            FXWindow* scroll = pane->getTabButtons()->getParent();
            FXWindow* bar = scroll->getParent();
            assert(bar->shown() && bar->getHeight() > 0);
            assert(scroll->getHeight() > 0 && pane->getTabButtons()->getHeight() > 0);
            assert(scroll->getY() + scroll->getHeight() <= bar->getHeight());
        }
    }
    puts("PASS: both tab bars retain visible geometry through hide/show, orientation changes and resizing");
    FilePanel* hoverPanel = window->getLeftPanel();
    window->raise();
    window->setFocus();
    TabButtons* hoverTabs = hoverPanel->getTabButtons();
    hoverTabs->addTab(fixture + "/folder");
    pump(app, 150);
    FXWindow* hoverButton = hoverTabs->getFirst()->getFirst();
    assert(hoverTabs->getCurrentTab() == 1);
    // TabDragButton's first private selector is its hover timeout.
    hoverButton->handle(NULL, FXSEL(SEL_TIMEOUT, FXButton::ID_LAST), NULL);
    assert(hoverTabs->getCurrentTab() == 0);
    assert(hoverPanel->getDirectory() == fixture);
    assert(find(hoverPanel->getList(), "folder") >= 0);
    puts("PASS: tab-hover timeout selects the inactive folder and populates its list immediately");
    XCloseDisplay(otherClient);
    std::fflush(stdout);
    // The application-global teardown is outside this focused widget test.
    std::_Exit(0);
}
