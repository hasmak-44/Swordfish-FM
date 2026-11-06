#ifndef CUSTOMACTIONSUI_H
#define CUSTOMACTIONSUI_H

#include "CustomActions.h"

class XFileExplorer;
class FilePanel;
class CustomActionMenu;

class CustomActionsUI : public FXObject
{
    XFileExplorer* owner;
    FXToolBar* toolbar;
    std::vector<FXIcon*> icons;
    CustomActionMenu* toolbarMenu = NULL;
    CustomActionMenu* mainMenu = NULL;
    FXMenuPane* mainPane = NULL;
    FXMenuTitle* mainTitle = NULL;
    FXString loadError;
    bool toolbarShown = true;
    void rebuild();
public:
    enum { ID_EDIT = 1, ID_TOGGLE, ID_LOAD_ERROR, ID_ACTION = 100 };
    CustomActions model;
    CustomActionsUI(XFileExplorer*, FXDockSite*, FXMenuPane*, FXMenuBar*);
    ~CustomActionsUI();
    long handle(FXObject*, FXSelector, void*);
    CustomActionContext context(FilePanel* = NULL) const;
    CustomActionMenu* appendContextMenu(FXMenuPane*, FilePanel*);
    void run(unsigned, const CustomActionContext&);
    FXIcon* icon(unsigned) const;
};

// Owns cascading panes (FOX menu cascades do not own their popup).
class CustomActionMenu : public FXObject
{
    CustomActionsUI* actions;
    bool live;
    CustomActionContext snapshot;
    std::vector<FXMenuPane*> panes;
public:
    CustomActionMenu(CustomActionsUI*, bool, const CustomActionContext&);
    ~CustomActionMenu();
    FXMenuPane* pane(FXWindow*);
    void fill(FXMenuPane*, const FXString&, bool filter);
    void appendEditor(FXMenuPane*);
    long handle(FXObject*, FXSelector, void*);
};

#endif
