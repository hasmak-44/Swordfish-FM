#ifndef THEME_H
#define THEME_H

#include <fx.h>
#include <map>
#include <vector>

#define NUM_COLORS 12
#define NUM_DEFAULT_THEMES 26

struct Theme
{
    FXString name;
    FXString tip;
    FXColor color[NUM_COLORS] = {0};
    FXString effect;
    FXString assetRoot;
    std::map<FXString, FXString> properties;
    FXbool deferred = false;

    Theme() {}
    Theme(const char*, const char*, FXColor = 0, FXColor = 0, FXColor = 0, FXColor = 0,
          FXColor = 0, FXColor = 0, FXColor = 0, FXColor = 0, FXColor = 0, FXColor = 0,
          FXColor = 0, FXColor = 0);

    FXbool operator!=(const Theme&) const;
    FXbool operator==(const Theme& other) const { return !(*this != other); }
    FXString property(const FXString&, const FXString&) const;
    FXbool overrides(const FXString&, const FXString&) const;
    void setProperty(const FXString&, const FXString&, const FXString&);
    void resetElement(const FXString&);
    FXColor colorFor(const FXString&, const FXString&) const;
    FXString fontFor(const FXString&, const FXString&) const;
    FXString encodeProperties() const;
    FXbool decodeProperties(const FXString&, FXString&);
    FXString serialize() const;
    Theme libraryReference() const;
    static Theme current(FXApp*);
    void writeActive(FXSettings&) const;
};

typedef std::vector<Theme> vector_Theme;

FXString themeColorText(FXColor, FXbool alpha = false);
FXbool themeColorParse(const FXString&, FXColor&);
FXColor themeBlend(FXColor, FXColor, double);
double themeContrast(FXColor, FXColor);
double themeLuminance(FXColor);
void themeImproveMenuContrast(FXMenuPane*);
FXString themeWidgetArea(FXWindow*, const FXString& = "main");
void applyThemeWidgets(FXWindow*, const Theme&, std::vector<FXFont*>&);

#endif
