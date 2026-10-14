#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <string.h>
#include <map>

#include "ColorSelector.h"
#include "ComboBox.h"
#include "DialogBox.h"
#include "KeyBindings.h"
#include "SystemIcons.h"

class ProgramChoice : public ComboBox
{
private:
    std::vector<FXString> names;
    std::vector<FXString> commands;
public:
    ProgramChoice(FXComposite* parent);
    void addApplication(const FXString& name, const FXString& command);
    void setText(const FXString& command);
    FXString getText() const;
};


// Number of modifiable colors
#define NUM_COLORS    12

// Number of default themes
#define NUM_DEFAULT_THEMES    14


struct Theme
{
    FXString name;
    FXString tip;
    FXColor color[NUM_COLORS];
    Theme()
    {
        name = "";
        tip = "";
    }

    Theme(const char* n, const char* t, FXColor base = 0, FXColor bdr = 0, FXColor bg = 0, FXColor fg = 0,
          FXColor selbg = 0, FXColor selfg = 0, FXColor listbg = 0, FXColor listfg = 0,
          FXColor listhl = 0, FXColor pbarfg = 0, FXColor attenfg = 0, FXColor scrollfg = 0)
    {
        name = FXString(n);
        tip = FXString(t);
        color[0] = base;
        color[1] = bdr;
        color[2] = bg;
        color[3] = fg;
        color[4] = selbg;
        color[5] = selfg;
        color[6] = listbg;
        color[7] = listfg;
        color[8] = listhl;
        color[9] = pbarfg;
        color[10] = attenfg;
        color[11] = scrollfg;
    }

    FXbool operator !=(const Theme&);
};


// Vector of themes
typedef std::vector<Theme>   vector_Theme;



class PreferencesBox : public DialogBox
{
    FXDECLARE(PreferencesBox)
private:
    ComboBox* colorsBox = NULL;
    FXList* themesList = NULL;
    FXTextField* iconpath = NULL;
    FXListBox* iconthemelist = NULL;
    std::vector<FXString> iconthemepaths;
    std::vector<SystemTheme> iconthemesystem;
    std::vector<int> iconthemesysindex;
    ProgramChoice* txtviewer = NULL;
    ProgramChoice* txteditor = NULL;
    ProgramChoice* filecomparator = NULL;
    FXTextField* timeformat = NULL;
    FXTextField* copysuffix = NULL;
    ProgramChoice* imgviewer = NULL;
    ProgramChoice* xterm = NULL;
    ProgramChoice* imgeditor = NULL;
    ProgramChoice* archiver = NULL;
    ProgramChoice* pdfviewer = NULL;
    ProgramChoice* pdfeditor = NULL;
    ProgramChoice* videoplayer = NULL;
    ProgramChoice* audioplayer = NULL;
    FXTextField* normalfont = NULL;
    FXTextField* textfont = NULL;
    FXTextField* mountcmd = NULL;
    FXTextField* umountcmd = NULL;
    FXList* associationlist = NULL;
    FXTextField* associationcommand = NULL;
    FXLabel* associationstatus = NULL;
    FXButton* associationapply = NULL;
    FXButton* associationreset = NULL;
    FXButton* restoreprogramdefaults = NULL;
    struct AssociationOverride
    {
        FXString extension;
        FXString command;
    };
    struct AssociationEntry
    {
        FXString extension;
        FXString mimetype;
        FXString systemApplication;
        FXString systemError;
    };
    std::vector<AssociationOverride> associationoverrides;
    std::vector<AssociationEntry> associationentries;
    FXbool restoreprogramsettings = false;
    std::map<FXString, FXString> systemdefaults;
    FXString systemdefaultserror;
    static void applyAssociationChanges(FXSettings&, const std::vector<AssociationOverride>&, FXbool);
    static void applyPreferredToolDefaults(FXSettings&);
    static void discardAssociationChanges(std::vector<AssociationOverride>&, FXbool&);
    static FXString associationApplicationStatus(const FXString&);
    static FXString associationEntryStatus(const AssociationEntry&);
    FXbool querySystemDefaultApplication(const FXString&, FXString&, FXString&);
    void populateProgramChoices();
    friend class ProgramSettingsSmokeTest;
#if defined(linux) && defined(XFE_AUTOMOUNTER)
    FXGroupBox* automountgroup = NULL;
#endif
    FXGroupBox* rootgroup = NULL;
    FXLabel* sulabel = NULL;
    FXLabel* sudolabel = NULL;
    FXTextField* sudocmd = NULL;
    FXTextField* sucmd = NULL;
    FXString oldiconpath;
    FXListBox* languagelist = NULL;
    std::vector<FXString> languagecodes;
    FXString oldlanguage;
    FXString oldtxtviewer;
    FXString oldtxteditor;
    FXString oldfilecomparator;
    FXString oldtimeformat;
    FXString oldcopysuffix;
    FXString oldimgviewer;
    FXString oldxterm;
    FXString oldnormalfont;
    FXString oldtextfont;
    FXString oldimgeditor;
    FXString oldarchiver;
    FXString oldpdfviewer;
    FXString oldpdfeditor;
    FXString oldaudioplayer;
    FXString oldvideoplayer;
    FXString oldmountcmd;
    FXString oldumountcmd;
    FXCheckButton* autosave = NULL;
    FXCheckButton* savewinpos = NULL;
    FXCheckButton* diropen = NULL;
    FXCheckButton* fileopen = NULL;
    FXCheckButton* filetooltips = NULL;
    FXCheckButton* relativeresize = NULL;
    FXCheckButton* alwaysshowtabbar = NULL;
    FXCheckButton* restoretabs = NULL;
    FXCheckButton* commonaddressbar = NULL;
    FXCheckButton* showpathlink = NULL;
#if defined(linux) && defined(XFE_AUTOMOUNTER)
    FXCheckButton* automountbutton = NULL;
    FXCheckButton* autoopenbutton = NULL;
    FXLabel* automountlabel = NULL;
#endif
    FXCheckButton* rootmode = NULL;
    FXCheckButton* trashcan = NULL;
    FXCheckButton* trashbypass = NULL;
    FXCheckButton* dnd = NULL;
    FXCheckButton* trashmv = NULL;
    FXCheckButton* del = NULL;
    FXCheckButton* properties = NULL;
    FXCheckButton* del_emptydir = NULL;
    FXCheckButton* overwrite = NULL;
    FXCheckButton* exec = NULL;
    FXCheckButton* ask = NULL;
    FXCheckButton* folder_warning = NULL;
    FXCheckButton* preserve_date_warning = NULL;
    FXCheckButton* root_warning = NULL;
    FXCheckButton* mountwarn = NULL;
    FXCheckButton* show_mount = NULL;
    FXCheckButton* scroll = NULL;
    FXRadioButton* dirpanel_places_tree = NULL;
    FXRadioButton* dirpanel_places = NULL;
    FXRadioButton* dirpanel_tree = NULL;
    FXLabel* dirpanel_startup = NULL;
    FXRadioButton* dirpanel_placesstartup = NULL;
    FXRadioButton* dirpanel_treestartup = NULL;
    FXSpinner* spindpi = NULL;
    FXSpinner* spinshade = NULL;
    FXSpinner* bigthumb_spin = NULL;
    FXSpinner* minithumb_spin = NULL;
    FXDataTarget startdirtarget;
    FXDataTarget newtabdirtarget;
    FXuint startdirmode = 0;
    FXuint oldstartdirmode = 0;
    FXuint newtabdirmode = 0;
    FXuint oldnewtabdirmode = 0;
#ifdef STARTUP_NOTIFICATION
    FXCheckButton* usesn = NULL;
#endif
    FXRadioButton* script_always = NULL;
    FXRadioButton* script_ask = NULL;
    FXRadioButton* script_never = NULL;
    FXDataTarget script_action_target;
    FXuint script_action = 1;
    FXuint old_script_action = 1;
    FXCheckButton* sizecol = NULL;
    FXCheckButton* typecol = NULL;
    FXCheckButton* extcol = NULL;
    FXCheckButton* datecol = NULL;
    FXCheckButton* usercol = NULL;
    FXCheckButton* groupcol = NULL;
    FXCheckButton* permscol = NULL;
    FXCheckButton* linkcol = NULL;
    FXList* listcol = NULL;
    FXButton* listcol_up = NULL;
    FXButton* listcol_down = NULL;
    FXbool sizecol_prev = false;
    FXbool typecol_prev = false;
    FXbool extcol_prev = false;
    FXbool datecol_prev = false;
    FXbool usercol_prev = false;
    FXbool groupcol_prev = false;
    FXbool permscol_prev = false;
    FXbool linkcol_prev = false;

    FXbool colShown[FileList::ID_COL_NAME + NMAX_COLS];
    FXuint idCol[NMAX_COLS] = { 0 };
    FXuint nbCols = 0;
    FXuint idCol_prev[NMAX_COLS] = { 0 };
    FXuint nbCols_prev = 0;

    ColorWell* cwell = NULL;
    vector_Theme Themes;
    Theme currTheme;
    Theme currTheme_prev;
    FXuint root_auth = 0;
    FXuint root_auth_prev = 0;
    FXbool trashcan_prev = false;
    FXbool trashbypass_prev = false;
    FXbool autosave_prev = false;
    FXbool savewinpos_prev = false;
    FXbool diropen_prev = false;
    FXbool fileopen_prev = false;
    FXbool filetooltips_prev = false;
    FXbool relativeresize_prev = false;
    FXbool alwaysshowtabbar_prev = false;
    FXbool restoretabs_prev = false;
    FXbool show_pathlink_prev = false;
    FXuint wheellines_prev = 0;
    FXint scrollbarsize_prev = 0;
    FXbool ask_prev = false;
    FXbool dnd_prev = false;
    FXbool trashmv_prev = false;
    FXbool del_prev = false;
    FXbool properties_prev = false;
    FXbool del_emptydir_prev = false;
    FXbool overwrite_prev = false;
    FXbool exec_prev = false;
#if defined(linux) && defined(XFE_AUTOMOUNTER)
    FXbool automount_prev = false;
    FXbool autoopen_prev = false;
#endif
    FXbool rootmode_prev = false;
    FXString sudocmd_prev;
    FXString sucmd_prev;
    FXint uidpi = 0;
    FXint uidpi_prev = 0;
    FXint bigthumb_size = 0;
    FXint minithumb_size = 0;
    FXint bigthumb_size_prev = 0;
    FXint minithumb_size_prev = 0;
    FXuint copysuffix_pos = 0;
    FXuint copysuffix_pos_prev = 0;
    FXuint dirpanel_list_startup = 0;
    FXuint dirpanel_list_startup_prev = 0;
    FXuint dirpanel_mode = 0;
    FXuint dirpanel_mode_prev = 0;

#ifdef STARTUP_NOTIFICATION
    FXbool usesn_prev = 0;
#endif
    FXuint script_action_prev = 1;
#if defined(linux)
    FXbool mountwarn_prev = false;
    FXbool show_mount_prev = false;
#endif
    FXbool root_warning_prev = false;
    FXbool folder_warning_prev = false;
    FXbool preserve_date_warning_prev = false;
    FXbool smoothscroll_prev = false;
    FXuint themelist_prev = 0;
    KeyBindingsBox* bindingsbox = NULL;
    FXStringDict* glbBindingsDict = NULL;
    FXStringDict* xfeBindingsDict = NULL;
    FXStringDict* xfiBindingsDict = NULL;
    FXStringDict* xfwBindingsDict = NULL;

    PreferencesBox()
    {
    }

public:
    enum
    {
        ID_ACCEPT=DialogBox::ID_LAST,
        ID_CANCEL,
        ID_BROWSE_TXTVIEW,
        ID_BROWSE_TXTEDIT,
        ID_BROWSE_FILECOMP,
        ID_BROWSE_IMGVIEW,
        ID_BROWSE_IMGEDIT,
        ID_BROWSE_ARCHIVER,
        ID_BROWSE_PDFVIEW,
        ID_BROWSE_PDFEDIT,
        ID_BROWSE_VIDEOPLAY,
        ID_BROWSE_AUDIOPLAY,
        ID_BROWSE_XTERM,
        ID_BROWSE_MOUNTCMD,
        ID_BROWSE_UMOUNTCMD,
        ID_COLOR,
        ID_THEME,
        ID_THEME_SAVEAS,
        ID_THEME_RENAME,
        ID_THEME_REMOVE,
        ID_THEME_EXPORT,
        ID_THEME_IMPORT,
        ID_NORMALFONT,
        ID_TEXTFONT,
        ID_ICON_THEME,
        ID_TRASH_BYPASS,
        ID_CONFIRM_TRASH,
        ID_CONFIRM_DEL_EMPTYDIR,
        ID_PKEXEC_CMD,
        ID_SU_CMD,
        ID_SUDO_CMD,
        ID_COPY_SUFFIX_BEFORE,
        ID_COPY_SUFFIX_AFTER,
        ID_WHEELADJUST,
        ID_SCROLLBARSIZE,
        ID_MOUNT_TIMEOUT,
        ID_AUTO_OPEN,
        ID_UIDPI,
        ID_SINGLE_CLICK_FILEOPEN,
        ID_EXEC_TEXT_FILES,
        ID_FILE_TOOLTIPS,
        ID_RELATIVE_RESIZE,
        ID_SHOW_PATHLINK,
        ID_CHANGE_KEYBINDINGS,
        ID_RESTORE_KEYBINDINGS,
        ID_START_HOMEDIR,
        ID_START_CURRENTDIR,
        ID_START_LASTDIR,
        ID_NEWTAB_HOMEDIR,
        ID_NEWTAB_CURRENTDIR,
        ID_NEWTAB_ROOTDIR,
        ID_SHOW_TABBAR,
        ID_RESTORE_TABS,
        ID_DIRPANEL_PLACES_TREE,
        ID_DIRPANEL_PLACES,
        ID_DIRPANEL_TREE,
        ID_DIRPANEL_TREE_STARTUP,
        ID_DIRPANEL_PLACES_STARTUP,
        ID_VIEW_COL,
        ID_LISTCOL,
        ID_LISTCOL_UP,
        ID_LISTCOL_DOWN,
        ID_BIGTHUMB_SIZE,
        ID_MINITHUMB_SIZE,
        ID_ASSOCIATION_LIST,
        ID_ASSOCIATION_APPLY,
        ID_ASSOCIATION_RESET,
        ID_RESTORE_PROGRAM_DEFAULTS,
        ID_LAST,
    };

public:
    PreferencesBox(FXWindow* win, FXColor listbackcolor = FXRGB(255, 255, 255), FXColor listforecolor = FXRGB(0, 0, 0),
                   FXColor highlightcolor = FXRGB(238, 238, 238), FXColor pbarcolor = FXRGB(10, 36, 106),
                   FXColor attentioncolor = FXRGB(255, 0, 0), FXColor scrollbackcolor = FXRGB(237, 236, 235));

    FXuint execute(FXuint);

    long onCmdAccept(FXObject*, FXSelector, void*);
    long onCmdBrowse(FXObject*, FXSelector, void*);
    long onCmdAssociationSelected(FXObject*, FXSelector, void*);
    long onCmdAssociationApply(FXObject*, FXSelector, void*);
    long onCmdAssociationReset(FXObject*, FXSelector, void*);
    long onCmdRestoreProgramDefaults(FXObject*, FXSelector, void*);
    void loadAssociations();
    void updateAssociationSelection();
    long onCmdColor(FXObject*, FXSelector, void*);
    long onUpdColor(FXObject*, FXSelector, void*);
    long onCmdPopupMenu(FXObject*, FXSelector, void*);
    long onCmdTheme(FXObject*, FXSelector, void*);
    long onCmdThemeSaveAs(FXObject*, FXSelector, void*);
    long onCmdThemeRename(FXObject*, FXSelector, void*);
    long onCmdThemeRemove(FXObject*, FXSelector, void*);
    long onCmdThemeExport(FXObject*, FXSelector, void*);
    long onCmdThemeImport(FXObject*, FXSelector, void*);
    long onUpdThemeExport(FXObject*, FXSelector, void*);
    long onCmdIconTheme(FXObject*, FXSelector, void*);
    void fillIconThemeList();
    void selectIconTheme();
    void fillLanguageList();
    long onCmdNormalFont(FXObject*, FXSelector, void*);
    long onCmdTextFont(FXObject*, FXSelector, void*);
    long onUpdTrash(FXObject*, FXSelector, void*);
    long onUpdConfirmDelEmptyDir(FXObject*, FXSelector, void*);
    long onCmdSuMode(FXObject*, FXSelector, void*);
    long onUpdSuMode(FXObject*, FXSelector, void*);
    long onCmdWheelAdjust(FXObject*, FXSelector, void*);
    long onUpdWheelAdjust(FXObject*, FXSelector, void*);
    long onCmdScrollBarSize(FXObject*, FXSelector, void*);
    long onUpdScrollBarSize(FXObject*, FXSelector, void*);
    long onCmdMountTimeout(FXObject*, FXSelector, void*);
    long onUpdMountTimeout(FXObject*, FXSelector, void*);
    long onUpdSingleClickFileopen(FXObject*, FXSelector, void*);
    long onUpdExecTextFiles(FXObject*, FXSelector, void*);
    long onCmdCancel(FXObject*, FXSelector, void*);
    long onCmdChangeKeyBindings(FXObject*, FXSelector, void*);
    long onCmdRestoreKeyBindings(FXObject*, FXSelector, void*);
    long onCmdStartDir(FXObject*, FXSelector, void*);
    long onUpdStartDir(FXObject*, FXSelector, void*);
    long onCmdNewTabDir(FXObject*, FXSelector, void*);
    long onUpdNewTabDir(FXObject*, FXSelector, void*);
    long onCmdCopySuffixPos(FXObject*, FXSelector, void*);
    long onUpdCopySuffixPos(FXObject*, FXSelector, void*);
    long onCmdDirPanelStartup(FXObject*, FXSelector, void*);
    long onUpdDirPanelStartup(FXObject*, FXSelector, void*);
    long onCmdDirPanelMode(FXObject*, FXSelector, void*);
    long onUpdDirPanelMode(FXObject*, FXSelector, void*);
    long onCmdColUp(FXObject*, FXSelector, void*);
    long onCmdColDown(FXObject*, FXSelector, void*);
    long onCmdViewCol(FXObject*, FXSelector, void*);
    long onUpdListColUpDown(FXObject*, FXSelector, void*);
#if defined(linux) && defined(XFE_AUTOMOUNTER)
    long onUpdAutoOpen(FXObject*, FXSelector, void*);
#endif

    // Display tooltip for themes list
    long onQueryTip(FXObject*, FXSelector, void*);
};
#endif
