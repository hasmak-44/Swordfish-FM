#include "config.h"
#include "i18n.h"

#include "AppLogger.h"
#include "TabButtons.h"
#include "MessageBox.h"
#include "RemoteDirectory.h"
#include "XFileExplorer.h"


extern FXString homedir;


// Delay before a tab hovered during a file drag becomes the current tab
#define TAB_HOVER_DELAY 1000


// Tab label button that can be dragged onto the Bookmarks list or another tab,
// and that accepts files dropped on it
class TabDragButton : public FXButton
{
    FXDECLARE(TabDragButton)

protected:
    FXint pressx = 0;
    FXint pressy = 0;
    FXuint pressstate = STATE_UP;
    FXbool dragging = false;
    FXbool hovering = false;
    FXDragType tabType = 0;
    FXDragType uriType = 0;
    static TabDragButton* dragged;      // Tab being dragged within this application

    TabDragButton()
    {
    }

    TabButtons* tabs()
    {
        return dynamic_cast<TabButtons*>(getTarget());
    }

    FXuint index()
    {
        return getSelector() - TabButtons::ID_START_TAB;
    }

    void stopHover()
    {
        hovering = false;
        getApp()->removeTimeout(this, ID_HOVER);
    }

public:
    enum
    {
        ID_HOVER = FXButton::ID_LAST,
        ID_LAST
    };

    TabDragButton(FXComposite* p, FXObject* tgt, FXSelector sel, FXuint opts) :
        FXButton(p, "", NULL, tgt, sel, opts, 0, 0, 0, 0, 4, 4, 2, 2)
    {
    }

    virtual void create()
    {
        FXButton::create();
        tabType = getApp()->registerDragType(TABBUTTONS_DRAG_TYPE);
        uriType = getApp()->registerDragType("text/uri-list");
        dropEnable();
    }

    virtual ~TabDragButton()
    {
        getApp()->removeTimeout(this, ID_HOVER);
        if (dragged == this)
        {
            dragged = NULL;
        }
    }

    // Short names like "TV" still get a comfortable tab width
    virtual FXint getDefaultWidth()
    {
        return FXMAX(FXButton::getDefaultWidth(), font->getTextWidth("00000000", 8) + padleft + padright);
    }

    long onLeftBtnPress(FXObject* sender, FXSelector sel, void* ptr)
    {
        FXEvent* event = (FXEvent*)ptr;
        pressx = event->root_x;
        pressy = event->root_y;
        pressstate = getState();
        dragging = false;
        return FXButton::onLeftBtnPress(sender, sel, ptr);
    }

    long onMotion(FXObject*, FXSelector, void* ptr)
    {
        FXEvent* event = (FXEvent*)ptr;
        if (!dragging && (flags & FLAG_PRESSED) &&
            FXABS(event->root_x - pressx) + FXABS(event->root_y - pressy) > 6 && beginDrag(&tabType, 1))
        {
            dragging = true;
            dragged = this;
        }
        if (dragging)
        {
            handleDrag(event->root_x, event->root_y, DRAG_COPY);
            setDragCursor(getApp()->getDefaultCursor(didAccept() != DRAG_REJECT ? DEF_DNDCOPY_CURSOR :
                                                     DEF_DNDSTOP_CURSOR));
            return 1;
        }
        return 0;
    }

    long onLeftBtnRelease(FXObject* sender, FXSelector sel, void* ptr)
    {
        if (!dragging)
        {
            return FXButton::onLeftBtnRelease(sender, sel, ptr);
        }
        dragging = false;
        endDrag(didAccept() != DRAG_REJECT);
        setDragCursor(getDefaultCursor());
        ungrab();
        flags &= ~FLAG_PRESSED;
        flags |= FLAG_UPDATE;
        // The drop may have moved another tab to this button
        setState((tabs() && index() == tabs()->getCurrentTab()) ? STATE_DOWN : STATE_UP);
        return 1;
    }

    long onDNDRequest(FXObject* sender, FXSelector sel, void* ptr)
    {
        FXEvent* event = (FXEvent*)ptr;
        if (FXButton::onDNDRequest(sender, sel, ptr))
        {
            return 1;
        }
        if (event->target == tabType && tabs())
        {
            FXString path = tabs()->getPath(index());
            FXuchar* data;
            FXMALLOC(&data, FXuchar, path.length() + 1);
            memcpy(data, path.text(), path.length() + 1);
            setDNDData(FROM_DRAGNDROP, tabType, data, path.length());
            return 1;
        }
        return 0;
    }

    long onDNDLeave(FXObject* sender, FXSelector sel, void* ptr)
    {
        FXButton::onDNDLeave(sender, sel, ptr);
        stopHover();
        return 1;
    }

    // Tabs can be dropped on other tabs, and files on tabs whose folder is writable
    long onDNDMotion(FXObject* sender, FXSelector sel, void* ptr)
    {
        if (FXButton::onDNDMotion(sender, sel, ptr))
        {
            return 1;
        }
        if (!tabs() || index() >= tabs()->getNumActiveTabs())
        {
            return 0;
        }
        if (offeredDNDType(FROM_DRAGNDROP, tabType))
        {
            if (dragged != this)
            {
                acceptDrop(DRAG_MOVE);
            }
            return 1;
        }
        if (offeredDNDType(FROM_DRAGNDROP, uriType) && tabs()->canDropFiles(index()))
        {
            if (!hovering && index() != tabs()->getCurrentTab())
            {
                hovering = true;
                getApp()->addTimeout(this, ID_HOVER, TAB_HOVER_DELAY);
            }
            acceptDrop(DRAG_ACCEPT);
            return 1;
        }
        return 0;
    }

    long onHoverTimeout(FXObject*, FXSelector, void*)
    {
        hovering = false;
        if (tabs() && index() < tabs()->getNumActiveTabs())
        {
            tabs()->setCurrentTab(index());
            // Show the new folder right away instead of waiting for the drag to go idle
            getApp()->forceRefresh();
            getShell()->layout();
            getApp()->repaint();
            getApp()->flush();
        }
        return 1;
    }

    long onDNDDrop(FXObject* sender, FXSelector sel, void* ptr)
    {
        stopHover();
        if (FXButton::onDNDDrop(sender, sel, ptr))
        {
            return 1;
        }
        TabButtons* target = tabs();
        if (!target || index() >= target->getNumActiveTabs())
        {
            return 0;
        }
        FXuchar* data = NULL;
        FXuint len = 0;
        if (getDNDData(FROM_DRAGNDROP, tabType, data, len))
        {
            FXString path((FXchar*)data, len);
            FXFREE(&data);
            TabButtons* source = dragged ? dragged->tabs() : NULL;
            if (source == target && dragged->index() < target->getNumActiveTabs() &&
                target->getPath(dragged->index()) == path)
            {
                target->moveTab(dragged->index(), index());
            }
            else if (dragged != this && !path.empty())
            {
                target->addTab(path);
            }
            dragged = NULL;
            return 1;
        }
        if (offeredDNDType(FROM_DRAGNDROP, uriType))
        {
            return target->dropFiles(index(), ptr);
        }
        return 0;
    }
};

TabDragButton* TabDragButton::dragged = NULL;

FXDEFMAP(TabDragButton) TabDragButtonMap[] =
{
    FXMAPFUNC(SEL_LEFTBUTTONPRESS, 0, TabDragButton::onLeftBtnPress),
    FXMAPFUNC(SEL_LEFTBUTTONRELEASE, 0, TabDragButton::onLeftBtnRelease),
    FXMAPFUNC(SEL_MOTION, 0, TabDragButton::onMotion),
    FXMAPFUNC(SEL_DND_REQUEST, 0, TabDragButton::onDNDRequest),
    FXMAPFUNC(SEL_DND_LEAVE, 0, TabDragButton::onDNDLeave),
    FXMAPFUNC(SEL_DND_MOTION, 0, TabDragButton::onDNDMotion),
    FXMAPFUNC(SEL_DND_DROP, 0, TabDragButton::onDNDDrop),
    FXMAPFUNC(SEL_TIMEOUT, TabDragButton::ID_HOVER, TabDragButton::onHoverTimeout),
};

FXIMPLEMENT(TabDragButton, FXButton, TabDragButtonMap, ARRAYNUMBER(TabDragButtonMap))


// Tab label for a folder: SMB mounts as share@computer, MTP devices by model, servers by name
static FXString tabLabel(const FXString& pathname)
{
    if (pathname == PATHSEPSTRING)
    {
        return PATHSEPSTRING;
    }
    if (RemoteDirectory::isServer(pathname))
    {
        FXString host = pathname.mid(6, pathname.length() - 6);
        return RemoteDirectory::hostName(host.before('/'));
    }
    FXString name = FXPath::name(pathname);
    if (name.left(10) == "smb-share:")
    {
        FXString server = name.mid(name.find("server=") + 7, name.length()).before(',');
        FXString share = name.mid(name.find("share=") + 6, name.length()).before(',');
        return share.substitute("%20", " ") + "@" + RemoteDirectory::hostName(server);
    }
    if (name.left(4) == "mtp:")
    {
        return name.after('_').before('_', 2);
    }
    return name;
}

FXDEFMAP(TabButtons) TabButtonsMap[] =
{
    FXMAPFUNC(SEL_COMMAND, TabButtons::ID_NEW_TAB, TabButtons::onCmdNewTab),
    FXMAPFUNC(SEL_LEFTBUTTONRELEASE, 0, TabButtons::onCmdNewTab),
    FXMAPFUNC(SEL_RIGHTBUTTONRELEASE, 0, TabButtons::onBlankMenu),
    FXMAPFUNC(SEL_RIGHTBUTTONRELEASE, TabButtons::ID_NEW_TAB, TabButtons::onBlankMenu),
    FXMAPFUNC(SEL_COMMAND, TabButtons::ID_REMOVE_ALL_TABS, TabButtons::onCmdRemoveAllTabs),
    FXMAPFUNCS(SEL_COMMAND, TabButtons::ID_START_TAB, TabButtons::ID_END_TAB, TabButtons::onCmdPathButton),
    FXMAPFUNCS(SEL_COMMAND, TabButtons::ID_START_CLOSE, TabButtons::ID_END_CLOSE, TabButtons::onCmdCloseButton),
    FXMAPFUNCS(SEL_RIGHTBUTTONRELEASE, TabButtons::ID_START_TAB, TabButtons::ID_END_TAB, TabButtons::onCmdPopupMenu),
    FXMAPFUNCS(SEL_RIGHTBUTTONRELEASE, TabButtons::ID_START_CLOSE, TabButtons::ID_END_CLOSE, TabButtons::onCmdPopupMenu),
    FXMAPFUNCS(SEL_QUERY_TIP, TabButtons::ID_START_TAB, TabButtons::ID_END_TAB, TabButtons::onQueryTip),
    FXMAPFUNCS(SEL_COMMAND, TabButtons::ID_START_MOVE_LEFT, TabButtons::ID_END_MOVE_LEFT, TabButtons::onCmdMoveLeft),
    FXMAPFUNCS(SEL_COMMAND, TabButtons::ID_START_MOVE_RIGHT, TabButtons::ID_END_MOVE_RIGHT, TabButtons::onCmdMoveRight),
    FXMAPFUNCS(SEL_UPDATE, TabButtons::ID_START_MOVE_LEFT, TabButtons::ID_END_MOVE_LEFT, TabButtons::onUpdMoveLeft),
    FXMAPFUNCS(SEL_UPDATE, TabButtons::ID_START_MOVE_RIGHT, TabButtons::ID_END_MOVE_RIGHT, TabButtons::onUpdMoveRight),
    FXMAPFUNCS(SEL_UPDATE, TabButtons::ID_START_TAB, TabButtons::ID_END_TAB, TabButtons::onUpdCurrentTab),
    FXMAPFUNCS(SEL_COMMAND, TabButtons::ID_START_BOOKMARK, TabButtons::ID_END_BOOKMARK, TabButtons::onCmdBookmarkTab),
    FXMAPFUNC(SEL_COMMAND, TabButtons::ID_BOOKMARK_GROUP, TabButtons::onCmdBookmarkGroup),
};

FXIMPLEMENT(TabButtons, FXHorizontalFrame, TabButtonsMap, ARRAYNUMBER(TabButtonsMap))


// Set the background color of a tab (frame, label and close button)
void TabButtons::setTabColor(int index, FXColor color)
{
    if ((index < 0) || (index >= (int)tabButtons.size()))
    {
        return;
    }
    tabButtons[index]->getParent()->setBackColor(color);
    tabButtons[index]->setBackColor(color);
    closeButtons[index]->setBackColor(color);

    // With shaded bars, the current tab is drawn flat in the lighter base color instead of pressed
    if ((basecolor != getApp()->getBaseColor()) && (color == activecolor))
    {
        tabButtons[index]->setState(STATE_UP);
        closeButtons[index]->setState(STATE_UP);
    }
}


// Construct
TabButtons::TabButtons(FXComposite* a, FilePanel* fpanel, DirPanel* dpanel, FXuint opts) :
    FXHorizontalFrame(a, opts, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2)
{
    // Frame background colors of selected and unselected tab
    // With bar shading, inactive tabs use the shaded bar color and the current tab the lighter base color
    basecolor = xf_barcolor(getApp(), 2);
    if (basecolor == getApp()->getBaseColor())
    {
        FXuint r = FXREDVAL(basecolor);
        FXuint g = FXGREENVAL(basecolor);
        FXuint b = FXBLUEVAL(basecolor);
        shadecolor = FXRGB(0.9 * r, 0.9 * g, 0.9 * b);
    }
    else
    {
        shadecolor = getApp()->getBaseColor();
    }
    // Tint the current tab with the selection color so it stands out from the others
    FXColor sel = getApp()->getSelbackColor();
    activecolor = FXRGB((FXREDVAL(shadecolor) * 60 + FXREDVAL(sel) * 40) / 100,
                        (FXGREENVAL(shadecolor) * 60 + FXGREENVAL(sel) * 40) / 100,
                        (FXBLUEVAL(shadecolor) * 60 + FXBLUEVAL(sel) * 40) / 100);
    setBackColor(basecolor);

    // File and dir panels
    filepanel = fpanel;
    dirpanel = dpanel;

    // Allocate vector of paths
    tabPaths = new FXString[NMAX_TABS];

    // Add tab and close buttons
    int idTab = ID_START_TAB;
    int idClose = ID_START_CLOSE;
    for (int i = 0; i < NMAX_TABS; i++)
    {
        FXHorizontalFrame* buttons = new FXHorizontalFrame(this, FRAME_GROOVE | LAYOUT_SIDE_TOP | LAYOUT_LEFT |
                                                           LAYOUT_FILL_Y, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2);
        tabButtons.push_back(new TabDragButton(buttons, this, idTab, BUTTON_TOOLBAR));
        closeButtons.push_back(new FXButton(buttons, "", minicloseicon, this, idClose, BUTTON_TOOLBAR,
                               0, 0, 0, 0, 0, 0, 2, 2));

        idTab++;
        idClose++;
        
        tabButtons[i]->hide();
        tabButtons[i]->setDefaultCursor(getApp()->getDefaultCursor(DEF_HAND_CURSOR));
        tabButtons[i]->getParent()->hide();

        closeButtons[i]->hide();
    }

    new FXButton(this, FXString("+") + TAB + _("New Tab"), NULL, this, ID_NEW_TAB, BUTTON_TOOLBAR | LAYOUT_CENTER_Y);
    new FXButton(this, "", NULL, this, ID_NEW_TAB,
                 BUTTON_TOOLBAR | LAYOUT_FILL_X | LAYOUT_FILL_Y, 0, 0, 0, 0, 0, 0, 0, 0);

    for (FXWindow* child = getFirst(); child; child = child->getNext())
    {
        child->setBackColor(basecolor);
    }
    for (int i = 0; i < NMAX_TABS; i++)
    {
        setTabColor(i, basecolor);
    }

    // Initializations
    nbActiveTabs = 0;
    currentTab = 0;
    prevTab = 0;

    // Create highlight font (bold if normal font is normal, and normal if normal font is bold)
    FXFontDesc fontdesc;
    normalFont = getApp()->getNormalFont();
    normalFont->getFontDesc(fontdesc);
    if (fontdesc.weight == FXFont::Normal)
    {
        fontdesc.weight = FXFont::Bold;
    }
    else
    {
        fontdesc.weight = FXFont::Normal;
    }
    highlightFont = new FXFont(getApp(), fontdesc);

}


// Create tab buttons
void TabButtons::create()
{
    highlightFont->create();
    FXHorizontalFrame::create();   
}


// Destruct object
TabButtons::~TabButtons()
{
    delete highlightFont;
    delete[] tabPaths;
}


// Add new tab
void TabButtons::addTab(FXString pathname, FXbool activate)
{
    if (nbActiveTabs < NMAX_TABS)
    {
        if (activate)
        {
            filepanel->setActive();
        }
        onUpdCurrentTab(this, 0, NULL);
        // Update previous and current tab indices
        FXuint index = nbActiveTabs++;
        prevTab = currentTab;
        currentTab = index;

        // Show buttons
        tabButtons[index]->show();
        tabButtons[index]->getParent()->show();
        closeButtons[index]->show();

        // Tab path
        tabPaths[index] = pathname;
        tabViews[index] = filepanel->getList()->getViewMode();

        // Tab label
        tabButtons[index]->setText(tabLabel(pathname));

        // Set directory
        filepanel->setDirectory(pathname);
        filepanel->updatePath(true);
        if (filepanel->isActive())
        {
            dirpanel->setDirectory(filepanel->getDirectory(), true);
        }

        // Change font and state
        tabButtons[prevTab]->setFont(normalFont);
        tabButtons[prevTab]->setState(STATE_UP);
        closeButtons[prevTab]->setState(STATE_UP);
        tabButtons[currentTab]->setFont(highlightFont);
        tabButtons[currentTab]->setState(STATE_DOWN);
        closeButtons[currentTab]->setState(STATE_DOWN);

        // Change frame background color
        setTabColor(prevTab, basecolor);
        setTabColor(currentTab, activecolor);
    } 
}


// Path button was pressed
long TabButtons::onCmdPathButton(FXObject* sender, FXSelector sel, void* ptr)
{
    // Set the focus on the file list
    filepanel->setActive();
    onUpdCurrentTab(this, 0, NULL);

    // Index of pressed button
    FXuint index = FXSELID(sel) - ID_START_TAB;
    if (index >= nbActiveTabs)
    {
        return 0;
    }
    prevTab = currentTab;
    currentTab = index;

    // Update the FileList and DirList directory
    FXString filePath = tabPaths[index];
    appLog(APP_LOG_UI, "tab-selected", FXString("index=") + FXStringVal(index) +
           " path=\"" + filePath + "\"");

    filepanel->setDirectory(filePath);
    filepanel->getList()->setViewMode((FileList::ViewMode)tabViews[index]);
    filepanel->updatePath(true);
    dirpanel->setDirectory(filePath, true);

    // Change fonts and states
    tabButtons[prevTab]->setFont(normalFont);
    tabButtons[prevTab]->setState(STATE_UP);
    closeButtons[prevTab]->setState(STATE_UP);
    tabButtons[currentTab]->setFont(highlightFont);
    tabButtons[currentTab]->setState(STATE_DOWN);
    closeButtons[currentTab]->setState(STATE_DOWN);

    // Change frame background color
    setTabColor(prevTab, basecolor);
    setTabColor(currentTab, activecolor);

    return 1;
}


// Close button was pressed
long TabButtons::onCmdCloseButton(FXObject* sender, FXSelector sel, void* ptr)
{
    // Index of pressed button
    FXuint index = FXSELID(sel) - ID_START_CLOSE;
    if (index >= nbActiveTabs)
    {
        return 0;
    }
    filepanel->setActive();
    onUpdCurrentTab(this, 0, NULL);
    FXuint oldCurrent = currentTab;
    
    // Change fonts and states
    tabButtons[index]->setFont(normalFont);
    tabButtons[index]->setState(STATE_UP);
    closeButtons[index]->setState(STATE_UP);
   
    // Shift labels and paths
    for (FXuint i = index; i < nbActiveTabs - 1; i++)
    {
        tabButtons[i]->setText(tabButtons[i + 1]->getText());
        tabPaths[i] = tabPaths[i + 1];
        tabViews[i] = tabViews[i + 1];
    }

    // Hide last button and reset its label and path
    tabButtons[nbActiveTabs - 1]->hide();
    tabButtons[nbActiveTabs - 1]->getParent()->hide();
    closeButtons[nbActiveTabs - 1]->hide();
    tabButtons[nbActiveTabs - 1]->setText("");
    tabPaths[nbActiveTabs - 1] = "";
    nbActiveTabs--;

    // Update previous and current tab
    if (index < oldCurrent)
    {
        currentTab = oldCurrent - 1;
    }
    else if (index == oldCurrent)
    {
        currentTab = nbActiveTabs ? FXMIN(index, nbActiveTabs - 1) : 0;
        if (nbActiveTabs)
        {
            filepanel->setDirectory(tabPaths[currentTab]);
            filepanel->getList()->setViewMode((FileList::ViewMode)tabViews[currentTab]);
            filepanel->updatePath(true);
            dirpanel->setDirectory(filepanel->getDirectory(), true);
        }
    }
    prevTab = FXMIN(oldCurrent, (FXuint)NMAX_TABS - 1);
    if (!nbActiveTabs)
    {
        prevTab = currentTab = 0;
        recalc();
        return 1;
    }
    
    // Change fonts and states
    tabButtons[prevTab]->setFont(normalFont);
    tabButtons[prevTab]->setState(STATE_UP);
    closeButtons[prevTab]->setState(STATE_UP);
    tabButtons[currentTab]->setFont(highlightFont);
    tabButtons[currentTab]->setState(STATE_DOWN);
    closeButtons[currentTab]->setState(STATE_DOWN);

    // Change frame background color
    setTabColor(prevTab, basecolor);
    setTabColor(currentTab, activecolor);

    return 1;
}


// Add a new tab
long TabButtons::onCmdNewTab(FXObject*, FXSelector, void*)
{
    filepanel->setActive();
    // Read new tab directory mode
    FXuint newtabdirmode = getApp()->reg().readUnsignedEntry("OPTIONS", "newtabdir_mode", NEWTAB_HOMEDIR);

    FXString pathname;
    if (newtabdirmode == NEWTAB_HOMEDIR)
    {
        // Home directory
        pathname = homedir;
    }
    else if (newtabdirmode == NEWTAB_CURRENTDIR)
    {
        // Current path
        pathname = filepanel->getDirectory();
    }
    else // Root directory
    {
        pathname = PATHSEPSTRING;
    }

    // Add new tab
    addTab(pathname);
    appLog(APP_LOG_UI, "tab-created", FXString("path=\"") + pathname + "\"");

    return 1;
}


// Remove all tabs
long TabButtons::onCmdRemoveAllTabs(FXObject*, FXSelector, void*)
{
    filepanel->setActive();
    if (BOX_CLICKED_CANCEL == MessageBox::question(FXApp::instance()->getActiveWindow(), BOX_OK_CANCEL,
                                                   _("Confirm Remove Tabs"),
                                                   _("Do you really want to close all tabs in this pane?")))
    {
        return 0;
    }
    else
    {
        appLog(APP_LOG_UI, "all-tabs-removed", FXString("count=") + FXStringVal(nbActiveTabs));
        clearTabs();
    }

    return 1;
}


// Close all tabs without asking
void TabButtons::clearTabs(void)
{
    for (FXuint i = 0; i < nbActiveTabs; i++)
    {
        tabButtons[i]->setText("");
        tabPaths[i] = "";
        tabButtons[i]->hide();
        tabButtons[i]->getParent()->hide();
        closeButtons[i]->hide();
        tabButtons[i]->setFont(normalFont);
        tabButtons[i]->setState(STATE_UP);
        closeButtons[i]->setState(STATE_UP);
        setTabColor(i, basecolor);
    }

    nbActiveTabs = 0;
    prevTab = 0;
    currentTab = 0;
}


void TabButtons::closeMount(const FXString& mountpoint)
{
    onUpdCurrentTab(this, 0, NULL);
    FXString prefix = mountpoint + "/";
    const FXuint oldCount = nbActiveTabs;
    const FXuint oldCurrent = currentTab;
    FXuint kept = 0;
    FXuint selected = 0;
    FXbool activeRemoved = false;
    for (FXuint i = 0; i < oldCount; ++i)
    {
        if (tabPaths[i] == mountpoint || tabPaths[i].left(prefix.length()) == prefix)
        {
            if (i == oldCurrent) activeRemoved = true;
            continue;
        }
        if (i == oldCurrent) selected = kept;
        tabPaths[kept] = tabPaths[i];
        tabViews[kept] = tabViews[i];
        tabButtons[kept]->setText(tabButtons[i]->getText());
        ++kept;
    }
    if (kept == oldCount) return;
    nbActiveTabs = kept;
    if (!nbActiveTabs)
    {
        nbActiveTabs = 1;
        tabPaths[0] = FXSystem::getHomeDirectory();
        tabViews[0] = filepanel->getList()->getViewMode();
        tabButtons[0]->setText(tabLabel(tabPaths[0]));
    }
    currentTab = activeRemoved ? FXMIN(oldCurrent, nbActiveTabs - 1) : selected;
    prevTab = currentTab;
    for (FXuint i = 0; i < oldCount; ++i)
    {
        const FXbool active = i == currentTab;
        tabButtons[i]->setFont(active ? highlightFont : normalFont);
        tabButtons[i]->setState(active ? STATE_DOWN : STATE_UP);
        closeButtons[i]->setState(STATE_UP);
        setTabColor(i, active ? activecolor : basecolor);
        if (i >= nbActiveTabs)
        {
            tabButtons[i]->hide();
            tabButtons[i]->getParent()->hide();
            closeButtons[i]->hide();
            tabPaths[i].clear();
            tabButtons[i]->setText("");
        }
    }
    if (activeRemoved)
    {
        filepanel->setDirectory(tabPaths[currentTab]);
        filepanel->getList()->setViewMode((FileList::ViewMode)tabViews[currentTab]);
        filepanel->updatePath(true);
        if (filepanel->isActive()) dirpanel->setDirectory(filepanel->getDirectory(), true);
    }
    recalc();
}


// Update current tab path
long TabButtons::onUpdCurrentTab(FXObject* senderbj, FXSelector sel, void* ptr)
{
    if (!nbActiveTabs)
    {
        return 0;
    }
    // Current path
    FXString pathname = filepanel->getDirectory();
    
    // Set current name and path
    tabPaths[currentTab] = pathname;
    tabViews[currentTab] = filepanel->getList()->getViewMode();
    tabButtons[currentTab]->setText(tabLabel(pathname));

    return 0;
}


// Display path tooltip
long TabButtons::onQueryTip(FXObject* sender, FXSelector sel, void* ptr)
{
    // Index of hovered button
    FXuint index = FXSELID(sel) - ID_START_TAB;

    FXString tip = tabPaths[index];

    sender->handle(this, FXSEL(SEL_COMMAND, ID_SETSTRINGVALUE), (void*)&tip);

    return 1;
}


// Context menu
long TabButtons::onBlankMenu(FXObject*, FXSelector, void*)
{
    FXMenuPane menu(this);
    new FXMenuCommand(&menu, _("New &Tab"), mininewtabicon, this, ID_NEW_TAB);
    menu.create();
    int x, y;
    FXuint state;
    getRoot()->getCursorPosition(x, y, state);
    menu.popup(NULL, x, y);
    getApp()->runModalWhileShown(&menu);
    return 1;
}


long TabButtons::onCmdPopupMenu(FXObject* sender, FXSelector sel, void* ptr)
{
    // Index of button
    FXuint id = FXSELID(sel);
    FXuint index;
    if (id >= ID_START_TAB && id <= ID_END_TAB)
    {
        index = id - ID_START_TAB;    // Tab button
    }
    else
    {
        index = id - ID_START_CLOSE;  // Close button
    }

    int x, y;
    FXuint state;

    getCursorPosition(x, y, state);

    // Menu items
    FXMenuPane* menu = new FXMenuPane(this);

    getRoot()->getCursorPosition(x, y, state);

    new FXMenuCommand(menu, _("New &Tab"), mininewtabicon, this, TabButtons::ID_NEW_TAB);
    
    new FXMenuSeparator(menu);

    new FXMenuCommand(menu, _("Move &Left"), minidirbackicon, this, index + ID_START_MOVE_LEFT);
    new FXMenuCommand(menu, _("Move &Right"), minidirforwardicon, this, index + ID_START_MOVE_RIGHT);

    new FXMenuSeparator(menu);

    new FXMenuCommand(menu, _("Add to &Bookmarks"), miniaddbookicon, this, index + ID_START_BOOKMARK);
    new FXMenuCommand(menu, _("Bookmark All Tabs as &Group..."), mininewtabicon, this, ID_BOOKMARK_GROUP);

    new FXMenuSeparator(menu);
    
    new FXMenuCommand(menu, _("&Close Tab"), minicloseicon, this, index + ID_START_CLOSE);

    menu->create();
    menu->popup(NULL, x, y);
    getApp()->runModalWhileShown(menu);

    delete(menu);

    return 1;
}


// Move tab left
long TabButtons::onCmdMoveLeft(FXObject* sender, FXSelector sel, void* ptr)
{
    filepanel->setActive();
    onUpdCurrentTab(this, 0, NULL);
    // Index of button
    FXuint index = FXSELID(sel) - ID_START_MOVE_LEFT;

    // Shift left
    if (index > 0 && index < nbActiveTabs)
    {
        FXString left_label = tabButtons[index - 1]->getText();
        FXString left_path = tabPaths[index - 1];
        FXuint left_view = tabViews[index - 1];
        
        tabButtons[index - 1]->setText(tabButtons[index]->getText());
        tabPaths[index - 1] = tabPaths[index];
        tabViews[index - 1] = tabViews[index];

        tabButtons[index]->setText(left_label);
        tabPaths[index] = left_path;        
        tabViews[index] = left_view;

        // Update previous and current tab
        if (index == currentTab)
        {
            prevTab = index;
            currentTab = index - 1;

        }
        else if (index == currentTab + 1)
        {
            prevTab = currentTab;
            currentTab = index;
        }        

        // Change fonts and states
        tabButtons[prevTab]->setFont(normalFont);
        tabButtons[prevTab]->setState(STATE_UP);
        closeButtons[prevTab]->setState(STATE_UP);
        tabButtons[currentTab]->setFont(highlightFont);
        tabButtons[currentTab]->setState(STATE_DOWN);
        closeButtons[currentTab]->setState(STATE_DOWN);
        setTabColor(prevTab, basecolor);
        setTabColor(currentTab, activecolor);
    }

    return 1;
}


// Move tab right
long TabButtons::onCmdMoveRight(FXObject* sender, FXSelector sel, void* ptr)
{
    filepanel->setActive();
    onUpdCurrentTab(this, 0, NULL);
    // Index of button
    FXuint index = FXSELID(sel) - ID_START_MOVE_RIGHT;

    // Shift right
    if (index < nbActiveTabs && index + 1 < nbActiveTabs)
    {
        FXString right_label = tabButtons[index + 1]->getText();
        FXString right_path = tabPaths[index + 1];
        FXuint right_view = tabViews[index + 1];
        
        tabButtons[index + 1]->setText(tabButtons[index]->getText());
        tabPaths[index + 1] = tabPaths[index];
        tabViews[index + 1] = tabViews[index];

        tabButtons[index]->setText(right_label);
        tabPaths[index] = right_path;        
        tabViews[index] = right_view;

        // Update previous and current tab
        if (index == currentTab)
        {
            prevTab = index;
            currentTab = index + 1;
        }
        else if (index == currentTab - 1)
        {
            prevTab = currentTab;
            currentTab = index;
        }        

        // Change fonts and states
        tabButtons[prevTab]->setFont(normalFont);
        tabButtons[prevTab]->setState(STATE_UP);
        closeButtons[prevTab]->setState(STATE_UP);
        tabButtons[currentTab]->setFont(highlightFont);
        tabButtons[currentTab]->setState(STATE_DOWN);
        closeButtons[currentTab]->setState(STATE_DOWN);
        setTabColor(prevTab, basecolor);
        setTabColor(currentTab, activecolor);
    }

    return 1;
}


// Bookmark the folder of a tab
long TabButtons::onCmdBookmarkTab(FXObject*, FXSelector sel, void*)
{
    FXuint index = FXSELID(sel) - ID_START_BOOKMARK;
    onUpdCurrentTab(this, 0, NULL);
    if (index < nbActiveTabs)
    {
        dirpanel->getBookmarksList()->bookmarkFolder(tabPaths[index]);
        appLog(APP_LOG_UI, "tab-bookmarked", FXString("path=\"") + tabPaths[index] + "\"");
    }
    return 1;
}


// Bookmark all tabs of both panes as a tab group
long TabButtons::onCmdBookmarkGroup(FXObject* sender, FXSelector, void* ptr)
{
    return getShell()->handle(sender, FXSEL(SEL_COMMAND, XFileExplorer::ID_ADD_TAB_GROUP), ptr);
}


// Show which tab is current (font, pressed state and color)
void TabButtons::showTabStates(void)
{
    for (FXuint i = 0; i < nbActiveTabs; i++)
    {
        const FXbool active = (i == currentTab);
        tabButtons[i]->setFont(active ? highlightFont : normalFont);
        tabButtons[i]->setState(active ? STATE_DOWN : STATE_UP);
        closeButtons[i]->setState(active ? STATE_DOWN : STATE_UP);
        setTabColor(i, active ? activecolor : basecolor);
    }
}


// Move a tab to another position, keeping the same tab current
void TabButtons::moveTab(FXuint from, FXuint to)
{
    if (from >= nbActiveTabs || to >= nbActiveTabs || from == to)
    {
        return;
    }
    onUpdCurrentTab(this, 0, NULL);
    FXString label = tabButtons[from]->getText();
    FXString path = tabPaths[from];
    FXuint view = tabViews[from];
    const int step = (to > from) ? 1 : -1;
    for (FXuint i = from; i != to; i += step)
    {
        tabButtons[i]->setText(tabButtons[i + step]->getText());
        tabPaths[i] = tabPaths[i + step];
        tabViews[i] = tabViews[i + step];
    }
    tabButtons[to]->setText(label);
    tabPaths[to] = path;
    tabViews[to] = view;

    if (currentTab == from)
    {
        currentTab = to;
    }
    else if (from < currentTab && currentTab <= to)
    {
        currentTab--;
    }
    else if (to <= currentTab && currentTab < from)
    {
        currentTab++;
    }
    prevTab = currentTab;
    if (tabButtons[from]->hasFocus())
    {
        tabButtons[to]->setFocus();
    }
    showTabStates();
    appLog(APP_LOG_UI, "tab-moved", FXString("from=") + FXStringVal(from) + " to=" + FXStringVal(to));
}


// Files can be dropped on a tab whose folder is local and writable
FXbool TabButtons::canDropFiles(FXuint index)
{
    return index < nbActiveTabs && !RemoteDirectory::isServer(tabPaths[index]) && xf_iswritable(tabPaths[index]);
}


// Files dropped on a tab go into its folder, which becomes the current tab
long TabButtons::dropFiles(FXuint index, void* ptr)
{
    if (!canDropFiles(index))
    {
        return 0;
    }
    if (index != currentTab)
    {
        setCurrentTab(index);
    }
    FileList* list = filepanel->getList();
    return list->dropIntoDirectory(tabPaths[index], ptr);
}


// Update move left menu item
long TabButtons::onUpdMoveLeft(FXObject* sender, FXSelector sel, void* ptr)
{
    // Index of button
    FXuint index = FXSELID(sel) - ID_START_MOVE_LEFT;

    if (index < 1)
    {
        sender->handle(this, FXSEL(SEL_COMMAND, FXWindow::ID_DISABLE), NULL);
    }
    else
    {
        sender->handle(this, FXSEL(SEL_COMMAND, FXWindow::ID_ENABLE), NULL);        
    }

    return 0;
}


// Update move right menu item
long TabButtons::onUpdMoveRight(FXObject* sender, FXSelector sel, void* ptr)
{
    // Index of button
    FXuint index = FXSELID(sel) - ID_START_MOVE_RIGHT;

    if (index >= nbActiveTabs - 1)
    {
        sender->handle(this, FXSEL(SEL_COMMAND, FXWindow::ID_DISABLE), NULL);
    }
    else
    {
        sender->handle(this, FXSEL(SEL_COMMAND, FXWindow::ID_ENABLE), NULL);        
    }
    
    return 0;
}


// Set current tab from index
void TabButtons::setCurrentTab(FXuint index)
{
    if (index >= nbActiveTabs)
    {
        return;
    }
    onUpdCurrentTab(this, 0, NULL);
    // Update previous and current tab indices
    prevTab = currentTab;
    currentTab = index;

    // Show buttons
    tabButtons[index]->show();
    tabButtons[index]->getParent()->show();
    closeButtons[index]->show();

    // Set directory
    FXString pathname = tabPaths[index];
    filepanel->setDirectory(pathname);
    filepanel->getList()->setViewMode((FileList::ViewMode)tabViews[index]);
    filepanel->updatePath(true);
    if (filepanel->isActive())
    {
        dirpanel->setDirectory(filepanel->getDirectory(), true);
    }

    // Change font and state
    tabButtons[prevTab]->setFont(normalFont);
    tabButtons[prevTab]->setState(STATE_UP);
    closeButtons[prevTab]->setState(STATE_UP);
    tabButtons[currentTab]->setFont(highlightFont);
    tabButtons[currentTab]->setState(STATE_DOWN);
    closeButtons[currentTab]->setState(STATE_DOWN);
    setTabColor(prevTab, basecolor);
    setTabColor(currentTab, activecolor);
}


void TabButtons::saveTabs(const char* section)
{
    onUpdCurrentTab(this, 0, NULL);
    getApp()->reg().deleteSection(section);
    for (FXuint i = 0; i < nbActiveTabs; i++)
    {
        FXString key = "tab" + FXStringVal(i);
        getApp()->reg().writeStringEntry(section, key.text(), tabPaths[i].text());
        key = "view" + FXStringVal(i);
        getApp()->reg().writeUnsignedEntry(section, key.text(), tabViews[i]);
    }
    getApp()->reg().writeUnsignedEntry(section, "current_tab", currentTab);
}


FXbool TabButtons::restoreTabs(const char* section, FXuint selected)
{
    FXuint restoredSelection = 0;
    for (FXuint i = 0; i < NMAX_TABS; i++)
    {
        FXString key = "tab" + FXStringVal(i);
        FXString path = getApp()->reg().readStringEntry(section, key.text(), "");
        if (path.empty())
        {
            break;
        }
        if (!RemoteDirectory::isServer(path) && (!xf_isdirectory(path) || !xf_isreadexecutable(path)))
        {
            appLog(APP_LOG_ERRORS, "tab-restore-unavailable", FXString("pane=") + section + " path=\"" + path + "\"");
            continue;
        }
        if (i <= selected)
        {
            restoredSelection = nbActiveTabs;
        }
        addTab(path, false);
        key = "view" + FXStringVal(i);
        FXuint mode = getApp()->reg().readUnsignedEntry(section, key.text(), filepanel->getList()->getViewMode());
        if (mode > FileList::VIEW_THUMBNAILS) mode = FileList::VIEW_DETAILS;
        tabViews[currentTab] = mode;
        filepanel->getList()->setViewMode((FileList::ViewMode)mode);
    }
    if (!nbActiveTabs)
    {
        return false;
    }
    setCurrentTab(restoredSelection);
    return true;
}
