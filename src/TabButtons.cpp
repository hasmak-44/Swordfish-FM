#include "config.h"
#include "i18n.h"

#include "AppLogger.h"
#include "TabButtons.h"
#include "MessageBox.h"


extern FXString homedir;


// Tab label button that can be dragged onto the Bookmarks list
class TabDragButton : public FXButton
{
    FXDECLARE(TabDragButton)

protected:
    FXint pressx = 0;
    FXint pressy = 0;
    FXuint pressstate = STATE_UP;
    FXbool dragging = false;
    FXDragType tabType = 0;

    TabDragButton()
    {
    }

public:
    TabDragButton(FXComposite* p, FXObject* tgt, FXSelector sel, FXuint opts) :
        FXButton(p, "", NULL, tgt, sel, opts, 0, 0, 0, 0, 4, 4, 2, 2)
    {
    }

    virtual void create()
    {
        FXButton::create();
        tabType = getApp()->registerDragType(TABBUTTONS_DRAG_TYPE);
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
        setState(pressstate);
        return 1;
    }

    long onDNDRequest(FXObject* sender, FXSelector sel, void* ptr)
    {
        FXEvent* event = (FXEvent*)ptr;
        if (FXButton::onDNDRequest(sender, sel, ptr))
        {
            return 1;
        }
        TabButtons* tabs = dynamic_cast<TabButtons*>(getTarget());
        if (event->target == tabType && tabs)
        {
            FXString path = tabs->getPath(getSelector() - TabButtons::ID_START_TAB);
            FXuchar* data;
            FXMALLOC(&data, FXuchar, path.length() + 1);
            memcpy(data, path.text(), path.length() + 1);
            setDNDData(FROM_DRAGNDROP, tabType, data, path.length());
            return 1;
        }
        return 0;
    }
};

FXDEFMAP(TabDragButton) TabDragButtonMap[] =
{
    FXMAPFUNC(SEL_LEFTBUTTONPRESS, 0, TabDragButton::onLeftBtnPress),
    FXMAPFUNC(SEL_LEFTBUTTONRELEASE, 0, TabDragButton::onLeftBtnRelease),
    FXMAPFUNC(SEL_MOTION, 0, TabDragButton::onMotion),
    FXMAPFUNC(SEL_DND_REQUEST, 0, TabDragButton::onDNDRequest),
};

FXIMPLEMENT(TabDragButton, FXButton, TabDragButtonMap, ARRAYNUMBER(TabDragButtonMap))


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
    if ((basecolor != getApp()->getBaseColor()) && (color == shadecolor))
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

        // Tab label
        if (pathname == PATHSEPSTRING)
        {
            tabButtons[index]->setText(PATHSEPSTRING);
        }
        else
        {
            FXString name = FXPath::name(pathname);

            // SMB mount
            if (name.left(10) == "smb-share:")
            {
                FXString tmp;
                int pos = 0;

                // Server name
                pos = name.find("server=") + 7;
                tmp = name.right(name.length() - pos);
                FXString server = tmp.before(',');

                // Share name
                pos = name.find("share=") + 6;
                tmp = name.right(name.length() - pos);
                FXString share = tmp.before(',');
                
                // Tab label
                FXString label = share.substitute("%20", " ") + "@" + server;
                tabButtons[index]->setText(label);
            }
            
            // MTP mount
            else if (name.left(4) == "mtp:")
            {
                // Tab label
                FXString label = name.after('_');
                label = label.before('_', 2);
                tabButtons[index]->setText(label);
            }
            
            // Other
            else
            {
                tabButtons[index]->setText(name);
            }
        }

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
        setTabColor(currentTab, shadecolor);
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
    setTabColor(currentTab, shadecolor);

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
    setTabColor(currentTab, shadecolor);

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
    if (pathname == PATHSEPSTRING)
    {
        tabButtons[currentTab]->setText(PATHSEPSTRING);
    }
    else
    {
        FXString name = FXPath::name(pathname);

        // SMB mount
        if (name.left(10) == "smb-share:")
        {
            FXString tmp;
            int pos = 0;

            // Server name
            pos = name.find("server=") + 7;
            tmp = name.right(name.length() - pos);
            FXString server = tmp.before(',');

            // Share name
            pos = name.find("share=") + 6;
            tmp = name.right(name.length() - pos);
            FXString share = tmp.before(',');
            
            // Tab label
            FXString label = share.substitute("%20", " ") + "@" + server;
            tabButtons[currentTab]->setText(label);
        }
        
        // MTP mount
        else if (name.left(4) == "mtp:")
        {
            // Tab label
            FXString label = name.after('_');
            label = label.before('_', 2);
            tabButtons[currentTab]->setText(label);
        }
        
        // Other
        else
        {
            tabButtons[currentTab]->setText(name);
        }
    }

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
        
        tabButtons[index - 1]->setText(tabButtons[index]->getText());
        tabPaths[index - 1] = tabPaths[index];

        tabButtons[index]->setText(left_label);
        tabPaths[index] = left_path;        

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
        setTabColor(currentTab, shadecolor);
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
        
        tabButtons[index + 1]->setText(tabButtons[index]->getText());
        tabPaths[index + 1] = tabPaths[index];

        tabButtons[index]->setText(right_label);
        tabPaths[index] = right_path;        

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
        setTabColor(currentTab, shadecolor);
    }

    return 1;
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
    setTabColor(currentTab, shadecolor);
}


void TabButtons::saveTabs(const char* section)
{
    onUpdCurrentTab(this, 0, NULL);
    getApp()->reg().deleteSection(section);
    for (FXuint i = 0; i < nbActiveTabs; i++)
    {
        FXString key = "tab" + FXStringVal(i);
        getApp()->reg().writeStringEntry(section, key.text(), tabPaths[i].text());
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
        if (!xf_isdirectory(path) || !xf_isreadexecutable(path))
        {
            appLog(APP_LOG_ERRORS, "tab-restore-unavailable", FXString("pane=") + section + " path=\"" + path + "\"");
            continue;
        }
        if (i <= selected)
        {
            restoredSelection = nbActiveTabs;
        }
        addTab(path, false);
    }
    if (!nbActiveTabs)
    {
        return false;
    }
    setCurrentTab(restoredSelection);
    return true;
}
