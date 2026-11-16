#ifndef BACKGROUNDIMAGE_H
#define BACKGROUNDIMAGE_H

#include <fx.h>
#include <memory>

class BackgroundImage
{
public:
    static FXbool validReference(const FXString&);
    static FXString path(const FXString&, const FXString& = FXString::null);
    static std::unique_ptr<FXImage> load(FXApp*, const FXString&, FXString&);
    static FXbool importImage(FXApp*, const FXString&, const FXString&, FXString&);
    static void composite(FXColor*, FXint, FXint, const FXImage&, const FXString&);
    static FXIcon* preview(FXApp*, const FXString&, FXColor, FXint, FXString&);
};

class BackgroundImageLayer
{
    FXString reference, mode, root;
    std::unique_ptr<FXImage> rendered;
    FXColor background = 0;
    FXint width = 0, height = 0;
    FXbool failed = false;
public:
    void set(const FXString&, const FXString&, const FXString&);
    FXbool draw(FXWindow*, FXWindow*, FXDC&, FXint, FXint, FXint, FXint);
};

class BackgroundImageOwner
{
public:
    BackgroundImageLayer backgroundImage;
    virtual FXbool allowsBackgroundImage(const FXWindow*) const { return true; }
    virtual ~BackgroundImageOwner() {}
};

FXbool drawBackgroundImage(FXWindow*, FXDC&, FXint, FXint, FXint, FXint);

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
