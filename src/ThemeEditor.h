#ifndef THEMEEDITOR_H
#define THEMEEDITOR_H

#include "DialogBox.h"
#include "Theme.h"

class PreferencesBox;
class XFileExplorer;
class ThemeMockup;

class ThemeEditor : public DialogBox
{
    FXDECLARE(ThemeEditor)
    friend class ThemeWorkflowRegression;
public:
    enum StartMode { EDIT_CURRENT, NEW_THEME, DUPLICATE };
    enum
    {
        ID_START_CURRENT = DialogBox::ID_LAST,
        ID_START_NEW,
        ID_START_DUPLICATE,
        ID_ELEMENT,
        ID_PROPERTY,
        ID_EDIT_PROPERTY,
        ID_COLOR,
        ID_INHERIT,
        ID_APPLY,
        ID_DISCARD,
        ID_UNDO,
        ID_REDO,
        ID_RESET_PROPERTY,
        ID_RESET_ELEMENT,
        ID_RESET_THEME,
        ID_ORIGINAL,
        ID_DEFAULTS,
        ID_COMPARE,
        ID_ZOOM,
        ID_OUTLINES,
        ID_PRIVACY,
        ID_STATE,
        ID_FIX_CONTRAST,
        ID_SAVE,
        ID_SAVE_AS,
        ID_CLOSE,
        ID_READABILITY,
        ID_LAST
    };

    ThemeEditor(PreferencesBox*, XFileExplorer*, const vector_Theme&, FXint, StartMode);
    virtual ~ThemeEditor();
    long onStart(FXObject*, FXSelector, void*);
    long onElement(FXObject*, FXSelector, void*);
    long onProperty(FXObject*, FXSelector, void*);
    long onEditProperty(FXObject*, FXSelector, void*);
    long onColor(FXObject*, FXSelector, void*);
    long onInherit(FXObject*, FXSelector, void*);
    long onApply(FXObject*, FXSelector, void*);
    long onDiscard(FXObject*, FXSelector, void*);
    long onUndo(FXObject*, FXSelector, void*);
    long onRedo(FXObject*, FXSelector, void*);
    long onReset(FXObject*, FXSelector, void*);
    long onPreview(FXObject*, FXSelector, void*);
    long onFixContrast(FXObject*, FXSelector, void*);
    long onSave(FXObject*, FXSelector, void*);
    long onCloseEditor(FXObject*, FXSelector, void*);
    long onUpdate(FXObject*, FXSelector, void*);
    long onReadability(FXObject*, FXSelector, void*);
    void previewEffect(const FXString&, FXbool stopEdit = false, const FXString& previous = "");
    void setGradientPreset(const FXString&, const FXString&);
    const Theme& getDraft() const { return draft; }
    const Theme& getApplied() const { return applied; }
    Theme* paletteTheme() { return &draft; }
    void selectElement(const FXString&);

private:
    ThemeEditor() {}
    PreferencesBox* preferences = NULL;
    vector_Theme choices;
    Theme original, applied, draft, saved;
    std::vector<Theme> undoStates, redoStates, pendingSteps;
    FXbool newTheme = false;
    FXint replaceIndex = -1;
    FXString area = "list";
    std::vector<FXString> propertyKeys;
    FXTreeList* elements = NULL;
    FXList* properties = NULL;
    FXLabel* value = NULL;
    FXLabel* readability = NULL;
    FXLabel* instructions = NULL;
    FXCheckButton* inheritance = NULL;
    FXCheckButton* outlines = NULL;
    FXCheckButton* privacy = NULL;
    FXCheckButton* compare = NULL;
    FXListBox* zoom = NULL;
    FXListBox* state = NULL;
    FXListBox* baseTheme = NULL;
    FXButton* edit = NULL;
    FXButton* contrastFix = NULL;
    ThemeMockup* mockup = NULL;
    FXColor suggestedColor = 0;
    void start(FXint, StartMode);
    FXbool settleProperty();
    void refreshProperties();
    void refreshPreview();
    void applyDraft();
    FXString currentProperty() const;
    FXbool save(FXbool);
    FXbool dirty() const;
};

#endif
