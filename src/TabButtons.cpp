#include "config.h"
#include "Theme.h"
#include "i18n.h"

#include "AppLogger.h"
#include "DragPreview.h"
#include "TabButtons.h"
#include "MessageBox.h"
#include "RemoteDirectory.h"
#include "XFileExplorer.h"
#include "xfeutils.h"


extern FXString homedir;


// Delay before a tab hovered during a file drag becomes the current tab
#define TAB_HOVER_DELAY 1000


class TabFrame : public FXHorizontalFrame
{
    FXDECLARE(TabFrame)
protected:
    TabFrame() {}
public:
    explicit TabFrame(FXComposite* parent) :
        FXHorizontalFrame(parent, FRAME_GROOVE | LAYOUT_SIDE_TOP | LAYOUT_LEFT | LAYOUT_FILL_Y,
                          0, 0, 0, 0, 0, 0, 0, 0, 2, 2) {}

    long onPaint(FXObject*, FXSelector, void*)
    {
        FXDCWindow dc(this);
        if (!drawBackgroundImage(this, dc, 0, 0, width, height))
        {
            dc.setForeground(backColor);
            dc.fillRectangle(0, 0, width, height);
        }
        if (width < 2 || height < 2) return 1;

        dc.setForeground(getApp()->getBorderColor());
        if (width < 8 || height < 5)
        {
            dc.drawRectangle(0, 0, width - 1, height - 1);
            return 1;
        }

        const FXint radius = 4;
        const FXPoint corners[] = {
            FXPoint(radius - 1, 0), FXPoint(radius - 2, 1), FXPoint(radius - 3, 2), FXPoint(0, radius - 1),
            FXPoint(width - radius, 0), FXPoint(width - radius + 1, 1),
            FXPoint(width - radius + 2, 2), FXPoint(width - 1, radius - 1)};
        dc.drawLine(radius, 0, width - radius - 1, 0);
        dc.drawLine(0, radius, 0, height - 1);
        dc.drawLine(width - 1, radius, width - 1, height - 1);
        dc.drawLine(0, height - 1, width - 1, height - 1);
        dc.drawPoints(corners, ARRAYNUMBER(corners));
        return 1;
    }
};

FXDEFMAP(TabFrame) TabFrameMap[] =
{
    FXMAPFUNC(SEL_PAINT, 0, TabFrame::onPaint),
};
FXIMPLEMENT(TabFrame, FXHorizontalFrame, TabFrameMap, ARRAYNUMBER(TabFrameMap))


class TabCloseButton : public FXButton
{
    FXDECLARE(TabCloseButton)

protected:
    FXbool dropAfter = false;
    TabCloseButton() {}

public:
    TabCloseButton(FXComposite* p, FXObject* tgt, FXSelector sel) :
        FXButton(p, "", minicloseicon, tgt, sel, BUTTON_TOOLBAR, 0, 0, 0, 0, 0, 0, 2, 2) {}

    void setDropAfter(FXbool after)
    {
        if (dropAfter != after)
        {
            dropAfter = after;
            update();
        }
    }

    long onPaint(FXObject* sender, FXSelector sel, void* ptr)
    {
        long result = FXButton::onPaint(sender, sel, ptr);
        if (dropAfter)
        {
            FXDCWindow dc(this, static_cast<FXEvent*>(ptr));
            dc.setForeground(getApp()->getSelbackColor());
            dc.fillRectangle(getWidth() - 3, 0, 3, getHeight());
        }
        return result;
    }
};

FXDEFMAP(TabCloseButton) TabCloseButtonMap[] =
{
    FXMAPFUNC(SEL_PAINT, 0, TabCloseButton::onPaint),
};
FXIMPLEMENT(TabCloseButton, FXButton, TabCloseButtonMap, ARRAYNUMBER(TabCloseButtonMap))


// Tab label button that can be dragged onto the Bookmarks list or another tab,
// and that accepts files dropped on it
class TabDragButton : public FXButton
{
    FXDECLARE(TabDragButton)

protected:
    FXint pressx = 0;
    FXint pressy = 0;
    FXuint pressstate = STATE_UP;
    FXbool started = false;             // Pointer moved far enough to start a drag
    FXbool dragging = false;            // XDND drag out of the tab strip is active
    FXbool hovering = false;
    FXint dropSide = 0;
    FXint grab = 0;                     // Pointer offset inside the grabbed tab
    FXint slot = -1;                    // Position the tab slides into within its strip
    std::vector<FXint> homes;           // Laid-out tab positions when the drag started
    DragPreview preview;
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

    void setDropSide(FXint side)
    {
        if (dropSide != side)
        {
            dropSide = side;
            TabCloseButton* close = dynamic_cast<TabCloseButton*>(getNext());
            if (close) close->setDropAfter(side > 0);
            update();
        }
    }

    FXWindow* frame(FXint i)
    {
        return tabs()->childAtIndex(i);
    }

    // Put every tab back where the layout placed it
    void restoreTabs()
    {
        for (FXint i = 0; i < (FXint)homes.size(); i++)
        {
            frame(i)->move(homes[i], frame(i)->getY());
        }
        slot = -1;
    }

    // Like Firefox, keep the grabbed tab under the pointer and slide its neighbours aside
    void slideTabs(FXint x)
    {
        FXint count = homes.size(), from = index();
        FXint width = frame(from)->getWidth();
        FXint step = width + tabs()->getHSpacing();
        FXint last = homes[count - 1] + frame(count - 1)->getWidth() - width;
        FXint left = FXCLAMP(homes[0], x - grab, last);
        FXint centre = left + width / 2;
        slot = from;
        while (slot > 0 && centre < homes[slot - 1] + frame(slot - 1)->getWidth() / 2) slot--;
        while (slot < count - 1 && centre > homes[slot + 1] + frame(slot + 1)->getWidth() / 2) slot++;
        for (FXint i = 0; i < count; i++)
        {
            FXint home = homes[i];
            if (i == from) home = left;
            else if (slot < from && i >= slot && i < from) home += step;
            else if (slot > from && i > from && i <= slot) home -= step;
            frame(i)->move(home, frame(i)->getY());
        }
        frame(from)->raise();
    }

    // Pointer position in the tab strip, or false when the pointer has left the strip
    FXbool inStrip(FXEvent* event, FXint& x)
    {
        FXint y;
        tabs()->translateCoordinatesFrom(x, y, getRoot(), event->root_x, event->root_y);
        FXint height = tabs()->getHeight();
        return x >= 0 && x < tabs()->getWidth() && y >= -height / 2 && y < height + height / 2;
    }

    void finishDrag()
    {
        if (dragging && isDragging()) endDrag(false);
        restoreTabs();
        homes.clear();
        preview.hide();
        started = false;
        dragging = false;
        if (dragged == this) dragged = NULL;
        setDragCursor(getDefaultCursor());
        setDefaultCursor(getApp()->getDefaultCursor(DEF_HAND_CURSOR));
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
        started = false;
        dragging = false;
        return FXButton::onLeftBtnPress(sender, sel, ptr);
    }

    long onMotion(FXObject*, FXSelector, void* ptr)
    {
        FXEvent* event = (FXEvent*)ptr;
        if (!started && (flags & FLAG_PRESSED) && tabs() &&
            FXABS(event->root_x - pressx) + FXABS(event->root_y - pressy) > 6)
        {
            FXint x, y;
            tabs()->translateCoordinatesFrom(x, y, getRoot(), pressx, pressy);
            started = true;
            dragged = this;
            grab = x - getParent()->getX();
            homes.clear();
            for (FXuint i = 0; i < tabs()->getNumActiveTabs(); i++)
            {
                homes.push_back(frame(i)->getX());
            }
            setState(pressstate);
        }
        if (!started)
        {
            return 0;
        }
        FXint x;
        if (inStrip(event, x))
        {
            if (dragging)
            {
                // Back in the strip: drop the XDND drag and slide the tab again
                endDrag(false);
                dragging = false;
                preview.hide();
            }
            slideTabs(x);
            setDefaultCursor(getApp()->getDefaultCursor(DEF_MOVE_CURSOR));
            return 1;
        }
        if (!dragging)
        {
            restoreTabs();
            if (!beginDrag(&tabType, 1))
            {
                return 1;
            }
            dragging = true;
            preview.show(this, getText(), getFont(), getBackColor(), getTextColor(),
                         getWidth(), getHeight(), event->root_x, event->root_y);
        }
        // Outside the strip the tab can still go to Bookmarks or the other panel's tabs
        preview.move(event->root_x, event->root_y);
        handleDrag(event->root_x, event->root_y, DRAG_COPY);
        setDragCursor(getApp()->getDefaultCursor(didAccept() != DRAG_REJECT ? DEF_DNDCOPY_CURSOR :
                                                 DEF_DNDSTOP_CURSOR));
        return 1;
    }

    long onLeftBtnRelease(FXObject* sender, FXSelector sel, void* ptr)
    {
        if (!started)
        {
            return FXButton::onLeftBtnRelease(sender, sel, ptr);
        }
        FXEvent* event = (FXEvent*)ptr;
        TabButtons* strip = tabs();
        FXuint from = index();
        FXint x, to = -1;
        if (!dragging && inStrip(event, x))
        {
            slideTabs(x);
            to = slot;
        }
        else if (dragging)
        {
            endDrag(didAccept() != DRAG_REJECT);
            dragging = false;
        }
        finishDrag();
        ungrab();
        flags &= ~FLAG_PRESSED;
        flags |= FLAG_UPDATE;
        if (to >= 0 && (FXuint)to != from)
        {
            strip->moveTab(from, to);
        }
        strip->recalc();
        // The drop may have moved another tab to this button
        setState((index() == strip->getCurrentTab()) ? STATE_DOWN : STATE_UP);
        return 1;
    }

    long onPaint(FXObject* sender, FXSelector sel, void* ptr)
    {
        long result = FXButton::onPaint(sender, sel, ptr);
        if (dropSide < 0)
        {
            FXDCWindow dc(this, static_cast<FXEvent*>(ptr));
            dc.setForeground(getApp()->getSelbackColor());
            dc.fillRectangle(0, 0, 3, getHeight());
        }
        return result;
    }

    long onUngrabbed(FXObject* sender, FXSelector sel, void* ptr)
    {
        if (started) finishDrag();
        long result = FXButton::onUngrabbed(sender, sel, ptr);
        setState((tabs() && index() == tabs()->getCurrentTab()) ? STATE_DOWN : STATE_UP);
        return result;
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
        setDropSide(0);
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
            FXEvent* event = static_cast<FXEvent*>(ptr);
            FXint x, y;
            getRoot()->translateCoordinatesTo(x, y, this, event->root_x, event->root_y);
            setDropSide(x < getWidth() / 2 ? -1 : 1);
            acceptDrop(DRAG_MOVE);
            return 1;
        }
        setDropSide(0);
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
        FXint side = dropSide;
        setDropSide(0);
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
            FXuint slot = index() + (side > 0 ? 1 : 0);
            if (source == target && dragged->index() < target->getNumActiveTabs() &&
                target->getPath(dragged->index()) == path)
            {
                FXuint from = dragged->index();
                target->moveTab(from, slot - (from < slot ? 1 : 0));
            }
            else if (!path.empty())
            {
                FXuint count = target->getNumActiveTabs();
                target->addTab(path);
                if (target->getNumActiveTabs() > count)
                    target->moveTab(count, slot);
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
    FXMAPFUNC(SEL_PAINT, 0, TabDragButton::onPaint),
    FXMAPFUNC(SEL_LEFTBUTTONPRESS, 0, TabDragButton::onLeftBtnPress),
    FXMAPFUNC(SEL_LEFTBUTTONRELEASE, 0, TabDragButton::onLeftBtnRelease),
    FXMAPFUNC(SEL_UNGRABBED, 0, TabDragButton::onUngrabbed),
    FXMAPFUNC(SEL_MOTION, 0, TabDragButton::onMotion),
    FXMAPFUNC(SEL_DND_REQUEST, 0, TabDragButton::onDNDRequest),
    FXMAPFUNC(SEL_DND_LEAVE, 0, TabDragButton::onDNDLeave),
    FXMAPFUNC(SEL_DND_MOTION, 0, TabDragButton::onDNDMotion),
    FXMAPFUNC(SEL_DND_DROP, 0, TabDragButton::onDNDDrop),
    FXMAPFUNC(SEL_TIMEOUT, TabDragButton::ID_HOVER, TabDragButton::onHoverTimeout),
};

FXIMPLEMENT(TabDragButton, FXButton, TabDragButtonMap, ARRAYNUMBER(TabDragButtonMap))


// Tab label for a folder: Trash, SMB mounts, MTP devices and servers have special names
static FXString tabLabel(const FXString& pathname)
{
    if (pathname == PATHSEPSTRING)
    {
        return PATHSEPSTRING;
    }
    if (pathname == FXPath::absolute(xf_trashdatahome() + PATHSEPSTRING TRASHFILESPATH))
    {
        return _("Trash");
    }
    if (RemoteDirectory::isMedia(pathname))
    {
        return RemoteDirectory::mediaLabel(pathname);
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
    FXMAPFUNC(SEL_PAINT, 0, TabButtons::onPaint),
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
    FXMAPFUNC(SEL_COMMAND, TabButtons::ID_THEME, TabButtons::onTheme),
};

FXIMPLEMENT(TabButtons, FXHorizontalFrame, TabButtonsMap, ARRAYNUMBER(TabButtonsMap))

long TabButtons::onPaint(FXObject* sender, FXSelector selector, void* pointer)
{
    return FXPacker::onPaint(sender, selector, pointer);
}

FXbool TabButtons::allowsBackgroundImage(const FXWindow* window) const
{
    for (FXuint i = 0; i < nbActiveTabs; ++i)
        if (window == tabButtons[i] || window == closeButtons[i] || window == tabButtons[i]->getParent())
            return i != currentTab;
    return true;
}


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
    tabButtons[index]->setTextColor(color == activecolor ? activeTextColor : textcolor);
    closeButtons[index]->setTextColor(color == activecolor ? activeTextColor : textcolor);

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
    textcolor = getApp()->getForeColor();
    activeTextColor = getApp()->getSelforeColor();
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
        FXHorizontalFrame* buttons = new TabFrame(this);
        tabButtons.push_back(new TabDragButton(buttons, this, idTab, BUTTON_TOOLBAR));
        closeButtons.push_back(new TabCloseButton(buttons, this, idClose));

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
TabButtons::ColumnWidths TabButtons::captureColumns() const
{
    return filepanel->getList()->getColumnSizing().widths;
}

void TabButtons::applyColumns(FXuint index)
{
    FileList* list = filepanel->getList();
    for (const auto& column : captureColumns()) tabColumns[index].emplace(column.first, column.second);
    FileList::ColumnSizing sizing;
    sizing.mode = tabColumnModes[index];
    sizing.widths = tabColumns[index];
    list->setColumnSizing(sizing);
}

FileList::ColumnSizing TabButtons::getColumnSizing(FXuint index)
{
    onUpdCurrentTab(this, 0, NULL);
    FileList::ColumnSizing sizing;
    if (index < nbActiveTabs)
    {
        sizing.mode = tabColumnModes[index];
        sizing.widths = tabColumns[index];
    }
    return sizing;
}

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
        FileList::ColumnSizing defaults = filepanel->getList()->defaultColumnSizing();
        tabColumnModes[index] = defaults.mode;
        tabColumns[index] = defaults.mode == FileList::ColumnSizing::LEGACY ? captureColumns() : defaults.widths;

        // Tab label
        tabButtons[index]->setText(tabLabel(pathname));

        // Set directory
        filepanel->setDirectory(pathname);
        applyColumns(index);
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
    applyColumns(index);
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
        tabColumns[i] = tabColumns[i + 1];
        tabColumnModes[i] = tabColumnModes[i + 1];
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
            applyColumns(currentTab);
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


TabButtons::State TabButtons::saveState()
{
    onUpdCurrentTab(this, 0, NULL);
    State saved;
    saved.selected = currentTab;
    saved.columnOrder = filepanel->getList()->getColumnLayout();
    for (FXuint i = 0; i < nbActiveTabs; ++i)
    {
        saved.paths.push_back(tabPaths[i]);
        saved.views.push_back(tabViews[i]);
        saved.columns.push_back(tabColumns[i]);
        saved.columnModes.push_back(tabColumnModes[i]);
    }
    return saved;
}

void TabButtons::restoreState(const State& saved)
{
    if (saved.paths.empty()) return;
    nbActiveTabs = saved.paths.size();
    currentTab = prevTab = saved.selected;
    for (FXuint i = 0; i < NMAX_TABS; ++i)
    {
        if (i < nbActiveTabs)
        {
            tabPaths[i] = saved.paths[i];
            tabViews[i] = saved.views[i];
            tabColumns[i] = saved.columns[i];
            tabColumnModes[i] = i < saved.columnModes.size() ? saved.columnModes[i] : FileList::ColumnSizing::LEGACY;
            tabButtons[i]->setText(tabLabel(tabPaths[i]));
            tabButtons[i]->show();
            tabButtons[i]->getParent()->show();
            closeButtons[i]->show();
            const FXbool active = i == currentTab;
            tabButtons[i]->setFont(active ? highlightFont : normalFont);
            tabButtons[i]->setState(active ? STATE_DOWN : STATE_UP);
            closeButtons[i]->setState(STATE_UP);
            setTabColor(i, active ? activecolor : basecolor);
        }
        else
        {
            tabPaths[i].clear();
            tabButtons[i]->hide();
            tabButtons[i]->getParent()->hide();
            closeButtons[i]->hide();
        }
    }
    filepanel->getList()->setColumnLayout(saved.columnOrder);
    filepanel->setDirectory(tabPaths[currentTab]);
    filepanel->getList()->setViewMode((FileList::ViewMode)tabViews[currentTab]);
    applyColumns(currentTab);
    filepanel->updatePath(true);
    if (filepanel->isActive()) dirpanel->setDirectory(filepanel->getDirectory(), true);
    recalc();
}

UnmountTabGuard::UnmountTabGuard(FXMainWindow* window, const FXString& path) : mountpoint(path)
{
    if (path.empty()) { completed = true; return; }
    XFileExplorer* explorer = static_cast<XFileExplorer*>(window);
    FilePanel* panels[] = {explorer->getLeftPanel(), explorer->getRightPanel()};
    for (int i = 0; i < 2; ++i)
    {
        if (!panels[i]) continue;
        TabButtons* candidate = panels[i]->getTabButtons();
        TabButtons::State state = candidate->saveState();
        for (const FXString& tab : state.paths)
        {
            if (tab == path || tab.left(path.length() + 1) == path + "/")
            {
                tabs[i] = candidate;
                states[i] = state;
                break;
            }
        }
    }
}

UnmountTabGuard::~UnmountTabGuard()
{
    if (completed) return;
    if (xf_gvfsmount(mountpoint).empty())
    {
        if (xf_mountsource(mountpoint).empty()) return;
    }
    else
    {
        struct stat info;
        if (lstat(mountpoint.text(), &info) != 0) return;
    }
    for (int i = 0; i < 2; ++i)
        if (tabs[i]) tabs[i]->restoreState(states[i]);
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
        tabColumns[kept] = tabColumns[i];
        tabColumnModes[kept] = tabColumnModes[i];
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
        tabColumns[0] = captureColumns();
        tabColumnModes[0] = filepanel->getList()->getColumnSizing().mode;
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
        applyColumns(currentTab);
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
    tabColumnModes[currentTab] = filepanel->getList()->getColumnSizing().mode;
    for (const auto& column : captureColumns()) tabColumns[currentTab][column.first] = column.second;
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
    xf_windowcolors(&menu);
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

    xf_windowcolors(menu);
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
        std::swap(tabColumns[index], tabColumns[index - 1]);
        std::swap(tabColumnModes[index], tabColumnModes[index - 1]);

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
        std::swap(tabColumns[index], tabColumns[index + 1]);
        std::swap(tabColumnModes[index], tabColumnModes[index + 1]);

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
        FileList::ColumnSizing sizing = getColumnSizing(index);
        dirpanel->getBookmarksList()->bookmarkFolder(tabPaths[index], &sizing);
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

long TabButtons::onTheme(FXObject*, FXSelector, void* pointer)
{
    const Theme& theme = *static_cast<const Theme*>(pointer);
    basecolor = theme.colorFor("tabs", "background");
    shadecolor = basecolor;
    activecolor = theme.colorFor("tabs", "selection_background");
    if (!theme.overrides("tabs", "selection_background")) activecolor = themeBlend(basecolor, activecolor, 0.4);
    textcolor = theme.colorFor("tabs", "foreground");
    activeTextColor = theme.colorFor("tabs", "selection_foreground");
    normalFont = tabButtons.empty() ? getApp()->getNormalFont() : tabButtons[0]->getFont();
    FXFontDesc description;
    normalFont->getFontDesc(description);
    description.weight = description.weight == FXFont::Normal ? FXFont::Bold : FXFont::Normal;
    FXFont* previous = highlightFont;
    highlightFont = new FXFont(getApp(), description);
    highlightFont->create();
    setBackColor(basecolor);
    showTabStates();
    delete previous;
    recalc();
    return 1;
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
    ColumnWidths columns = tabColumns[from];
    FXuint columnMode = tabColumnModes[from];
    const int step = (to > from) ? 1 : -1;
    for (FXuint i = from; i != to; i += step)
    {
        tabButtons[i]->setText(tabButtons[i + step]->getText());
        tabPaths[i] = tabPaths[i + step];
        tabViews[i] = tabViews[i + step];
        tabColumns[i] = tabColumns[i + step];
        tabColumnModes[i] = tabColumnModes[i + step];
    }
    tabButtons[to]->setText(label);
    tabPaths[to] = path;
    tabViews[to] = view;
    tabColumns[to] = columns;
    tabColumnModes[to] = columnMode;

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
    applyColumns(index);
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
        getApp()->reg().writeStringEntry(section, key.text(), xf_portablepathreference(tabPaths[i]).text());
        key = "view" + FXStringVal(i);
        getApp()->reg().writeUnsignedEntry(section, key.text(), tabViews[i]);
        FileList::ColumnSizing sizing;
        sizing.mode = tabColumnModes[i];
        sizing.widths = tabColumns[i];
        FileList::saveColumnSizing(getApp()->reg(), section, FXStringVal(i), sizing);
    }
    getApp()->reg().writeUnsignedEntry(section, "current_tab", currentTab);
}


FXbool TabButtons::restoreTabs(const char* section, FXuint selected)
{
    FXuint restoredSelection = 0;
    for (FXuint i = 0; i < NMAX_TABS; i++)
    {
        FXString key = "tab" + FXStringVal(i);
        FXString saved = getApp()->reg().readStringEntry(section, key.text(), "");
        FXString path = xf_resolveportablepath(saved);
        if (path.empty())
        {
            break;
        }
        if (!RemoteDirectory::isServer(path) && (!xf_isdirectory(path) || !xf_isreadexecutable(path)))
        {
            appLog(APP_LOG_ERRORS, "tab-restore-unavailable", FXString("pane=") + section + " path=\"" + path + "\"");
            continue;
        }
        FXString reference = xf_portablepathreference(path);
        if (reference != saved) getApp()->reg().writeStringEntry(section, key.text(), reference.text());
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
        FileList::ColumnSizing fallback = getColumnSizing(currentTab);
        fallback.mode = FileList::ColumnSizing::LEGACY;
        FileList::ColumnSizing sizing = FileList::loadColumnSizing(getApp()->reg(), section, FXStringVal(i), fallback);
        tabColumnModes[currentTab] = sizing.mode;
        tabColumns[currentTab] = sizing.widths;
        applyColumns(currentTab);
    }
    if (!nbActiveTabs)
    {
        return false;
    }
    setCurrentTab(restoredSelection);
    return true;
}
