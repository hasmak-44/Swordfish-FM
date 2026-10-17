#ifndef PLACES_H
#define PLACES_H

#include "xfeutils.h"
#include "icons.h"
#include "FileDict.h"

class SidebarItem : public FXTreeItem
{
public:
    enum Kind { GROUP, PLACE, BOOKMARK, DEVICE, DIRECTORY, SERVER, REFRESH, NOTICE };
    Kind kind;
    FXString path;
    FXString key;
    FXbool loaded = false;
    FXTime modified = 0;
    SidebarItem(const FXString& text, FXIcon* icon, Kind type, const FXString& location = "",
                const FXString& name = "") :
        FXTreeItem(text, icon, icon), kind(type), path(location), key(name) {}
};

class SidebarTree : public FXTreeList
{
    FXDECLARE(SidebarTree)
protected:
    SidebarTree() {}
    FXint clearance = 0;
    FXDragType tabType = 0;
    FXDragType uriType = 0;
    FXbool remoteBusy = false;
    FXString remoteError;
public:
    SidebarTree(FXComposite* parent, FXObject* target, FXSelector selector, FXuint opts);
    virtual FXint getContentHeight();
    void setClearance(FXint value);
    long onExpandItem(FXObject*, FXSelector, void*);
    long onCollapseItem(FXObject*, FXSelector, void*);
    long onRemoteChanged(FXObject*, FXSelector, void*);
    enum { ID_REMOTE_CHANGED = FXTreeList::ID_LAST };
    ~SidebarTree();
    long onDNDEnter(FXObject*, FXSelector, void*);
    long onDNDMotion(FXObject*, FXSelector, void*);
    long onDNDDrop(FXObject*, FXSelector, void*);
    class BookmarksList* bookmarks = NULL;
};

// A free-positioned overlay must not participate in the tree's layout.
class SidebarFrame : public FXPacker
{
    FXDECLARE(SidebarFrame)
protected:
    SidebarFrame() {}
public:
    SidebarTree* tree = NULL;
    class DirList* directoryTree = NULL;
    FXWindow* content = NULL;
    FXTabBook* tabs = NULL;
    FXVerticalFrame* overlay = NULL;
    SidebarFrame(FXComposite* parent);
    virtual FXint getDefaultWidth();
    virtual FXint getDefaultHeight();
    virtual void layout();
};


// Places list
class PlacesList : public FXList
{
    FXDECLARE(PlacesList)

protected:
    FXStringDict* places = NULL;        // String dictionary of places
    FXString desktopDirectory;

public:
    PlacesList(FXWindow*, FXComposite*, FXObject*, FXSelector, FXColor listbackcolor = FXRGB(255, 255, 255),
               FXColor listforecolor = FXRGB(0, 0, 0), FXuint opts = 0, int x = 0, int y = 0, int w = 0, int h = 0);
    PlacesList()
    {
    }
    virtual void create();
    ~PlacesList();
    FXString getDesktopDirectory() const
    {
        return desktopDirectory;
    }

    // Get current item name
    FXString getCurrentItem(void)
    {
        return getItemText(FXList::getCurrentItem());
    }

    // Get item path name
    FXString getItemPathname(FXString key)
    {
        return places->find(key.text());
    }

    // Deselect all items
    void deselectAll(void)
    {
        killSelection();
    }

    // Select item from key and set it current
    void setPlace(FXString&);

    // Display tooltip
    long onQueryTip(FXObject*, FXSelector, void*);
};


// Mount points list
class MountsList : public FXList
{
    FXDECLARE(MountsList)

protected:
    FXStringDict* mounts = NULL;          // String dictionary of mount points
    FXulong hash_prev = 0;
    FXString gvfsmounts_prev = "";

public:
    MountsList(FXWindow*, FXComposite*, FXObject*, FXSelector, FXColor listbackcolor = FXRGB(255, 255, 255),
               FXColor listforecolor = FXRGB(0, 0, 0), FXuint opts = 0, int x = 0, int y = 0, int w = 0, int h = 0);
    MountsList()
    {
    }
    virtual void create();
    ~MountsList();

    enum
    {
        ID_MOUNTS_REFRESH=FXList::ID_LAST,
        ID_LAST
    };

    // Get current item name
    FXString getCurrentItem(void)
    {
        return getItemText(FXList::getCurrentItem());
    }

    // Get item path name
    FXString getItemPathname(FXString key)
    {
        return mounts->find(key.text());
    }

    // Deselect all items
    void deselectAll(void)
    {
        killSelection();
    }

    // Select item from key and set it current
    void setMount(FXString&);

    // Refresh mount points
    long onMountsRefresh(FXObject*, FXSelector, void*);

    // Display tooltip
    long onQueryTip(FXObject*, FXSelector, void*);
};



// Bookmarks list
class BookmarksList : public FXList
{
    FXDECLARE(BookmarksList)

protected:
    FXuint nbBookmarks = 0;
    vector_FXString bookmarks;               // String vector of bookmarks
    double scalefrac = 1.0;                  // Icon scaling factor
    FileDict* associations = NULL;           // Association table
    FXDragType tabType = 0;                  // Tab dragged from a tab bar
    FXDragType uriType = 0;                  // Files dragged from a file list



public:
    BookmarksList(FXWindow*, FXComposite*, FXObject*, FXSelector, FXColor listbackcolor = FXRGB(255, 255, 255),
                  FXColor listforecolor = FXRGB(0, 0, 0), FXuint opts = 0, int x = 0, int y = 0, int w = 0, int h = 0);
    BookmarksList()
    {
    }
    virtual void create();
    ~BookmarksList();


    // Return bookmark name from index
    FXString getBookmarkName(FXuint index)
    {
        FXString data = bookmarks[index];

        return data.before(';', 1);
    }

    // Return bookmark pathname from index
    FXString getBookmarkPathname(FXuint index)
    {
        FXString data = bookmarks[index];

        FXString str = data.rbefore(';', 1);

        return str.after(';', 1);
    }

    // Return bookmark icon path name from index
    FXString getBookmarkIconPathname(FXuint index)
    {
        FXString data = bookmarks[index];

        return data.after(';', 2);
    }

    // Return bookmark name from data
    FXString getBookmarkName(FXString& data)
    {
        return data.before(';', 1);
    }

    // Return bookmark pathname from data
    FXString getBookmarkPathname(FXString& data)
    {
        FXString str = data.rbefore(';', 1);

        return str.after(';', 1);
    }

    // Return bookmark icon pathname from data
    FXString getBookmarkIconPathname(FXString& data)
    {
        return data.after(';', 2);
    }

    // Append an item
    void appendItem(FXString&, FXString&, FXString&);

    // Remove an item
    void removeItem(const FXString&);

    // Remove all items
    void removeAllItems(void);

    // Get current item name
    FXString getCurrentItem(void)
    {
        return getItemText(FXList::getCurrentItem());
    }

    // Get item icon path name
    FXString getItemIconPathname(FXString name)
    {
        FXString iconpathname;

        for (FXuint i = 0; i < nbBookmarks; i++)
        {
            if (getBookmarkName(bookmarks[i]) == name)
            {
                iconpathname = getBookmarkIconPathname(bookmarks[i]);
                break;
            }
        }

        return iconpathname;
    }

    // Get item path name
    FXString getItemPathname(FXString name)
    {
        FXString pathname;

        for (FXuint i = 0; i < nbBookmarks; i++)
        {
            if (getBookmarkName(bookmarks[i]) == name)
            {
                pathname = getBookmarkPathname(bookmarks[i]);
                break;
            }
        }

        return pathname;
    }

    // Deselect all items
    void deselectAll(void)
    {
        killSelection();
    }

    // Select item from key and set it current
    void setBookmark(FXString&);

    // Modify bookmark
    int modifyBookmark(FXString&, FXString&, FXString&, FXString&, FXString&);

    // Append bookmark
    int appendBookmark(FXString&, FXString&, FXString&);

    // Move bookmark up
    void moveUp(FXString&);

    // Move bookmark down
    void moveDown(FXString&);

    // Display tooltip
    long onQueryTip(FXObject*, FXSelector, void*);

    // Bookmark tabs and folders dropped on the list
    long onDNDEnter(FXObject*, FXSelector, void*);
    long onDNDMotion(FXObject*, FXSelector, void*);
    long onDNDDrop(FXObject*, FXSelector, void*);
    int bookmarkFolder(const FXString&);
};

#endif
