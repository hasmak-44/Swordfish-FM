#include "config.h"
#include <fx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
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
#include <fxkeys.h>
#include <dlfcn.h>
#include <fstream>
#include <iterator>
#include <vector>
#include <sys/stat.h>

extern FXMainWindow* mainWindow;
extern FXString execpath, homedir, xdgdatahome, xdgconfighome;
extern FXString clipboard;

static FXString crossDeviceSource;
static int crossDeviceRenames;

extern "C" int rename(const char* source, const char* destination) noexcept
{
    if (!crossDeviceSource.empty() && FXString(source).left(crossDeviceSource.length()) == crossDeviceSource)
    {
        ++crossDeviceRenames;
        errno = EXDEV;
        return -1;
    }
    typedef int (*Rename)(const char*, const char*);
    static Rename realRename = reinterpret_cast<Rename>(dlsym(RTLD_NEXT, "rename"));
    return realRename(source, destination);
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

int main(int argc, char** argv)
{
    assert(argc == 3);
    FXString prefix = argv[1], fixture = argv[2];
    FXApp app("SwordfishSearchTest", "SwordfishSearchTest");
    app.init(argc, argv);
    homedir = FXSystem::getHomeDirectory();
    xdgconfighome = homedir + "/config";
    xdgdatahome = homedir + "/data";
    execpath = prefix + "/bin/swordfish";
    FXuint status;
    assert(loadAppIcons(&app, &status));
    if (getenv("SWORDFISH_TEST_RESTORE_SESSION"))
    {
        assert(app.reg().read());
        vector_FXString noPaths;
        XFileExplorer* restored = new XFileExplorer(&app, noPaths, -1);
        mainWindow = restored;
        app.create();
        FilePanel* panels[] = {restored->getLeftPanel(), restored->getRightPanel()};
        const char* sections[] = {"LEFT TABS", "RIGHT TABS"};
        FXSettings expected;
        assert(expected.parseFile(fixture + "/session-expected.rc", true));
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
                for (const auto& width : state.columns[i])
                {
                    key = "width" + FXStringVal(i) + "_" + FXStringVal(width.first);
                    assert(width.second == expected.readUnsignedEntry(sections[pane], key.text(), 0));
                }
                FXString order;
                for (FXuint id : state.columnOrder)
                {
                    if (!order.empty()) order += ",";
                    order += FXStringVal(id - FileList::ID_COL_NAME);
                }
                assert(order == expected.readStringEntry(pane ? "RIGHT PANEL" : "LEFT PANEL",
                                                          "column_layout", ""));
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
    checkColumns(files);
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
    assert(FXStat::isFile(target + "/other.txt"));
    assert(unlink((target + "/other.txt").text()) == 0);
    files->selectItem(sourceIndex);
    panel->onCmdOtherPane(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_LINK_OTHER_PANE), NULL);
    assert(FXStat::isLink(target + "/other.txt"));
    assert(unlink((target + "/other.txt").text()) == 0);
    files->selectItem(sourceIndex);
    panel->onCmdFileMan(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_FILE_CUT), NULL);
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
