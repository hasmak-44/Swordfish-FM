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
    navigation->setFocus();
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(sharePanel->onCmdMount(NULL, FXSEL(SEL_COMMAND, FilePanel::ID_UMOUNT), NULL) == 0);
    assert(mountMessagesResponder.errors == 4 && mountMessagesResponder.successes == 0);
    assert(sharePanel->getDirectory() != simulatedMount);
    assert(!busyCursor);
    unsetenv("SWORDFISH_TEST_UNMOUNT_FAIL");
    // Tabs were left before the failed attempt; reopen them for the successful case.
    sharePanel->getTabButtons()->addTab(simulatedMount + "/nested");
    inactiveMountTab = sharePanel->getTabButtons()->getCurrentTab();
    sharePanel->getTabButtons()->addTab(simulatedMount);
    activeMountTab = sharePanel->getTabButtons()->getCurrentTab();
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
    const FXString label4 = "4tb@" + RemoteDirectory::hostName("mx-hp.local");
    const FXString label5 = "5tb@" + RemoteDirectory::hostName("mx-hp.local");
    assert(deviceCount(label4) == 1 && deviceCount(label5) == 1);
    for (FXTreeItem* entry = devices->getFirst(); entry; entry = entry->getNext())
    {
        SidebarItem* device = static_cast<SidebarItem*>(entry);
        if (device->path == ROOTDIR) assert(device->deviceAction == SidebarItem::NO_ACTION);
        if (device->getText() == label4 || device->getText() == label5)
            assert(device->deviceAction == SidebarItem::UNMOUNT_DEVICE);
    }
    FXString mount5 = gvfsBase + FXPath::name(disk5);
    FXuint beforeExternal = sharePanel->getTabButtons()->getNumActiveTabs();
    sharePanel->getTabButtons()->addTab(mount5 + "/nested");
    sharePanel->getTabButtons()->addTab(mount5);
    assert(rmdir(disk5.text()) == 0);
    pump(app, 1500);
    assert(deviceCount(label5) == 0 && deviceCount(label4) == 1);
    assert(sharePanel->getTabButtons()->getNumActiveTabs() == beforeExternal);
    assert(sharePanel->getDirectory() == fixture + "/folder");
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        assert(mkdir(disk5.text(), 0700) == 0);
        pump(app, 1500);
        assert(deviceCount(label5) == 1);
        assert(rmdir(disk5.text()) == 0);
        pump(app, 1500);
        assert(deviceCount(label5) == 0);
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
    setenv("SWORDFISH_TEST_DISK_BUSY", "1", 1);
    const int previousErrors = mountMessagesResponder.errors;
    app.addTimeout(&mountMessagesResponder, 1, 20);
    assert(!panel->unmountRemovable(fixture + "/folder", false));
    assert(mountMessagesResponder.errors == previousErrors + 1);
    assert(mountMessagesResponder.message.contains("still in use"));
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
    // Exercise the visible device-row button, not only the command handler.
    SidebarItem* rowButton = new SidebarItem("USB action test", miniremovabledeviceicon,
                                             SidebarItem::DEVICE, fixture + "/folder");
    rowButton->deviceAction = SidebarItem::EJECT_DEVICE;
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
    assert(navigation->deviceButtonAt(buttonX - 30, buttonY) == NULL);
    FXEvent buttonEvent = {};
    buttonEvent.win_x = buttonX;
    buttonEvent.win_y = buttonY;
    navigation->onDevicePress(NULL, 0, &buttonEvent);
    buttonEvent.win_x = buttonX - 30;
    navigation->onDeviceRelease(NULL, 0, &buttonEvent);
    assert(!navigation->grabbed() && !xf_mountpointof("/dev/test-usb1").empty());
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
        "disk mount", "disk unmount", "disk power-off"}));
    unsetenv("SWORDFISH_TEST_DISK_MTAB");
    unsetenv("SWORDFISH_TEST_DISK_PATH");
    app.reg().writeUnsignedEntry("OPTIONS", "mount_messages", mountMessages);
    assert(unlink(simulatedMountTable.text()) == 0);
    simulatedMountTable.clear();
    puts("PASS: desktop-service mount, busy-unmount failure after one retry, transient busy retried successfully, remount and safe eject execute the expected commands");
    puts("PASS: visible device action button ejects on release; releasing outside cancels without unmounting");

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
