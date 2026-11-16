#ifndef COLORTOOLS_H
#define COLORTOOLS_H

#include <fx.h>
#include <map>
#include <vector>

class ColorSelector;
class ColorPlane;
class ColorWell;
struct Theme;

FXPicker* createScreenPicker(FXComposite*, FXObject*, FXSelector, FXIcon*);

class ColorTools : public FXVerticalFrame
{
    FXDECLARE(ColorTools)
    friend class ThemeWorkflowRegression;
public:
    struct Entry { FXColor color; FXString name; };
    enum
    {
        ID_PLANE = FXVerticalFrame::ID_LAST,
        ID_VALUE,
        ID_ALPHA,
        ID_CODE,
        ID_COPY = ID_CODE + 6,
        ID_BEFORE = ID_COPY + 6,
        ID_RANDOM,
        ID_GENERATED,
        ID_CONTRAST,
        ID_SUGGEST,
        ID_SCOPE,
        ID_PALETTE,
        ID_PALETTE_ADD,
        ID_PALETTE_REMOVE,
        ID_PALETTE_COLOR,
        ID_PALETTE_SAVE,
        ID_PALETTE_IMPORT,
        ID_PALETTE_EXPORT,
        ID_EXTRACT,
        ID_LAST
    };
    ColorTools(FXComposite*, ColorSelector*);
    void setRGBA(FXColor);
    void setOriginalRGBA(FXColor);
    void setTheme(Theme*);
    void setOpaqueOnly(FXbool);
    void rememberColor();
    FXbool validInput() const { return !codeError; }
    static FXbool parseColor(const FXString&, FXColor&, FXString&);
    long onPlane(FXObject*, FXSelector, void*);
    long onCode(FXObject*, FXSelector, void*);
    long onCopy(FXObject*, FXSelector, void*);
    long onChoose(FXObject*, FXSelector, void*);
    long onContrast(FXObject*, FXSelector, void*);
    long onPalette(FXObject*, FXSelector, void*);
    long onPaletteEdit(FXObject*, FXSelector, void*);
    long onPaletteFile(FXObject*, FXSelector, void*);

private:
    ColorTools() {}
    ColorSelector* selector = NULL;
    Theme* theme = NULL;
    FXColor current = FXRGB(0, 0, 0), original = FXRGB(0, 0, 0);
    FXColor contrastBackground = FXRGB(30, 36, 45);
    ColorPlane* plane = NULL;
    FXSlider* brightness = NULL;
    FXSlider* alpha = NULL;
    FXLabel* alphaLabel = NULL;
    ColorWell* before = NULL;
    ColorWell* after = NULL;
    FXTextField* codes[6] = {NULL};
    FXTextField* editing = NULL;
    FXLabel* error = NULL;
    FXLabel* contrast = NULL;
    ColorWell* generated[10] = {NULL};
    FXListBox* scope = NULL;
    FXListBox* palettes = NULL;
    FXTextField* paletteName = NULL;
    FXTextField* colorName = NULL;
    FXColorList* paletteColors = NULL;
    std::map<FXString, FXString> storedPalettes;
    std::vector<Entry> entries;
    FXbool codeError = false;
    void refreshPalettes();
    void showEntries();
    FXString encodeEntries() const;
    FXbool decodeEntries(const FXString&, FXString&);
    void choose(FXColor, FXuint = SEL_COMMAND);
};

#endif
