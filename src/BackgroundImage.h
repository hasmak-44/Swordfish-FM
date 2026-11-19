#ifndef BACKGROUNDIMAGE_H
#define BACKGROUNDIMAGE_H

#include <fx.h>
#include <memory>
#include <vector>

class BackgroundImage
{
public:
    static FXbool validReference(const FXString&);
    static FXString path(const FXString&, const FXString& = FXString::null);
    static std::unique_ptr<FXImage> load(FXApp*, const FXString&, FXString&);
    static FXbool importImage(FXApp*, const FXString&, const FXString&, FXString&, FXbool = false);
    static void composite(FXColor*, FXint, FXint, const FXImage&, const FXString&);
    static FXIcon* preview(FXApp*, const FXString&, FXColor, FXint, FXString&);
};

// Image pixels scaled to a window, with straight alpha that already includes the chosen opacity
class BackgroundOverlay
{
public:
    std::vector<FXColor> pixels;
    FXint width = 0, height = 0;
    void render(const FXImage&, FXint, FXint, const FXString&, FXint,
                FXint = 0, FXint = 0, FXint = -1, FXint = -1);
    void blend(FXColor*, FXint, FXint, FXint, FXint) const;
};

// Widgets that keep some of their children (such as the current tab) solid
class BackgroundImageOwner
{
public:
    virtual FXbool allowsBackgroundImage(const FXWindow*) const { return true; }
    virtual ~BackgroundImageOwner() {}
};

FXint backgroundImageOpacity(const FXString&);

// One image spans the whole main window; each widget blends its own part over its background colour
void setWindowBackground(FXWindow*, const FXString&, const FXString&, FXint, const FXString&,
                         const FXString&, FXWindow*, FXWindow*, FXWindow*);
const BackgroundOverlay* windowBackground(const FXWindow*, FXint&, FXint&, FXuint* = NULL);
void trackBackgroundOrigin(FXWindow*);
// A fully transparent base means the target's own background colour
// True when the window paints the window-fixed image, so scrolled pixels must be repainted, not reused
FXbool showsBackgroundImage(const FXWindow*);
FXbool drawBackgroundImage(FXWindow*, FXDC&, FXint, FXint, FXint, FXint, FXColor base = 0);
FXbool drawBackgroundGaps(FXWindow*, FXDC&, FXint, FXint, FXint, FXint);

class ThemeToolBar : public FXToolBar, public BackgroundImageOwner
{
    FXDECLARE(ThemeToolBar)
protected:
    ThemeToolBar() {}
public:
    ThemeToolBar(FXComposite*, FXComposite*, FXuint);
    ThemeToolBar(FXComposite*, FXuint);
    long onPaint(FXObject*, FXSelector, void*);
};

#endif
