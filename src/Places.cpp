#include "config.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <set>
#include <string>

#if defined(linux)
#include <mntent.h>
#endif

#include <fx.h>
#include <fxkeys.h>
#include <FXPNGIcon.h>

#include "xfedefs.h"
#include "xfeutils.h"
#include "MessageBox.h"
#include "Places.h"
#include "DirList.h"
#include "AppLogger.h"
#include "RemoteDirectory.h"

FXDEFMAP(SidebarTree) SidebarTreeMap[] =
{
    FXMAPFUNC(SEL_COMMAND, SidebarTree::ID_REMOTE_CHANGED, SidebarTree::onRemoteChanged),
    FXMAPFUNC(SEL_COLLAPSED, 0, SidebarTree::onCollapseItem),
    FXMAPFUNC(SEL_EXPANDED, 0, SidebarTree::onExpandItem),
    FXMAPFUNC(SEL_QUERY_TIP, 0, SidebarTree::onQueryTip),
    FXMAPFUNC(SEL_DND_ENTER, 0, SidebarTree::onDNDEnter),
    FXMAPFUNC(SEL_DND_MOTION, 0, SidebarTree::onDNDMotion),
    FXMAPFUNC(SEL_DND_DROP, 0, SidebarTree::onDNDDrop)
};
FXIMPLEMENT(SidebarTree, FXTreeList, SidebarTreeMap, ARRAYNUMBER(SidebarTreeMap))
FXIMPLEMENT(SidebarFrame, FXPacker, NULL, 0)

SidebarTree::SidebarTree(FXComposite* parent, FXObject* target, FXSelector selector, FXuint opts) :
    FXTreeList(parent, target, selector, opts | TREELIST_BROWSESELECT | TREELIST_SHOWS_BOXES |
               TREELIST_ROOT_BOXES | TREELIST_SHOWS_LINES)
{
    dropEnable();
}

FXint SidebarTree::getContentHeight()
{
    return FXTreeList::getContentHeight() + clearance;
}

void SidebarTree::setClearance(FXint value)
{
    if (clearance != value)
    {
        clearance = value;
        recalc();
    }
}

// Items with their own tooltip text (discovered servers) show it instead of their label
long SidebarTree::onQueryTip(FXObject* sender, FXSelector sel, void* ptr)
{
    if (FXWindow::onQueryTip(sender, sel, ptr))
    {
        return 1;
    }
    if ((flags & FLAG_TIP) && !(flags & FLAG_PRESSED))
    {
        FXint x, y;
        FXuint buttons;
        getCursorPosition(x, y, buttons);
        SidebarItem* item = static_cast<SidebarItem*>(getItemAt(x, y));
        if (item && !item->tip.empty())
        {
            FXString tip = item->tip;
            sender->handle(this, FXSEL(SEL_COMMAND, ID_SETSTRINGVALUE), (void*)&tip);
            return 1;
        }
    }
    return FXTreeList::onQueryTip(sender, sel, ptr);
}

long SidebarTree::onExpandItem(FXObject*, FXSelector, void* ptr)
{
    const FXTime started = FXThread::time();
    SidebarItem* item = static_cast<SidebarItem*>(ptr);
    if (grabbed())
    {
        // A slow filesystem must not keep the desktop's pointer grabbed.
        ungrab();
        getApp()->flush();
    }
    xf_setwaitcursor(getApp(), BEGIN_CURSOR);
    if (item && RemoteDirectory::handles(item->path))
    {
        RemoteDirectory::entries(getApp(), item->path, this, ID_REMOTE_CHANGED);
        item->loaded = true;
        onRemoteChanged(NULL, 0, NULL);
        xf_setwaitcursor(getApp(), END_CURSOR);
        return 1;
    }
    FXStat info;
    if (item && !item->path.empty() && item->kind != SidebarItem::SERVER &&
        (!item->loaded || (FXStat::statFile(item->path, info) && info.modified() != item->modified)))
    {
        FXDir directory;
        if (!directory.open(item->path))
        {
            xf_setwaitcursor(getApp(), END_CURSOR);
            MessageBox::error(this, BOX_OK, _("Error"), _("Can't open folder %s: %s"),
                              item->path.text(), strerror(errno));
            return 1;
        }
        while (item->getFirst())
        {
            removeItem(item->getFirst());
        }
        while (directory.next())
        {
            FXString name = directory.name();
            if (name.empty() || name[0] == '.')
            {
                continue;
            }
            FXString path = FXPath::absolute(item->path, name);
            if (FXStat::isDirectory(path))
            {
                SidebarItem* child = new SidebarItem(name, minifoldericon, SidebarItem::DIRECTORY, path);
                child->setHasItems(true);
                appendItem(item, child);
            }
        }
        directory.close();
        item->loaded = true;
        if (FXStat::statFile(item->path, info))
        {
            item->modified = info.modified();
        }
        sortChildItems(item);
        recalc();
    }
    xf_setwaitcursor(getApp(), END_CURSOR);
    const FXTime elapsed = FXThread::time() - started;
    if (item && elapsed >= 1000000000LL)
    {
        appLog(APP_LOG_FILE_OPERATIONS, "slow-sidebar-expansion",
               FXString("path=\"") + item->path + "\" elapsed_ms=" +
               FXStringVal(elapsed / 1000000) + " children=" + FXStringVal(item->getNumChildren()));
    }
    return 1;
}

SidebarTree::~SidebarTree()
{
    RemoteDirectory::release(this);
    if (remoteBusy) xf_setwaitcursor(getApp(), END_CURSOR);
}

long SidebarTree::onCollapseItem(FXObject*, FXSelector, void* ptr)
{
    SidebarItem* item = static_cast<SidebarItem*>(ptr);
    if (item && !item->path.empty()) RemoteDirectory::release(this, item->path);
    if (item)
        for (FXTreeItem* child = item->getFirst(); child; child = child->getNext())
        {
            SidebarItem* directory = static_cast<SidebarItem*>(child);
            if (!directory->path.empty()) RemoteDirectory::release(this, directory->path);
        }
    if (remoteBusy && !RemoteDirectory::loading(this))
    {
        xf_setwaitcursor(getApp(), END_CURSOR);
        remoteBusy = false;
    }
    return 1;
}

long SidebarTree::onRemoteChanged(FXObject*, FXSelector, void*)
{
    FXbool busy = false;
    FXString failure;
    for (FXTreeItem* node = getFirstItem(); node; node = node->getBelow())
    {
        SidebarItem* item = static_cast<SidebarItem*>(node);
        if (!item->loaded || !item->isExpanded() || !RemoteDirectory::handles(item->path)) continue;
        const auto& rows = RemoteDirectory::entries(getApp(), item->path, this, ID_REMOTE_CHANGED);
        std::set<std::string> names;
        for (const auto& row : rows)
        {
            if (!S_ISDIR(row.info.st_mode) || row.name[0] == '.') continue;
            names.insert(row.name.text());
            FXTreeItem* child = item->getFirst();
            while (child && child->getText() != row.name) child = child->getNext();
            if (!child)
            {
                SidebarItem* added = new SidebarItem(row.name, minifoldericon, SidebarItem::DIRECTORY,
                                                     FXPath::absolute(item->path, row.name));
                added->setHasItems(true);
                appendItem(item, added);
            }
        }
        const FXbool pending = RemoteDirectory::loading(item->path);
        busy |= pending;
        FXString diagnostic = RemoteDirectory::error(item->path);
        if (!diagnostic.empty()) failure = diagnostic;
        if (!pending && diagnostic.empty())
        {
            for (FXTreeItem* child = item->getFirst(); child;)
            {
                FXTreeItem* next = child->getNext();
                if (names.find(child->getText().text()) == names.end()) removeItem(child);
                child = next;
            }
            item->setHasItems(item->getFirst() != NULL);
        }
        sortChildItems(item);
    }
    if (remoteBusy != busy)
    {
        xf_setwaitcursor(getApp(), busy ? BEGIN_CURSOR : END_CURSOR);
        remoteBusy = busy;
    }
    recalc();
    if (!busy && !failure.empty() && remoteError != failure)
    {
        remoteError = failure;
        MessageBox::error(this, BOX_OK, _("Error"), _("Remote folder loading failed: %s"), failure.text());
    }
    if (failure.empty()) remoteError.clear();
    return 1;
}
long SidebarTree::onDNDEnter(FXObject*, FXSelector, void*)
{
    tabType = getApp()->registerDragType(TABBUTTONS_DRAG_TYPE);
    uriType = getApp()->registerDragType("text/uri-list");
    return 1;
}

long SidebarTree::onDNDMotion(FXObject*, FXSelector, void* ptr)
{
    // Drop events carry window coordinates of the drag source, so locate the item from root coordinates
    FXEvent* event = static_cast<FXEvent*>(ptr);
    FXint x, y;
    getRoot()->translateCoordinatesTo(x, y, this, event->root_x, event->root_y);
    SidebarItem* item = static_cast<SidebarItem*>(getItemAt(x, y));
    while (item && item->getParent())
    {
        item = static_cast<SidebarItem*>(item->getParent());
    }
    if (bookmarks && item && item->key == "bookmarks" &&
        (offeredDNDType(FROM_DRAGNDROP, tabType) || offeredDNDType(FROM_DRAGNDROP, uriType)))
    {
        acceptDrop(DRAG_COPY);
        return 1;
    }
    acceptDrop(DRAG_REJECT);
    return 0;
}

long SidebarTree::onDNDDrop(FXObject*, FXSelector, void* ptr)
{
    if (!onDNDMotion(NULL, 0, ptr))
    {
        return 0;
    }
    FXuchar* data = NULL;
    FXuint length = 0;
    if (getDNDData(FROM_DRAGNDROP, tabType, data, length))
    {
        FXString path(reinterpret_cast<FXchar*>(data), length);
        FXFREE(&data);
        bookmarks->bookmarkFolder(path);
    }
    else if (getDNDData(FROM_DRAGNDROP, uriType, data, length))
    {
        FXString uris(reinterpret_cast<FXchar*>(data), length);
        FXFREE(&data);
        uris.substitute("\r", "");
        for (int i = 0; i <= uris.contains('\n'); ++i)
        {
            FXString uri = uris.section('\n', i);
            if (uri.left(7) == "file://")
            {
                bookmarks->bookmarkFolder(FXURL::decode(FXURL::fileFromURL(uri)));
            }
        }
    }
    else
    {
        return 0;
    }
    return 1;
}

SidebarFrame::SidebarFrame(FXComposite* parent) :
    FXPacker(parent, FRAME_NONE | LAYOUT_FILL_X | LAYOUT_FILL_Y, 0, 0, 0, 0, 0, 0, 0, 0) {}

FXint SidebarFrame::getDefaultWidth() { return 220; }
FXint SidebarFrame::getDefaultHeight() { return 300; }

void SidebarFrame::layout()
{
    if (content)
    {
        content->position(0, 0, width, height);
    }
    else if (tree)
    {
        tree->position(0, 0, width, height);
    }
    if (overlay && overlay->shown() && tree)
    {
        FXint overlayHeight = FXMIN(190, FXMAX(60, height / 2));
        FXScrollArea* active = (tabs && tabs->getCurrent() == 1) || !tree->getParent()->shown() ?
                               static_cast<FXScrollArea*>(directoryTree) : tree;
        FXint scrollbar = active && active->horizontalScrollBar()->shown() ?
                          active->horizontalScrollBar()->getHeight() : 0;
        FXint vertical = active && active->verticalScrollBar()->shown() ?
                        active->verticalScrollBar()->getWidth() : 0;
        overlay->position(3, FXMAX(0, height - scrollbar - overlayHeight - 3),
                          FXMAX(1, width - vertical - 6), overlayHeight);
        tree->setClearance(overlayHeight + 6);
        if (directoryTree)
        {
            directoryTree->setPreviewClearance(overlayHeight + 6);
        }
        overlay->raise();
    }
    else if (overlay && tree)
    {
        tree->setClearance(0);
        if (directoryTree)
        {
            directoryTree->setPreviewClearance(0);
        }
    }
    flags &= ~FLAG_DIRTY;
}


// mtab polling interval (ms)
#define MTAB_POLL_INTERVAL    1000


// Margin for places list height (in number of items)
#define PLACES_HEIGHT_MARGIN    2


// Obtain directory path from XDG directory name and user-dirs.dirs file contents
FXString getXDGDirPath(FXString xdgdirname, FXString contents)
{
    int pos1 = contents.find(xdgdirname) + xdgdirname.length() + 2;
    int pos2 = contents.find("\n", pos1) - 1;

    FXString xdgdirpath = contents.mid(pos1, pos2 - pos1);

    xdgdirpath.substitute("$HOME", FXSystem::getHomeDirectory());

    return xf_cleanpath(xdgdirpath);
}


// Map
FXDEFMAP(PlacesList) PlacesListMap[] =
{
    FXMAPFUNC(SEL_QUERY_TIP, 0, PlacesList::onQueryTip)
};


// Object implementation
FXIMPLEMENT(PlacesList, FXList, PlacesListMap, ARRAYNUMBER(PlacesListMap))


// Construct places list
PlacesList::PlacesList(FXWindow* owner, FXComposite* p, FXObject* tgt, FXSelector sel, FXColor listbackcolor,
                       FXColor listforecolor, FXuint opts, int x, int y, int w, int h) :
    FXList(p, tgt, sel, opts | LIST_SINGLESELECT, x, y, w, h)
{
    // Colors
    setTextColor(listforecolor);
    setBackColor(listbackcolor);

    // Dictionary of places
    places = new FXStringDict();

    FXString name, pathname;
    desktopDirectory = FXSystem::getHomeDirectory() + PATHSEPSTRING + "Desktop";

    // Number of displayed items
    FXuint num = 0;

    // Home directory
    name = _("Home");
    pathname = FXSystem::getHomeDirectory();
    places->insert(name.text(), pathname.text());
    FXList::appendItem(name, minifolderhomeicon);
    num++;

    // Read user-dir.dirs file
    FXString userdirsfile = FXSystem::getHomeDirectory() + PATHSEPSTRING + ".config/user-dirs.dirs";
    FILE* file = fopen(userdirsfile.text(), "r");

    if (file)  // File exists
    {
        FXString str;

        char* line = NULL;
        size_t len = 0;
        ssize_t nread;

        while ((nread = getline(&line, &len, file)) != -1)
        {
            str += line;
        }

        fclose(file);
        free(line);

        // Desktop directory
        FXString xdgdirname = "XDG_DESKTOP_DIR";
        pathname = getXDGDirPath(xdgdirname, str);
        if (str.find(xdgdirname + "=") >= 0 && !pathname.empty())
        {
            desktopDirectory = pathname;
        }

        if (pathname != FXSystem::getHomeDirectory())
        {
            name = _("Desktop");
            places->insert(name.text(), pathname.text());
            FXList::appendItem(name, minidesktopicon);
            num++;
        }

        // Downloads directory
        xdgdirname = "XDG_DOWNLOAD_DIR";
        pathname = getXDGDirPath(xdgdirname, str);

        if (pathname != FXSystem::getHomeDirectory())
        {
            name = _("Downloads");
            places->insert(name.text(), pathname.text());
            FXList::appendItem(name, minifolderdownloadicon);
            num++;
        }

        // Documents directory
        xdgdirname = "XDG_DOCUMENTS_DIR";
        pathname = getXDGDirPath(xdgdirname, str);

        if (pathname != FXSystem::getHomeDirectory())
        {
            name = _("Documents");
            places->insert(name.text(), pathname.text());
            FXList::appendItem(name, minifolderdocumentsicon);
            num++;
        }

        // Music directory
        xdgdirname = "XDG_MUSIC_DIR";
        pathname = getXDGDirPath(xdgdirname, str);

        if (pathname != FXSystem::getHomeDirectory())
        {
            name = _("Music");
            places->insert(name.text(), pathname.text());
            FXList::appendItem(name, minifoldermusicicon);
            num++;
        }

        // Images directory
        xdgdirname = "XDG_PICTURES_DIR";
        pathname = getXDGDirPath(xdgdirname, str);

        if (pathname != FXSystem::getHomeDirectory())
        {
            name = _("Images");
            places->insert(name.text(), pathname.text());
            FXList::appendItem(name, minifolderimagesicon);
            num++;
        }

        // Videos directory
        xdgdirname = "XDG_VIDEOS_DIR";
        pathname = getXDGDirPath(xdgdirname, str);

        if (pathname != FXSystem::getHomeDirectory())
        {
            name = _("Videos");
            places->insert(name.text(), pathname.text());
            FXList::appendItem(name, minifoldervideosicon);
            num++;
        }
    }

    // Trash
    name = _("Trash can");
    pathname = FXSystem::getHomeDirectory() + PATHSEPSTRING + ".local/share/Trash/files";
    places->insert(name.text(), pathname.text());
    FXList::appendItem(name, minideleteicon);
    num++;

    // Resize list to the number of items (including a margin)
    FXList::setNumVisible(num + PLACES_HEIGHT_MARGIN);
}


// Create X window
void PlacesList::create()
{
    FXList::create();
}


// Destructor
PlacesList::~PlacesList()
{
    delete places;
}


// Select item from key and set it current
void PlacesList::setPlace(FXString& key)
{
    for (int i = 0; i < getNumItems(); i++)
    {
        if (getItemText(i) == key)
        {
            selectItem(i);
            setCurrentItem(i);
            break;
        }
    }
}


// Display bookmark location tooltip
long PlacesList::onQueryTip(FXObject* sender, FXSelector, void* ptr)
{
    int x, y;
    FXuint state;

    getCursorPosition(x, y, state);
    int index = getItemAt(x, y);

    if (index != -1)
    {
        FXString key = getItemText(index);
        FXString tip = getItemPathname(key);

        sender->handle(this, FXSEL(SEL_COMMAND, ID_SETSTRINGVALUE), (void*)&tip);

        return 1;
    }

    return 0;
}



// Map
FXDEFMAP(MountsList) MountsListMap[] =
{
#if defined(linux)
    FXMAPFUNC(SEL_TIMEOUT, MountsList::ID_MOUNTS_REFRESH, MountsList::onMountsRefresh),
#endif
    FXMAPFUNC(SEL_QUERY_TIP, 0, MountsList::onQueryTip)
};


// Object implementation
FXIMPLEMENT(MountsList, FXList, MountsListMap, ARRAYNUMBER(MountsListMap))


// Construct mount points list
MountsList::MountsList(FXWindow* owner, FXComposite* p, FXObject* tgt, FXSelector sel, FXColor listbackcolor,
                       FXColor listforecolor, FXuint opts, int x, int y, int w, int h) :
    FXList(p, tgt, sel, opts | LIST_SINGLESELECT, x, y, w, h)
{
    // Colors
    setTextColor(listforecolor);
    setBackColor(listbackcolor);

    // Dictionary of mount points
    mounts = new FXStringDict();

#if defined(linux)
    hash_prev = xf_hashfile(MTAB_PATH);
    blocks_prev = blockDevices();
    gvfsmounts_prev = gvfsMounts();
    scan();
#endif
}


// Create X window
void MountsList::create()
{
#if defined(linux)
    getApp()->addTimeout(this, ID_MOUNTS_REFRESH, MTAB_POLL_INTERVAL);
#endif
    FXList::create();
}


// Destructor
MountsList::~MountsList()
{
#if defined(linux)
    getApp()->removeTimeout(this, ID_MOUNTS_REFRESH);
#endif
    delete mounts;
}


// Select item from key and set it current
void MountsList::setMount(FXString& key)
{
    for (int i = 0; i < getNumItems(); i++)
    {
        if (getItemText(i) == key)
        {
            selectItem(i);
            setCurrentItem(i);
            break;
        }
    }
}


#if defined(linux)

// Names in a directory, concatenated (cheap change detection)
static FXString directoryNames(const FXString& path)
{
    FXString names;
    DIR* dirp = opendir(path.text());
    if (dirp)
    {
        struct dirent* dp;
        while ((dp = readdir(dirp)) != NULL)
        {
            names += dp->d_name;
            names += "\n";
        }
        closedir(dirp);
    }
    return names;
}


FXString MountsList::gvfsMounts()
{
    return directoryNames("/run/user/" + FXStringVal(FXSystem::user()) + "/gvfs");
}


FXString MountsList::blockDevices()
{
    return directoryNames("/sys/class/block");
}


// Add a row, keeping labels unique because they are the dictionary keys
void MountsList::addMount(FXString name, const FXString& pathname, FXIcon* icon)
{
    if (mounts->find(name.text()))
    {
        name += " (" + FXPath::name(pathname) + ")";
    }
    mounts->insert(name.text(), pathname.text());
    FXList::appendItem(name, icon);
}


// Rebuild the list from mtab, unmounted drives and GVFS mounts
void MountsList::scan()
{
    // Labels are not unique keys: remove every old row, including duplicates.
    clearItems();
    mounts->clear();

    struct mntent* mnt;
    FILE* mtab = setmntent(MTAB_PATH, "r");
    if (mtab)
    {
        while ((mnt = getmntent(mtab)))
        {
            // Filter out some file systems
            if (!xf_keepmount(mnt->mnt_dir, mnt->mnt_fsname))
            {
                continue;
            }
            FXString pathname = mnt->mnt_dir;

            // Root file system
            if (pathname == ROOTDIR)
            {
                addMount(_("File system"), pathname, miniharddriveicon);
            }
            else if (strncmp(mnt->mnt_type, "cifs", 4) == 0 || strncmp(mnt->mnt_type, "nfs", 3) == 0)
            {
                addMount(pathname, pathname, mininetdriveicon);
            }
            else if (pathname.left(7) == "/media/" || pathname.left(11) == "/run/media/")
            {
                // Desktop mounts are named after their volume, like other file managers
                if (strncmp(mnt->mnt_fsname, "/dev/", 5) == 0)
                {
                    knownDevices.insert(mnt->mnt_fsname, "");
                }
                addMount(FXPath::name(pathname), pathname,
                         strncmp(mnt->mnt_type, "iso", 3) == 0 ? minicdromicon : miniremovabledeviceicon);
            }
        }
        endmntent(mtab);
    }

    // Plugged-in drives that are not mounted, plus any drive mounted earlier in this session.
    // Their path is the device node; activating them mounts them.
    vector_FXString volumes;
    if (xf_unmountedvolumes(volumes))
    {
        for (FXuint i = 0; i < volumes.size(); i++)
        {
            FXString device = volumes[i].section('\t', 0);
            FXString label = volumes[i].section('\t', 1);
            if (volumes[i].section('\t', 2) != "1" && !knownDevices.find(device.text()))
            {
                continue;
            }
            addMount(label.empty() ? FXPath::name(device) : label, device, miniremovabledeviceicon);
        }
    }

    // GVFS mount points
    FXString gvfspath = "/run/user/" + FXStringVal(FXSystem::user()) + "/gvfs";
    DIR* dirp = opendir(gvfspath.text());
    if (dirp)
    {
        struct dirent* dp;
        while ((dp = readdir(dirp)) != NULL)
        {
            FXString gvfsname = dp->d_name;
            FXString mounttype = gvfsname.before(':');
            FXString pathname = gvfspath + PATHSEPSTRING + gvfsname;

            // Samba share
            if (mounttype == "smb-share")
            {
                addMount(RemoteDirectory::mountLabel(gvfsname), pathname, mininetdriveicon);
            }

            // SSH, FTP and WebDAV mounts
            if (mounttype == "sftp" || mounttype == "ftp" || mounttype == "ftps" || mounttype == "dav" ||
                mounttype == "davs")
            {
                addMount(RemoteDirectory::mountLabel(gvfsname), pathname, mininetdriveicon);
            }

            // MTP device
            if (mounttype == "mtp")
            {
                FXString name = gvfsname.after('_');
                addMount(name.before('_', 2), pathname, miniremovabledeviceicon);
            }
        }
        closedir(dirp);
    }
}


// Refresh mount points
long MountsList::onMountsRefresh(FXObject* sender, FXSelector, void* ptr)
{
    FXulong hash = xf_hashfile(MTAB_PATH);
    FXString blocks = blockDevices();
    FXString gvfsmounts = gvfsMounts();

    // Mount points, plugged-in drives or GVFS mounts have changed
    if (hash != hash_prev || blocks != blocks_prev || gvfsmounts != gvfsmounts_prev)
    {
        hash_prev = hash;
        blocks_prev = blocks;
        gvfsmounts_prev = gvfsmounts;
        scan();
    }

    // Restart timer
    getApp()->addTimeout(this, ID_MOUNTS_REFRESH, MTAB_POLL_INTERVAL);

    return 1;
}


// Force a rescan on the next timer tick (after mounting or ejecting)
void MountsList::invalidate()
{
    hash_prev = 0;
    blocks_prev.clear();
}

#endif


// Display bookmark location tooltip
long MountsList::onQueryTip(FXObject* sender, FXSelector, void* ptr)
{
    int x, y;
    FXuint state;

    getCursorPosition(x, y, state);
    int index = getItemAt(x, y);

    if (index != -1)
    {
        FXString key = getItemText(index);
        FXString tip = getItemPathname(key);

        sender->handle(this, FXSEL(SEL_COMMAND, ID_SETSTRINGVALUE), (void*)&tip);

        return 1;
    }

    return 0;
}


// Map
FXDEFMAP(BookmarksList) BookmarksListMap[] =
{
    FXMAPFUNC(SEL_QUERY_TIP, 0, BookmarksList::onQueryTip),
    FXMAPFUNC(SEL_DND_ENTER, 0, BookmarksList::onDNDEnter),
    FXMAPFUNC(SEL_DND_MOTION, 0, BookmarksList::onDNDMotion),
    FXMAPFUNC(SEL_DND_DROP, 0, BookmarksList::onDNDDrop),
};


// Object implementation
FXIMPLEMENT(BookmarksList, FXList, BookmarksListMap, ARRAYNUMBER(BookmarksListMap))


// Construct bookmarks list
BookmarksList::BookmarksList(FXWindow* owner, FXComposite* p, FXObject* tgt, FXSelector sel, FXColor listbackcolor,
                             FXColor listforecolor, FXuint opts, int x, int y, int w, int h) :
    FXList(p, tgt, sel, opts | LIST_SINGLESELECT, x, y, w, h)
{
    // Fractional scaling factor
    FXint res = getApp()->reg().readUnsignedEntry("SETTINGS", "screenres", 100);
    scalefrac = FXMAX(1.0, res / 100.0);

    // Colors
    setTextColor(listforecolor);
    setBackColor(listbackcolor);

    // Tabs and folders can be dropped to bookmark them
    dropEnable();

    // Read bookmark keys from registry
    if (getApp()->reg().readStringEntry("BOOKMARKS", "bookmark0", NULL))
    {
        associations = new FileDict(getApp());

        FXuint i = 0;
        while (1)
        {
            FXString regkey = "bookmark" + FXStringVal(i);
            FXString data = getApp()->reg().readStringEntry("BOOKMARKS", regkey.text(), NULL);

            if (data != "")
            {
                FXString name = getBookmarkName(data);
                FXString pathname = getBookmarkPathname(data);
                FXString iconpathname = getBookmarkIconPathname(data);

                // Append to vector
                bookmarks.push_back(data);

                // Append to list
                if (iconpathname == "" || !xf_existfile(iconpathname))
                {
                    FXList::appendItem(name, minibookmarkicon);   // Default icon
                }
                else
                {
                    FXIcon* icon = xf_loadiconfile(getApp(), FXPath::directory(iconpathname),
                                                FXPath::name(iconpathname), scalefrac, this->getBackColor());
                    if (icon)
                    {
                        icon->create();
                    }

                    FXList::appendItem(name, icon);  // Dedicated icon
                }

                i++;
            }
            else
            {
                break;
            }
        }

        nbBookmarks = bookmarks.size();
    }

    // Check if there are legacy bookmarks
    FXbool legacy_bookmarks = getApp()->reg().readUnsignedEntry("OPTIONS", "legacy_bookmarks", true);
    if (legacy_bookmarks && getApp()->reg().readStringEntry("bookmarks", "BOOKMARK1", NULL))
    {
        FXuint i = 1;
        FXuint j = bookmarks.size();
        while (1)
        {
            FXString regkey = "BOOKMARK" + FXStringVal(i);
            FXString pathname = getApp()->reg().readStringEntry("bookmarks", regkey.text(), NULL);

            if (pathname != "")
            {
                // Append bookmark to vector if directory exists
                if (xf_existfile(pathname))
                {
                    // Append to vector
                    FXString data = pathname + ";" + pathname + ";";
                    bookmarks.push_back(data);

                    // Append to list with default icon
                    FXList::appendItem(pathname, minibookmarkicon);

                    // Add registry key
                    regkey = "bookmark" + FXStringVal(j);
                    getApp()->reg().writeStringEntry("BOOKMARKS", regkey.text(), data.text());

                    j++;
                }

                i++;
            }
            else
            {
                break;
            }
        }

        nbBookmarks = bookmarks.size();

        // Remove legacy bookmarks section
        getApp()->reg().deleteSection("bookmarks");
    }

    // Write legacy bookmarks flag
    getApp()->reg().writeUnsignedEntry("OPTIONS", "legacy_bookmarks", false);

    // Update registry
    getApp()->reg().write();
}


// Create X window
void BookmarksList::create()
{
    FXList::create();
}


// Destructor
BookmarksList::~BookmarksList()
{
    if (associations)
    {
        delete associations;
    }
}


// Select item from key and set it current
void BookmarksList::setBookmark(FXString& key)
{
    for (int i = 0; i < getNumItems(); i++)
    {
        if (getItemText(i) == key)
        {
            selectItem(i);
            setCurrentItem(i);
            break;
        }
    }
}


// Modify bookmark (name or icon)
// Return 1 if success else return 0
int BookmarksList::modifyBookmark(FXString& oldname, FXString& newname, FXString& oldiconpathname,
                                  FXString& newiconpathname, FXString& pathname)
{
    // Check new name
    if (newname == "" || (newname == oldname && newiconpathname == oldiconpathname))
    {
        return 0;
    }

    if (newname.contains(';'))
    {
        MessageBox::error(this, BOX_OK, _("Error"), _("Bookmark name must not contain the character ';'"));
        return 0;
    }

    if (newiconpathname.contains(';'))
    {
        MessageBox::error(this, BOX_OK, _("Error"), _("Bookmark icon path name must not contain the character ';'"));
        return 0;
    }

    // Bookmark position
    FXuint index = FXList::getCurrentItem();

    // Check if an item with the same name already exists
    for (FXuint i = 0; i < nbBookmarks; i++)
    {
        if ((getBookmarkName(i) == newname) && (i != index))
        {
            MessageBox::error(this, BOX_OK, _("Error"), _("Bookmark %s already exists"), newname.text());
            return 0;
        }
    }

    // Modify bookmark
    if (newiconpathname == "" || !xf_existfile(newiconpathname))
    {
        FXList::setItem(index, newname, minibookmarkicon);   // Default icon
        bookmarks[index] = newname + ";" + getBookmarkPathname(index) + ";" + "";
    }
    else
    {
        FXIcon* icon = xf_loadiconfile(getApp(), FXPath::directory(newiconpathname),
                                    FXPath::name(newiconpathname), scalefrac, this->getBackColor());
        if (icon)
        {
            icon->create();
        }

        FXList::setItem(index, newname, icon);   // Dedicated icon

        bookmarks[index] = newname + ";" + getBookmarkPathname(index) + ";" + newiconpathname;
    }

    // Write registry key
    FXString regkey = "bookmark" + FXStringVal(index);
    getApp()->reg().writeStringEntry("BOOKMARKS", regkey.text(), bookmarks[index].text());

    // Update registry
    getApp()->reg().write();

    return 1;
}


// Append bookmark
// Return 1 if success else return 0
int BookmarksList::appendBookmark(FXString& name, FXString& pathname, FXString& iconpathname)
{
    // Check name
    if (name == "")
    {
        return 0;
    }
    else
    {
        for (FXuint i = 0; i < nbBookmarks; i++)
        {
            if (getBookmarkName(i) == name)
            {
                MessageBox::error(this, BOX_OK, _("Error"), _("Bookmark %s already exists"), name.text());
                return 0;
            }

            if (getBookmarkPathname(i) == pathname)
            {
                MessageBox::error(this, BOX_OK, _("Error"), _("Bookmark location %s already exists"), pathname.text());
                return 0;
            }
        }
    }

    // Append bookmark
    appendItem(name, pathname, iconpathname);

    return 1;
}


// Append an item
void BookmarksList::appendItem(FXString& name, FXString& pathname, FXString& iconpathname)
{
    // Append item to vector
    FXString data;

    if (xf_existfile(iconpathname))
    {
        data = name + ";" + pathname + ";" + iconpathname;
    }
    else
    {
        data = name + ";" + pathname + ";";
    }
    bookmarks.push_back(data);
    nbBookmarks = bookmarks.size();

    // Write registry key
    FXString regkey = "bookmark" + FXStringVal(nbBookmarks - 1);
    getApp()->reg().writeStringEntry("BOOKMARKS", regkey.text(), data.text());


    // Append item to list
    if (xf_existfile(iconpathname))
    {
        FXIcon* icon = xf_loadiconfile(getApp(), FXPath::directory(iconpathname),
                                    FXPath::name(iconpathname), scalefrac, this->getBackColor());
        if (icon)
        {
            icon->create();
        }

        FXList::appendItem(name, icon);  // Dedicated icon
    }
    else
    {
        FXList::appendItem(name, minibookmarkicon);   // Default icon
    }

    // Update registry
    getApp()->reg().write();
}


// Remove an item
void BookmarksList::removeItem(const FXString& name)
{
    // Remove item from list
    FXList::removeItem(FXList::findItem(name));

    // Remove item from vector
    for (FXuint i = 0; i < nbBookmarks; i++)
    {
        if (getBookmarkName(i) == name)
        {
            bookmarks.erase(bookmarks.begin() + i);
            break;
        }
    }
    nbBookmarks = bookmarks.size();

    // Delete bookmarks section
    getApp()->reg().read();
    getApp()->reg().deleteSection("BOOKMARKS");

    // Add bookmarks to registry
    for (FXuint i = 0; i < nbBookmarks; i++)
    {
        FXString regkey = "bookmark" + FXStringVal(i);
        getApp()->reg().writeStringEntry("BOOKMARKS", regkey.text(), bookmarks[i].text());
    }

    // Update registry
    getApp()->reg().write();
}


// Remove all items
void BookmarksList::removeAllItems(void)
{
    // Remove all bookmarks from list
    FXList::clearItems();

    // Clear bookmarks vector
    bookmarks.clear();
    nbBookmarks = 0;

    // Remove registry section
    getApp()->reg().read();
    getApp()->reg().deleteSection("BOOKMARKS");

    // Update registry
    getApp()->reg().write();
}


// Move bookmark up
void BookmarksList::moveUp(FXString& name)
{
    FXuint index = FXList::getCurrentItem();

    if (index > 0)
    {
        FXString tmp = bookmarks[index - 1];
        bookmarks[index - 1] = bookmarks[index];
        bookmarks[index] = tmp;

        FXList::clearItems();

        getApp()->reg().deleteSection("BOOKMARKS");

        for (FXuint i = 0; i < nbBookmarks; i++)
        {
            // Append item to list
            FXString iconpathname = getItemIconPathname(getBookmarkName(i));
            if (xf_existfile(iconpathname))
            {
                FXIcon* icon = xf_loadiconfile(getApp(), FXPath::directory(iconpathname),
                                            FXPath::name(iconpathname), scalefrac, this->getBackColor());
                if (icon)
                {
                    icon->create();
                }

                FXList::appendItem(getBookmarkName(i), icon);  // Dedicated icon
            }
            else
            {
                FXList::appendItem(getBookmarkName(i), minibookmarkicon);   // Default icon
            }

            FXString regkey = "bookmark" + FXStringVal(i);
            getApp()->reg().writeStringEntry("BOOKMARKS", regkey.text(), bookmarks[i].text());
        }
        getApp()->reg().write();
    }
}

// Move bookmark down
void BookmarksList::moveDown(FXString& name)
{
    FXuint index = FXList::getCurrentItem();

    if (index < nbBookmarks - 1)
    {
        FXString tmp = bookmarks[index + 1];
        bookmarks[index + 1] = bookmarks[index];
        bookmarks[index] = tmp;

        FXList::clearItems();

        getApp()->reg().deleteSection("BOOKMARKS");

        for (FXuint i = 0; i < nbBookmarks; i++)
        {
            // Append item to list
            FXString iconpathname = getItemIconPathname(getBookmarkName(i));
            if (xf_existfile(iconpathname))
            {
                FXIcon* icon = xf_loadiconfile(getApp(), FXPath::directory(iconpathname),
                                            FXPath::name(iconpathname), scalefrac, this->getBackColor());
                if (icon)
                {
                    icon->create();
                }

                FXList::appendItem(getBookmarkName(i), icon);  // Dedicated icon
            }
            else
            {
                FXList::appendItem(getBookmarkName(i), minibookmarkicon);       // Default icon
            }

            FXString regkey = "bookmark" + FXStringVal(i);
            getApp()->reg().writeStringEntry("BOOKMARKS", regkey.text(), bookmarks[i].text());
        }
        getApp()->reg().write();
    }
}


// Display bookmark location tooltip
long BookmarksList::onQueryTip(FXObject* sender, FXSelector, void* ptr)
{
    int x, y;
    FXuint state;

    getCursorPosition(x, y, state);
    int index = getItemAt(x, y);

    if (index != -1)
    {
        FXString name = getItemText(index);

        FXString tip;
        for (FXuint i = 0; i < nbBookmarks; i++)
        {
            if (getBookmarkName(i) == name)
            {
                tip = getBookmarkPathname(i);
                break;
            }
        }

        sender->handle(this, FXSEL(SEL_COMMAND, ID_SETSTRINGVALUE), (void*)&tip);

        return 1;
    }

    return 0;
}


long BookmarksList::onDNDEnter(FXObject* sender, FXSelector sel, void* ptr)
{
    FXList::onDNDEnter(sender, sel, ptr);
    if (!tabType)
    {
        tabType = getApp()->registerDragType(TABBUTTONS_DRAG_TYPE);
        uriType = getApp()->registerDragType(FXString("text/uri-list"));
    }
    return 1;
}


long BookmarksList::onDNDMotion(FXObject* sender, FXSelector sel, void* ptr)
{
    if (FXList::onDNDMotion(sender, sel, ptr))
    {
        return 1;
    }
    if (offeredDNDType(FROM_DRAGNDROP, tabType) || offeredDNDType(FROM_DRAGNDROP, uriType))
    {
        acceptDrop(DRAG_COPY);
        return 1;
    }
    return 0;
}


// Add a folder as a bookmark named after it (a number is appended if the name is taken)
int BookmarksList::bookmarkFolder(const FXString& folder)
{
    FXString pathname = folder;
    if (!xf_isdirectory(pathname))
    {
        return 0;
    }
    FXString base = (pathname == PATHSEPSTRING) ? pathname : FXPath::name(pathname);
    FXString name = base;
    for (int n = 2; ; n++)
    {
        FXbool taken = false;
        for (FXuint i = 0; i < nbBookmarks; i++)
        {
            if (getBookmarkPathname(i) == pathname)
            {
                FXString existing = getBookmarkName(i);
                setBookmark(existing);
                return 0;
            }
            if (getBookmarkName(i) == name)
            {
                taken = true;
            }
        }
        if (!taken)
        {
            break;
        }
        name = base + " (" + FXStringVal(n) + ")";
    }
    FXString icon = "";
    return appendBookmark(name, pathname, icon);
}


long BookmarksList::onDNDDrop(FXObject* sender, FXSelector sel, void* ptr)
{
    if (FXList::onDNDDrop(sender, sel, ptr))
    {
        return 1;
    }
    FXuchar* data = NULL;
    FXuint len = 0;
    int added = 0;
    if (getDNDData(FROM_DRAGNDROP, tabType, data, len))
    {
        FXString path((FXchar*)data, len);
        FXFREE(&data);
        added += bookmarkFolder(path);
    }
    else if (getDNDData(FROM_DRAGNDROP, uriType, data, len))
    {
        FXString list((FXchar*)data, len);
        FXFREE(&data);
        list.substitute("\r", "");
        for (int i = 0; i < list.contains('\n') + 1; i++)
        {
            FXString uri = list.section('\n', i);
            if (!uri.empty())
            {
                added += bookmarkFolder(FXURL::decode(FXURL::fileFromURL(uri)));
            }
        }
    }
    else
    {
        return 0;
    }
    if (added)
    {
        setCurrentItem(getNumItems() - 1);
        selectItem(getNumItems() - 1);
    }
    return 1;
}
