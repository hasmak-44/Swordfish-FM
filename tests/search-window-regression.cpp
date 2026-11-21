#include "config.h"
#include <fx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <glib.h>
#include <limits.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#include "icons.h"
#include "xfeutils.h"
#include "DragPreview.h"
#include "FileList.h"
#include "TextWindow.h"
#include "SearchWindow.h"
#include "XFileExplorer.h"
#include "MessageBox.h"
#include "Preferences.h"
#include "TabButtons.h"
#include "File.h"
#include "Transfer.h"
#include <fxkeys.h>
#include <dlfcn.h>
#include <fstream>
#include <algorithm>
#include <iterator>
#include <vector>
#include <sys/stat.h>

extern FXMainWindow* mainWindow;
extern FXString execpath, homedir, xdgdatahome, xdgconfighome;
extern FXString clipboard;
extern "C" int swordfish_main(int, char**);

static FXString crossDeviceSource;
static int crossDeviceRenames;
static File* cancelReadOperation;
static FXString cancelReadPath;
static int cancelReadBlocks;

extern "C" int rename(const char* source, const char* destination) noexcept
{
    const char* forcedSource = getenv("SWORDFISH_TEST_CROSS_DEVICE");
    if ((!crossDeviceSource.empty() && FXString(source).left(crossDeviceSource.length()) == crossDeviceSource) ||
        (forcedSource && strcmp(source, forcedSource) == 0))
    {
        ++crossDeviceRenames;
        errno = EXDEV;
        return -1;
    }
    typedef int (*Rename)(const char*, const char*);
    static Rename realRename = reinterpret_cast<Rename>(dlsym(RTLD_NEXT, "rename"));
    return realRename(source, destination);
}

extern "C" ssize_t read(int fd, void* buffer, size_t count)
{
    typedef ssize_t (*Read)(int, void*, size_t);
    static Read realRead = reinterpret_cast<Read>(dlsym(RTLD_NEXT, "read"));
    const char* slow = getenv("SWORDFISH_TEST_SLOW_FILE");
    if ((slow || cancelReadOperation) && count >= 65536)
    {
        char link[64], path[PATH_MAX];
        snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
        ssize_t length = readlink(link, path, sizeof(path) - 1);
        if (length >= 0)
        {
            path[length] = '\0';
            if (slow && strcmp(path, slow) == 0) usleep(250000);
            // Press Cancel once part of the file has already been written to the target
            if (cancelReadOperation && cancelReadPath == path && --cancelReadBlocks == 0)
            {
                ssize_t result = realRead(fd, buffer, count);
                File* operation = cancelReadOperation;
                cancelReadOperation = NULL;
                operation->handle(NULL, FXSEL(SEL_COMMAND, File::ID_CANCEL_BUTTON), NULL);
                return result;
            }
        }
    }
    return realRead(fd, buffer, count);
}

extern "C" int lstat(const char* path, struct stat* info) noexcept
{
    const char* slow = getenv("SWORDFISH_TEST_SLOW_STAT");
    if (slow && strncmp(path, slow, strlen(slow)) == 0 && path[strlen(slow)] == '/')
    {
        const char* marker = getenv("SWORDFISH_TEST_STAT_MARKER");
        if (marker)
        {
            int fd = open(marker, O_CREAT | O_WRONLY, 0600);
            if (fd >= 0) close(fd);
        }
        usleep(40000);
    }
    typedef int (*Lstat)(const char*, struct stat*);
    static Lstat realLstat = reinterpret_cast<Lstat>(dlsym(RTLD_NEXT, "lstat"));
    return realLstat(path, info);
}

extern "C" int rmdir(const char* path) noexcept
{
    const char* late = getenv("SWORDFISH_TEST_LATE_SOURCE");
    if (late && strcmp(path, late) == 0)
    {
        int directory = open(path, O_RDONLY | O_DIRECTORY);
        assert(directory >= 0);
        int file = openat(directory, "added-after-scan.txt", O_WRONLY | O_CREAT | O_EXCL, 0600);
        assert(file >= 0 && write(file, "late", 4) == 4);
        assert(close(file) == 0 && close(directory) == 0);
        unsetenv("SWORDFISH_TEST_LATE_SOURCE");
    }
    typedef int (*Rmdir)(const char*);
    static Rmdir realRmdir = reinterpret_cast<Rmdir>(dlsym(RTLD_NEXT, "rmdir"));
    return realRmdir(path);
}

static int corruptVerifiedCopies;
static int verifiedCopySyncs;

// Copy verification flushes the new file before re-reading it; corrupting it here simulates bad storage
extern "C" int fdatasync(int fd)
{
    ++verifiedCopySyncs;
    if (corruptVerifiedCopies > 0)
    {
        --corruptVerifiedCopies;
        assert(pwrite(fd, "X", 1, 0) == 1);
    }
    typedef int (*Sync)(int);
    static Sync realSync = reinterpret_cast<Sync>(dlsym(RTLD_NEXT, "fdatasync"));
    return realSync(fd);
}

static void checkPreferencesScrolling(FXApp& app, FXWindow* window)
{
    if (!window->shown()) return;
    if (FXList* list = dynamic_cast<FXList*>(window))
    {
        if (list->getContentHeight() > list->verticalScrollBar()->getPage())
        {
            FXScrollWindow* outer = NULL;
            for (FXWindow* parent = list->getParent(); parent; parent = parent->getParent())
                if ((outer = dynamic_cast<FXScrollWindow*>(parent))) break;
            assert(outer);
            const FXint outerPosition = outer->getYPosition();
            list->setPosition(0, 0);
            FXEvent wheel = {};
            wheel.code = -120;
            list->handle(NULL, FXSEL(SEL_MOUSEWHEEL, 0), &wheel);
            const FXTime deadline = FXThread::time() + 500000000LL;
            while (FXThread::time() < deadline) { app.runOneEvent(false); usleep(1000); }
            assert(list->getYPosition() < 0 && outer->getYPosition() == outerPosition);

        }
    }
    if (FXScrollWindow* scroll = dynamic_cast<FXScrollWindow*>(window))
    {
        if (scroll->shown() && scroll->getContentHeight() > scroll->getViewportHeight())
        {
            scroll->setPosition(0, -scroll->getContentHeight());
            assert(scroll->getYPosition() == scroll->verticalScrollBar()->getPage() - scroll->getContentHeight());
            assert(scroll->contentWindow()->getY() + scroll->contentWindow()->getHeight() <=
                   scroll->getViewportHeight());
            scroll->setPosition(0, 0);
            FXEvent wheel = {};
            wheel.code = -120;
            scroll->handle(NULL, FXSEL(SEL_MOUSEWHEEL, 0), &wheel);
            const FXTime deadline = FXThread::time() + 500000000LL;
            while (FXThread::time() < deadline) { app.runOneEvent(false); usleep(1000); }
            assert(-scroll->getYPosition() >= FXMIN(app.getNormalFont()->getFontHeight(),
                     scroll->getContentHeight() - scroll->getViewportHeight()));
            wheel.code = -120000;
            scroll->handle(NULL, FXSEL(SEL_MOUSEWHEEL, 0), &wheel);
            const FXTime bottomDeadline = FXThread::time() + 500000000LL;
            while (FXThread::time() < bottomDeadline) { app.runOneEvent(false); usleep(1000); }
            assert(scroll->getYPosition() == scroll->verticalScrollBar()->getPage() -
                   scroll->getContentHeight());
            FXWindow* last = scroll->contentWindow()->getLast();
            assert(last && last->getY() + last->getHeight() + scroll->getYPosition() <=
                   scroll->verticalScrollBar()->getPage());
            wheel.code = 120000;
            scroll->handle(NULL, FXSEL(SEL_MOUSEWHEEL, 0), &wheel);
            const FXTime topDeadline = FXThread::time() + 500000000LL;
            while (FXThread::time() < topDeadline) { app.runOneEvent(false); usleep(1000); }
            assert(scroll->getYPosition() == 0);
        }
    }
    for (FXWindow* child = window->getFirst(); child; child = child->getNext())
        checkPreferencesScrolling(app, child);
}

static void dispatchWheel(FXApp& app, FXWindow* window, FXuint button)
{
    XEvent raw = {};
    raw.xbutton.type = ButtonPress;
    raw.xbutton.display = (Display*)app.getDisplay();
    raw.xbutton.window = window->id();
    raw.xbutton.button = button;
    raw.xbutton.subwindow = 1;
    raw.xbutton.x = window->getWidth() / 2;
    raw.xbutton.y = window->getHeight() / 2;
    static_cast<XFApp&>(app).dispatchEvent(raw);
    const FXTime deadline = FXThread::time() + 500000000LL;
    while (FXThread::time() < deadline) { app.runOneEvent(false); usleep(1000); }
}

// Firefox-style chaining: a nested list at its end passes the wheel on to the page around it
static void checkWheelChaining(FXApp& app, FXWindow* owner)
{
    FXDialogBox* dialog = new FXDialogBox(owner, "Wheel chaining", DECOR_ALL, 0, 0, 400, 300);
    FXScrollWindow* page = new FXScrollWindow(dialog, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    xf_readablescrolling(page);
    FXVerticalFrame* content = new FXVerticalFrame(page, LAYOUT_FILL_X);
    new FXFrame(content, LAYOUT_FIX_HEIGHT, 0, 0, 0, 200);
    FXList* list = new FXList(content, NULL, 0, LAYOUT_FILL_X | LAYOUT_FIX_HEIGHT, 0, 0, 0, 100);
    for (FXint i = 0; i < 50; ++i) list->appendItem(FXStringVal(i));
    FXSpinner* spinner = new FXSpinner(content, 4, NULL, 0, FRAME_SUNKEN);
    new FXFrame(content, LAYOUT_FIX_HEIGHT, 0, 0, 0, 600);
    dialog->create();
    dialog->show(PLACEMENT_OWNER);
    dialog->layout();
    app.flush(true);
    app.repaint();
    assert(page->getContentHeight() > page->getViewportHeight());
    assert(list->getContentHeight() > list->getViewportHeight());

    page->setPosition(0, -60);
    list->setPosition(0, 0);
    assert(xf_wheeltarget(list, 120) == page);
    dispatchWheel(app, list, Button4);
    assert(list->getYPosition() == 0 && page->getYPosition() > -60);

    const FXint pagePosition = page->getYPosition();
    assert(xf_wheeltarget(list, -120) == NULL);
    dispatchWheel(app, list, Button5);
    assert(list->getYPosition() < 0 && page->getYPosition() == pagePosition);

    assert(xf_wheeltarget(spinner, 120) == NULL);
    page->setPosition(0, 0);
    assert(xf_wheeltarget(page->contentWindow(), 120) == NULL);
    delete dialog;
}

static FXTabBook* findPreferencesTabs(FXWindow* window)
{
    if (FXTabBook* tabs = dynamic_cast<FXTabBook*>(window)) return tabs;
    for (FXWindow* child = window->getFirst(); child; child = child->getNext())
        if (FXTabBook* tabs = findPreferencesTabs(child)) return tabs;
    return NULL;
}

class TestSearch : public SearchWindow
{
public:
    TestSearch(FXApp* app) : SearchWindow(app, "Search regression", 0, 0, 640, 480) {}

    SearchPanel* results() { return searchpanel; }

    FXString collect(const FXString& command)
    {
        assert(execCmd(command) == 1);
        application->removeInput(out[0], INPUT_READ);
        ::close(in[1]);
        FXString result;
        char buffer[4096];
        ssize_t size;
        while ((size = read(out[0], buffer, sizeof(buffer))) != 0)
        {
            if (size < 0 && errno == EINTR) continue;
            assert(size > 0);
            result.append(buffer, size);
        }
        ::close(out[0]);
        int status;
        assert(waitpid(pid, &status, 0) == pid);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        pid = -1;
        return result;
    }
};

static FileList* findList(FXWindow* window)
{
    if (FileList* list = dynamic_cast<FileList*>(window)) return list;
    for (FXWindow* child = window->getFirst(); child; child = child->getNext())
    {
        if (FileList* list = findList(child)) return list;
    }
    return NULL;
}

class ArchivePrompt : public FXObject
{
public:
    FXApp* app;
    FXbool accept = false;
    FXbool seen = false;
    ArchivePrompt(FXApp* application) : app(application) {}
    long handle(FXObject*, FXSelector, void*) override
    {
        MessageBox* dialog = dynamic_cast<MessageBox*>(app->getModalWindow());
        if (!dialog) { app->addTimeout(this, 1, 10); return 1; }
        assert(dialog->getTitle() == "Archive folder support");
        assert(dialog->getText().contains("archivemount"));
        assert(dialog->getText().contains("will not download packages or install software"));
        seen = true;
        dialog->handle(this, FXSEL(SEL_COMMAND, accept ? MessageBox::ID_CLICKED_YES : MessageBox::ID_CLICKED_NO), NULL);
        return 1;
    }
};

class ArchiveReadOnlyNotice : public FXObject
{
public:
    FXApp* app;
    FXbool seen = false;
    ArchiveReadOnlyNotice(FXApp* application) : app(application) {}
    long handle(FXObject*, FXSelector, void*) override
    {
        MessageBox* dialog = dynamic_cast<MessageBox*>(app->getModalWindow());
        if (!dialog) { app->addTimeout(this, 1, 10); return 1; }
        assert(dialog->getTitle() == "ZIP archive opened read-only");
        assert(dialog->getText().contains("To protect your data, ZIP archives are always opened read-only"));
        assert(dialog->getText().contains("drag or copy files out"));
        assert(dialog->getText().contains("cannot add, edit, rename, or delete"));
        seen = true;
        dialog->handle(this, FXSEL(SEL_COMMAND, MessageBox::ID_CLICKED_OK), NULL);
        return 1;
    }
};

class ArchiveMenu : public FXObject
{
public:
    FXApp* app;
    FXbool seen = false;
    FXuint selector = FilePanel::ID_ARCHIVE_OPEN;
    ArchiveMenu(FXApp* application) : app(application) {}
    long handle(FXObject*, FXSelector, void*) override
    {
        for (FXWindow* child = app->getRootWindow()->getFirst(); child; child = child->getNext())
        {
            FXMenuPane* menu = dynamic_cast<FXMenuPane*>(child);
            if (!menu || !menu->shown()) continue;
            for (FXWindow* entry = menu->getFirst(); entry; entry = entry->getNext())
            {
                FXMenuCommand* command = dynamic_cast<FXMenuCommand*>(entry);
                if (command && command->getSelector() == selector)
                {
                    seen = true;
                    assert(command->isEnabled());
                }
            }
            menu->popdown();
            return 1;
        }
        app->addTimeout(this, 1, 10);
        return 1;
    }
};

static void checkRenameMenu(SearchPanel* panel, const FXString& fixture)
{
    FXString first = fixture + "/data/match.txt", second = fixture + "/data/other.txt";
    assert(panel->appendItem(first));
    assert(panel->appendItem(second));
    FileList* list = findList(panel);
    assert(list);
    FXMenuPane menu(panel);
    FXMenuCommand* single = new FXMenuCommand(&menu, "Rename", NULL, panel, SearchPanel::ID_CONTEXT_RENAME);
    FXMenuCommand* bulk = new FXMenuCommand(&menu, "Bulk Rename...", NULL, panel, SearchPanel::ID_CONTEXT_BULK_RENAME);
    for (int count = 0; count <= 2; ++count)
    {
        list->killSelection();
        for (int i = 0; i < count; ++i) list->selectItem(i);
        panel->handle(single, FXSEL(SEL_UPDATE, SearchPanel::ID_CONTEXT_RENAME), NULL);
        panel->handle(bulk, FXSEL(SEL_UPDATE, SearchPanel::ID_CONTEXT_BULK_RENAME), NULL);
        assert(single->isEnabled() == (count == 1));
        assert(bulk->isEnabled() == (count > 1));
        const FXuint disabled = count == 1 ? SearchPanel::ID_CONTEXT_BULK_RENAME : SearchPanel::ID_CONTEXT_RENAME;
        panel->handle(single, FXSEL(SEL_COMMAND, disabled), NULL);
    }
    list->killSelection();
    puts("PASS: search Rename and Bulk Rename are enabled only for their intended selection counts");
}

static void checkIcon(FXTopWindow* window)
{
    assert(window->getIcon() == xfedockicon);
    assert(window->getMiniIcon() == minixfeicon);
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char* data = NULL;
    Display* display = static_cast<Display*>(window->getApp()->getDisplay());
    assert(XGetWindowProperty(display, window->id(), XInternAtom(display, "_NET_WM_ICON", False),
                              0, 1024 * 1024, False, XA_CARDINAL, &type, &format,
                              &count, &remaining, &data) == Success);
    assert(type == XA_CARDINAL && format == 32 && remaining == 0);
    unsigned long* values = reinterpret_cast<unsigned long*>(data);
    assert(count == 2 + static_cast<unsigned long>(xfedockicon->getWidth() * xfedockicon->getHeight()));
    assert(values[0] == static_cast<unsigned long>(xfedockicon->getWidth()));
    assert(values[1] == static_cast<unsigned long>(xfedockicon->getHeight()));
    const FXColor* pixels = xfedockicon->getData();
    for (unsigned long i = 0; i < count - 2; ++i)
    {
        unsigned long expected = (static_cast<unsigned long>(FXALPHAVAL(pixels[i])) << 24) |
                                 (static_cast<unsigned long>(FXREDVAL(pixels[i])) << 16) |
                                 (static_cast<unsigned long>(FXGREENVAL(pixels[i])) << 8) |
                                 FXBLUEVAL(pixels[i]);
        assert((values[i + 2] & 0xffffffffUL) == expected);
    }
    XFree(data);
}

class ColumnMenuProbe : public FXObject
{
public:
    FXApp* app;
    FXbool seen = false;
    explicit ColumnMenuProbe(FXApp* application) : app(application) {}
    long handle(FXObject*, FXSelector, void*) override
    {
        for (FXWindow* window = app->getRootWindow()->getFirst(); window; window = window->getNext())
        {
            FXMenuPane* menu = dynamic_cast<FXMenuPane*>(window);
            if (!menu || !menu->shown()) continue;
            FXbool nameFound = false, sizeFound = false;
            for (FXWindow* entry = menu->getFirst(); entry; entry = entry->getNext())
            {
                FXMenuCheck* check = dynamic_cast<FXMenuCheck*>(entry);
                if (!check) continue;
                if (check->getSelector() == FileList::ID_COLUMN_TOGGLE)
                {
                    nameFound = true;
                    assert(check->getCheck() && !check->isEnabled());
                }
                if (check->getSelector() == FileList::ID_COLUMN_TOGGLE + 1)
                {
                    sizeFound = true;
                    assert(check->isEnabled() && check->getCheck());
                }
            }
            assert(nameFound && sizeFound);
            seen = true;
            menu->popdown();
            return 1;
        }
        app->addTimeout(this, 1, 10);
        return 1;
    }
};

class PaneColumnsRegression : public FXObject
{
public:
    PreferencesBox* preferences;
    FXbool accept = false;
    explicit PaneColumnsRegression(PreferencesBox* box) : preferences(box) {}
    long handle(FXObject*, FXSelector, void*) override
    {
        preferences->columnPane->setCurrentItem(0);
        preferences->onCmdColumnPane(NULL, 0, NULL);
        preferences->sizecol->setCheck(false);
        preferences->extcol->setCheck(true);
        preferences->onCmdViewCol(NULL, 0, NULL);
        std::vector<FXuint> left(preferences->idCol, preferences->idCol + preferences->nbCols);
        preferences->columnPane->setCurrentItem(1);
        preferences->onCmdColumnPane(NULL, 0, NULL);
        preferences->sizecol->setCheck(true);
        preferences->extcol->setCheck(false);
        preferences->onCmdViewCol(NULL, 0, NULL);
        preferences->columnPane->setCurrentItem(0);
        preferences->onCmdColumnPane(NULL, 0, NULL);
        assert(std::vector<FXuint>(preferences->idCol, preferences->idCol + preferences->nbCols) == left);
        if (accept) preferences->onCmdAccept(NULL, 0, NULL);
        else preferences->onCmdCancel(NULL, 0, NULL);
        return 1;
    }
};

static DragPreviewTip* visibleDragPreview(FXApp* app)
{
    for (FXWindow* child = app->getRootWindow()->getFirst(); child; child = child->getNext())
    {
        DragPreviewTip* tip = dynamic_cast<DragPreviewTip*>(child);
        if (tip && tip->shown()) return tip;
    }
    return NULL;
}

static void checkColumns(FileList* files)
{
    const std::vector<FXuint> original = files->getColumnLayout();
    files->setColumnLayout({FileList::ID_COL_NAME, FileList::ID_COL_SIZE, FileList::ID_COL_TYPE,
                            FileList::ID_COL_DATE});
    files->setHeaderSize(1, 177);
    files->setHeaderSize(2, 211);
    FXString name = files->getItemFilename(0);
    FXString size = files->getItemText(0).section('\t', 1);
    FXString type = files->getItemText(0).section('\t', 2);
    files->selectItem(0);
    FXHeader* header = NULL;
    for (FXWindow* child = files->getFirst(); child; child = child->getNext())
        if ((header = dynamic_cast<FXHeader*>(child))) break;
    assert(header);
    ColumnMenuProbe menu(files->getApp());
    files->getApp()->addTimeout(&menu, 1, 10);
    FXEvent context = {};
    header->handle(NULL, FXSEL(SEL_RIGHTBUTTONRELEASE, 0), &context);
    assert(menu.seen);
    files->layout();
    header->layout();
    const FXint headerWidth = header->getWidth();
    header->resize(FXMAX(headerWidth, header->getTotalSize() + 1), header->getHeight());
    FXEvent event = {};
    event.win_x = header->getItem(1)->getPos() + header->getPosition() + 40;
    event.win_y = header->getHeight() / 2;
    event.state = LEFTBUTTONMASK;
    header->handle(NULL, FXSEL(SEL_LEFTBUTTONPRESS, 0), &event);
    FXWindow* focus = files->getApp()->getFocusWindow();
    event.win_x = header->getItem(2)->getPos() + header->getPosition() + 160;
    header->translateCoordinatesTo(event.root_x, event.root_y, header->getRoot(), event.win_x, event.win_y);
    header->handle(NULL, FXSEL(SEL_MOTION, 0), &event);
    // The column slides inside the header; no detached tooltip follows the pointer.
    assert(!visibleDragPreview(files->getApp()));
    assert(files->getApp()->getFocusWindow() == focus);
    header->ungrab();
    header->handle(NULL, FXSEL(SEL_UNGRABBED, 0), &event);
    assert(!visibleDragPreview(files->getApp()) && !header->isItemPressed(1));
    assert(files->getHeaderId(1) == FileList::ID_COL_SIZE);

    event.win_x = header->getItem(1)->getPos() + header->getPosition() + 40;
    header->handle(NULL, FXSEL(SEL_LEFTBUTTONPRESS, 0), &event);
    event.win_x = header->getItem(2)->getPos() + header->getPosition() + 160;
    header->handle(NULL, FXSEL(SEL_MOTION, 0), &event);
    event.state = 0;
    header->handle(NULL, FXSEL(SEL_LEFTBUTTONRELEASE, 0), &event);
    assert(!visibleDragPreview(files->getApp()));
    assert(files->getHeaderId(1) == FileList::ID_COL_TYPE);
    assert(files->getHeaderId(2) == FileList::ID_COL_SIZE);
    assert(files->getHeaderSize(1) == 211 && files->getHeaderSize(2) == 177);
    assert(files->getItemFilename(0) == name);
    assert(files->getItemText(0).section('\t', 1) == type);
    assert(files->getItemText(0).section('\t', 2) == size);
    assert(files->isItemSelected(0));

    event.state = LEFTBUTTONMASK;
    event.win_x = header->getItem(2)->getPos() + header->getPosition() + 40;
    header->handle(NULL, FXSEL(SEL_LEFTBUTTONPRESS, 0), &event);
    event.win_x = header->getItem(1)->getPos() + header->getPosition() + 20;
    header->handle(NULL, FXSEL(SEL_MOTION, 0), &event);
    event.state = 0;
    header->handle(NULL, FXSEL(SEL_LEFTBUTTONRELEASE, 0), &event);
    assert(files->getHeaderId(1) == FileList::ID_COL_SIZE);
    assert(files->getHeaderId(2) == FileList::ID_COL_TYPE);

    event.state = LEFTBUTTONMASK;
    event.win_x = header->getItem(1)->getPos() + header->getPosition() + 40;
    header->handle(NULL, FXSEL(SEL_LEFTBUTTONPRESS, 0), &event);
    event.win_x = header->getWidth();
    header->handle(NULL, FXSEL(SEL_MOTION, 0), &event);
    event.state = 0;
    header->handle(NULL, FXSEL(SEL_LEFTBUTTONRELEASE, 0), &event);
    assert(files->getHeaderId(1) == FileList::ID_COL_SIZE);
    assert(files->getHeaderId(2) == FileList::ID_COL_TYPE);
    assert(!visibleDragPreview(files->getApp()));
    header->resize(headerWidth, header->getHeight());

    files->onToggleColumn(NULL, FXSEL(SEL_COMMAND, FileList::ID_COLUMN_TOGGLE +
                             FileList::ID_COL_TYPE - FileList::ID_COL_NAME), NULL);
    assert(files->getHeaderIndex(FileList::ID_COL_TYPE) == -1);
    files->onToggleColumn(NULL, FXSEL(SEL_COMMAND, FileList::ID_COLUMN_TOGGLE +
                             FileList::ID_COL_TYPE - FileList::ID_COL_NAME), NULL);
    assert(files->getHeaderIndex(FileList::ID_COL_TYPE) >= 0);
    files->moveColumn(0, 1);
    files->onToggleColumn(NULL, FXSEL(SEL_COMMAND, FileList::ID_COLUMN_TOGGLE), NULL);
    assert(files->getHeaderId(0) == FileList::ID_COL_NAME);
    files->killSelection();
    files->setColumnLayout(original);
    puts("PASS: real header drag reorders values and widths; column checklist toggles visibility and protects Name");
}

class ConflictResponder : public FXObject
{
    FXApp& app;
    struct Choice
    {
        FXuint command;
        bool all;
        bool merge;
    };
    std::vector<Choice> choices;
public:
    FXuint answered = 0;
    FXString details;
    explicit ConflictResponder(FXApp& application) : app(application) {}
    ~ConflictResponder() { app.removeTimeout(this, 1); }
    void choose(FXuint command, bool all = false, bool merge = false)
    {
        choices.push_back({command, all, merge});
    }
    void start() { app.addTimeout(this, 1, 10); }
    void collect(FXWindow* window, FXCheckButton*& checkbox, FXButton*& replace)
    {
        if (FXLabel* label = dynamic_cast<FXLabel*>(window)) details += label->getText() + "\n";
        if (FXGroupBox* group = dynamic_cast<FXGroupBox*>(window)) details += group->getText() + "\n";
        if (FXTextField* field = dynamic_cast<FXTextField*>(window)) details += field->getText() + "\n";
        if (FXCheckButton* check = dynamic_cast<FXCheckButton*>(window)) checkbox = check;
        if (FXButton* button = dynamic_cast<FXButton*>(window))
            if (button->getSelector() == OverwriteBox::ID_CLICKED_OVERWRITE) replace = button;
        for (FXWindow* child = window->getFirst(); child; child = child->getNext())
            collect(child, checkbox, replace);
    }
    long handle(FXObject*, FXSelector, void*) override
    {
        OverwriteBox* dialog = dynamic_cast<OverwriteBox*>(app.getModalWindow());
        if (dialog)
        {
            assert(answered < choices.size());
            const Choice& choice = choices[answered++];
            FXCheckButton* checkbox = NULL;
            FXButton* replace = NULL;
            details.clear();
            collect(dialog, checkbox, replace);
            assert(checkbox && replace);
            assert(replace->getText() == (choice.merge ? "Merge" : "Replace"));
            assert(details.contains("Source (incoming)") && details.contains("Destination (existing)"));
            assert(details.contains("Location:") && details.contains("Modified:"));
            assert(!checkbox->getCheck());
            checkbox->setCheck(choice.all);
            dialog->handle(this, FXSEL(SEL_COMMAND, choice.command), NULL);
        }
        else
        {
            assert(!dynamic_cast<MessageBox*>(app.getModalWindow()));
        }
        app.addTimeout(this, 1, 10);
        return 1;
    }
    void done() { assert(answered == choices.size()); app.removeTimeout(this, 1); }
};

static void writeConflictFile(const FXString& path, const char* contents)
{
    std::ofstream file(path.text());
    file << contents;
    assert(file.good());
}

static std::string readConflictFile(const FXString& path)
{
    std::ifstream file(path.text());
    assert(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

class ErrorResponder : public FXObject
{
    FXApp& app;
public:
    FXString text;
    int shown = 0;
    explicit ErrorResponder(FXApp& application) : app(application) {}
    ~ErrorResponder() { app.removeTimeout(this, 1); }
    void start() { app.addTimeout(this, 1, 10); }
    long handle(FXObject*, FXSelector, void*) override
    {
        if (MessageBox* box = dynamic_cast<MessageBox*>(app.getModalWindow()))
        {
            ++shown;
            text = box->getText();
            box->handle(this, FXSEL(SEL_COMMAND, MessageBox::ID_CLICKED_OK), NULL);
        }
        app.addTimeout(this, 1, 10);
        return 1;
    }
};

static void checkCopyVerification(FXApp& app, FXWindow* owner, const FXString& fixture)
{
    const FXString source = fixture + "/verify-source", destination = fixture + "/verify-destination";
    assert(mkdir(source.text(), 0700) == 0 && mkdir(destination.text(), 0700) == 0);
    const FXulong start = xf_getcurrenttime() - 1;
    std::string large(3 * 1024 * 1024 + 17, 'a');
    for (size_t i = 0; i < large.size(); ++i) large[i] = static_cast<char>('a' + i % 23);
    std::ofstream(source.text() + std::string("/large.bin")) << large;
    writeConflictFile(source + "/move.txt", "moved");
    writeConflictFile(source + "/bad-copy.txt", "precious");
    writeConflictFile(source + "/bad-move.txt", "precious");
    writeConflictFile(source + "/unverified.txt", "fast");

    app.reg().writeUnsignedEntry("OPTIONS", "verify_copy", false);
    verifiedCopySyncs = 0;
    corruptVerifiedCopies = 1;
    {
        File operation(owner, "Unverified copy regression", COPY);
        operation.create();
        assert(operation.copy(source + "/unverified.txt", destination, "1 kB", 1024, start) == 1);
    }
    assert(verifiedCopySyncs == 0 && readConflictFile(destination + "/unverified.txt") == "fast");
    corruptVerifiedCopies = 0;

    app.reg().writeUnsignedEntry("OPTIONS", "verify_copy", true);
    {
        File operation(owner, "Verified copy regression", COPY);
        operation.create();
        ErrorResponder errors(app);
        errors.start();
        assert(operation.copy(source + "/large.bin", destination, "3 MB", large.size(), start) == 1);
        crossDeviceSource = source + "/";
        assert(operation.fmove(source + "/move.txt", destination, "1 kB", 1024, start) == 1);
        crossDeviceSource.clear();
        assert(errors.shown == 0);
    }
    assert(verifiedCopySyncs == 2);
    assert(readConflictFile(destination + "/large.bin") == large);
    assert(readConflictFile(destination + "/move.txt") == "moved" && !xf_existfile(source + "/move.txt"));

    {
        File operation(owner, "Failed copy verification regression", COPY);
        operation.create();
        ErrorResponder errors(app);
        errors.start();
        corruptVerifiedCopies = 1;
        assert(operation.copy(source + "/bad-copy.txt", destination, "1 kB", 1024, start) == -2);
        assert(errors.shown == 1 && errors.text.contains("Verification failed"));
        corruptVerifiedCopies = 1;
        crossDeviceSource = source + "/";
        assert(operation.fmove(source + "/bad-move.txt", destination, "1 kB", 1024, start) == -1);
        crossDeviceSource.clear();
        assert(errors.shown == 2 && errors.text.contains("Verification failed"));
    }
    assert(corruptVerifiedCopies == 0);
    assert(readConflictFile(source + "/bad-copy.txt") == "precious" && !xf_existfile(destination + "/bad-copy.txt"));
    assert(readConflictFile(source + "/bad-move.txt") == "precious" && !xf_existfile(destination + "/bad-move.txt"));
    app.reg().writeUnsignedEntry("OPTIONS", "verify_copy", false);
    puts("PASS: copy verification is skipped when disabled; when enabled, copies and cross-device moves are MD5-checked, and a damaged copy is reported, removed and never replaces the move source");
}

static void checkCancelRemovesPartialCopy(FXWindow* owner, const FXString& fixture)
{
    const FXString source = fixture + "/cancel-source", destination = fixture + "/cancel-destination";
    assert(mkdir(source.text(), 0700) == 0 && mkdir(destination.text(), 0700) == 0);
    assert(mkdir((source + "/folder").text(), 0700) == 0);
    const FXulong start = xf_getcurrenttime() - 1;
    std::string large(3 * 1024 * 1024 + 17, 'c');
    for (size_t i = 0; i < large.size(); ++i) large[i] = static_cast<char>('a' + i % 19);
    for (const char* name : {"/copy.bin", "/move.bin", "/folder/big.bin"})
        std::ofstream(source.text() + std::string(name)) << large;
    writeConflictFile(source + "/folder/small.txt", "small");

    struct Case { const char* path; int op; };
    for (const Case& test : {Case{"/copy.bin", COPY}, Case{"/move.bin", MOVE}, Case{"/folder/big.bin", MOVE}})
    {
        File operation(owner, "Cancel partial copy regression", test.op);
        operation.create();
        cancelReadOperation = &operation;
        cancelReadPath = source + test.path;
        cancelReadBlocks = 2;
        if (test.op == COPY)
            assert(operation.copy(source + test.path, destination, "3 MB", large.size(), start) == 0);
        else
        {
            crossDeviceSource = source + "/";
            const FXString item = FXString(test.path).contains("/folder/") ? source + "/folder" : source + test.path;
            assert(operation.fmove(item, destination, "3 MB", large.size(), start) == 0);
            crossDeviceSource.clear();
        }
        assert(!cancelReadOperation);
        assert(!xf_existfile(destination + test.path));
        assert(readConflictFile(source + test.path) == large);
    }
    // Files finished before Cancel stay moved; the rest, including the source folder, stay put
    assert(xf_existfile(source + "/folder/small.txt") != xf_existfile(destination + "/folder/small.txt"));
    assert(xf_isdirectory(source + "/folder"));
    puts("PASS: cancelling a copy or a cross-device file/folder move removes the half-copied target file and keeps the original");
}

static void checkFileConflicts(FXApp& app, FXWindow* owner, const FXString& fixture)
{
    const FXString root = fixture + "/conflicts";
    assert(mkdir(root.text(), 0700) == 0);
    const FXString source = root + "/source", destination = root + "/destination";
    assert(mkdir(source.text(), 0700) == 0 && mkdir(destination.text(), 0700) == 0);
    const FXulong start = xf_getcurrenttime() - 1;
    const char* names[] = {"report.txt", "backup.tar.gz", ".hidden"};
    for (const char* name : names)
    {
        writeConflictFile(source + "/" + name, "incoming");
        writeConflictFile(destination + "/" + name, "existing");
    }
    writeConflictFile(destination + "/report (2).txt", "numbered");
    assert(::symlink("missing", (destination + "/report (1).txt").text()) == 0);
    app.reg().writeUnsignedEntry("OPTIONS", "confirm_overwrite", true);
    {
        File operation(owner, "Conflict regression", COPY, 3);
        operation.create();
        ConflictResponder answer(app);
        answer.choose(OverwriteBox::ID_CLICKED_RENAME, true);
        answer.start();
        for (const char* name : names)
            assert(operation.copy(source + "/" + name, destination, "1 kB", 1024, start) == 1);
        answer.done();
        assert(readConflictFile(destination + "/report (3).txt") == "incoming");
        assert(readConflictFile(destination + "/report (2).txt") == "numbered");
        assert(readConflictFile(destination + "/backup (1).tar.gz") == "incoming");
        assert(readConflictFile(destination + "/.hidden (1)") == "incoming");
        for (const char* name : names) assert(readConflictFile(destination + "/" + name) == "existing");
        assert(answer.details.contains("8 bytes") && answer.details.contains("report.txt"));
        assert(answer.details.contains(source) && answer.details.contains(destination));
    }
    {
        File operation(owner, "Conflict regression", COPY, 3);
        operation.create();
        ConflictResponder answer(app);
        answer.choose(OverwriteBox::ID_CLICKED_OVERWRITE);
        answer.choose(OverwriteBox::ID_CLICKED_SKIP);
        answer.choose(OverwriteBox::ID_CLICKED_CANCEL);
        answer.start();
        assert(operation.copy(source + "/report.txt", destination, "1 kB", 1024, start) == 1);
        assert(operation.copy(source + "/backup.tar.gz", destination, "1 kB", 1024, start) == 1);
        assert(operation.copy(source + "/.hidden", destination, "1 kB", 1024, start) == 0);
        answer.done();
        assert(operation.isCancelled());
        assert(readConflictFile(destination + "/report.txt") == "incoming");
        assert(readConflictFile(destination + "/backup.tar.gz") == "existing");
        assert(readConflictFile(destination + "/.hidden") == "existing");
    }
    puts("PASS: file conflicts show source/destination metadata; numbered Rename keeps both, applies to all, skips occupied names and resets per operation; Replace, Skip and Cancel work");

    for (FXint mode = 0; mode < 5; ++mode)
    {
        const FXbool moving = mode == 1 || mode == 2 || mode == 4;
        const FXString from = root + "/from" + FXStringVal(mode), to = root + "/to" + FXStringVal(mode);
        for (const FXString& path : {from, to, from + "/tree", to + "/tree",
                                    from + "/tree/nested", to + "/tree/nested"})
            assert(mkdir(path.text(), 0700) == 0);
        writeConflictFile(from + "/tree/nested/conflict.txt", "incoming");
        writeConflictFile(to + "/tree/nested/conflict.txt", "existing");
        writeConflictFile(from + "/tree/nested/another.txt", "incoming");
        writeConflictFile(to + "/tree/nested/another.txt", "existing");
        writeConflictFile(from + "/tree/new.txt", "new");
        writeConflictFile(from + "/tree/nested/new.txt", "nested");
        writeConflictFile(to + "/tree/only-here.txt", "keep");
        File operation(owner, "Merge regression", moving ? MOVE : COPY);
        operation.create();
        ConflictResponder answer(app);
        answer.choose(OverwriteBox::ID_CLICKED_OVERWRITE, true, true);
        answer.choose(mode == 4 ? OverwriteBox::ID_CLICKED_CANCEL :
                      mode == 3 ? OverwriteBox::ID_CLICKED_OVERWRITE : OverwriteBox::ID_CLICKED_SKIP,
                      mode != 4);
        answer.start();
        crossDeviceRenames = 0;
        if (mode == 2) crossDeviceSource = from + "/";
        assert((moving ? operation.fmove(from + "/tree", to, "1 kB", 1024, start) :
                         operation.copy(from + "/tree", to, "1 kB", 1024, start)) == (mode == 4 ? 0 : 1));
        answer.done();
        if (mode == 2) assert(crossDeviceRenames > 0);
        crossDeviceSource.clear();
        assert(readConflictFile(from + "/tree/nested/conflict.txt") == "incoming");
        assert(readConflictFile(from + "/tree/nested/another.txt") == "incoming");
        assert(readConflictFile(to + "/tree/nested/conflict.txt") == (mode == 3 ? "incoming" : "existing"));
        assert(readConflictFile(to + "/tree/nested/another.txt") == (mode == 3 ? "incoming" : "existing"));
        assert(readConflictFile(to + "/tree/only-here.txt") == "keep");
        if (mode == 4)
        {
            assert(operation.isCancelled());
            continue;
        }
        assert(readConflictFile(to + "/tree/new.txt") == "new");
        assert(readConflictFile(to + "/tree/nested/new.txt") == "nested");
        assert(xf_existfile(from + "/tree/new.txt") == !moving);
        assert(xf_existfile(from + "/tree/nested/new.txt") == !moving);
    }
    puts("PASS: copy, same-drive move and cross-device move separate Merge All from file Replace/Skip All; skipped sources and unrelated destinations survive; nested Cancel stops safely");
    {
        writeConflictFile(source + "/link.txt", "incoming");
        writeConflictFile(destination + "/untouched.txt", "external");
        assert(::symlink("untouched.txt", (destination + "/link.txt").text()) == 0);
        File operation(owner, "Link conflict regression", COPY);
        operation.create();
        ConflictResponder answer(app);
        answer.choose(OverwriteBox::ID_CLICKED_OVERWRITE);
        answer.start();
        assert(operation.copy(source + "/link.txt", destination, "1 kB", 1024, start) == 1);
        answer.done();
        assert(readConflictFile(destination + "/link.txt") == "incoming");
        assert(readConflictFile(destination + "/untouched.txt") == "external");
    }
    {
        File operation(owner, "Numbered move regression", MOVE, 3);
        operation.create();
        ConflictResponder answer(app);
        answer.choose(OverwriteBox::ID_CLICKED_RENAME, true);
        answer.choose(OverwriteBox::ID_CLICKED_RENAME, true, true);
        answer.start();
        crossDeviceSource = source + "/";
        assert(operation.fmove(source + "/report.txt", destination, "1 kB", 1024, start) == 1);
        assert(operation.fmove(source + "/backup.tar.gz", destination, "1 kB", 1024, start) == 1);
        assert(mkdir((source + "/folder").text(), 0700) == 0);
        assert(mkdir((destination + "/folder").text(), 0700) == 0);
        writeConflictFile(source + "/folder/new.txt", "moved");
        writeConflictFile(destination + "/folder/old.txt", "keep");
        assert(operation.fmove(source + "/folder", destination, "1 kB", 1024, start) == 1);
        crossDeviceSource.clear();
        answer.done();
        assert(!xf_existfile(source + "/report.txt") && !xf_existfile(source + "/backup.tar.gz"));
        assert(readConflictFile(destination + "/report (4).txt") == "incoming");
        assert(readConflictFile(destination + "/backup (2).tar.gz") == "incoming");
        assert(!xf_existfile(source + "/folder"));
        assert(readConflictFile(destination + "/folder (1)/new.txt") == "moved");
        assert(readConflictFile(destination + "/folder/old.txt") == "keep");
    }
    puts("PASS: replacing a symbolic link leaves its target untouched; numbered file/folder moves keep both and honor renamed destinations across devices");
    for (FXuint type : {RENAME, SYMLINK})
    {
        const FXString name = type == RENAME ? "rename.txt" : "linked.txt";
        const FXString numbered = type == RENAME ? "rename (1).txt" : "linked (1).txt";
        writeConflictFile(source + "/" + name, "incoming");
        writeConflictFile(destination + "/" + name, "existing");
        File operation(owner, "Related conflict regression", type);
        operation.create();
        ConflictResponder answer(app);
        answer.choose(OverwriteBox::ID_CLICKED_RENAME);
        answer.start();
        assert((type == RENAME ? operation.rename(source + "/" + name, destination + "/" + name) :
                               operation.symlink(source + "/" + name, destination)) == 1);
        answer.done();
        assert(readConflictFile(destination + "/" + name) == "existing");
        assert(readConflictFile(destination + "/" + numbered) == "incoming");
        assert(xf_existfile(source + "/" + name) == (type == SYMLINK));
        if (type == SYMLINK)
        {
            struct stat info;
            assert(lstat((destination + "/" + numbered).text(), &info) == 0 && S_ISLNK(info.st_mode));
        }
    }
    {
        app.reg().writeUnsignedEntry("OPTIONS", "confirm_overwrite", false);
        File operation(owner, "Confirmation preference regression", COPY);
        operation.create();
        ConflictResponder answer(app);
        answer.start();
        assert(operation.copy(source + "/linked.txt", destination, "1 kB", 1024, start) == 1);
        answer.done();
        assert(readConflictFile(destination + "/linked.txt") == "incoming");
        app.reg().writeUnsignedEntry("OPTIONS", "confirm_overwrite", true);
    }
    puts("PASS: rename and link commands honor numbered conflict destinations; the existing overwrite-confirmation preference is preserved");
}

static void pumpTransferEvents(FXApp& app, FXTime duration)
{
    FXTime until = FXThread::time() + duration;
    while (FXThread::time() < until)
    {
        app.runOneEvent(false);
        usleep(1000);
    }
}

static Window findTransferWindowUnder(Display* display, Window parent, const char* title, int depth)
{
    char* name = NULL;
    XWindowAttributes attributes;
    if (XFetchName(display, parent, &name) && name)
    {
        bool matches = strcmp(name, title) == 0;
        XFree(name);
        if (matches && XGetWindowAttributes(display, parent, &attributes) && attributes.map_state == IsViewable)
            return parent;
    }
    if (!depth) return 0;
    Window root, actualParent, *children = NULL, found = 0;
    unsigned int count = 0;
    if (XQueryTree(display, parent, &root, &actualParent, &children, &count))
    {
        for (unsigned int i = 0; i < count && !found; ++i)
            found = findTransferWindowUnder(display, children[i], title, depth - 1);
        if (children) XFree(children);
    }
    return found;
}

static XErrorHandler previousTransferXError = NULL;

static int handleTransferXError(Display* display, XErrorEvent* error)
{
    if (error->error_code == BadWindow &&
        (error->request_code == X_GetProperty || error->request_code == X_QueryTree ||
         error->request_code == X_GetWindowAttributes))
        return 0;
    if (previousTransferXError) return previousTransferXError(display, error);
    fprintf(stderr, "Unexpected X error %d during transfer window lookup\n", error->error_code);
    std::abort();
}

static Window findTransferWindow(Display* display, Window root, const char* title)
{
    XSync(display, False);
    previousTransferXError = XSetErrorHandler(handleTransferXError);
    XGrabServer(display);
    Window found = findTransferWindowUnder(display, root, title, 2);
    XUngrabServer(display);
    XSync(display, False);
    XSetErrorHandler(previousTransferXError);
    return found;
}

static Window waitTransferWindow(FXApp& app, const char* title, bool visible, FXTime timeout)
{
    Display* display = static_cast<Display*>(app.getDisplay());
    FXTime until = FXThread::time() + timeout;
    Window window = 0;
    while (FXThread::time() < until)
    {
        window = findTransferWindow(display, DefaultRootWindow(display), title);
        if ((window != 0) == visible) return window;
        app.runOneEvent(false);
        usleep(1000);
    }
    if ((window != 0) != visible)
        fprintf(stderr, "Timed out waiting for window '%s' to become %s\n", title, visible ? "visible" : "hidden");
    assert((window != 0) == visible);
    return window;
}

static void clickTransferWindow(FXApp& app, Window window, FXint x, FXint y)
{
    typedef int (*FakeButton)(Display*, unsigned int, Bool, unsigned long);
    static void* library = dlopen("libXtst.so.6", RTLD_NOW | RTLD_LOCAL);
    assert(library);
    static FakeButton fakeButton = reinterpret_cast<FakeButton>(dlsym(library, "XTestFakeButtonEvent"));
    assert(fakeButton);
    Display* display = static_cast<Display*>(app.getDisplay());
    XRaiseWindow(display, window);
    XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);
    XWarpPointer(display, None, window, 0, 0, 0, 0, x, y);
    XSync(display, False);
    assert(fakeButton(display, 1, True, CurrentTime));
    XFlush(display);
    usleep(300000);
    assert(fakeButton(display, 1, False, CurrentTime));
    XSync(display, False);
}

static void captureTransferWindow(Window window, const char* name)
{
    const char* directory = getenv("SWORDFISH_TEST_TRANSFER_SCREENSHOTS");
    if (!directory) return;
    Display* display = XOpenDisplay(NULL);
    assert(display);
    if (window != DefaultRootWindow(display)) XRaiseWindow(display, window);
    XSync(display, False);
    XCloseDisplay(display);
    FXString id, path = FXString(directory) + "/" + name + ".png";
    id.format("0x%lx", window);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        execlp("import", "import", "-window", id.text(), path.text(), static_cast<char*>(NULL));
        std::_Exit(127);
    }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void captureAlternateTransferWidth(FXApp& app, Window window, const char* name,
                                          bool restore = true, FXint width = 0)
{
    if (!getenv("SWORDFISH_TEST_TRANSFER_SCREENSHOTS")) return;
    Display* display = static_cast<Display*>(app.getDisplay());
    XWindowAttributes geometry;
    assert(XGetWindowAttributes(display, window, &geometry));
    XResizeWindow(display, window, width ? width : geometry.width < 400 ? 500 : 320, geometry.height);
    XSync(display, False);
    pumpTransferEvents(app, 400000000LL);
    captureTransferWindow(window, name);
    if (restore)
    {
        XResizeWindow(display, window, geometry.width, geometry.height);
        XSync(display, False);
        pumpTransferEvents(app, 350000000LL);
    }
}

static void acknowledgeTransferFailure(FXApp& app, const char* title, const char* progressTitle,
                                       const char* screenshot = NULL)
{
    Display* display = static_cast<Display*>(app.getDisplay());
    XWindowAttributes geometry;
    Window error = waitTransferWindow(app, title, true, 8000000000LL);
    if (screenshot)
    {
        captureTransferWindow(error, screenshot);
        FXString alternate = FXString(screenshot) + "-narrow";
        captureAlternateTransferWidth(app, error, alternate.text(), true, 320);
    }
    assert(XGetWindowAttributes(display, error, &geometry));
    clickTransferWindow(app, error, geometry.width / 2, geometry.height - 18);
    waitTransferWindow(app, title, false, 3000000000LL);
    pumpTransferEvents(app, 250000000LL);
    Window progress = waitTransferWindow(app, progressTitle, true, 3000000000LL);
    assert(XGetWindowAttributes(display, progress, &geometry));
    clickTransferWindow(app, progress, geometry.width / 2, geometry.height - 32);
    waitTransferWindow(app, progressTitle, false, 3000000000LL);
}

static off_t transferFileSize(const FXString& path)
{
    struct stat info;
    return stat(path.text(), &info) == 0 ? info.st_size : -1;
}

static FXint transferItemIndex(FileList* list, const FXString& name)
{
    for (FXint i = 0; i < list->getNumItems(); ++i)
        if (list->getItemFilename(i) == name) return i;
    return -1;
}

static void makeSlowTransferFile(const FXString& path, off_t size)
{
    int fd = open(path.text(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
    assert(fd >= 0);
    assert(ftruncate(fd, size) == 0);
    assert(close(fd) == 0);
}

static void waitTransferSize(FXApp& app, const FXString& path, off_t size, FXTime timeout)
{
    FXTime until = FXThread::time() + timeout;
    while (transferFileSize(path) != size && FXThread::time() < until)
    {
        app.runOneEvent(false);
        usleep(1000);
    }
    assert(transferFileSize(path) == size);
}

static char transferProcessState(pid_t pid)
{
    FXString path;
    path.format("/proc/%ld/stat", static_cast<long>(pid));
    std::ifstream status(path.text());
    long number;
    std::string command;
    char state = 0;
    status >> number >> command >> state;
    return state;
}

static int lockTransferPath(const FXString& path)
{
    FXString directory;
    directory.format("/tmp/swordfish-transfer-%lu", static_cast<unsigned long>(getuid()));
    assert(mkdir(directory.text(), 0700) == 0 || errno == EEXIST);
    FXString canonical = xf_realpath(FXPath::absolute(path));
    gchar* digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, canonical.text(), canonical.length());
    FXString lock = directory + "/" + digest;
    g_free(digest);
    int fd = open(lock.text(), O_CREAT | O_RDWR, 0600);
    assert(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0);
    return fd;
}

class WaitingTransferProbe : public File
{
public:
    explicit WaitingTransferProbe(FXApp* app) : File(app, "Queued transfer regression", COPY) {}
    FXString sourceText() const { return uplabel->getText(); }
    FXString dataText() const { return datalabel->getText(); }
    FXString timeText() const { return timelabel->getText(); }
};

class MovingTransferProbe : public File
{
public:
    explicit MovingTransferProbe(FXApp* app) : File(app, "Move folder regression", MOVE) {}
    int moveDirectory(const FXString& source, const FXString& target)
    {
        struct stat info;
        assert(lstat(source.text(), &info) == 0);
        return copydir(source, target, "0 bytes", 0, xf_getcurrenttime(), info, NULL, true, true);
    }
};

class UnexpectedTransferStop : public FXObject
{
    FXApp& app;
public:
    explicit UnexpectedTransferStop(FXApp& application) : app(application) { app.addTimeout(this, 1, 10); }
    ~UnexpectedTransferStop() { app.removeTimeout(this, 1); }
    long handle(FXObject*, FXSelector, void*) override
    {
        MessageBox* dialog = dynamic_cast<MessageBox*>(app.getModalWindow());
        assert(!dialog || dialog->getTitle() != "Transfer stopped");
        app.addTimeout(this, 1, 10);
        return 1;
    }
};

static void checkIndependentTransfers(FXApp& app, XFileExplorer* explorer, const FXString& prefix,
                                      const FXString& fixture)
{
    UnexpectedTransferStop unexpectedStop(app);
    {
        WaitingTransferProbe waiting(&app);
        waiting.create();
        waiting.setIndependentPaths("/queued-source", "/queued-target");
        waiting.startIndependent();
        pumpTransferEvents(app, 700000000LL);
        assert(waiting.sourceText().find("/queued-source") >= 0);
        FXulong scanned = 0;
        waiting.sourcesize(std::vector<FXString>{fixture + "/data/match.txt"}, &scanned);
        assert(scanned > 0 && waiting.sourceText().find("/queued-source") >= 0);
        assert(waiting.dataText().find("Computing the size") >= 0);
        waiting.setTotalSourceSize(1024);
        waiting.onSourceSizeRefresh(NULL, 0, NULL);
        assert(waiting.sourceText().find("/queued-source") >= 0);
        assert(waiting.dataText().find("Computing the size") >= 0);
        waiting.beginTransfer();
        assert(waiting.dataText().find("Copied data:") >= 0 && waiting.dataText().find("/") >= 0);
        waiting.onCmdPause(NULL, 0, NULL);
        for (int tick = 0; tick < 25; ++tick) waiting.onCopySpeed(NULL, 0, NULL);
        assert(waiting.timeText().find("--") >= 0 && waiting.dataText().find("/s") < 0);
        waiting.onCmdPause(NULL, 0, NULL);
        waiting.hide();
    }
    {
        FXString sourceFolder = fixture + "/late-source-folder";
        FXString targetFolder = fixture + "/late-target-folder";
        assert(mkdir(sourceFolder.text(), 0700) == 0);
        writeConflictFile(sourceFolder + "/original.txt", "original");
        MovingTransferProbe moving(&app);
        moving.create();
        moving.setIndependentPaths(sourceFolder, targetFolder);
        moving.startIndependent();
        setenv("SWORDFISH_TEST_LATE_SOURCE", sourceFolder.text(), 1);
        assert(moving.moveDirectory(sourceFolder, targetFolder) == true);
        assert(moving.hasSkipped() && readConflictFile(sourceFolder + "/added-after-scan.txt") == "late");
        assert(readConflictFile(targetFolder + "/original.txt") == "original");
        moving.hide();
    }
    puts("PASS: items added after a moving folder was scanned remain at the source and are reported as skipped");

    if (getenv("SWORDFISH_TEST_LIGHT_PROGRESS"))
    {
        app.reg().writeColorEntry("SETTINGS", "basecolor", FXRGB(230, 232, 235));
        app.reg().writeColorEntry("SETTINGS", "bordercolor", FXRGB(120, 120, 120));
        app.reg().writeColorEntry("SETTINGS", "backcolor", FXRGB(248, 248, 248));
        app.reg().writeColorEntry("SETTINGS", "forecolor", FXRGB(20, 20, 20));
        app.reg().writeColorEntry("SETTINGS", "selbackcolor", FXRGB(55, 100, 160));
        app.reg().writeColorEntry("SETTINGS", "selforecolor", FXRGB(255, 255, 255));
    }
    const FXString source = fixture + "/transfer source";
    const FXString target = fixture + "/transfer target";
    const FXString inactive = fixture + "/inactive target";
    assert(mkdir(source.text(), 0700) == 0);
    assert(mkdir(target.text(), 0700) == 0);
    assert(mkdir(inactive.text(), 0700) == 0);
    writeConflictFile(source + "/m-middle.txt", "middle");
    writeConflictFile(target + "/a-first.txt", "first");
    writeConflictFile(target + "/z-last.txt", "last");
    const FXString slow = source + "/slow.bin";
    const off_t slowSize = 24 * 1024 * 1024;
    makeSlowTransferFile(slow, slowSize);

    FilePanel* left = explorer->getLeftPanel();
    FilePanel* right = explorer->getRightPanel();
    TabButtons::State leftState = left->getTabButtons()->saveState();
    TabButtons::State rightState = right->getTabButtons()->saveState();
    FileList* leftList = left->getList();
    FileList* rightList = right->getList();
    IconListSortFunc leftSort = leftList->getSortFunc();
    IconListSortFunc rightSort = rightList->getSortFunc();
    left->setDirectory(target);
    right->setDirectory(target);
    leftList->setSortFunc(FileList::ascending);
    rightList->setSortFunc(FileList::descending);
    leftList->scan(true);
    rightList->scan(true);

    setenv("SWORDFISH_TEST_SLOW_FILE", slow.text(), 1);
    std::vector<TransferItem> items;
    items.push_back(TransferItem(source + "/m-middle.txt", target));
    items.push_back(TransferItem(slow, target));
    FXTime started = FXThread::time();
    assert(xf_startTransfer(explorer, COPY, items));
    assert(FXThread::time() - started < 3000000000LL);
    Window progress = waitTransferWindow(app, "Copy Files", true, 3000000000LL);
    Display* display = static_cast<Display*>(app.getDisplay());
    Window transient = None;
    assert(!XGetTransientForHint(display, progress, &transient) || transient == None);
    FXTime refreshDeadline = FXThread::time() + 3000000000LL;
    while ((transferItemIndex(leftList, "m-middle.txt") < 0 ||
            transferItemIndex(rightList, "m-middle.txt") < 0) && FXThread::time() < refreshDeadline)
    {
        app.runOneEvent(false);
        usleep(1000);
    }
    assert(transferFileSize(target + "/slow.bin") < slowSize);
    assert(transferItemIndex(leftList, "a-first.txt") < transferItemIndex(leftList, "m-middle.txt"));
    assert(transferItemIndex(leftList, "m-middle.txt") < transferItemIndex(leftList, "z-last.txt"));
    assert(transferItemIndex(rightList, "z-last.txt") < transferItemIndex(rightList, "m-middle.txt"));
    assert(transferItemIndex(rightList, "m-middle.txt") < transferItemIndex(rightList, "a-first.txt"));

    FXTime writeDeadline = FXThread::time() + 3000000000LL;
    while (transferFileSize(target + "/slow.bin") <= 0 && FXThread::time() < writeDeadline)
        pumpTransferEvents(app, 10000000LL);
    assert(transferFileSize(target + "/slow.bin") > 0);
    XWindowAttributes geometry;
    assert(XGetWindowAttributes(display, progress, &geometry));
    captureTransferWindow(progress, "copy-running");
    XResizeWindow(display, progress, 320, geometry.height);
    XSync(display, False);
    pumpTransferEvents(app, 600000000LL);
    assert(XGetWindowAttributes(display, progress, &geometry));
    captureTransferWindow(progress, "copy-narrow");
    clickTransferWindow(app, progress, geometry.width / 2 - 45, geometry.height - 32);
    pumpTransferEvents(app, 600000000LL);
    off_t pausedSize = transferFileSize(target + "/slow.bin");
    pumpTransferEvents(app, 800000000LL);
    captureTransferWindow(progress, "copy-paused");
    captureAlternateTransferWidth(app, progress, "copy-paused-wide");
    if (pausedSize >= slowSize || transferFileSize(target + "/slow.bin") != pausedSize)
        fprintf(stderr, "Pause did not stop copying: window %dx%d, sizes %lld and %lld of %lld\n",
                geometry.width, geometry.height, (long long)pausedSize,
                (long long)transferFileSize(target + "/slow.bin"), (long long)slowSize);
    assert(pausedSize < slowSize && transferFileSize(target + "/slow.bin") == pausedSize);
    clickTransferWindow(app, progress, geometry.width / 2 - 45, geometry.height - 32);
    waitTransferSize(app, target + "/slow.bin", slowSize, 12000000000LL);
    FXTime completion = FXThread::time();
    pumpTransferEvents(app, 400000000LL);
    captureTransferWindow(progress, "copy-complete");
    captureAlternateTransferWidth(app, progress, "copy-complete-wide");
    while (FXThread::time() < completion + 2400000000LL) pumpTransferEvents(app, 10000000LL);
    assert(findTransferWindow(display, DefaultRootWindow(display), "Copy Files"));
    waitTransferWindow(app, "Copy Files", false, 3000000000LL);
    assert(FXThread::time() - completion >= 2700000000LL &&
           FXThread::time() - completion < 4500000000LL);
    unsetenv("SWORDFISH_TEST_SLOW_FILE");
    puts("PASS: independent copies refresh both panes in their existing sort order while running; pause/resume works and completion stays visible for three seconds");

    const FXString unusual = source + "/quote' percent% and\nnewline.txt";
    writeConflictFile(unusual, "unusual filename");
    TabButtons* tabs = right->getTabButtons();
    FXuint previousTab = tabs->getCurrentTab();
    tabs->addTab(inactive);
    FXuint inactiveTab = tabs->getCurrentTab();
    tabs->setCurrentTab(previousTab);
    FXString uris = FXURL::encode(xf_filetouri(unusual)) + "\r\n";
    assert(xf_startDroppedTransfer(explorer, COPY, uris, inactive, false));
    waitTransferWindow(app, "Copy Files", true, 3000000000LL);
    waitTransferSize(app, inactive + "/" + FXPath::name(unusual), 16, 5000000000LL);
    tabs->setCurrentTab(inactiveTab);
    assert(transferItemIndex(rightList, FXPath::name(unusual)) >= 0);
    waitTransferWindow(app, "Copy Files", false, 5000000000LL);
    puts("PASS: URI drops preserve newline, quote and percent in filenames and refresh an inactive destination tab when activated");

    const FXString moveSource = source + "/cross-device.txt";
    writeConflictFile(moveSource, "move safely");
    setenv("SWORDFISH_TEST_CROSS_DEVICE", moveSource.text(), 1);
    assert(xf_startTransfer(explorer, MOVE, {TransferItem(moveSource, target)}));
    FXTime moveDeadline = FXThread::time() + 5000000000LL;
    while ((FXStat::exists(moveSource) || transferFileSize(target + "/cross-device.txt") != 11) &&
           FXThread::time() < moveDeadline)
        pumpTransferEvents(app, 10000000LL);
    assert(!FXStat::exists(moveSource));
    assert(readConflictFile(target + "/cross-device.txt") == "move safely");
    FXTime moveCompletion = FXThread::time();
    Window moveProgress = waitTransferWindow(app, "Move Files", true, 3000000000LL);
    pumpTransferEvents(app, 400000000LL);
    captureTransferWindow(moveProgress, "move-complete");
    captureAlternateTransferWidth(app, moveProgress, "move-complete-narrow");
    while (FXThread::time() < moveCompletion + 2400000000LL) pumpTransferEvents(app, 10000000LL);
    assert(findTransferWindow(display, DefaultRootWindow(display), "Move Files"));
    waitTransferWindow(app, "Move Files", false, 3000000000LL);
    assert(FXThread::time() - moveCompletion >= 2700000000LL &&
           FXThread::time() - moveCompletion < 4500000000LL);
    unsetenv("SWORDFISH_TEST_CROSS_DEVICE");
    puts("PASS: cross-device moves copy before removing the source and show completion for three seconds");

    const FXString hardLinkA = source + "/hard-link-a.txt";
    const FXString hardLinkB = source + "/hard-link-b.txt";
    writeConflictFile(hardLinkA, "linked");
    assert(link(hardLinkA.text(), hardLinkB.text()) == 0);
    assert(xf_startTransfer(explorer, MOVE, {TransferItem(hardLinkA, target), TransferItem(hardLinkB, target)}));
    waitTransferWindow(app, "Move Files", true, 3000000000LL);
    waitTransferSize(app, target + "/hard-link-a.txt", 6, 5000000000LL);
    waitTransferSize(app, target + "/hard-link-b.txt", 6, 5000000000LL);
    waitTransferWindow(app, "Move Files", false, 5000000000LL);
    struct stat movedA, movedB;
    assert(!FXStat::exists(hardLinkA) && !FXStat::exists(hardLinkB));
    assert(stat((target + "/hard-link-a.txt").text(), &movedA) == 0 &&
           stat((target + "/hard-link-b.txt").text(), &movedB) == 0 &&
           movedA.st_dev == movedB.st_dev && movedA.st_ino == movedB.st_ino);
    const FXString crossLinkA = source + "/cross-link-a.txt";
    const FXString crossLinkB = source + "/cross-link-b.txt";
    writeConflictFile(crossLinkA, "linked");
    assert(link(crossLinkA.text(), crossLinkB.text()) == 0);
    setenv("SWORDFISH_TEST_CROSS_DEVICE", crossLinkA.text(), 1);
    assert(xf_startTransfer(explorer, MOVE, {TransferItem(crossLinkA, target), TransferItem(crossLinkB, target)}));
    waitTransferWindow(app, "Move Files", true, 3000000000LL);
    waitTransferSize(app, target + "/cross-link-a.txt", 6, 5000000000LL);
    waitTransferSize(app, target + "/cross-link-b.txt", 6, 5000000000LL);
    waitTransferWindow(app, "Move Files", false, 5000000000LL);
    assert(!FXStat::exists(crossLinkA) && !FXStat::exists(crossLinkB));
    unsetenv("SWORDFISH_TEST_CROSS_DEVICE");
    puts("PASS: selected hard links move together without treating the first rename as an external change");

    const FXString changingMove = source + "/changing-during-move.bin";
    const off_t changingSize = 8 * 1024 * 1024;
    makeSlowTransferFile(changingMove, changingSize);
    setenv("SWORDFISH_TEST_SLOW_FILE", changingMove.text(), 1);
    setenv("SWORDFISH_TEST_CROSS_DEVICE", changingMove.text(), 1);
    assert(xf_startTransfer(explorer, MOVE, {TransferItem(changingMove, target)}));
    waitTransferWindow(app, "Move Files", true, 3000000000LL);
    FXTime changingDeadline = FXThread::time() + 5000000000LL;
    while (transferFileSize(target + "/changing-during-move.bin") <= 0 && FXThread::time() < changingDeadline)
        pumpTransferEvents(app, 10000000LL);
    assert(transferFileSize(target + "/changing-during-move.bin") > 0);
    int changingFd = open(changingMove.text(), O_WRONLY | O_APPEND);
    assert(changingFd >= 0 && write(changingFd, "new", 3) == 3 && close(changingFd) == 0);
    acknowledgeTransferFailure(app, "Move source changed", "Move Files", "move-source-modified");
    assert(transferFileSize(changingMove) == changingSize + 3);
    assert(transferFileSize(target + "/changing-during-move.bin") >= changingSize);
    unsetenv("SWORDFISH_TEST_SLOW_FILE");
    unsetenv("SWORDFISH_TEST_CROSS_DEVICE");
    puts("PASS: a cross-device move preserves and reports a source modified during copying");

    const FXString modifiedLinkA = source + "/modified-link-a.txt";
    const FXString modifiedLinkB = source + "/modified-link-b.txt";
    const FXString interveningMove = source + "/intervening-move.bin";
    writeConflictFile(modifiedLinkA, "linked");
    assert(chmod(modifiedLinkA.text(), 0644) == 0);
    assert(link(modifiedLinkA.text(), modifiedLinkB.text()) == 0);
    makeSlowTransferFile(interveningMove, changingSize);
    setenv("SWORDFISH_TEST_SLOW_FILE", interveningMove.text(), 1);
    setenv("SWORDFISH_TEST_CROSS_DEVICE", interveningMove.text(), 1);
    assert(xf_startTransfer(explorer, MOVE, {TransferItem(modifiedLinkA, target),
                                             TransferItem(interveningMove, target),
                                             TransferItem(modifiedLinkB, target)}));
    waitTransferWindow(app, "Move Files", true, 3000000000LL);
    FXTime interveningDeadline = FXThread::time() + 5000000000LL;
    while (transferFileSize(target + "/intervening-move.bin") <= 0 && FXThread::time() < interveningDeadline)
        pumpTransferEvents(app, 10000000LL);
    assert(transferFileSize(target + "/intervening-move.bin") > 0);
    assert(chmod(modifiedLinkB.text(), 0600) == 0);
    acknowledgeTransferFailure(app, "Transfer source changed", "Move Files");
    struct stat modifiedLinkInfo;
    assert(stat(modifiedLinkB.text(), &modifiedLinkInfo) == 0 && (modifiedLinkInfo.st_mode & 0777) == 0600);
    assert(readConflictFile(modifiedLinkB) == "linked" &&
           !FXStat::exists(target + "/modified-link-b.txt"));
    unsetenv("SWORDFISH_TEST_SLOW_FILE");
    unsetenv("SWORDFISH_TEST_CROSS_DEVICE");
    puts("PASS: moving one hard link does not hide later metadata changes to another selected link");

    const FXString blockedParent = fixture + "/locked parent";
    const FXString blockedTarget = blockedParent + "/nested";
    assert(mkdir(blockedParent.text(), 0700) == 0);
    assert(mkdir(blockedTarget.text(), 0700) == 0);
    writeConflictFile(source + "/blocked.txt", "blocked");
    writeConflictFile(source + "/unrelated.txt", "unrelated");
    int lock = lockTransferPath(blockedParent);
    assert(xf_startTransfer(explorer, COPY, {TransferItem(source + "/blocked.txt", blockedTarget)}));
    progress = waitTransferWindow(app, "Copy Files", true, 3000000000LL);
    pumpTransferEvents(app, 400000000LL);
    captureTransferWindow(progress, "copy-waiting");
    captureAlternateTransferWidth(app, progress, "copy-waiting-narrow");
    assert(!FXStat::exists(blockedTarget + "/blocked.txt"));
    writeConflictFile(blockedTarget + "/created-while-waiting.txt", "unrelated");
    assert(xf_startTransfer(explorer, COPY, {TransferItem(source + "/unrelated.txt", target)}));
    waitTransferSize(app, target + "/unrelated.txt", 9, 5000000000LL);
    assert(!FXStat::exists(blockedTarget + "/blocked.txt"));
    assert(flock(lock, LOCK_UN) == 0 && close(lock) == 0);
    waitTransferSize(app, blockedTarget + "/blocked.txt", 7, 5000000000LL);
    waitTransferWindow(app, "Copy Files", false, 5000000000LL);
    puts("PASS: transfers wait for locked ancestor folders while unrelated destinations continue");

    const FXString changedTarget = fixture + "/changed destination";
    assert(mkdir(changedTarget.text(), 0700) == 0);
    writeConflictFile(source + "/destination-change.txt", "safe");
    lock = lockTransferPath(changedTarget);
    assert(xf_startTransfer(explorer, COPY, {TransferItem(source + "/destination-change.txt", changedTarget)}));
    waitTransferWindow(app, "Copy Files", true, 3000000000LL);
    pumpTransferEvents(app, 300000000LL);
    assert(rename(changedTarget.text(), (changedTarget + "-moved").text()) == 0);
    assert(mkdir(changedTarget.text(), 0700) == 0);
    assert(flock(lock, LOCK_UN) == 0 && close(lock) == 0);
    acknowledgeTransferFailure(app, "Transfer destination changed", "Copy Files", "destination-replaced");
    assert(!FXStat::exists(changedTarget + "/destination-change.txt") &&
           !FXStat::exists(changedTarget + "-moved/destination-change.txt") &&
           FXStat::isFile(source + "/destination-change.txt"));

    const FXString recreatedTarget = fixture + "/recreated destination";
    assert(mkdir(recreatedTarget.text(), 0700) == 0);
    writeConflictFile(source + "/destination-recreated.txt", "safe");
    lock = lockTransferPath(recreatedTarget);
    assert(xf_startTransfer(explorer, COPY, {TransferItem(source + "/destination-recreated.txt", recreatedTarget)}));
    waitTransferWindow(app, "Copy Files", true, 3000000000LL);
    pumpTransferEvents(app, 300000000LL);
    assert(rmdir(recreatedTarget.text()) == 0 && mkdir(recreatedTarget.text(), 0700) == 0);
    assert(flock(lock, LOCK_UN) == 0 && close(lock) == 0);
    acknowledgeTransferFailure(app, "Transfer destination changed", "Copy Files");
    assert(!FXStat::exists(recreatedTarget + "/destination-recreated.txt") &&
           FXStat::isFile(source + "/destination-recreated.txt"));

    const FXString linkTargetA = fixture + "/link target A";
    const FXString linkTargetB = fixture + "/link target B";
    const FXString targetLink = fixture + "/target link";
    assert(mkdir(linkTargetA.text(), 0700) == 0 && mkdir(linkTargetB.text(), 0700) == 0);
    assert(symlink(linkTargetA.text(), targetLink.text()) == 0);
    const FXString linkSource = source + "/link-destination-change.txt";
    writeConflictFile(linkSource, "preserve link source");
    lock = lockTransferPath(linkTargetA);
    assert(xf_startTransfer(explorer, MOVE, {TransferItem(linkSource, targetLink)}));
    waitTransferWindow(app, "Move Files", true, 3000000000LL);
    pumpTransferEvents(app, 300000000LL);
    assert(unlink(targetLink.text()) == 0 && symlink(linkTargetB.text(), targetLink.text()) == 0);
    assert(flock(lock, LOCK_UN) == 0 && close(lock) == 0);
    acknowledgeTransferFailure(app, "Transfer destination changed", "Move Files");
    assert(readConflictFile(linkSource) == "preserve link source" &&
           !FXStat::exists(linkTargetA + "/link-destination-change.txt") &&
           !FXStat::exists(linkTargetB + "/link-destination-change.txt"));
    puts("PASS: queued transfers reject replaced destination directories and retargeted symlinks");

    const FXString changedSource = source + "/source-change.txt";
    writeConflictFile(changedSource, "original source");
    lock = lockTransferPath(target);
    assert(xf_startTransfer(explorer, MOVE, {TransferItem(changedSource, target)}));
    waitTransferWindow(app, "Move Files", true, 3000000000LL);
    pumpTransferEvents(app, 300000000LL);
    assert(rename(changedSource.text(), (changedSource + ".saved").text()) == 0);
    writeConflictFile(changedSource, "replacement source");
    assert(flock(lock, LOCK_UN) == 0 && close(lock) == 0);
    acknowledgeTransferFailure(app, "Transfer source changed", "Move Files", "source-replaced");
    assert(readConflictFile(changedSource) == "replacement source" &&
           readConflictFile(changedSource + ".saved") == "original source" &&
           !FXStat::exists(target + "/source-change.txt"));

    const FXString recreatedSource = source + "/source-recreated.txt";
    writeConflictFile(recreatedSource, "original source");
    lock = lockTransferPath(target);
    assert(xf_startTransfer(explorer, MOVE, {TransferItem(recreatedSource, target)}));
    waitTransferWindow(app, "Move Files", true, 3000000000LL);
    pumpTransferEvents(app, 300000000LL);
    assert(unlink(recreatedSource.text()) == 0);
    usleep(20000);
    writeConflictFile(recreatedSource, "replacement source");
    assert(flock(lock, LOCK_UN) == 0 && close(lock) == 0);
    acknowledgeTransferFailure(app, "Transfer source changed", "Move Files");
    assert(readConflictFile(recreatedSource) == "replacement source" &&
           !FXStat::exists(target + "/source-recreated.txt"));
    puts("PASS: a queued move refuses a replaced source without deleting either file");

    const FXString firstMove = source + "/first-slow-move.bin";
    const FXString laterMove = source + "/later-move.txt";
    makeSlowTransferFile(firstMove, 8 * 1024 * 1024);
    writeConflictFile(laterMove, "original later source");
    setenv("SWORDFISH_TEST_SLOW_FILE", firstMove.text(), 1);
    setenv("SWORDFISH_TEST_CROSS_DEVICE", firstMove.text(), 1);
    assert(xf_startTransfer(explorer, MOVE,
                            {TransferItem(firstMove, target), TransferItem(laterMove, target)}));
    waitTransferWindow(app, "Move Files", true, 3000000000LL);
    FXTime firstMoveDeadline = FXThread::time() + 5000000000LL;
    while (transferFileSize(target + "/first-slow-move.bin") <= 0 && FXThread::time() < firstMoveDeadline)
        pumpTransferEvents(app, 10000000LL);
    assert(transferFileSize(target + "/first-slow-move.bin") > 0);
    assert(rename(laterMove.text(), (laterMove + ".saved").text()) == 0);
    writeConflictFile(laterMove, "replacement later source");
    acknowledgeTransferFailure(app, "Transfer source changed", "Move Files");
    assert(!FXStat::exists(firstMove) && transferFileSize(target + "/first-slow-move.bin") == 8 * 1024 * 1024);
    assert(readConflictFile(laterMove) == "replacement later source" &&
           readConflictFile(laterMove + ".saved") == "original later source" &&
           !FXStat::exists(target + "/later-move.txt"));
    unsetenv("SWORDFISH_TEST_SLOW_FILE");
    unsetenv("SWORDFISH_TEST_CROSS_DEVICE");
    puts("PASS: moves recheck later source identities after earlier items finish");

    const FXString selfMove = source + "/self-move.txt";
    writeConflictFile(selfMove, "unchanged");
    assert(xf_startTransfer(explorer, MOVE, {TransferItem(selfMove, source)}));
    acknowledgeTransferFailure(app, "Error", "Move Files");
    assert(readConflictFile(selfMove) == "unchanged");
    const FXString selfFolder = source + "/self-folder";
    assert(mkdir(selfFolder.text(), 0700) == 0 && mkdir((selfFolder + "/child").text(), 0700) == 0);
    writeConflictFile(selfFolder + "/file.txt", "unchanged");
    assert(xf_startTransfer(explorer, MOVE, {TransferItem(selfFolder, selfFolder + "/child")}));
    acknowledgeTransferFailure(app, "Error", "Move Files");
    assert(readConflictFile(selfFolder + "/file.txt") == "unchanged" &&
           !FXStat::exists(selfFolder + "/child/self-folder"));
    puts("PASS: moving onto the source or into its own child is refused without data loss");

    const FXString detachedSource = source + "/parent-closed.bin";
    const off_t detachedSize = 8 * 1024 * 1024;
    makeSlowTransferFile(detachedSource, detachedSize);
    setenv("SWORDFISH_TEST_SLOW_FILE", detachedSource.text(), 1);
    fflush(stdout);
    pid_t parent = fork();
    assert(parent >= 0);
    if (parent == 0)
    {
        execl(execpath.text(), execpath.text(), "--test-transfer-parent-close", prefix.text(),
              detachedSource.text(), target.text(), static_cast<char*>(NULL));
        std::_Exit(127);
    }
    int parentStatus = 0;
    assert(waitpid(parent, &parentStatus, 0) == parent);
    assert(WIFEXITED(parentStatus) && WEXITSTATUS(parentStatus) == 0);
    waitTransferSize(app, target + "/parent-closed.bin", detachedSize, 8000000000LL);
    assert(FXStat::isFile(detachedSource));
    waitTransferWindow(app, "Copy Files", false, 5000000000LL);
    unsetenv("SWORDFISH_TEST_SLOW_FILE");
    puts("PASS: a transfer continues after its Swordfish parent process exits");

    const FXString cancelSource = source + "/cancel.bin";
    makeSlowTransferFile(cancelSource, slowSize);
    setenv("SWORDFISH_TEST_SLOW_FILE", cancelSource.text(), 1);
    assert(xf_startTransfer(explorer, COPY, {TransferItem(cancelSource, target)}));
    progress = waitTransferWindow(app, "Copy Files", true, 3000000000LL);
    writeDeadline = FXThread::time() + 3000000000LL;
    while (transferFileSize(target + "/cancel.bin") <= 0 && FXThread::time() < writeDeadline)
        pumpTransferEvents(app, 10000000LL);
    assert(transferFileSize(target + "/cancel.bin") > 0);
    assert(XGetWindowAttributes(display, progress, &geometry));
    clickTransferWindow(app, progress, geometry.width / 2 + 45, geometry.height - 32);
    pumpTransferEvents(app, 400000000LL);
    captureTransferWindow(progress, "copy-cancelled");
    captureAlternateTransferWidth(app, progress, "copy-cancelled-narrow");
    waitTransferWindow(app, "Copy Files", false, 5000000000LL);
    assert(FXStat::isFile(cancelSource) && transferFileSize(target + "/cancel.bin") < slowSize);
    unsetenv("SWORDFISH_TEST_SLOW_FILE");
    puts("PASS: cancelling an independent copy stops it without deleting the source");

    const FXString flatSource = source + "/many-flat-files";
    const FXString statMarker = fixture + "/stat-started";
    assert(mkdir(flatSource.text(), 0700) == 0);
    for (FXint i = 0; i < 100; ++i)
        writeConflictFile(flatSource + "/" + FXStringVal(i), "file");
    setenv("SWORDFISH_TEST_SLOW_STAT", flatSource.text(), 1);
    setenv("SWORDFISH_TEST_STAT_MARKER", statMarker.text(), 1);
    assert(xf_startTransfer(explorer, COPY, {TransferItem(flatSource, target)}));
    progress = waitTransferWindow(app, "Copy Files", true, 3000000000LL);
    FXTime scanDeadline = FXThread::time() + 3000000000LL;
    while (!FXStat::isFile(statMarker) && FXThread::time() < scanDeadline)
        pumpTransferEvents(app, 10000000LL);
    assert(FXStat::isFile(statMarker));
    captureTransferWindow(progress, "copy-preparing");
    captureAlternateTransferWidth(app, progress, "copy-preparing-narrow", false);
    assert(XGetWindowAttributes(display, progress, &geometry));
    started = FXThread::time();
    clickTransferWindow(app, progress, geometry.width / 2 + 45, geometry.height - 32);
    pumpTransferEvents(app, 300000000LL);
    captureTransferWindow(progress, "copy-scan-cancelled");
    waitTransferWindow(app, "Copy Files", false, 5000000000LL);
    assert(FXThread::time() - started < 4500000000LL);
    assert(FXStat::isDirectory(flatSource) && !FXStat::exists(target + "/many-flat-files"));
    unsetenv("SWORDFISH_TEST_SLOW_STAT");
    unsetenv("SWORDFISH_TEST_STAT_MARKER");
    puts("PASS: cancellation remains responsive while scanning a large flat source folder");

    writeConflictFile(source + "/conflict.txt", "incoming");
    writeConflictFile(target + "/conflict.txt", "existing");
    app.reg().writeUnsignedEntry("OPTIONS", "confirm_overwrite", true);
    assert(xf_startTransfer(explorer, COPY, {TransferItem(source + "/conflict.txt", target)}));
    Window conflict = waitTransferWindow(app, "Name already exists", true, 3000000000LL);
    assert(XGetWindowAttributes(display, conflict, &geometry));
    captureTransferWindow(conflict, "copy-conflict");
    clickTransferWindow(app, conflict, geometry.width * 3 / 8, geometry.height - 32);
    waitTransferWindow(app, "Name already exists", false, 3000000000LL);
    progress = waitTransferWindow(app, "Copy Files", true, 3000000000LL);
    pumpTransferEvents(app, 300000000LL);
    captureTransferWindow(progress, "copy-skipped");
    captureAlternateTransferWidth(app, progress, "copy-skipped-narrow");
    waitTransferWindow(app, "Copy Files", false, 5000000000LL);
    assert(readConflictFile(target + "/conflict.txt") == "existing");
    assert(readConflictFile(source + "/conflict.txt") == "incoming");
    puts("PASS: skipping an independent transfer conflict preserves both files");

    const FXString gioTools = fixture + "/gio tools";
    const FXString gioTarget = fixture + "/mtp:host=test";
    const FXString gioMarker = fixture + "/gio.pid";
    assert(mkdir(gioTools.text(), 0700) == 0);
    assert(mkdir(gioTarget.text(), 0700) == 0);
    const FXString gioScript = gioTools + "/gio";
    writeConflictFile(gioScript,
        "#!/bin/sh\n"
        "[ \"$1\" = copy ] || exit 64\n"
        "case \"$SWORDFISH_TEST_GIO_MODE\" in\n"
        "  success) /bin/cp \"$2\" \"$3\" || exit $?; echo 'Copy diagnostic'; exit 0 ;;\n"
        "  failure) exit 42 ;;\n"
        "  slow) echo \"$$\" > \"$SWORDFISH_TEST_GIO_MARKER\"; trap '' TERM; /bin/sleep 15; "
        "/bin/cp \"$2\" \"$3\" ;;\n"
        "  noisy) echo \"$$\" > \"$SWORDFISH_TEST_GIO_MARKER\"; trap '' TERM; exec /usr/bin/yes diagnostic ;;\n"
        "  orphan) /bin/sh -c 'trap \"\" TERM; echo \"$$\" > \"$SWORDFISH_TEST_GIO_MARKER\"; "
        "/bin/sleep 15; /bin/cp \"$1\" \"$2\"' sh \"$2\" \"$3\" & wait ;;\n"
        "  *) exit 65 ;;\n"
        "esac\n");
    assert(chmod(gioScript.text(), 0700) == 0);
    const char* previousHostPath = getenv("SWORDFISH_HOST_PATH");
    const bool hadHostPath = previousHostPath != NULL;
    const FXString savedHostPath = hadHostPath ? previousHostPath : "";
    const FXString testHostPath = gioTools + ":/usr/bin:/bin";
    setenv("SWORDFISH_HOST_PATH", testHostPath.text(), 1);
    setenv("SWORDFISH_TEST_GIO_MARKER", gioMarker.text(), 1);
    setenv("SWORDFISH_TEST_GIO_MODE", "success", 1);
    writeConflictFile(source + "/gio-success.txt", "gio success");
    assert(xf_startTransfer(explorer, COPY, {TransferItem(source + "/gio-success.txt", gioTarget)}));
    waitTransferSize(app, gioTarget + "/gio-success.txt", 11, 5000000000LL);
    waitTransferWindow(app, "Copy Files", false, 5000000000LL);
    assert(readConflictFile(gioTarget + "/gio-success.txt") == "gio success");

    setenv("SWORDFISH_TEST_GIO_MODE", "slow", 1);
    writeConflictFile(source + "/gio-cancel.txt", "do not remove");
    assert(xf_startTransfer(explorer, COPY, {TransferItem(source + "/gio-cancel.txt", gioTarget)}));
    progress = waitTransferWindow(app, "Copy Files", true, 3000000000LL);
    FXTime gioDeadline = FXThread::time() + 3000000000LL;
    while (transferFileSize(gioMarker) <= 0 && FXThread::time() < gioDeadline)
        pumpTransferEvents(app, 10000000LL);
    std::ifstream gioPidFile(gioMarker.text());
    long gioPid = -1;
    gioPidFile >> gioPid;
    assert(gioPid > 0);
    assert(XGetWindowAttributes(display, progress, &geometry));
    clickTransferWindow(app, progress, geometry.width / 2 - 45, geometry.height - 32);
    gioDeadline = FXThread::time() + 3000000000LL;
    while (transferProcessState(gioPid) != 'T' && FXThread::time() < gioDeadline)
        pumpTransferEvents(app, 10000000LL);
    assert(transferProcessState(gioPid) == 'T');
    clickTransferWindow(app, progress, geometry.width / 2 - 45, geometry.height - 32);
    gioDeadline = FXThread::time() + 3000000000LL;
    while (transferProcessState(gioPid) == 'T' && FXThread::time() < gioDeadline)
        pumpTransferEvents(app, 10000000LL);
    assert(transferProcessState(gioPid) != 'T' && transferProcessState(gioPid) != 0);
    clickTransferWindow(app, progress, geometry.width / 2 + 45, geometry.height - 32);
    pumpTransferEvents(app, 250000000LL);
    captureTransferWindow(progress, "copy-cancelling");
    captureAlternateTransferWidth(app, progress, "copy-cancelling-narrow");
    waitTransferWindow(app, "Copy Files", false, 6000000000LL);
    assert(transferProcessState(gioPid) == 0);
    assert(FXStat::isFile(source + "/gio-cancel.txt") && !FXStat::exists(gioTarget + "/gio-cancel.txt"));
    puts("PASS: GIO copy uses exit status and pauses, resumes, and cancels its process group safely");

    assert(unlink(gioMarker.text()) == 0);
    writeConflictFile(source + "/gio-paused-cancel.txt", "do not remove");
    assert(xf_startTransfer(explorer, COPY, {TransferItem(source + "/gio-paused-cancel.txt", gioTarget)}));
    progress = waitTransferWindow(app, "Copy Files", true, 3000000000LL);
    gioDeadline = FXThread::time() + 3000000000LL;
    while (transferFileSize(gioMarker) <= 0 && FXThread::time() < gioDeadline)
        pumpTransferEvents(app, 10000000LL);
    std::ifstream pausedPidFile(gioMarker.text());
    long pausedPid = -1;
    pausedPidFile >> pausedPid;
    assert(pausedPid > 0);
    assert(XGetWindowAttributes(display, progress, &geometry));
    clickTransferWindow(app, progress, geometry.width / 2 - 45, geometry.height - 32);
    gioDeadline = FXThread::time() + 3000000000LL;
    while (transferProcessState(pausedPid) != 'T' && FXThread::time() < gioDeadline)
        pumpTransferEvents(app, 10000000LL);
    assert(transferProcessState(pausedPid) == 'T');
    clickTransferWindow(app, progress, geometry.width / 2 + 45, geometry.height - 32);
    waitTransferWindow(app, "Copy Files", false, 6000000000LL);
    assert(transferProcessState(pausedPid) == 0);
    assert(FXStat::isFile(source + "/gio-paused-cancel.txt") &&
           !FXStat::exists(gioTarget + "/gio-paused-cancel.txt"));
    puts("PASS: cancelling a paused GIO copy resumes and stops its whole process group");

    assert(unlink(gioMarker.text()) == 0);
    setenv("SWORDFISH_TEST_GIO_MODE", "noisy", 1);
    writeConflictFile(source + "/gio-noisy-cancel.txt", "do not remove");
    assert(xf_startTransfer(explorer, COPY, {TransferItem(source + "/gio-noisy-cancel.txt", gioTarget)}));
    progress = waitTransferWindow(app, "Copy Files", true, 3000000000LL);
    gioDeadline = FXThread::time() + 3000000000LL;
    while (transferFileSize(gioMarker) <= 0 && FXThread::time() < gioDeadline)
        pumpTransferEvents(app, 10000000LL);
    std::ifstream noisyPidFile(gioMarker.text());
    long noisyPid = -1;
    noisyPidFile >> noisyPid;
    assert(noisyPid > 0);
    assert(XGetWindowAttributes(display, progress, &geometry));
    clickTransferWindow(app, progress, geometry.width / 2 + 45, geometry.height - 32);
    waitTransferWindow(app, "Copy Files", false, 6000000000LL);
    assert(transferProcessState(noisyPid) == 0 &&
           FXStat::isFile(source + "/gio-noisy-cancel.txt") &&
           !FXStat::exists(gioTarget + "/gio-noisy-cancel.txt"));
    puts("PASS: cancelling remains responsive when a GIO command emits continuous output");

    assert(unlink(gioMarker.text()) == 0);
    setenv("SWORDFISH_TEST_GIO_MODE", "orphan", 1);
    writeConflictFile(source + "/gio-orphan.txt", "do not remove");
    assert(xf_startTransfer(explorer, COPY, {TransferItem(source + "/gio-orphan.txt", gioTarget)}));
    progress = waitTransferWindow(app, "Copy Files", true, 3000000000LL);
    gioDeadline = FXThread::time() + 3000000000LL;
    while (transferFileSize(gioMarker) <= 0 && FXThread::time() < gioDeadline)
        pumpTransferEvents(app, 10000000LL);
    std::ifstream orphanPidFile(gioMarker.text());
    long orphanPid = -1;
    orphanPidFile >> orphanPid;
    assert(orphanPid > 0);
    assert(XGetWindowAttributes(display, progress, &geometry));
    clickTransferWindow(app, progress, geometry.width / 2 + 45, geometry.height - 32);
    waitTransferWindow(app, "Copy Files", false, 6000000000LL);
    const char orphanState = transferProcessState(orphanPid);
    assert(orphanState == 0 || orphanState == 'Z');
    assert(FXStat::isFile(source + "/gio-orphan.txt") && !FXStat::exists(gioTarget + "/gio-orphan.txt"));
    puts("PASS: cancelling GIO also stops descendants if the process-group leader exits first");

    setenv("SWORDFISH_TEST_GIO_MODE", "failure", 1);
    writeConflictFile(source + "/gio-failure.txt", "preserve");
    assert(xf_startTransfer(explorer, COPY, {TransferItem(source + "/gio-failure.txt", gioTarget)}));
    Window gioError = waitTransferWindow(app, "Error", true, 3000000000LL);
    assert(XGetWindowAttributes(display, gioError, &geometry));
    captureTransferWindow(gioError, "gio-error-dialog");
    clickTransferWindow(app, gioError, geometry.width * 3 / 4, geometry.height - 18);
    waitTransferWindow(app, "Error", false, 3000000000LL);
    pumpTransferEvents(app, 3500000000LL);
    progress = waitTransferWindow(app, "Copy Files", true, 1000000000LL);
    assert(XGetWindowAttributes(display, progress, &geometry));
    clickTransferWindow(app, progress, geometry.width / 2, geometry.height - 32);
    waitTransferWindow(app, "Copy Files", false, 3000000000LL);
    assert(FXStat::isFile(source + "/gio-failure.txt") && !FXStat::exists(gioTarget + "/gio-failure.txt"));
    if (hadHostPath) setenv("SWORDFISH_HOST_PATH", savedHostPath.text(), 1);
    else unsetenv("SWORDFISH_HOST_PATH");
    unsetenv("SWORDFISH_TEST_GIO_MARKER");
    unsetenv("SWORDFISH_TEST_GIO_MODE");
    puts("PASS: a GIO failure without diagnostic output remains visible and preserves the source");

    assert(xf_startTransfer(explorer, COPY, {TransferItem(source + "/missing.txt", target)}));
    Window error = waitTransferWindow(app, "Error", true, 3000000000LL);
    assert(XGetWindowAttributes(display, error, &geometry));
    clickTransferWindow(app, error, geometry.width / 2, geometry.height - 18);
    waitTransferWindow(app, "Error", false, 3000000000LL);
    pumpTransferEvents(app, 3500000000LL);
    progress = waitTransferWindow(app, "Copy Files", true, 1000000000LL);
    captureTransferWindow(progress, "copy-error");
    captureAlternateTransferWidth(app, progress, "copy-error-narrow");
    assert(XGetWindowAttributes(display, progress, &geometry));
    clickTransferWindow(app, progress, geometry.width / 2, geometry.height - 32);
    waitTransferWindow(app, "Copy Files", false, 3000000000LL);
    puts("PASS: failed independent transfers remain visible until explicitly closed");

    const FXString burst = source + "/many-notifications";
    assert(mkdir(burst.text(), 0700) == 0);
    for (FXint i = 0; i < 800; ++i)
        writeConflictFile(burst + "/item-" + FXStringVal(i), "data");
    assert(xf_startTransfer(explorer, COPY, {TransferItem(burst, target)}));
    waitTransferWindow(app, "Copy Files", true, 3000000000LL);
    FXTime burstDeadline = FXThread::time() + 15000000000LL;
    while (findTransferWindow(display, DefaultRootWindow(display), "Copy Files") &&
           FXThread::time() < burstDeadline)
        usleep(10000);
    assert(!findTransferWindow(display, DefaultRootWindow(display), "Copy Files"));
    assert(readConflictFile(target + "/many-notifications/item-799") == "data");
    {
        ErrorResponder unexpected(app);
        unexpected.start();
        pumpTransferEvents(app, 1000000000LL);
        assert(unexpected.shown == 0);
    }
    puts("PASS: a busy transfer reports completion correctly even while the parent is not processing events");

    left->getTabButtons()->restoreState(leftState);
    right->getTabButtons()->restoreState(rightState);
    leftList->setSortFunc(leftSort);
    rightList->setSortFunc(rightSort);
    leftList->scan(true);
    rightList->scan(true);
}

static int runTransferParentClose(char** argv)
{
    FXString prefix = argv[2], source = argv[3], target = argv[4];
    XFApp app("SwordfishParentExitTest", "SwordfishParentExitTest");
    int appArgc = 1;
    char* appArgv[] = {argv[0], NULL};
    app.init(appArgc, appArgv);
    homedir = FXSystem::getHomeDirectory();
    xdgconfighome = homedir + "/config";
    xdgdatahome = homedir + "/data";
    execpath = prefix + "/bin/swordfish";
    FXuint status;
    if (!loadAppIcons(&app, &status)) return 1;
    vector_FXString paths;
    paths.push_back(FXPath::directory(source));
    XFileExplorer* explorer = new XFileExplorer(&app, paths, -1);
    mainWindow = explorer;
    app.create();
    explorer->show();
    std::_Exit(xf_startTransfer(explorer, COPY, {TransferItem(source, target)}) ? 0 : 1);
}

class ColumnWidthsRegression : public FXObject
{
    PreferencesBox* preferences;
public:
    FXbool accept = false;
    void screenshot(const char* name)
    {
        FXApp* app = preferences->getApp();
        app->repaint();
        app->flush(true);
        Display* display = static_cast<Display*>(app->getDisplay());
        XWindowAttributes attributes;
        assert(XGetWindowAttributes(display, preferences->id(), &attributes) && attributes.map_state == IsViewable);
        captureTransferWindow(preferences->id(), name);
    }
    explicit ColumnWidthsRegression(PreferencesBox* box) : preferences(box)
    {
        if (getenv("SWORDFISH_TEST_TRANSFER_SCREENSHOTS"))
            for (const Theme& theme : preferences->Themes)
                if (theme.name == (getenv("SWORDFISH_TEST_COLUMNS_DARK") ? "Dark" : "Clearlooks"))
                {
                    theme.writeActive(preferences->getApp()->reg());
                    preferences->currTheme = theme;
                }
    }
    long handle(FXObject*, FXSelector, void*) override
    {
        preferences->widthPane->setCurrentItem(0);
        preferences->onCmdColumnPane(preferences->widthPane, 0, NULL);
        preferences->autoColumnWidths->setCheck(true);
        preferences->onCmdColumnWidths(NULL, 0, NULL);
        for (FXuint column = 0; column < NMAX_COLS; ++column)
            assert(!preferences->columnWidths[column]->isEnabled());
        preferences->autoColumnWidths->setCheck(false);
        preferences->columnWidths[0]->setValue(333);
        preferences->columnWidths[2]->setValue(91);
        preferences->columnWidths[8]->setValue(213);
        preferences->onCmdColumnWidths(NULL, 0, NULL);
        preferences->widthPane->setCurrentItem(1);
        preferences->onCmdColumnPane(preferences->widthPane, 0, NULL);
        preferences->autoColumnWidths->setCheck(false);
        preferences->columnWidths[1]->setValue(117);
        preferences->onCmdColumnWidths(NULL, 0, NULL);
        preferences->widthPane->setCurrentItem(0);
        preferences->onCmdColumnPane(preferences->widthPane, 0, NULL);
        assert(!preferences->autoColumnWidths->getCheck() && preferences->columnWidths[0]->getValue() == 333);
        for (FXuint column = 0; column < NMAX_COLS; ++column)
            assert(preferences->columnWidths[column]->isEnabled() && preferences->columnWidths[column]->getValue() >= 32);
        if (getenv("SWORDFISH_TEST_TRANSFER_SCREENSHOTS") && accept)
        {
            preferences->show(PLACEMENT_OWNER);
            preferences->selectCategory(PreferencesBox::FILE_LIST);
            FXTabBook* tabs = preferences->categories[PreferencesBox::FILE_LIST].tabs;
            for (FXWindow* child = tabs->getFirst(); child; child = child->getNext())
            {
                FXTabItem* tab = dynamic_cast<FXTabItem*>(child);
                if (tab && tab->getText() == "Column widths")
                    tabs->setCurrent(tabs->indexOfChild(child) / 2);
            }
            preferences->layout();
            pumpTransferEvents(*preferences->getApp(), 200000000LL);
            screenshot("column-widths-manual");
            preferences->autoColumnWidths->setCheck(true);
            preferences->onUpdColumnWidths(NULL, 0, NULL);
            pumpTransferEvents(*preferences->getApp(), 200000000LL);
            screenshot("column-widths-auto");
            preferences->autoColumnWidths->setCheck(false);
            preferences->onUpdColumnWidths(NULL, 0, NULL);
            preferences->resize(620, 440);
            preferences->layout();
            pumpTransferEvents(*preferences->getApp(), 200000000LL);
            screenshot("column-widths-narrow");
            preferences->autoColumnWidths->setCheck(true);
            preferences->onUpdColumnWidths(NULL, 0, NULL);
            pumpTransferEvents(*preferences->getApp(), 200000000LL);
            screenshot("column-widths-auto-narrow");
            preferences->autoColumnWidths->setCheck(false);
            preferences->onUpdColumnWidths(NULL, 0, NULL);
            checkPreferencesScrolling(*preferences->getApp(), preferences);
            FXScrollWindow* outer = dynamic_cast<FXScrollWindow*>(preferences->getFirst());
            assert(outer);
            outer->setPosition(-outer->getContentWidth(), -outer->getContentHeight());
            pumpTransferEvents(*preferences->getApp(), 200000000LL);
            screenshot("column-widths-narrow-bottom");
        }
        if (accept) preferences->onCmdAccept(NULL, 0, NULL);
        else preferences->onCmdCancel(NULL, 0, NULL);
        return 1;
    }
};

static void checkColumnWidthPreferences(FXApp& app, XFileExplorer* explorer, const FXString& fixture)
{
    FileList* left = explorer->getLeftPanel()->getList();
    FileList* right = explorer->getRightPanel()->getList();
    TabButtons::State leftTabs = explorer->getLeftPanel()->getTabButtons()->saveState();
    TabButtons::State rightTabs = explorer->getRightPanel()->getTabButtons()->saveState();
    FileList::ColumnSizing original = left->getColumnSizing();
    FileList::ColumnSizing originalRight = right->getColumnSizing();
    const std::vector<FXuint> leftLayout = left->getColumnLayout(), rightLayout = right->getColumnLayout();
    FXString longest;
    for (int i = 0; i < 180; ++i) longest += "W";
    FXString folder = fixture + "/widths";
    assert(mkdir(folder.text(), 0700) == 0);
    writeConflictFile(folder + "/" + longest, "long");
    writeConflictFile(folder + "/short.txt", "short");
    left->setDirectory(folder);
    right->setDirectory(folder);
    std::vector<FXuint> columns;
    for (FXuint i = 0; i < NMAX_COLS; ++i) columns.push_back(FileList::ID_COL_NAME + i);
    left->setColumnLayout(columns);
    right->setColumnLayout(columns);
    right->moveColumn(2, 1);
    FileList::ColumnSizing automatic = left->getColumnSizing();
    automatic.mode = FileList::ColumnSizing::AUTO_FIT;
    left->setColumnSizing(automatic);
    left->layout();
    FXint widest = left->getHeaderSize(0);
    assert(widest > 1000 && widest >= left->getFont()->getTextWidth(longest.text(), longest.length()));
    for (FXint column = 0; column < left->getNumHeaders(); ++column)
    {
        for (FXint item = 0; item < left->getNumItems(); ++item)
        {
            FXString text = left->getItemText(item).section('\t', column);
            assert(left->getHeaderSize(column) >= left->getFont()->getTextWidth(text.text(), text.length()));
        }
    }
    assert(unlink((folder + "/" + longest).text()) == 0);
    left->scan(true);
    left->layout();
    assert(left->getHeaderSize(0) < widest);
    TabButtons* tabs = explorer->getLeftPanel()->getTabButtons();
    FXuint autoTab = tabs->getCurrentTab();
    tabs->addTab(folder);
    FXuint manualTab = tabs->getCurrentTab();
    BookmarksList* bookmarks = explorer->getDirPanel()->getBookmarksList();
    FXString earlyBookmark = "Column widths before preferences", earlyIcon, earlyFolder = fixture + "/widths-early";
    assert(mkdir(earlyFolder.text(), 0700) == 0);
    assert(bookmarks->appendBookmark(earlyBookmark, earlyFolder, earlyIcon, &automatic) == 1);

    PreferencesBox preferences(explorer);
    preferences.create();
    ColumnWidthsRegression driver(&preferences);
    app.addTimeout(&driver, 1, 10);
    preferences.execute(PLACEMENT_OWNER);
    assert(!app.reg().existingEntry("LEFT PANEL", "column_modedefault"));
    assert(left->getColumnSizing().mode == FileList::ColumnSizing::LEGACY);
    driver.accept = true;
    app.addTimeout(&driver, 1, 10);
    preferences.execute(PLACEMENT_OWNER);
    assert(left->getColumnSizing().mode == FileList::ColumnSizing::MANUAL &&
           right->getColumnSizing().mode == FileList::ColumnSizing::MANUAL);
    assert(left->getHeaderSize(0) == 333 && left->getHeaderSize(left->getHeaderIndex(FileList::ID_COL_TYPE)) == 91);
    assert(right->getHeaderSize(right->getHeaderIndex(FileList::ID_COL_SIZE)) == 117);
    left->moveColumn(2, 1);
    left->layout();
    assert(left->getHeaderSize(left->getHeaderIndex(FileList::ID_COL_TYPE)) == 91);
    std::vector<FXuint> hidden = left->getColumnLayout();
    left->setHeaderSize(left->getHeaderIndex(FileList::ID_COL_LINK), 229);
    hidden.erase(std::find(hidden.begin(), hidden.end(), FileList::ID_COL_LINK));
    left->setColumnLayout(hidden);
    hidden.push_back(FileList::ID_COL_LINK);
    left->setColumnLayout(hidden);
    assert(left->getHeaderSize(left->getHeaderIndex(FileList::ID_COL_LINK)) == 229);
    left->setHeaderSize(0, 357);
    left->onHeaderChanged(NULL, 0, NULL);
    assert(left->getHeaderSize(0) == 357);
    explorer->resize(700, 500);
    left->onConfigure(NULL, 0, NULL);
    assert(left->getHeaderSize(0) == 357);
    tabs->setCurrentTab(autoTab);
    assert(left->getColumnSizing().mode == FileList::ColumnSizing::AUTO_FIT && left->getHeaderSize(0) != 333);
    tabs->setCurrentTab(manualTab);
    assert(left->getColumnSizing().mode == FileList::ColumnSizing::MANUAL && left->getHeaderSize(0) == 357);
    assert(left->getHeaderSize(left->getHeaderIndex(FileList::ID_COL_LINK)) == 229);
    tabs->addTab(folder);
    assert(left->getColumnSizing().mode == FileList::ColumnSizing::MANUAL && left->getHeaderSize(0) == 333);
    left->setListStyle(ICONLIST_BIG_ICONS);
    left->layout();
    left->setListStyle(ICONLIST_DETAILED);
    left->layout();
    assert(left->getHeaderSize(0) == 333);
    FXString bookmarkName = "Column widths regression", bookmarkIcon;
    assert(bookmarks->appendBookmark(bookmarkName, folder, bookmarkIcon) == 1);
    left->setColumnSizing(automatic);
    bookmarks->setBookmark(bookmarkName);
    explorer->getDirPanel()->onCmdGotoBookmark(NULL, 0, NULL);
    assert(left->getColumnSizing().mode == FileList::ColumnSizing::MANUAL && left->getHeaderSize(0) == 333);
    left->setColumnSizing(automatic);
    FXString renamed = "Renamed column widths";
    assert(bookmarks->modifyBookmark(bookmarkName, renamed, bookmarkIcon, bookmarkIcon, folder) == 1);
    bookmarks->restoreColumnSizing(renamed, left);
    assert(left->getColumnSizing().mode == FileList::ColumnSizing::MANUAL && left->getHeaderSize(0) == 333);
    left->setColumnSizing(automatic);
    explorer->getDirPanel()->onCmdOpenBookmarkNewTab(NULL, 0, NULL);
    assert(left->getColumnSizing().mode == FileList::ColumnSizing::MANUAL && left->getHeaderSize(0) == 333);
    tabs->setCurrentTab(autoTab);
    assert(left->getColumnSizing().mode == FileList::ColumnSizing::AUTO_FIT);
    tabs->saveTabs("LEFT TABS");
    explorer->getRightPanel()->getTabButtons()->saveTabs("RIGHT TABS");
    app.reg().writeUnsignedEntry("OPTIONS", "restore_tabs", true);
    assert(app.reg().write());
    explorer->saveConfig();
    app.reg().writeUnsignedEntry("LEFT TABS", "test_count", tabs->getNumActiveTabs());
    app.reg().writeUnsignedEntry("RIGHT TABS", "test_count", explorer->getRightPanel()->getTabButtons()->getNumActiveTabs());
    assert(app.reg().write());
    assert(app.reg().unparseFile(fixture + "/columns-expected.rc"));
    pid_t child = fork();
    assert(child >= 0);
    if (!child)
    {
        setenv("SWORDFISH_TEST_COLUMNS_RESTORE", "1", 1);
        execl("/proc/self/exe", "swordfish-columns-test", FXPath::directory(FXPath::directory(execpath)).text(),
              fixture.text(), (char*)NULL);
        _exit(127);
    }
    int childStatus;
    assert(waitpid(child, &childStatus, 0) == child && WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 0);
    bookmarks->removeItem(renamed);
    left->setColumnSizing(automatic);
    bookmarks->restoreColumnSizing(renamed, left);
    assert(left->getColumnSizing().mode == FileList::ColumnSizing::AUTO_FIT);
    tabs->setCurrentTab(manualTab);
    assert(left->getColumnSizing().mode == FileList::ColumnSizing::MANUAL);
    bookmarks->restoreColumnSizing(earlyBookmark, left);
    assert(left->getColumnSizing().mode == FileList::ColumnSizing::AUTO_FIT && left->getHeaderSize(0) != 333);
    bookmarks->removeItem(earlyBookmark);
    FXSettings saved;
    FXString filename = xdgconfighome + "/swordfish/" + app.getAppName() + "rc";
    assert(saved.parseFile(filename, true));
    assert(FileList::loadColumnSizing(saved, "LEFT PANEL", "default", original).widths[FileList::ID_COL_NAME] == 333 &&
           FileList::loadColumnSizing(saved, "RIGHT PANEL", "default", originalRight).widths[FileList::ID_COL_SIZE] == 117);
    for (const char* section : {"LEFT PANEL", "RIGHT PANEL"})
    {
        app.reg().deleteEntry(section, "column_modedefault");
        for (FXuint column = 0; column < NMAX_COLS; ++column)
            app.reg().deleteEntry(section, ("widthdefault_" + FXStringVal(FileList::ID_COL_NAME + column)).text());
    }
    left->setColumnLayout(leftLayout);
    right->setColumnLayout(rightLayout);
    explorer->getLeftPanel()->getTabButtons()->restoreState(leftTabs);
    explorer->getRightPanel()->getTabButtons()->restoreState(rightTabs);
    left->setColumnSizing(original);
    right->setColumnSizing(originalRight);
    puts("PASS: optional widest-item fitting, exclusive manual controls, independent tab sizing, new-tab defaults, earlier bookmarks preserved, bookmark restore/rename/remove and fresh-process startup; Cancel changes nothing");
}

int main(int argc, char** argv)
{
    if (argc == 2 && FXString(argv[1]) == "--internal-transfer-worker")
        return swordfish_main(argc, argv);
    if (argc == 5 && FXString(argv[1]) == "--test-transfer-parent-close")
        return runTransferParentClose(argv);
    assert(argc == 3);
    setvbuf(stdout, NULL, _IONBF, 0);
    FXString prefix = argv[1], fixture = argv[2];
    XFApp app("SwordfishSearchTest", "SwordfishSearchTest");
    app.init(argc, argv);
    homedir = FXSystem::getHomeDirectory();
    xdgconfighome = homedir + "/config";
    xdgdatahome = homedir + "/data";
    execpath = prefix + "/bin/swordfish";
    FXuint status;
    assert(loadAppIcons(&app, &status));
    if (getenv("SWORDFISH_TEST_RESTORE_SESSION") || getenv("SWORDFISH_TEST_COLUMNS_RESTORE"))
    {
        assert(app.reg().read());
        vector_FXString noPaths;
        XFileExplorer* restored = new XFileExplorer(&app, noPaths, -1);
        mainWindow = restored;
        app.create();
        FilePanel* panels[] = {restored->getLeftPanel(), restored->getRightPanel()};
        const char* sections[] = {"LEFT TABS", "RIGHT TABS"};
        FXSettings expected;
        assert(expected.parseFile(fixture + (getenv("SWORDFISH_TEST_COLUMNS_RESTORE") ?
                                  "/columns-expected.rc" : "/session-expected.rc"), true));
        for (FXint pane = 0; pane < 2; ++pane)
        {
            TabButtons* tabs = panels[pane]->getTabButtons();
            TabButtons::State state = tabs->saveState();
            assert(state.paths.size() == expected.readUnsignedEntry(sections[pane], "test_count", 0));
            assert(state.selected == expected.readUnsignedEntry(sections[pane], "current_tab", 0));
            for (FXuint i = 0; i < state.paths.size(); ++i)
            {
                FXString key = "tab" + FXStringVal(i);
                assert(state.paths[i] == expected.readStringEntry(sections[pane], key.text(), ""));
                key = "view" + FXStringVal(i);
                assert(state.views[i] == expected.readUnsignedEntry(sections[pane], key.text(), 99));
                key = "column_mode" + FXStringVal(i);
                assert(state.columnModes[i] == expected.readUnsignedEntry(sections[pane], key.text(), 99));
                for (FXuint id = FileList::ID_COL_NAME; id <= FileList::ID_COL_DIRNAME; ++id)
                {
                    key = "width" + FXStringVal(i) + "_" + FXStringVal(id);
                    if (!expected.existingEntry(sections[pane], key.text())) continue;
                    auto width = state.columns[i].find(id);
                    assert(width != state.columns[i].end());
                    if (width->second != expected.readUnsignedEntry(sections[pane], key.text(), 0))
                        fprintf(stderr, "pane %d tab %u column %u width %u, expected %u\n", pane, i, id, width->second,
                                expected.readUnsignedEntry(sections[pane], key.text(), 0));
                    assert(width->second == expected.readUnsignedEntry(sections[pane], key.text(), 0));
                }
                FXString order;
                for (FXuint id : state.columnOrder)
                {
                    if (!order.empty()) order += ",";
                    order += FXStringVal(id - FileList::ID_COL_NAME);
                }
                FXString expectedOrder = expected.readStringEntry(pane ? "RIGHT PANEL" : "LEFT PANEL", "column_layout", "");
                if (order != expectedOrder)
                    fprintf(stderr, "pane %d restored columns '%s', expected '%s'\n", pane, order.text(), expectedOrder.text());
                assert(order == expectedOrder);
            }
        }
        assert(restored->getLeftPanel()->shown() && restored->getRightPanel()->shown());
        std::fflush(stdout);
        std::_Exit(0);
    }
    mainWindow = new FXMainWindow(&app, "Main regression");
    app.create();
    checkFileConflicts(app, mainWindow, fixture);
    checkCopyVerification(app, mainWindow, fixture);
    checkCancelRemovesPartialCopy(mainWindow, fixture);
    TestSearch* search = new TestSearch(&app);
    search->create();
    search->show(PLACEMENT_SCREEN);
    checkIcon(search);
    FXDialogBox* dialog = new FXDialogBox(search, "Child regression");
    dialog->create();
    dialog->show(PLACEMENT_OWNER);
    checkIcon(dialog);
    dialog->hide();
    dialog->show(PLACEMENT_OWNER);
    checkIcon(dialog);
    puts("PASS: search and child windows expose the complete Swordfish taskbar icon");
    checkRenameMenu(search->results(), fixture);

    const FXString poison = fixture + "/poison";
    setenv("LD_LIBRARY_PATH", poison.text(), 1);
    setenv("SWORDFISH_HOST_LD_LIBRARY_PATH", "", 1);
    setenv("SWORDFISH_HOST_PATH", "/usr/bin:/bin", 1);
    FXString command = "find " + xf_quote(fixture + "/data") +
                       " -type f -exec grep -q needle '{}' \\; -print";
    assert(search->collect(command) == fixture + "/data/match.txt\n");
    assert(FXString(getenv("LD_LIBRARY_PATH")) == poison);
    puts("PASS: search find/grep ignore incompatible bundled libraries, without changing the parent");

    setenv("SWORDFISH_HOST_LD_LIBRARY_PATH", "/host/library path", 1);
    assert(search->collect("printf '%s' \"$LD_LIBRARY_PATH\"") == "/host/library path");
    unsetenv("SWORDFISH_HOST_LD_LIBRARY_PATH");
    assert(search->collect("printf '%s' \"$LD_LIBRARY_PATH\"") == poison);
    puts("PASS: explicit host library paths and nonportable environments are preserved");
    unsetenv("LD_LIBRARY_PATH");
    setenv("SWORDFISH_HOST_LD_LIBRARY_PATH", "", 1);
    const FXString tools = fixture + "/tools", browserLog = fixture + "/browser.log";
    assert(mkdir(tools.text(), 0700) == 0);
    assert(symlink("/usr/bin/env", (tools + "/env").text()) == 0);
    {
        std::ofstream browser((tools + "/xdg-open").text());
        browser << "#!/bin/sh\n"
                   "test -z \"$LD_LIBRARY_PATH\" || exit 1\n"
                   "printf '%s\\n' \"$1\" > \"$SWORDFISH_TEST_BROWSER_LOG\"\n";
    }
    assert(chmod((tools + "/xdg-open").text(), 0700) == 0);
    setenv("SWORDFISH_TEST_BROWSER_LOG", browserLog.text(), 1);
    setenv("SWORDFISH_HOST_PATH", tools.text(), 1);
    const FXString data = fixture + "/data";
    { std::ofstream zip((data + "/sample.zip").text()); }
    { std::ofstream iso((data + "/sample.iso").text()); }
    vector_FXString paths;
    paths.push_back(data);
    mainWindow = NULL;
    app.reg().writeUnsignedEntry("OPTIONS", "panel_view", 3);
    XFileExplorer* explorer = new XFileExplorer(&app, paths, -1);
    mainWindow = explorer;
    explorer->create();
    explorer->show();
    FilePanel* panel = explorer->getCurrentPanel();
    FileList* files = panel->getList();
    if (getenv("SWORDFISH_TEST_COLUMNS_ONLY"))
    {
        checkColumnWidthPreferences(app, explorer, fixture);
        std::fflush(stdout);
        std::_Exit(0);
    }
    checkColumns(files);
    checkIndependentTransfers(app, explorer, prefix, fixture);
    {
        FileList* results = findList(search->results());
        std::vector<FXuint> original = results->getColumnLayout();
        results->setColumnLayout({FileList::ID_COL_NAME, FileList::ID_COL_DIRNAME, FileList::ID_COL_SIZE});
        results->moveColumn(2, 1);
        assert(results->getHeaderId(1) == FileList::ID_COL_SIZE);
        FXString extra = data + "/match.txt";
        assert(search->results()->appendItem(extra));
        FXint last = results->getNumItems() - 1;
        assert(results->getItemText(last).section('\t', 2) == data);
        assert(results->getItemFullPathname(last) == extra);
        results->setColumnLayout(original);
        puts("PASS: search results use the live column order for existing and newly arriving rows");
    }
    // Five normal columns (the Trash column count) must not be mistaken for the Trash layout
    files->setColumnLayout({FileList::ID_COL_NAME, FileList::ID_COL_SIZE, FileList::ID_COL_TYPE,
                            FileList::ID_COL_EXT, FileList::ID_COL_DATE});
    assert(files->getHeaderIndex(FileList::ID_COL_EXT) == 3);
    assert(files->getHeaderIndex(FileList::ID_COL_DATE) == 4);
    assert(files->getHeaderIndex(FileList::ID_COL_ORIGPATH) == -1);
    assert(files->getHeaderIndex(FileList::ID_COL_USER) == -1);
    files->onToggleColumn(NULL, FXSEL(SEL_COMMAND, FileList::ID_COLUMN_TOGGLE +
                             FileList::ID_COL_EXT - FileList::ID_COL_NAME), NULL);
    assert(files->getHeaderIndex(FileList::ID_COL_EXT) == -1);
    assert(files->getHeaderIndex(FileList::ID_COL_DATE) == 3);
    puts("PASS: a normal five-column layout reports its own columns, not the Trash columns");
    TabButtons* columnTabs = panel->getTabButtons();
    files->setColumnLayout({FileList::ID_COL_NAME, FileList::ID_COL_SIZE, FileList::ID_COL_TYPE});
    files->setHeaderSize(0, 333);
    const std::vector<FXuint> firstLayout = files->getColumnLayout();
    columnTabs->addTab(data);
    files->onToggleColumn(NULL, FXSEL(SEL_COMMAND, FileList::ID_COLUMN_TOGGLE +
                             FileList::ID_COL_LINK - FileList::ID_COL_NAME), NULL);
    const std::vector<FXuint> secondLayout = files->getColumnLayout();
    assert(firstLayout != secondLayout);
    files->setHeaderSize(files->getHeaderIndex(FileList::ID_COL_LINK), 278);
    files->setHeaderSize(0, 555);
    columnTabs->setCurrentTab(0);
    assert(files->getHeaderSize(0) == 333);
    assert(files->getColumnLayout() == secondLayout);
    columnTabs->setCurrentTab(1);
    assert(files->getHeaderSize(0) == 555);
    assert(files->getColumnLayout() == secondLayout);
    columnTabs->moveTab(1, 0);
    assert(files->getHeaderSize(0) == 555);
    assert(files->getColumnLayout() == secondLayout);
    columnTabs->saveTabs("COLUMN_TEST");
    const FXString savedColumns = fixture + "/columns.rc";
    assert(app.reg().unparseFile(savedColumns));
    app.reg().deleteSection("COLUMN_TEST");
    assert(app.reg().parseFile(savedColumns, true));
    columnTabs->clearTabs();
    assert(columnTabs->restoreTabs("COLUMN_TEST", 0));
    assert(files->getHeaderSize(0) == 555);
    assert(files->getHeaderSize(files->getHeaderIndex(FileList::ID_COL_LINK)) == 278);
    columnTabs->setCurrentTab(1);
    assert(files->getHeaderSize(0) == 333);
    const TabButtons::State savedTabs = columnTabs->saveState();
    columnTabs->onCmdMoveLeft(NULL, FXSEL(SEL_COMMAND, TabButtons::ID_START_MOVE_LEFT + 1), NULL);
    assert(files->getHeaderSize(0) == 333);
    columnTabs->onCmdMoveRight(NULL, FXSEL(SEL_COMMAND, TabButtons::ID_START_MOVE_RIGHT), NULL);
    assert(files->getHeaderSize(0) == 333);
    columnTabs->onCmdCloseButton(NULL, FXSEL(SEL_COMMAND, TabButtons::ID_START_CLOSE + 1), NULL);
    assert(files->getHeaderSize(0) == 555);
    columnTabs->restoreState(savedTabs);
    assert(files->getHeaderSize(0) == 333);
    const std::vector<FXuint> newDefaults = {FileList::ID_COL_NAME, FileList::ID_COL_DATE};
    columnTabs->clearTabs();
    files->setColumnLayout(newDefaults);
    assert(columnTabs->restoreTabs("COLUMN_TEST", 0));
    assert(files->getColumnLayout() == newDefaults);
    columnTabs->restoreState(savedTabs);
    puts("PASS: tab column widths survive switching, both reorder methods, closing and saved-settings restoration");
    puts("PASS: changing tabs or restoring saved tabs preserves the panel's shared column layout");
    const FXString folder = data + "/tab target";
    assert(mkdir(folder.text(), 0700) == 0);
    files->setDirectory(fixture);
    files->setDirectory(data);
    files->killSelection();
    FXint folderIndex = 0;
    while (folderIndex < files->getNumItems() && files->getItemFilename(folderIndex) != "tab target")
        ++folderIndex;
    assert(folderIndex < files->getNumItems());
    files->setCurrentItem(folderIndex);
    files->selectItem(folderIndex);
    panel->onCmdCopyName(NULL, 0, NULL);
    assert(clipboard == folder);
    ArchiveMenu folderMenu(&app);
    folderMenu.selector = FilePanel::ID_OPEN_NEW_TAB;
    app.addTimeout(&folderMenu, 1, 10);
    FXEvent folderEvent = {};
    folderEvent.code = KEY_Menu;
    panel->onCmdPopupMenu(NULL, 0, &folderEvent);
    assert(folderMenu.seen);
    TabButtons* tabs = panel->getTabButtons();
    const FXuint originalTab = tabs->getCurrentTab(), originalCount = tabs->getNumActiveTabs();
    assert(panel->onCmdOpenNewTab(NULL, 0, NULL) == 1);
    assert(tabs->getNumActiveTabs() == originalCount + 1 && panel->getDirectory() == folder);
    assert(tabs->getPath(originalTab) == data);
    tabs->onCmdPathButton(NULL, FXSEL(SEL_COMMAND, TabButtons::ID_START_TAB + originalTab), NULL);
    assert(panel->getDirectory() == data);
    files->killSelection();
    assert(panel->onCmdOpenNewTab(NULL, 0, NULL) == 0);
    FXMenuPane validationMenu(panel);
    FXMenuCommand folderCommand(&validationMenu, "Open in New Tab", NULL, panel, FilePanel::ID_OPEN_NEW_TAB);
    for (FXint i = 0; i < files->getNumItems(); ++i)
    {
        if (files->getItemFilename(i) == "match.txt")
        {
            files->setCurrentItem(i);
            files->selectItem(i);
            panel->onUpdOpenNewTab(&folderCommand, 0, NULL);
            assert(!folderCommand.isEnabled() && panel->onCmdOpenNewTab(NULL, 0, NULL) == 0);
        }
    }
    files->setCurrentItem(folderIndex);
    files->selectItem(folderIndex);
    panel->onUpdOpenNewTab(&folderCommand, 0, NULL);
    assert(!folderCommand.isEnabled() && panel->onCmdOpenNewTab(NULL, 0, NULL) == 0);
    assert(tabs->getNumActiveTabs() == originalCount + 1);
    files->killSelection();
    puts("PASS: selected folder context menu opens a new active tab without changing the original tab");
    FilePanel* destination = panel == explorer->getLeftPanel() ?
                             explorer->getRightPanel() : explorer->getLeftPanel();
    const FXString target = fixture + "/other pane";
    assert(mkdir(target.text(), 0700) == 0);
    destination->getTabButtons()->addTab(target, false);
    panel->setActive();
    app.reg().writeUnsignedEntry("OPTIONS", "ask_before_copy", false);
    files->killSelection();
    FXint sourceIndex = 0;
    while (sourceIndex < files->getNumItems() && files->getItemFilename(sourceIndex) != "other.txt") ++sourceIndex;
    assert(sourceIndex < files->getNumItems());
    files->setCurrentItem(sourceIndex);
    files->selectItem(sourceIndex);
    panel->onCmdFileMan(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_FILE_COPY), NULL);
    FXTime copyDeadline = FXThread::time() + 5000000000LL;
    while ((!FXStat::isFile(target + "/other.txt") ||
            readConflictFile(target + "/other.txt") != "other\n") && FXThread::time() < copyDeadline)
    {
        app.runOneEvent(false);
        usleep(1000);
    }
    assert(FXStat::isFile(target + "/other.txt"));
    assert(readConflictFile(target + "/other.txt") == "other\n");
    assert(unlink((target + "/other.txt").text()) == 0);
    files->selectItem(sourceIndex);
    panel->onCmdOtherPane(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_LINK_OTHER_PANE), NULL);
    assert(FXStat::isLink(target + "/other.txt"));
    assert(unlink((target + "/other.txt").text()) == 0);
    files->selectItem(sourceIndex);
    panel->onCmdFileMan(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_FILE_CUT), NULL);
    FXTime moveDeadline = FXThread::time() + 5000000000LL;
    while ((!FXStat::isFile(target + "/other.txt") || FXStat::exists(data + "/other.txt")) &&
           FXThread::time() < moveDeadline)
    {
        app.runOneEvent(false);
        usleep(1000);
    }
    assert(FXStat::isFile(target + "/other.txt") && !FXStat::exists(data + "/other.txt"));
    puts("PASS: Copy Path is plain full-path text and other-pane transfers use its active tab");
    PreferencesBox* preferences = new PreferencesBox(explorer);
    preferences->create();
    preferences->show();
    preferences->resize(preferences->getWidth(), 400);
    preferences->layout();
    FXTabBook* pages = findPreferencesTabs(preferences);
    assert(pages);
    for (FXint page = 0; page < pages->numChildren() / 2; ++page)
    {
        pages->setCurrent(page);
        preferences->layout();
        checkPreferencesScrolling(app, preferences);
    }
    preferences->hide();
    puts("PASS: every Preferences page scrolls by readable lines and reaches both ends; nested lists scroll independently");
    checkWheelChaining(app, explorer);
    puts("PASS: wheel over a nested list at its end scrolls the page; otherwise the list or spinner keeps the wheel");
    const std::vector<FXuint> beforeLeft = explorer->getLeftPanel()->getList()->getColumnLayout();
    const std::vector<FXuint> beforeRight = explorer->getRightPanel()->getList()->getColumnLayout();
    PaneColumnsRegression panePreferences(preferences);
    app.addTimeout(&panePreferences, 1, 10);
    preferences->execute(PLACEMENT_OWNER);
    assert(explorer->getLeftPanel()->getList()->getColumnLayout() == beforeLeft);
    assert(explorer->getRightPanel()->getList()->getColumnLayout() == beforeRight);
    panePreferences.accept = true;
    app.addTimeout(&panePreferences, 1, 10);
    preferences->execute(PLACEMENT_OWNER);
    assert(explorer->getLeftPanel()->getList()->getHeaderIndex(FileList::ID_COL_SIZE) < 0);
    assert(explorer->getRightPanel()->getList()->getHeaderIndex(FileList::ID_COL_SIZE) >= 0);
    assert(explorer->getLeftPanel()->getList()->getHeaderIndex(FileList::ID_COL_EXT) >= 0);
    assert(explorer->getRightPanel()->getList()->getHeaderIndex(FileList::ID_COL_EXT) < 0);
    {
        FileList* left = explorer->getLeftPanel()->getList();
        FileList* right = explorer->getRightPanel()->getList();
        right->setColumnLayout({FileList::ID_COL_NAME, FileList::ID_COL_TYPE, FileList::ID_COL_EXT});
        left->setSortFunc(FileList::ascendingExt);
        left->sortItems();
        FXString previous;
        for (FXint i = 0; i < left->getNumItems(); ++i)
        {
            if (left->isItemDirectory(i)) continue;
            FXString extension = left->getItemText(i).section('\t', left->getHeaderIndex(FileList::ID_COL_EXT));
            assert(compare(previous, extension) <= 0);
            previous = extension;
        }
        right->setColumnLayout({FileList::ID_COL_NAME, FileList::ID_COL_SIZE});
        puts("PASS: sorting resolves field indices from the current pane, not the other pane's layout");
    }
    std::vector<FXuint> sharedLayout = explorer->getLeftPanel()->getList()->getColumnLayout();
    for (FXuint i = 0; i < explorer->getLeftPanel()->getTabButtons()->getNumActiveTabs(); ++i)
    {
        explorer->getLeftPanel()->getTabButtons()->setCurrentTab(i);
        assert(explorer->getLeftPanel()->getList()->getColumnLayout() == sharedLayout);
    }
    panel->setActive();
    files->setDirectory(data);
    puts("PASS: Preferences edits left/right columns independently, Cancel discards, Accept applies immediately to every tab");
    checkColumnWidthPreferences(app, explorer, fixture);
    assert(explorer->onCmdUserManual(NULL, 0, NULL) == 1);
    for (int i = 0; i < 500 && access(browserLog.text(), F_OK) != 0; ++i)
    {
        app.runOneEvent(false);
        usleep(10000);
    }
    {
        std::ifstream browser(browserLog.text());
        std::string uri;
        std::getline(browser, uri);
        assert(FXURL::decode(uri.c_str()) == xf_filetouri(prefix + "/share/swordfish/manual/index.html"));
        assert(uri.find(' ') == std::string::npos && uri.find("%25") != std::string::npos);
    }
    assert(unlink(browserLog.text()) == 0);
    puts("PASS: User Manual uses the host browser and safely encodes spaces and percent signs in its local path");
    for (const char* name : {"sample.zip", "sample.iso"})
    {
        files->killSelection();
        int index = 0;
        while (index < files->getNumItems() && files->getItemFilename(index) != name) ++index;
        assert(index < files->getNumItems());
        files->setCurrentItem(index);
        files->selectItem(index);
        ArchiveMenu menu(&app);
        app.addTimeout(&menu, 1, 10);
        FXEvent event = {};
        event.code = KEY_Menu;
        panel->onCmdPopupMenu(NULL, 0, &event);
        assert(menu.seen);
        ArchivePrompt prompt(&app);
        app.addTimeout(&prompt, 1, 10);
        panel->onCmdArchiveOpen(NULL, 0, NULL);
        assert(prompt.seen && access(browserLog.text(), F_OK) != 0);
    }
    ArchivePrompt accept(&app);
    accept.accept = true;
    app.addTimeout(&accept, 1, 10);
    panel->onCmdArchiveOpen(NULL, 0, NULL);
    assert(accept.seen);
    for (int i = 0; i < 500 && access(browserLog.text(), F_OK) != 0; ++i)
    {
        app.runOneEvent(false);
        usleep(10000);
    }
    std::ifstream browser(browserLog.text());
    std::string url;
    std::getline(browser, url);
    if (url.empty()) fprintf(stderr, "Browser log missing after accepting archive installation help\n");
    assert(url == "https://github.com/cybernoid/archivemount#installation");
    puts("PASS: ZIP/ISO archive-folder menu remains visible without archivemount; declining opens nothing, accepting opens installation help with host libraries");
    // ISOs without Unix ownership (e.g. Windows media) show root-only entries unless mounted as the user
    if (getuid() != 0 && access("/usr/bin/archivemount", X_OK) == 0 && access("/dev/fuse", R_OK | W_OK) == 0 &&
        system("command -v bsdtar >/dev/null") == 0)
    {
        const FXString zipDir = fixture + "/zip";
        assert(mkdir(zipDir.text(), 0755) == 0 && mkdir((zipDir + "/src").text(), 0755) == 0);
        { std::ofstream inner((zipDir + "/src/inside.txt").text()); inner << "inside\n"; }
        FXString buildZip = "bsdtar --format zip -cf " + xf_quote(zipDir + "/sample.zip") +
                            " -C " + xf_quote(zipDir + "/src") + " inside.txt";
        assert(system(buildZip.text()) == 0);
        const FXString isoDir = fixture + "/iso";
        assert(mkdir(isoDir.text(), 0755) == 0 && mkdir((isoDir + "/src").text(), 0755) == 0 &&
               mkdir((isoDir + "/src/sub").text(), 0755) == 0);
        { std::ofstream inner((isoDir + "/src/sub/inside.txt").text()); inner << "inside\n"; }
        FXString build = "bsdtar --format iso9660 --options '!rockridge,joliet' -cf " +
                         xf_quote(isoDir + "/windows.iso") + " -C " + xf_quote(isoDir + "/src") + " .";
        assert(system(build.text()) == 0 && chmod((isoDir + "/windows.iso").text(), 0444) == 0);
        setenv("SWORDFISH_HOST_PATH", "/usr/bin:/bin", 1);
        // main.cpp registers this at startup; File::mount() swaps it while archivemount runs
        app.addSignal(SIGCHLD, explorer, XFileExplorer::ID_HARVEST);
        auto selectZip = [&]()
        {
            files->setDirectory(zipDir);
            int index = -1;
            for (int tries = 0; tries < 300 && index < 0; ++tries)
            {
                for (int i = 0; i < files->getNumItems(); ++i)
                    if (files->getItemFilename(i) == "sample.zip") index = i;
                if (index < 0) { app.runOneEvent(false); usleep(10000); }
            }
            assert(index >= 0);
            files->killSelection();
            files->setCurrentItem(index);
            files->selectItem(index);
        };
        selectZip();
        ArchiveReadOnlyNotice readOnlyNotice(&app);
        app.addTimeout(&readOnlyNotice, 1, 10);
        panel->onCmdArchiveOpen(NULL, 0, NULL);
        assert(readOnlyNotice.seen);
        const FXString zipMounted = zipDir + "/sample_archive";
        assert(access((zipMounted + "/inside.txt").text(), R_OK) == 0);
        std::ofstream writeAttempt((zipMounted + "/new-file.txt").text());
        assert(!writeAttempt && !xf_existfile(zipMounted + "/new-file.txt"));
        selectZip();
        panel->onCmdArchiveClose(NULL, 0, NULL);
        for (int tries = 0; tries < 300 && xf_existfile(zipMounted + "/inside.txt"); ++tries)
        { app.runOneEvent(false); usleep(10000); }
        assert(!xf_existfile(zipMounted + "/inside.txt"));
        puts("PASS: ZIP archives mount read-only, explain that files can be dragged or copied out, and reject writes");
        {
            const FXString tarDir = fixture + "/tar";
            assert(mkdir(tarDir.text(), 0755) == 0 && mkdir((tarDir + "/src").text(), 0755) == 0);
            { std::ofstream inner((tarDir + "/src/inside.txt").text()); inner << "inside\n"; }
            assert(system(("tar -czf " + xf_quote(tarDir + "/sample.tar.gz") + " -C " +
                           xf_quote(tarDir + "/src") + " inside.txt").text()) == 0);
            files->setDirectory(tarDir);
            int index = -1;
            for (int tries = 0; tries < 300 && index < 0; ++tries)
            {
                for (int i = 0; i < files->getNumItems(); ++i)
                    if (files->getItemFilename(i) == "sample.tar.gz") index = i;
                if (index < 0) { app.runOneEvent(false); usleep(10000); }
            }
            assert(index >= 0);
            files->killSelection();
            files->setCurrentItem(index);
            files->selectItem(index);
            panel->onCmdArchiveOpen(NULL, 0, NULL);
            const FXString tarMounted = tarDir + "/sample_archive";
            { std::ofstream added((tarMounted + "/added.txt").text()); assert(added); added << "added\n"; }
            files->setDirectory(tarDir);
            for (int i = 0; i < files->getNumItems(); ++i)
                if (files->getItemFilename(i) == "sample_archive") { files->killSelection(); files->selectItem(i); }
            panel->onCmdArchiveClose(NULL, 0, NULL);
            // Closing waits for archivemount to finish saving, so the archive is complete straight away
            FXString listing;
            assert(xf_hostcommandoutput("tar -tzf " + xf_quote(tarDir + "/sample.tar.gz"), listing));
            assert(listing.contains("added.txt") && listing.contains("inside.txt"));
            assert(!xf_existfile(tarDir + "/sample.tar.gz.failed"));
            puts("PASS: tar archives stay writable; closing waits for the save and the saved archive is verified");
        }
        auto selectIso = [&]()
        {
            files->setDirectory(isoDir);
            int index = -1;
            for (int tries = 0; tries < 300 && index < 0; ++tries)
            {
                for (int i = 0; i < files->getNumItems(); ++i)
                    if (files->getItemFilename(i) == "windows.iso") index = i;
                if (index < 0) { app.runOneEvent(false); usleep(10000); }
            }
            assert(index >= 0);
            files->killSelection();
            files->setCurrentItem(index);
            files->selectItem(index);
        };
        selectIso();
        panel->onCmdArchiveOpen(NULL, 0, NULL);
        const FXString mounted = isoDir + "/windows_archive";
        struct stat info;
        assert(stat((mounted + "/sub").text(), &info) == 0 && info.st_uid == getuid());
        assert(access((mounted + "/sub").text(), R_OK | X_OK) == 0);
        assert(access((mounted + "/sub/inside.txt").text(), R_OK) == 0);
        selectIso();
        panel->onCmdArchiveClose(NULL, 0, NULL);
        for (int tries = 0; tries < 300 && xf_existfile(mounted + "/sub"); ++tries)
        { app.runOneEvent(false); usleep(10000); }
        assert(!xf_existfile(mounted + "/sub"));
        setenv("SWORDFISH_HOST_PATH", tools.text(), 1);
        files->setDirectory(data);
        puts("PASS: an ISO without Unix ownership opens as a folder readable by the user");
    }
    explorer->onCmdFileSearch(NULL, 0, NULL);
    SearchWindow* savedSearch = explorer->getSearchWindow();
    assert(savedSearch);
    FileList* searchFiles = findList(savedSearch);
    assert(searchFiles);
    searchFiles->setColumnLayout({FileList::ID_COL_NAME, FileList::ID_COL_DIRNAME, FileList::ID_COL_SIZE});
    searchFiles->moveColumn(2, 1);
    searchFiles->setHeaderSize(2, 287);
    FilePanel* savedPanels[] = {explorer->getLeftPanel(), explorer->getRightPanel()};
    const char* savedSections[] = {"LEFT TABS", "RIGHT TABS"};
    for (FXint pane = 0; pane < 2; ++pane)
    {
        FileList* paneFiles = savedPanels[pane]->getList();
        paneFiles->setColumnLayout({FileList::ID_COL_NAME, FileList::ID_COL_SIZE, FileList::ID_COL_TYPE});
        paneFiles->onToggleColumn(NULL, FXSEL(SEL_COMMAND, FileList::ID_COLUMN_TOGGLE + 1), NULL);
        paneFiles->onToggleColumn(NULL, FXSEL(SEL_COMMAND, FileList::ID_COLUMN_TOGGLE + 3), NULL);
        paneFiles->moveColumn(2, 1);
        paneFiles->setHeaderSize(1, 193 + pane);
        TabButtons* savedTabs = savedPanels[pane]->getTabButtons();
        savedTabs->saveTabs(savedSections[pane]);
        app.reg().writeUnsignedEntry(savedSections[pane], "test_count", savedTabs->getNumActiveTabs());
    }
    assert(FXStat::isDirectory(xdgconfighome) || mkdir(xdgconfighome.text(), 0700) == 0);
    app.reg().writeUnsignedEntry("OPTIONS", "restore_tabs", true);
    assert(app.reg().write());
    assert(app.reg().unparseFile(fixture + "/session-expected.rc"));
    explorer->saveConfig();
    FXSettings savedSession;
    assert(savedSession.parseFile(xdgconfighome + "/swordfish/" + app.getAppName() + "rc", true));
    assert(savedSession.readUnsignedEntry("SEARCH PANEL", "dir_size", 0) == 287);
    assert(savedSession.readUnsignedEntry("OPTIONS", "panel_view", 99) == 3);
    pid_t restoreChild = fork();
    assert(restoreChild >= 0);
    if (!restoreChild)
    {
        setenv("SWORDFISH_TEST_RESTORE_SESSION", "1", 1);
        execl("/proc/self/exe", "swordfish-session-test", prefix.text(), fixture.text(), (char*)NULL);
        _exit(127);
    }
    int restoreStatus;
    assert(waitpid(restoreChild, &restoreStatus, 0) == restoreChild);
    assert(WIFEXITED(restoreStatus) && WEXITSTATUS(restoreStatus) == 0);
    puts("PASS: full shutdown save and fresh-process startup restore both panes, tabs, active tabs and changed columns; search directory width follows its column");
    std::fflush(stdout);
    // Match the existing widget suite: application-global teardown is not under test.
    std::_Exit(0);
}
