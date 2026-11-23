// Dialog Box with additional toggle option
#include "config.h"
#include "i18n.h"

#include <fx.h>
#include <fxkeys.h>

#include "DialogBox.h"
#include "xfeutils.h"
#include <X11/Xlib.h>
#include <vector>


// Main window
extern FXMainWindow* mainWindow;

// Map
FXDEFMAP(DialogBox) DialogBoxMap[] =
{
    FXMAPFUNC(SEL_KEYPRESS, 0, DialogBox::onKeyPress),
    FXMAPFUNC(SEL_KEYRELEASE, 0, DialogBox::onKeyRelease),
    FXMAPFUNC(SEL_CLOSE, 0, DialogBox::onClose),
    FXMAPFUNC(SEL_COMMAND, DialogBox::ID_CANCEL, DialogBox::onCmdCancel),
    FXMAPFUNC(SEL_COMMAND, DialogBox::ID_ACCEPT, DialogBox::onCmdAccept),
    FXMAPFUNC(SEL_COMMAND, DialogBox::ID_TOGGLE_OPTION, DialogBox::onCmdToggleOption)
};


// Object implementation
FXIMPLEMENT(DialogBox, FXTopWindow, DialogBoxMap, ARRAYNUMBER(DialogBoxMap))

FXIMPLEMENT(DialogButtonBox, FXHorizontalFrame, NULL, 0)

static FXint dialogbuttonwidth(FXWindow* child)
{
    FXint width = (child->getLayoutHints() & LAYOUT_FIX_WIDTH) ? child->getWidth() : child->getDefaultWidth();
    FXButton* button = dynamic_cast<FXButton*>(child);
    FXint minimum = child->getApp()->getNormalFont()->getTextWidth("00000000", 8) + 20;
    return button && !button->getText().empty() ? FXMAX(minimum, width) : width;
}


FXint DialogButtonBox::getDefaultWidth()
{
    FXint width = 0, count = 0;
    for (FXWindow* child = getFirst(); child; child = child->getNext())
    {
        if (child->shown())
        {
            width += dialogbuttonwidth(child);
            ++count;
        }
    }
    return padleft + padright + 2 * border + width + FXMAX(0, count - 1) * hspacing;
}


FXint DialogButtonBox::arrange(FXint available, FXbool position)
{
    std::vector<FXWindow*> left, actions;
    for (FXWindow* child = getFirst(); child; child = child->getNext())
    {
        if (!child->shown()) continue;
        FXuint hints = child->getLayoutHints();
        if (dynamic_cast<FXButton*>(child) && (hints & LAYOUT_RIGHT)) actions.insert(actions.begin(), child);
        else if (dynamic_cast<FXButton*>(child) && (hints & (LAYOUT_CENTER_X | LAYOUT_FILL_X))) actions.push_back(child);
        else left.push_back(child);
    }
    available = FXMAX(1, available - padleft - padright - 2 * border);
    FXint leftwidth = 0, actionwidth = 0, rowheight = 0, y = border + padtop;
    for (FXWindow* child : left)
    {
        leftwidth += dialogbuttonwidth(child) + hspacing;
        rowheight = FXMAX(rowheight, child->getDefaultHeight());
    }
    if (!left.empty()) leftwidth -= hspacing;
    for (FXWindow* child : actions)
    {
        actionwidth += dialogbuttonwidth(child) + hspacing;
        rowheight = FXMAX(rowheight, child->getDefaultHeight());
    }
    if (!actions.empty()) actionwidth -= hspacing;
    if (leftwidth + actionwidth + (!left.empty() && !actions.empty() ? hspacing : 0) <= available)
    {
        for (int group = 0; group < 2; ++group)
        {
            FXint x = border + padleft + (group ? available - actionwidth : 0);
            for (FXWindow* child : group ? actions : left)
            {
                FXint width = dialogbuttonwidth(child), height = child->getDefaultHeight();
                if (position) child->position(x, y + (rowheight - height) / 2, width, height);
                x += width + hspacing;
            }
        }
        return y + rowheight + padbottom + border;
    }
    // On narrow windows, keep Help/options above the actions and the primary action at bottom right.
    for (int group = 0; group < 2; ++group)
    {
        const std::vector<FXWindow*>& controls = group ? actions : left;
        for (size_t start = 0; start < controls.size();)
        {
            size_t end = start;
            FXint rowwidth = 0;
            rowheight = 0;
            while (end < controls.size())
            {
                FXint width = FXMIN(available, dialogbuttonwidth(controls[end]));
                if (end > start && rowwidth + hspacing + width > available) break;
                rowwidth += (end > start ? hspacing : 0) + width;
                rowheight = FXMAX(rowheight, controls[end]->getDefaultHeight());
                ++end;
            }
            FXint x = border + padleft + (group ? available - rowwidth : 0);
            for (size_t i = start; i < end; ++i)
            {
                FXint width = FXMIN(available, dialogbuttonwidth(controls[i])), height = controls[i]->getDefaultHeight();
                if (position) controls[i]->position(x, y + (rowheight - height) / 2, width, height);
                x += width + hspacing;
            }
            y += rowheight;
            start = end;
            if (start < controls.size() || (!group && !actions.empty())) y += vspacing;
        }
    }
    return y + padbottom + border;
}


FXint DialogButtonBox::getDefaultHeight()
{
    FXint available = getParent()->getWidth();
    if (FXTopWindow* window = dynamic_cast<FXTopWindow*>(getParent()))
        available -= window->getPadLeft() + window->getPadRight();
    else if (FXPacker* packer = dynamic_cast<FXPacker*>(getParent()))
        available -= packer->getPadLeft() + packer->getPadRight() + 2 * packer->getBorderWidth();
    return arrange(available > 1 ? available : getDefaultWidth(), false);
}


void DialogButtonBox::layout()
{
    arrange(width, true);
    flags &= ~FLAG_DIRTY;
}


// Contruct dialog which will stay on top of owner
DialogBox::DialogBox(FXWindow* win, const FXString& name, FXuint opts, int x, int y, int w, int h,
                     int pl, int pr, int pt, int pb, int hs, int vs) :
    FXTopWindow(win, name, NULL, NULL, opts, x, y, w, h, pl, pr, pt, pb, hs, vs)
{
    _option = 0;
}


// Contruct free floating dialog
DialogBox::DialogBox(FXApp* a, const FXString& name, FXuint opts, int x, int y, int w, int h,
                     int pl, int pr, int pt, int pb, int hs, int vs) :
    FXTopWindow(a, name, NULL, NULL, opts, x, y, w, h, pl, pr, pt, pb, hs, vs)
{
    _option = 0;
}


// Close window & cancel out of dialog
long DialogBox::onClose(FXObject*, FXSelector, void*)
{
    if (target && target->handle(this, FXSEL(SEL_CLOSE, message), NULL))
    {
        // Set focus to main window (if defined)
        if (mainWindow)
        {
            mainWindow->setFocus();
        }

        return 1;
    }
    handle(this, FXSEL(SEL_COMMAND, DialogBox::ID_CANCEL), NULL);
    return 1;
}


// Close dialog with an accept
long DialogBox::onCmdAccept(FXObject*, FXSelector, void*)
{
    getApp()->stopModal(this, true);
    hide();

    // Set focus to main window (if defined)
    if (mainWindow)
    {
        mainWindow->setFocus();
    }

    return 1;
}


// Close dialog with a cancel
long DialogBox::onCmdCancel(FXObject*, FXSelector, void*)
{
    getApp()->stopModal(this, false);
    hide();

    // Set focus to main window (if defined)
    if (mainWindow)
    {
        mainWindow->setFocus();
    }

    return 1;
}


// Toggle option
long DialogBox::onCmdToggleOption(FXObject*, FXSelector, void*)
{
    _option = !_option;
    return 1;
}


// Get option state
FXuint DialogBox::getOption()
{
    return _option;
}


// Scroll window that keeps the natural size of its contents as its own default size
class DialogScroller : public FXScrollWindow
{
    FXDECLARE(DialogScroller)
protected:
    DialogScroller() {}
public:
    explicit DialogScroller(FXComposite* parent) : FXScrollWindow(parent, LAYOUT_FILL_X | LAYOUT_FILL_Y)
    {
        xf_readablescrolling(this);
    }
    virtual FXint getDefaultWidth()
    {
        return contentWindow() ? contentWindow()->getDefaultWidth() : 1;
    }
    virtual FXint getDefaultHeight()
    {
        return contentWindow() ? contentWindow()->getDefaultHeight() : 1;
    }
    virtual void create()
    {
        FXbool initial = !id();
        FXScrollWindow::create();
        if (initial)
        {
            FXTopWindow* window = static_cast<FXTopWindow*>(getParent());
            FXint maxwidth = FXMAX(1, getRoot()->getWidth() - 20);
            FXint maxheight = FXMAX(1, getRoot()->getHeight() - 40);
            FXint naturalwidth = window->getDefaultWidth();
            window->resize(FXMIN(maxwidth, FXMAX(window->getWidth(), naturalwidth)), window->getHeight());
            FXint naturalheight = window->getDefaultHeight();
            if (naturalheight > maxheight)
                naturalwidth += verticalScrollBar()->getDefaultWidth();
            if (naturalwidth > maxwidth)
                naturalheight += horizontalScrollBar()->getDefaultHeight();
            window->resize(FXMIN(maxwidth, FXMAX(window->getWidth(), naturalwidth)),
                           FXMIN(maxheight, FXMAX(window->getHeight(), naturalheight)));
        }
    }
};
FXIMPLEMENT(DialogScroller, FXScrollWindow, NULL, 0)


void xf_readablescrolling(FXScrollWindow* scroll)
{
    scroll->verticalScrollBar()->setLine(scroll->getApp()->getNormalFont()->getFontHeight());
    scroll->horizontalScrollBar()->setLine(scroll->getApp()->getNormalFont()->getFontWidth());
}


static FXbool canwheelscroll(FXScrollArea* area, FXint code)
{
    FXScrollBar* bar = area->verticalScrollBar();
    FXint maximum = FXMAX(0, bar->getRange() - bar->getPage());
    return code > 0 ? bar->getPosition() > 0 : bar->getPosition() < maximum;
}


FXScrollArea* xf_wheeltarget(FXWindow* window, FXint code)
{
    FXScrollArea* inner = NULL;
    for (; window && !inner; window = window->getParent())
    {
        if (dynamic_cast<FXSpinner*>(window) || dynamic_cast<FXRealSpinner*>(window) ||
            dynamic_cast<FXSlider*>(window) || dynamic_cast<FXComboBox*>(window) ||
            dynamic_cast<FXListBox*>(window) || dynamic_cast<FXDial*>(window))
        {
            return NULL;
        }
        inner = dynamic_cast<FXScrollArea*>(window);
    }
    if (!inner || canwheelscroll(inner, code))
    {
        return NULL;
    }
    for (FXWindow* parent = inner->getParent(); parent; parent = parent->getParent())
    {
        FXScrollArea* outer = dynamic_cast<FXScrollArea*>(parent);
        if (outer && canwheelscroll(outer, code))
        {
            return outer;
        }
        if (parent->isMemberOf(FXMETACLASS(FXTopWindow)))
        {
            break;
        }
    }
    return NULL;
}


bool XFApp::dispatchEvent(FXRawEvent& ev)
{
    const unsigned int buttons = Button1Mask | Button2Mask | Button3Mask;
    if (ev.xany.type == ButtonPress && (ev.xbutton.button == Button4 || ev.xbutton.button == Button5) &&
        !(ev.xbutton.state & (buttons | ControlMask | ShiftMask)))
    {
        FXWindow* window = findWindowWithId(ev.xbutton.window);
        FXbool grabbed = false;
        for (FXWindow* w = window; w; w = w->getParent())
        {
            grabbed |= w->grabbed();
        }
        FXScrollArea* target = (window && !grabbed) ?
                               xf_wheeltarget(window, ev.xbutton.button == Button4 ? 120 : -120) : NULL;
        if (target)
        {
            FXint x, y;
            window->translateCoordinatesTo(x, y, target, ev.xbutton.x, ev.xbutton.y);
            ev.xbutton.window = target->id();
            ev.xbutton.x = x;
            ev.xbutton.y = y;
        }
    }
    return FXApp::dispatchEvent(ev);
}


static void dialogfooters(FXWindow* parent, std::vector<DialogButtonBox*>& footers)
{
    for (FXWindow* child = parent->getFirst(); child; child = child->getNext())
    {
        if (DialogButtonBox* footer = dynamic_cast<DialogButtonBox*>(child))
            footers.push_back(footer);
        else if (!dynamic_cast<FXTopWindow*>(child))
            dialogfooters(child, footers);
    }
}


void xf_scrollabledialog(FXTopWindow* window)
{
    window->setDecorations(window->getDecorations() | DECOR_TITLE | DECOR_BORDER | DECOR_RESIZE | DECOR_MAXIMIZE);
    FXWindow* first = window->getFirst();
    if (!first) return;
    for (FXWindow* child = first; child; child = child->getNext())
        if (dynamic_cast<DialogScroller*>(child)) return;
    std::vector<DialogButtonBox*> footers;
    dialogfooters(window, footers);
    for (auto footer = footers.rbegin(); footer != footers.rend(); ++footer)
    {
        if (*footer != window->getFirst()) (*footer)->reparent(window, window->getFirst());
        (*footer)->setLayoutHints(LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
    }
    DialogScroller* scroller = new DialogScroller(window);
    FXPacker* content = new FXPacker(scroller, LAYOUT_FILL_X | LAYOUT_FILL_Y, 0, 0, 0, 0,
                                     window->getPadLeft(), window->getPadRight(), window->getPadTop(),
                                     window->getPadBottom(), window->getHSpacing(), window->getVSpacing());
    for (FXWindow* child = window->getFirst(); child && child != scroller;)
    {
        FXWindow* next = child->getNext();
        if (!dynamic_cast<DialogButtonBox*>(child)) child->reparent(content);
        child = next;
    }
    window->setPadLeft(0);
    window->setPadRight(0);
    window->setPadTop(0);
    window->setPadBottom(0);
    if (window->id()) scroller->create();
}


// Create window
void DialogBox::create()
{
    xf_scrollabledialog(this);
    xf_dialogcolors(this);
    FXTopWindow::create();
}


// Show window such that the cursor is in it
void DialogBox::show(FXuint placement)
{
    xf_dialogcolors(this);
    int rw, rh, wx, wy, ww, wh, x, y;
    FXuint state;

    // Ensure the dialog has a usable size and fits on screen.
    rw = getRoot()->getWidth();
    rh = getRoot()->getHeight();
    ww = getWidth();
    wh = getHeight();
    if (ww <= 1)
    {
        ww = getDefaultWidth();
    }
    if (wh <= 1)
    {
        wh = getDefaultHeight();
    }
    ww = FXMIN(ww, FXMAX(1, rw - 20));
    wh = FXMIN(wh, FXMAX(1, rh - 40));
    if (ww != getWidth() || wh != getHeight())
    {
        resize(ww, wh);
    }

    // Get dialog position
    translateCoordinatesTo(wx, wy, getRoot(), 0, 0);
    ww = getWidth();
    wh = getHeight();

    // Where's the mouse?
    getRoot()->getCursorPosition(x, y, state);

    // Place such that mouse in the middle
    if ((x < wx) || (y < wy) || (wx + ww <= x) || (wy + wh <= y))
    {
        // Move by the minimal amount
        if (x < wx)
        {
            wx = x - 20;
        }
        else if (wx + ww <= x)
        {
            wx = x - ww + 20;
        }
        if (y < wy)
        {
            wy = y - 20;
        }
        else if (wy + wh <= y)
        {
            wy = y - wh + 20;
        }

        // Adjust so dialog is fully visible
        if (wx < 0)
        {
            wx = 10;
        }
        if (wy < 0)
        {
            wy = 10;
        }
        if (wx + ww > rw)
        {
            wx = rw - ww - 10;
        }
        if (wy + wh > rh)
        {
            wy = rh - wh - 10;
        }

        move(wx, wy);
    }

    // Pop the window
    FXTopWindow::show(placement);
}


// Keyboard press; handle escape and return to close the dialog
long DialogBox::onKeyPress(FXObject* sender, FXSelector sel, void* ptr)
{
    FXEvent* event = (FXEvent*)ptr;

    switch (event->code)
    {
    case KEY_Escape:
        handle(this, FXSEL(SEL_COMMAND, ID_CANCEL), NULL);
        return 1;

    default:
        FXTopWindow::onKeyPress(sender, sel, ptr);
        return 1;
    }

    return 0;
}

// Keyboard release; handle escape to close the dialog
long DialogBox::onKeyRelease(FXObject* sender, FXSelector sel, void* ptr)
{
    if (FXTopWindow::onKeyRelease(sender, sel, ptr))
    {
        return 1;
    }

    if (((FXEvent*)ptr)->code == KEY_Escape)
    {
        return 1;
    }
    return 0;
}


// Execute dialog box modally
FXuint DialogBox::execute(FXuint placement)
{
    create();
    show(placement);
    getApp()->refresh();
    return getApp()->runModalFor(this);
}

FXDEFMAP(DialogTabItem) DialogTabItemMap[] = {
    FXMAPFUNC(SEL_PAINT, 0, DialogTabItem::onPaint)
};
FXIMPLEMENT(DialogTabItem, FXTabItem, DialogTabItemMap, ARRAYNUMBER(DialogTabItemMap))

long DialogTabItem::onPaint(FXObject* sender, FXSelector selector, void* pointer)
{
    FXTabBook* book = dynamic_cast<FXTabBook*>(getParent());
    FXTabBar* bar = dynamic_cast<FXTabBar*>(getParent());
    FXbool active = book ? book->indexOfChild(this) / 2 == book->getCurrent() :
                    bar ? bar->indexOfChild(this) == bar->getCurrent() : false;
    if (activeKnown && active != lastActive && bar)
    {
        for (FXWindow* child = bar->getFirst(); child; child = child->getNext())
            if (DialogTabItem* sibling = dynamic_cast<DialogTabItem*>(child))
                if (sibling != this) sibling->update();
    }
    lastActive = active;
    activeKnown = true;
    FXTabItem::onPaint(sender, selector, pointer);
    if (book && active && !xf_fixedpalette(this))
    {
        FXDCWindow dc(this, static_cast<FXEvent*>(pointer));
        dc.setForeground(highlight);
        dc.fillRectangle(3, getHeight() - 3, FXMAX(0, getWidth() - 6), 3);
    }
    return 1;
}
