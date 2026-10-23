#include <fx.h>
#include "FileList.h"
#include "DirList.h"
#include "Places.h"
#include "XFileExplorer.h"
#include "RemoteDirectory.h"
#include "DialogBox.h"
#include "TabButtons.h"
#include "File.h"
#include "MessageBox.h"
#include "icons.h"
#include <X11/Xlib.h>
#include <cassert>
#include <chrono>
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
        dialog->handle(this, FXSEL(SEL_COMMAND, MessageBox::ID_CLICKED_OK), NULL);
        return 1;
    }
};

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
    mainWindow = NULL;
    XFileExplorer* window = new XFileExplorer(&app, paths, 3);
    mainWindow = window;
    window->create();
    window->show();
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
    // A single click opens a normal share-browser tab without mounting anything.
    TabButtons* tabs = window->getCurrentPanel()->getTabButtons();
    const FXuint previousTab = tabs->getCurrentTab();
    const FXuint previousCount = tabs->getNumActiveTabs();
    panel->onSidebarActivate(NULL, FXSEL(SEL_COMMAND, DirPanel::ID_SIDEBAR), dell);
    FilePanel* sharePanel = window->getCurrentPanel();
    assert(sharePanel->getDirectory() == "smb://192.168.1.104");
    assert(tabs->getNumActiveTabs() == previousCount + 1);
    const FXuint serverTab = tabs->getCurrentTab();
    pump(app, 650);
    assert(sharePanel->getList()->getNumItems() == 2);
    assert(sharePanel->getList()->getItemFilename(0) == "5TB");
    assert(sharePanel->getList()->getItemFilename(1) == "Team files");
    assert(sharePanel->getList()->getItemPathname(1) == "smb://192.168.1.104/Team%20files");
    assert(access(getenv("SWORDFISH_TEST_MOUNT_LOG"), F_OK) != 0);
    tabs->onCmdPathButton(NULL, FXSEL(SEL_COMMAND, TabButtons::ID_START_TAB + previousTab), NULL);
    assert(sharePanel->getDirectory() != "smb://192.168.1.104");
    panel->onSidebarActivate(NULL, FXSEL(SEL_COMMAND, DirPanel::ID_SIDEBAR), dell);
    assert(tabs->getNumActiveTabs() == previousCount + 1 && tabs->getCurrentTab() == serverTab);
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
    puts("PASS: server single-click lists shares in a tab without mounting; share double-click mounts only the chosen share");
    // A share whose gio mount never returns must not freeze the window forever.
    ConnectCanceller canceller;
    testApp->addTimeout(&canceller, 1, 50);
    FXString hungPath;
    const time_t hungStart = time(NULL);
    assert(sharePanel->resolveNetworkPath("smb://hang.test/share", hungPath) == -1);
    assert(time(NULL) - hungStart < 20);
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
    assert(xf_gvfsmounturi(gvfsBase + "mtp:host=%5Busb%3A001%2C002%5D") ==
           "mtp://%5Busb%3A001%2C002%5D/");
    assert(xf_gvfsmounturi(gvfsBase + "smb-share:share=data").empty());
    const FXbool mountMessages = app.reg().readUnsignedEntry("OPTIONS", "mount_messages", true);
    app.reg().writeUnsignedEntry("OPTIONS", "mount_messages", false);
    MountMessageResponder mountMessagesResponder;
    File* unmount = new File(window, "Unmount regression", UNMOUNT);
    unmount->create();
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(unmount->mount(fixture, "Unmount regression", "printf 'ordinary unmount failed'; exit 7") == -1);
    assert(mountMessagesResponder.errors == 1 && mountMessagesResponder.successes == 0);
    assert(mountMessagesResponder.message == "ordinary unmount failed");
    setenv("SWORDFISH_TEST_UNMOUNT_FAIL", "1", 1);
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(unmount->mount(simulatedMount, "Unmount regression", "exit 99") == -1);
    assert(mountMessagesResponder.errors == 2 && mountMessagesResponder.message.contains("share is busy"));
    unsetenv("SWORDFISH_TEST_UNMOUNT_FAIL");
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
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(sharePanel->onCmdMount(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_UMOUNT), NULL) == 0);
    assert(mountMessagesResponder.errors == 4 && mountMessagesResponder.successes == 0);
    assert(sharePanel->getDirectory() == simulatedMount);
    assert(!busyCursor);
    unsetenv("SWORDFISH_TEST_UNMOUNT_FAIL");
    app.reg().writeUnsignedEntry("OPTIONS", "mount_messages", true);
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(sharePanel->onCmdMount(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_UMOUNT), NULL) == 1);
    assert(mountMessagesResponder.errors == 4 && mountMessagesResponder.successes == 1);
    assert(!busyCursor);
    assert(sharePanel->getDirectory() == fixture + "/large");
    assert(sharePanel->getTabButtons()->getNumActiveTabs() == inactiveMountTab);
    assert(sharePanel->getTabButtons()->getPath(activeMountTab).empty());
    assert(sharePanel->getTabButtons()->getPath(serverTab) == fixture + "/large");
    delete unmount;
    app.reg().writeUnsignedEntry("OPTIONS", "mount_messages", mountMessages);
    std::ifstream unmountLog(getenv("SWORDFISH_TEST_MOUNT_LOG"));
    int unmountCommands = 0;
    while (std::getline(unmountLog, mountedUri))
        if (mountedUri == "unmount smb://unmount.test/Team%20files/") ++unmountCommands;
    assert(unmountCommands == 4);
    puts("PASS: GVFS unmount uses host gio, rejects failures and still-mounted results, closes active/inactive tabs");

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
            if (item->getText() == name) ++count;
        return count;
    };
    assert(deviceCount("4tb@mx-hp.local") == 1 && deviceCount("5tb@mx-hp.local") == 1);
    FXString mount5 = gvfsBase + FXPath::name(disk5);
    FXuint beforeExternal = sharePanel->getTabButtons()->getNumActiveTabs();
    sharePanel->getTabButtons()->addTab(mount5 + "/nested");
    sharePanel->getTabButtons()->addTab(mount5);
    assert(rmdir(disk5.text()) == 0);
    pump(app, 1500);
    assert(deviceCount("5tb@mx-hp.local") == 0 && deviceCount("4tb@mx-hp.local") == 1);
    assert(sharePanel->getTabButtons()->getNumActiveTabs() == beforeExternal);
    assert(sharePanel->getDirectory() == fixture + "/folder");
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        assert(mkdir(disk5.text(), 0700) == 0);
        pump(app, 1500);
        assert(deviceCount("5tb@mx-hp.local") == 1);
        assert(rmdir(disk5.text()) == 0);
        pump(app, 1500);
        assert(deviceCount("5tb@mx-hp.local") == 0);
    }
    FilePanel* otherPanel = window->getRightPanel();
    assert(otherPanel != sharePanel);
    otherPanel->getTabButtons()->clearTabs();
    otherPanel->getTabButtons()->addTab(mount5);
    otherPanel->getTabButtons()->addTab(mount5 + "/nested");
    otherPanel->getTabButtons()->closeMount(mount5);
    assert(otherPanel->getTabButtons()->getNumActiveTabs() == 1);
    assert(otherPanel->getDirectory() == FXSystem::getHomeDirectory());
    assert(otherPanel->getTabButtons()->getPath(0) == FXSystem::getHomeDirectory());
    assert(rmdir(disk4.text()) == 0);
    assert(rmdir(simulatedGvfsDirectory.text()) == 0);
    simulatedGvfsDirectory.clear();
    puts("PASS: external unmount removes Devices entry and share tabs, repeated cycles leave no duplicates, last tab becomes Home");

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
    XCloseDisplay(otherClient);
    std::fflush(stdout);
    // The application-global teardown is outside this focused widget test.
    std::_Exit(0);
}
