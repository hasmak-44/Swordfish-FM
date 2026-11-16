#ifndef BACKGROUNDEFFECT_H
#define BACKGROUNDEFFECT_H

#include <fx.h>
#include <vector>

struct BackgroundEffect
{
    FXString fill = "solid";
    std::vector<FXColor> colors;
    std::vector<FXdouble> positions;
    std::vector<FXdouble> midpoints;
    FXString direction = "vertical";
    FXdouble angle = 90;
    FXbool angled = false;
    FXint pattern = 0;
    FXdouble size = 24;
    FXString texture;
    FXint strength = 40;
    FXbool originalColors = false;
    FXbool rows = true;

    static BackgroundEffect parse(const FXString&);
    FXString serialize() const;
    static FXbool validTextureName(const FXString&);
    static FXString textureReference(const FXString&);
    static FXbool textureRelativePath(const FXString&, FXString&);
    static FXbool prepareTextureDestination(const FXString&, const FXString&, FXString&, FXString&);
    static FXString texturePath(const FXString&, const FXString& = FXString::null);
    static FXbool validateTexture(FXApp*, const FXString&, FXString&);
    static FXbool importTexture(FXApp*, const FXString&, const FXString&, FXbool, FXString&);
    static FXIcon* preview(FXComposite*, const FXString&, FXColor, FXint = 48,
                           const FXString& = FXString::null);
};

#endif
