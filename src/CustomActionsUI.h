#ifndef CUSTOMACTIONSUI_H
#define CUSTOMACTIONSUI_H

#include "CustomActions.h"

class XFileExplorer;
class FilePanel;
class CustomActionMenu;

class CustomActionsUI : public FXObject
{
    friend class CustomActionMenu;
    XFileExplorer* owner;
    FXToolBar* toolbar;
    std::vector<FXIcon*> icons;
    std::vector<FXIcon*> toolbarIcons;
    CustomActionMenu* toolbarMenu = NULL;
    CustomActionMenu* mainMenu = NULL;
    FXMenuPane* mainPane = NULL;
    FXMenuTitle* mainTitle = NULL;
    FXString loadError;
    FXString iconWarning;
    FXString storageRoot;
    FXString storageFingerprint;
    FXString storageWarning;
    FXString lastStorageError;
    FXString newActionsWarning;
    bool toolbarShown = true;
    bool editorOpen = false;
    unsigned transientMenus = 0;
    void rebuild();
    void showToolbarMenu();
    bool refreshStorage(bool notify);
    bool writeKnownActions(const CustomActions&, FXString&);
public:
    enum { ID_EDIT = 1, ID_TOGGLE, ID_LOAD_ERROR, ID_TOOLBAR_MENU, ID_REBUILD,
           ID_STYLE_BOTH, ID_STYLE_ICONS, ID_STYLE_TITLES, ID_SIZE_16, ID_SIZE_24, ID_SIZE_32,
           ID_ICON_WARNING, ID_STORAGE_WARNING, ID_NEW_ACTIONS, ID_REFRESH,
           ID_ACTION = 100 };
    CustomActions model;
    CustomActionsUI(XFileExplorer*, FXDockSite*, FXMenuPane*, FXMenuBar*);
    ~CustomActionsUI();
    long handle(FXObject*, FXSelector, void*);
    CustomActionContext context(FilePanel* = NULL) const;
    CustomActionMenu* appendContextMenu(FXMenuPane*, FilePanel*);
    bool saveStorage(CustomActions&, FXString&, FXString&);
    void run(unsigned, const CustomActionContext&);
    FXIcon* icon(unsigned) const;
    // Rebuild after the custom toolbar icon size preference changes
    void refreshToolbarIcons() { rebuild(); }
    // Opens the display/size menu when a custom toolbar button is right-clicked
    long toolbarButtonRightClick(FXObject*, FXSelector);
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
    FXMenuPane* pane(FXWindow*, FXuint opts = 0);
    FXMenuCaption* append(FXMenuPane*, unsigned, bool filter);
    void fill(FXMenuPane*, const FXString&, bool filter);
    void appendEditor(FXMenuPane*);
    bool isShowing() const;
    long handle(FXObject*, FXSelector, void*);
};

#endif
