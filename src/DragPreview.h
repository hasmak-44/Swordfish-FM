#ifndef DRAGPREVIEW_H
#define DRAGPREVIEW_H

#include <fx.h>

class DragPreviewTip : public FXToolTip
{
public:
    explicit DragPreviewTip(FXApp* app) : FXToolTip(app, TOOLTIP_PERMANENT) {}

    virtual long handle(FXObject* sender, FXSelector selector, void* data)
    {
        // A normal tooltip hides itself when the pointer leaves it.
        if (FXSELTYPE(selector) == SEL_UPDATE) return 1;
        return FXToolTip::handle(sender, selector, data);
    }
};

class DragPreview
{
    DragPreviewTip* tip = NULL;

public:
    ~DragPreview() { hide(); }

    void show(FXWindow* owner, const FXString& text, FXFont* font, FXColor background,
              FXColor foreground, FXint width, FXint height, FXint x, FXint y)
    {
        hide();
        tip = new DragPreviewTip(owner->getApp());
        tip->setText(text);
        tip->setFont(font);
        tip->setBackColor(background);
        tip->setTextColor(foreground);
        tip->create();
        tip->position(0, 0, FXMAX(width, tip->getDefaultWidth()),
                      FXMAX(height, tip->getDefaultHeight()));
        move(x, y);
        tip->show();
    }

    void move(FXint x, FXint y)
    {
        if (tip)
        {
            FXint left = x + 16, top = y + 18;
            if (left + tip->getWidth() > tip->getRoot()->getWidth() && x >= tip->getWidth() + 16)
                left = x - tip->getWidth() - 16;
            if (top + tip->getHeight() > tip->getRoot()->getHeight() && y >= tip->getHeight() + 18)
                top = y - tip->getHeight() - 18;
            tip->move(left, top);
        }
    }

    void hide()
    {
        delete tip;
        tip = NULL;
    }
};

#endif
