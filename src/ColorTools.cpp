#include "config.h"
#include "i18n.h"
#include <fx.h>
#include <FXIconSource.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "ColorTools.h"
#include "ColorSelector.h"
#include "Theme.h"
#include "FileDialog.h"
#include "MessageBox.h"
#include "xfeutils.h"
#include "AppLogger.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>

class MagnifiedPixels : public FXCanvas
{
    FXDECLARE(MagnifiedPixels)
    MagnifiedPixels() {}
public:
    FXColor pixels[81] = {0};
    FXint selectedX = 4, selectedY = 4;
    MagnifiedPixels(FXComposite* parent) : FXCanvas(parent, NULL, 0,
        LAYOUT_FILL_X | LAYOUT_FILL_Y | LAYOUT_FIX_WIDTH | LAYOUT_FIX_HEIGHT, 0, 0, 108, 108) {}
    long onPaint(FXObject*, FXSelector, void* pointer)
    {
        FXDCWindow dc(this, static_cast<FXEvent*>(pointer));
        for (FXint y = 0; y < 9; ++y)
            for (FXint x = 0; x < 9; ++x)
            {
                dc.setForeground(pixels[y * 9 + x]);
                dc.fillRectangle(x * 12, y * 12, 12, 12);
            }
        dc.setForeground(DialogColors::accent);
        dc.setLineWidth(2);
        dc.drawRectangle(selectedX * 12 - 1, selectedY * 12 - 1, 14, 14);
        return 1;
    }
};
FXDEFMAP(MagnifiedPixels) MagnifiedPixelsMap[] = {
    FXMAPFUNC(SEL_PAINT, 0, MagnifiedPixels::onPaint)
};
FXIMPLEMENT(MagnifiedPixels, FXCanvas, MagnifiedPixelsMap, ARRAYNUMBER(MagnifiedPixelsMap))

class MagnifyingPicker : public FXPicker
{
    FXDECLARE(MagnifyingPicker)
    MagnifyingPicker() {}
    FXPopup* popup = NULL;
    MagnifiedPixels* pixels = NULL;
    FXLabel* label = NULL;
    FXbool available = true;
    FXbool picking = false;
    FXbool sample(FXint x, FXint y)
    {
        Display* display = static_cast<Display*>(getApp()->getDisplay());
        FXint width = getRoot()->getWidth(), height = getRoot()->getHeight();
        if (width < 9 || height < 9) return false;
        FXint left = FXCLAMP(0, x - 4, width - 9), top = FXCLAMP(0, y - 4, height - 9);
        XImage* image = XGetImage(display, getRoot()->id(), left, top, 9, 9, AllPlanes, ZPixmap);
        if (!image) return false;
        if (!image->red_mask || !image->green_mask || !image->blue_mask)
        {
            XDestroyImage(image);
            pixels->selectedX = x - left;
            pixels->selectedY = y - top;
            return false;
        }
        auto channel = [](unsigned long value, unsigned long mask) {
            while (!(mask & 1)) { value >>= 1; mask >>= 1; }
            return static_cast<FXuint>((value & mask) * 255 / mask);
        };
        for (FXint row = 0; row < 9; ++row)
            for (FXint column = 0; column < 9; ++column)
            {
                unsigned long pixel = XGetPixel(image, column, row);
                pixels->pixels[row * 9 + column] = FXRGB(channel(pixel, image->red_mask),
                    channel(pixel, image->green_mask), channel(pixel, image->blue_mask));
            }
        XDestroyImage(image);
        label->setText(themeColorText(pixels->pixels[(y - top) * 9 + x - left]));
        pixels->update();
        popup->position(FXMAX(0, x + 144 <= width ? x + 24 : x - 144),
                         FXMAX(0, y + 168 <= height ? y + 24 : y - 168), 120, 144);
        popup->show();
        popup->raise();
        return true;
    }
public:
    MagnifyingPicker(FXComposite* parent, FXObject* receiver, FXSelector selector, FXIcon* icon) :
        FXPicker(parent, _("\tPick colour with magnifier"), icon, receiver, selector,
            FRAME_GROOVE | LAYOUT_CENTER_X | LAYOUT_FIX_WIDTH | LAYOUT_FIX_HEIGHT, 0, 0, 56, 32, 0, 0, 0, 0)
    {
        popup = new FXPopup(this, FRAME_LINE);
        FXVerticalFrame* contents = new FXVerticalFrame(popup, LAYOUT_FILL_X | LAYOUT_FILL_Y);
        pixels = new MagnifiedPixels(contents);
        label = new FXLabel(contents, "", NULL, JUSTIFY_CENTER_X | LAYOUT_FILL_X);
    }
    ~MagnifyingPicker() { delete popup; }
    long onPress(FXObject* sender, FXSelector selector, void* pointer)
    {
        if (!FXSystem::getEnvironment("WAYLAND_DISPLAY").empty())
        {
            MessageBox::warning(getShell(), BOX_OK, _("Screen colour picker"),
                _("Global screen picking is unavailable in this Wayland session.\n"
                  "Manual colour selection, palettes and image extraction remain available."));
            return 1;
        }
        popup->create();
        xf_dialogcolors(popup);
        picking = true;
        available = true;
        return FXPicker::onLeftBtnPress(sender, selector, pointer);
    }
    long onMove(FXObject* sender, FXSelector selector, void* pointer)
    {
        if (picking)
        {
            FXEvent* event = static_cast<FXEvent*>(pointer);
            available = sample(FXCLAMP(0, event->root_x, getRoot()->getWidth() - 1),
                                 FXCLAMP(0, event->root_y, getRoot()->getHeight() - 1));
        }
        return FXPicker::onMotion(sender, selector, pointer);
    }
    long onRelease(FXObject* sender, FXSelector selector, void* pointer)
    {
        popup->hide();
        picking = false;
        FXObject* receiver = getTarget();
        if (!available) setTarget(NULL);
        long result = FXPicker::onLeftBtnRelease(sender, selector, pointer);
        setTarget(receiver);
        if (!available)
        {
            appLog(APP_LOG_ERRORS, "screen-colour-pick-unavailable", "X11 root capture failed");
            MessageBox::warning(getShell(), BOX_OK, _("Screen colour picker"),
                _("The screen could not be sampled. Manual colour selection is still available."));
        }
        return result;
    }
    long onUngrab(FXObject* sender, FXSelector selector, void* pointer)
    {
        picking = false;
        popup->hide();
        return FXButton::onUngrabbed(sender, selector, pointer);
    }
};
FXDEFMAP(MagnifyingPicker) MagnifyingPickerMap[] = {
    FXMAPFUNC(SEL_LEFTBUTTONPRESS, 0, MagnifyingPicker::onPress),
    FXMAPFUNC(SEL_LEFTBUTTONRELEASE, 0, MagnifyingPicker::onRelease),
    FXMAPFUNC(SEL_MOTION, 0, MagnifyingPicker::onMove),
    FXMAPFUNC(SEL_UNGRABBED, 0, MagnifyingPicker::onUngrab)
};
FXIMPLEMENT(MagnifyingPicker, FXPicker, MagnifyingPickerMap, ARRAYNUMBER(MagnifyingPickerMap))

FXPicker* createScreenPicker(FXComposite* parent, FXObject* target, FXSelector selector, FXIcon* icon)
{
    return new MagnifyingPicker(parent, target, selector, icon);
}

class ColorPlane : public FXCanvas
{
    FXDECLARE(ColorPlane)
    ColorPlane() {}
    FXfloat hue = 0, saturation = 0, value = 1;
    FXColor color = FXRGB(255, 255, 255);
    FXImage* image = NULL;
    FXfloat renderedValue = -1;
    FXint renderedWidth = 0, renderedHeight = 0;
    FXbool pressed = false;
public:
    ColorPlane(FXComposite* parent, FXObject* target, FXSelector selector) :
        FXCanvas(parent, target, selector, FRAME_SUNKEN | FRAME_THICK | LAYOUT_FILL_X | LAYOUT_FILL_Y,
                   0, 0, 230, 160) {}
    ~ColorPlane() { delete image; }
    void setRGBA(FXColor rgba)
    {
        color = rgba;
        fxrgb_to_hsv(hue, saturation, value, FXREDVAL(rgba) / 255.0f,
                      FXGREENVAL(rgba) / 255.0f, FXBLUEVAL(rgba) / 255.0f);
        update();
    }
    long onPaint(FXObject*, FXSelector, void* pointer)
    {
        FXint width = FXMAX(1, getWidth()), height = FXMAX(1, getHeight());
        if (!image || renderedValue != value || width != renderedWidth || height != renderedHeight)
        {
            delete image;
            image = new FXImage(getApp(), NULL, IMAGE_OWNED | IMAGE_KEEP, width, height);
            image->create();
            image->restore();
            FXColor* pixels = image->getData();
            for (FXint y = 0; y < height; ++y)
                for (FXint x = 0; x < width; ++x)
                {
                    FXfloat red, green, blue;
                    fxhsv_to_rgb(red, green, blue, x * 360.0f / FXMAX(1, width - 1),
                                  1 - y / static_cast<FXfloat>(FXMAX(1, height - 1)), value);
                    pixels[y * width + x] = FXRGB(std::lround(red * 255), std::lround(green * 255), std::lround(blue * 255));
                }
            image->render();
            renderedValue = value;
            renderedWidth = width;
            renderedHeight = height;
        }
        FXDCWindow dc(this, static_cast<FXEvent*>(pointer));
        dc.drawImage(image, 0, 0);
        FXint x = static_cast<FXint>(hue * (width - 1) / 360), y = static_cast<FXint>((1 - saturation) * (height - 1));
        dc.setForeground(FXRGB(0, 0, 0));
        dc.drawArc(x - 5, y - 5, 10, 10, 0, 360 * 64);
        dc.setForeground(FXRGB(255, 255, 255));
        dc.drawArc(x - 4, y - 4, 8, 8, 0, 360 * 64);
        return 1;
    }
    long onPointer(FXObject*, FXSelector selector, void* pointer)
    {
        FXEvent* event = static_cast<FXEvent*>(pointer);
        if (FXSELTYPE(selector) == SEL_LEFTBUTTONPRESS) { pressed = true; grab(); }
        if (!pressed) return 1;
        hue = FXCLAMP(0, event->win_x, getWidth() - 1) * 359.999f / FXMAX(1, getWidth() - 1);
        saturation = 1 - FXCLAMP(0, event->win_y, getHeight() - 1) / static_cast<FXfloat>(FXMAX(1, getHeight() - 1));
        FXfloat red, green, blue;
        fxhsv_to_rgb(red, green, blue, hue, saturation, value);
        FXColor selected = FXRGBA(std::lround(red * 255), std::lround(green * 255), std::lround(blue * 255), FXALPHAVAL(color));
        if (FXSELTYPE(selector) == SEL_LEFTBUTTONRELEASE) { pressed = false; ungrab(); }
        if (target) target->handle(this, FXSEL(pressed ? SEL_CHANGED : SEL_COMMAND, message),
                                    reinterpret_cast<void*>(static_cast<FXuval>(selected)));
        update();
        return 1;
    }
};
FXDEFMAP(ColorPlane) ColorPlaneMap[] = {
    FXMAPFUNC(SEL_PAINT, 0, ColorPlane::onPaint),
    FXMAPFUNC(SEL_LEFTBUTTONPRESS, 0, ColorPlane::onPointer),
    FXMAPFUNC(SEL_LEFTBUTTONRELEASE, 0, ColorPlane::onPointer),
    FXMAPFUNC(SEL_MOTION, 0, ColorPlane::onPointer)
};
FXIMPLEMENT(ColorPlane, FXCanvas, ColorPlaneMap, ARRAYNUMBER(ColorPlaneMap))

FXDEFMAP(ColorTools) ColorToolsMap[] = {
    FXMAPFUNC(SEL_COMMAND, ColorTools::ID_PLANE, ColorTools::onPlane),
    FXMAPFUNC(SEL_CHANGED, ColorTools::ID_PLANE, ColorTools::onPlane),
    FXMAPFUNC(SEL_COMMAND, ColorTools::ID_VALUE, ColorTools::onPlane),
    FXMAPFUNC(SEL_CHANGED, ColorTools::ID_VALUE, ColorTools::onPlane),
    FXMAPFUNC(SEL_COMMAND, ColorTools::ID_ALPHA, ColorTools::onPlane),
    FXMAPFUNC(SEL_CHANGED, ColorTools::ID_ALPHA, ColorTools::onPlane),
    FXMAPFUNCS(SEL_CHANGED, ColorTools::ID_CODE, ColorTools::ID_CODE + 5, ColorTools::onCode),
    FXMAPFUNCS(SEL_COMMAND, ColorTools::ID_CODE, ColorTools::ID_CODE + 5, ColorTools::onCode),
    FXMAPFUNCS(SEL_COMMAND, ColorTools::ID_COPY, ColorTools::ID_COPY + 5, ColorTools::onCopy),
    FXMAPFUNC(SEL_COMMAND, ColorTools::ID_BEFORE, ColorTools::onChoose),
    FXMAPFUNC(SEL_COMMAND, ColorTools::ID_RANDOM, ColorTools::onChoose),
    FXMAPFUNC(SEL_COMMAND, ColorTools::ID_GENERATED, ColorTools::onChoose),
    FXMAPFUNC(SEL_COMMAND, ColorTools::ID_CONTRAST, ColorTools::onContrast),
    FXMAPFUNC(SEL_CHANGED, ColorTools::ID_CONTRAST, ColorTools::onContrast),
    FXMAPFUNC(SEL_COMMAND, ColorTools::ID_SUGGEST, ColorTools::onChoose),
    FXMAPFUNC(SEL_COMMAND, ColorTools::ID_SCOPE, ColorTools::onPalette),
    FXMAPFUNC(SEL_COMMAND, ColorTools::ID_PALETTE, ColorTools::onPalette),
    FXMAPFUNCS(SEL_COMMAND, ColorTools::ID_PALETTE_ADD, ColorTools::ID_PALETTE_SAVE, ColorTools::onPaletteEdit),
    FXMAPFUNCS(SEL_COMMAND, ColorTools::ID_PALETTE_IMPORT, ColorTools::ID_EXTRACT, ColorTools::onPaletteFile)
};
FXIMPLEMENT(ColorTools, FXVerticalFrame, ColorToolsMap, ARRAYNUMBER(ColorToolsMap))

FXbool ColorTools::parseColor(const FXString& input, FXColor& color, FXString& error)
{
    FXString text = FXString(input).trim().lower();
    error.clear();
    if (themeColorParse(text, color)) return true;
    if ((text.length() == 4 || text.length() == 5) && text[0] == '#')
    {
        FXString expanded = "#";
        for (FXint i = 1; i < text.length(); ++i) { expanded += text[i]; expanded += text[i]; }
        if (themeColorParse(expanded, color)) return true;
    }
    if (text == "transparent") { color = FXRGBA(0, 0, 0, 0); return true; }
    for (FXuint i = 0; i < fxnumcolornames; ++i)
        if (FXString(fxcolornames[i].name).lower() == text) { color = fxcolornames[i].color; return true; }
    FXint open = text.find('(');
    FXString model = text.left(open);
    FXbool rgb = model == "rgb" || model == "rgba", hsv = model == "hsv" || model == "hsb", cmyk = model == "cmyk";
    if (open > 0 && text[text.length() - 1] == ')' && (rgb || hsv || cmyk))
    {
        FXString body = text.mid(open + 1, text.length() - open - 2);
        FXint count = model == "rgba" || cmyk ? 4 : 3;
        FXdouble values[4] = {0, 0, 0, 1};
        FXbool valid = body.contains(',') == count - 1;
        for (FXint i = 0; i < count && valid; ++i)
        {
            FXString part = body.section(',', i).trim();
            FXbool percent = !part.empty() && part[part.length() - 1] == '%';
            if (percent) part.trunc(part.length() - 1);
            char* end = NULL;
            FXdouble number = strtod(part.text(), &end);
            valid = !part.empty() && end && !*end && std::isfinite(number) && number >= 0;
            FXdouble maximum = rgb ? (i == 3 ? 1 : 255) : (hsv && i == 0 ? 360 : 100);
            if (percent && hsv && i == 0) valid = false;
            if (percent) number *= maximum / 100;
            if (number > maximum) valid = false;
            values[i] = number / maximum;
        }
        if (valid)
        {
            FXfloat red, green, blue;
            if (hsv)
                fxhsv_to_rgb(red, green, blue, std::fmod(values[0] * 360, 360), values[1], values[2]);
            else if (cmyk)
            {
                red = (1 - values[0]) * (1 - values[3]);
                green = (1 - values[1]) * (1 - values[3]);
                blue = (1 - values[2]) * (1 - values[3]);
            }
            else { red = values[0]; green = values[1]; blue = values[2]; }
            color = FXRGBA(std::lround(red * 255), std::lround(green * 255), std::lround(blue * 255),
                            std::lround((model == "rgba" ? values[3] : 1) * 255));
            return true;
        }
    }
    error = _("Use HEX / HexRGBA, rgb(...), rgba(...), hsv(...), hsb(...) or cmyk(...).\n"
              "RGB channels: 0-255; alpha: 0-1; hue: 0-360; other channels: 0-100%.");
    return false;
}

ColorTools::ColorTools(FXComposite* parent, ColorSelector* owner) :
    FXVerticalFrame(parent, LAYOUT_FILL_X | LAYOUT_FILL_Y), selector(owner)
{
    FXTabBook* pages = new FXTabBook(this, NULL, 0, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    new DialogTabItem(pages, _("Picker"));
    FXVerticalFrame* picker = new FXVerticalFrame(pages, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    FXHorizontalFrame* body = new FXHorizontalFrame(picker, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    FXVerticalFrame* left = new FXVerticalFrame(body, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    new FXLabel(left, _("Hue / saturation"), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    FXHorizontalFrame* planeRow = new FXHorizontalFrame(left, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    plane = new ColorPlane(planeRow, this, ID_PLANE);
    brightness = new FXSlider(planeRow, this, ID_VALUE,
                               SLIDER_VERTICAL | SLIDER_INSIDE_BAR | LAYOUT_FILL_Y | LAYOUT_FIX_WIDTH, 0, 0, 22);
    brightness->setRange(0, 1000);
    brightness->setTipText(_("Brightness"));
    FXHorizontalFrame* wells = new FXHorizontalFrame(left, LAYOUT_FILL_X);
    new FXLabel(wells, _("Before"));
    before = new ColorWell(wells, original, this, ID_BEFORE, _COLORWELL_SOURCEONLY |
                            LAYOUT_FIX_WIDTH | LAYOUT_FIX_HEIGHT, 0, 0, 55, 30);
    new FXLabel(wells, _("After"));
    after = new ColorWell(wells, current, NULL, 0, _COLORWELL_SOURCEONLY |
                           LAYOUT_FIX_WIDTH | LAYOUT_FIX_HEIGHT, 0, 0, 55, 30);
    alphaLabel = new FXLabel(left, _("Alpha / opacity"));
    alpha = new FXSlider(left, this, ID_ALPHA, SLIDER_HORIZONTAL | SLIDER_INSIDE_BAR | LAYOUT_FILL_X);
    alpha->setRange(0, 255);
    FXMatrix* formats = new FXMatrix(body, 3, MATRIX_BY_COLUMNS | LAYOUT_FILL_Y);
    const char* names[] = {"HEX", "HexRGBA", "RGB", "RGBA", "HSV / HSB", "CMYK"};
    for (FXint i = 0; i < 6; ++i)
    {
        new FXLabel(formats, names[i], NULL, JUSTIFY_LEFT);
        codes[i] = new FXTextField(formats, 28, this, ID_CODE + i, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        codes[i]->setTipText(_("Paste any supported colour format; it is detected automatically."));
        new FXButton(formats, _("Copy"), NULL, this, ID_COPY + i, BUTTON_NORMAL);
    }
    error = new FXLabel(picker, "", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    new FXButton(picker, _("Random colour"), NULL, this, ID_RANDOM, BUTTON_NORMAL | LAYOUT_LEFT);
    new DialogTabItem(pages, _("Palettes"));
    FXVerticalFrame* palettePage = new FXVerticalFrame(pages, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    FXHorizontalFrame* paletteTop = new FXHorizontalFrame(palettePage, LAYOUT_FILL_X);
    scope = new FXListBox(paletteTop, this, ID_SCOPE, FRAME_SUNKEN | FRAME_THICK);
    scope->appendItem(_("Shared palettes"));
    scope->setNumVisible(2);
    palettes = new FXListBox(paletteTop, this, ID_PALETTE, FRAME_SUNKEN | FRAME_THICK | LAYOUT_FILL_X);
    palettes->setNumVisible(8);
    paletteName = new FXTextField(paletteTop, 15, NULL, 0, TEXTFIELD_NORMAL);
    paletteName->setTipText(_("Palette name"));
    paletteColors = new FXColorList(palettePage, this, ID_PALETTE_COLOR,
                                      LIST_BROWSESELECT | LAYOUT_FILL_X | LAYOUT_FILL_Y | FRAME_SUNKEN | FRAME_THICK);
    paletteColors->setNumVisible(5);
    FXHorizontalFrame* namesRow = new FXHorizontalFrame(palettePage, LAYOUT_FILL_X);
    colorName = new FXTextField(namesRow, 20, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
    colorName->setTipText(_("Optional name for the current colour"));
    new FXButton(namesRow, _("Add current colour"), NULL, this, ID_PALETTE_ADD, BUTTON_NORMAL);
    new FXButton(namesRow, _("Remove"), NULL, this, ID_PALETTE_REMOVE, BUTTON_NORMAL);
    FXHorizontalFrame* files = new FXHorizontalFrame(palettePage, LAYOUT_FILL_X);
    new FXButton(files, _("Save palette"), NULL, this, ID_PALETTE_SAVE, BUTTON_NORMAL);
    new FXButton(files, _("Import..."), NULL, this, ID_PALETTE_IMPORT, BUTTON_NORMAL);
    new FXButton(files, _("Export..."), NULL, this, ID_PALETTE_EXPORT, BUTTON_NORMAL);
    new FXButton(files, _("Extract from image..."), NULL, this, ID_EXTRACT, BUTTON_NORMAL);
    new FXLabel(palettePage, _("Shared Save writes only the palette. Theme palettes stay in the draft until the theme is saved."),
                  NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    new DialogTabItem(pages, _("Tints and contrast"));
    FXVerticalFrame* tools = new FXVerticalFrame(pages, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    const char* kinds[] = {_("Shades"), _("Tints")};
    for (FXint row = 0; row < 2; ++row)
    {
        FXHorizontalFrame* samples = new FXHorizontalFrame(tools, LAYOUT_FILL_X);
        new FXLabel(samples, kinds[row]);
        for (FXint i = 0; i < 5; ++i)
            generated[row * 5 + i] = new ColorWell(samples, current, this, ID_GENERATED,
                _COLORWELL_SOURCEONLY | LAYOUT_FILL_X | LAYOUT_FIX_HEIGHT, 0, 0, 45, 40);
    }
    FXHorizontalFrame* comparison = new FXHorizontalFrame(tools, LAYOUT_FILL_X);
    new FXLabel(comparison, _("Contrast background:"));
    new ColorWell(comparison, contrastBackground, this, ID_CONTRAST,
                    COLORWELL_NORMAL | LAYOUT_FIX_WIDTH | LAYOUT_FIX_HEIGHT, 0, 0, 60, 35);
    contrast = new FXLabel(tools, "", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    new FXButton(tools, _("Use suggested black / white text"), NULL, this, ID_SUGGEST, BUTTON_NORMAL | LAYOUT_LEFT);
    new FXLabel(tools, _("The Theme Editor's Background tool provides named and recent gradients,\n"
                         "separate stop undo, linear / radial modes, angle, patterns and PNG textures."),
                  NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    FXHorizontalFrame* recent = new FXHorizontalFrame(this, LAYOUT_FILL_X);
    new FXLabel(recent, _("Recent:"));
    for (FXint i = 0; i < 8; ++i)
    {
        FXString key = "recent" + FXStringVal(i);
        if (!getApp()->reg().existingEntry("COLOR_PICKER", key.text())) continue;
        new ColorWell(recent, getApp()->reg().readColorEntry("COLOR_PICKER", key.text()), this, ID_GENERATED,
                       _COLORWELL_SOURCEONLY | LAYOUT_FIX_WIDTH | LAYOUT_FIX_HEIGHT, 0, 0, 28, 22);
    }
    refreshPalettes();
    setRGBA(current);
}

void ColorTools::choose(FXColor color, FXuint type)
{
    selector->selectColor(color, type);
}

void ColorTools::setRGBA(FXColor color)
{
    current = color;
    if (plane) plane->setRGBA(color);
    if (!after) return;
    after->setRGBA(color);
    FXfloat hue, saturation, value;
    FXfloat red = FXREDVAL(color) / 255.0f, green = FXGREENVAL(color) / 255.0f, blue = FXBLUEVAL(color) / 255.0f;
    fxrgb_to_hsv(hue, saturation, value, red, green, blue);
    brightness->setValue(std::lround(value * 1000));
    alpha->setValue(FXALPHAVAL(color));
    FXfloat black = 1 - FXMAX(red, FXMAX(green, blue));
    FXfloat cyan = black >= 1 ? 0 : (1 - red - black) / (1 - black);
    FXfloat magenta = black >= 1 ? 0 : (1 - green - black) / (1 - black);
    FXfloat yellow = black >= 1 ? 0 : (1 - blue - black) / (1 - black);
    FXString values[] = {themeColorText(color), themeColorText(color, true),
        FXString().format("rgb(%d, %d, %d)", FXREDVAL(color), FXGREENVAL(color), FXBLUEVAL(color)),
        FXString().format("rgba(%d, %d, %d, %.4f)", FXREDVAL(color), FXGREENVAL(color), FXBLUEVAL(color), FXALPHAVAL(color) / 255.0),
        FXString().format("hsv(%.1f, %.1f%%, %.1f%%)", hue, saturation * 100, value * 100),
        FXString().format("cmyk(%.1f%%, %.1f%%, %.1f%%, %.1f%%)", cyan * 100, magenta * 100, yellow * 100, black * 100)};
    for (FXint i = 0; i < 6; ++i) if (codes[i] != editing) codes[i]->setText(values[i]);
    if (!editing) { codeError = false; error->setText(""); }
    for (FXint i = 0; i < 10; ++i)
    {
        FXColor mixed = themeBlend(color, i < 5 ? FXRGB(0, 0, 0) : FXRGB(255, 255, 255), (i % 5 + 1) / 6.0);
        generated[i]->setRGBA(FXRGBA(FXREDVAL(mixed), FXGREENVAL(mixed), FXBLUEVAL(mixed), FXALPHAVAL(color)));
    }
    double ratio = themeContrast(current, contrastBackground);
    contrast->setText(FXString().format(_("Contrast %.2f:1\nAA normal: %s   AA large: %s   AAA normal: %s"),
        ratio, ratio >= 4.5 ? _("Pass") : _("Fail"), ratio >= 3 ? _("Pass") : _("Fail"), ratio >= 7 ? _("Pass") : _("Fail")));
}

void ColorTools::setOriginalRGBA(FXColor color)
{
    original = color;
    before->setRGBA(color);
}

void ColorTools::setOpaqueOnly(FXbool opaque)
{
    if (opaque) { alpha->hide(); alphaLabel->hide(); }
    else { alpha->show(); alphaLabel->show(); }
}

void ColorTools::setTheme(Theme* selected)
{
    theme = selected;
    scope->clearItems();
    scope->appendItem(_("Shared palettes"));
    if (theme) scope->appendItem(_("Theme palettes"));
    refreshPalettes();
}

void ColorTools::rememberColor()
{
    std::vector<FXColor> colors = {current};
    for (FXint i = 0; i < 8; ++i)
    {
        FXString key = "recent" + FXStringVal(i);
        if (!getApp()->reg().existingEntry("COLOR_PICKER", key.text())) continue;
        FXColor previous = getApp()->reg().readColorEntry("COLOR_PICKER", key.text());
        if (std::find(colors.begin(), colors.end(), previous) == colors.end()) colors.push_back(previous);
    }
    for (size_t i = 0; i < colors.size() && i < 8; ++i)
        getApp()->reg().writeColorEntry("COLOR_PICKER", ("recent" + FXStringVal(static_cast<FXint>(i))).text(), colors[i]);
}

long ColorTools::onPlane(FXObject*, FXSelector selectorId, void* pointer)
{
    FXint id = FXSELID(selectorId);
    FXColor color = current;
    if (id == ID_PLANE) color = static_cast<FXColor>(reinterpret_cast<FXuval>(pointer));
    else if (id == ID_ALPHA) color = FXRGBA(FXREDVAL(current), FXGREENVAL(current), FXBLUEVAL(current), alpha->getValue());
    else
    {
        FXfloat hue, saturation, value, red, green, blue;
        fxrgb_to_hsv(hue, saturation, value, FXREDVAL(current) / 255.0f, FXGREENVAL(current) / 255.0f, FXBLUEVAL(current) / 255.0f);
        fxhsv_to_rgb(red, green, blue, hue, saturation, brightness->getValue() / 1000.0f);
        color = FXRGBA(std::lround(red * 255), std::lround(green * 255), std::lround(blue * 255), FXALPHAVAL(current));
    }
    choose(color, FXSELTYPE(selectorId));
    return 1;
}

long ColorTools::onCode(FXObject* sender, FXSelector, void*)
{
    editing = static_cast<FXTextField*>(sender);
    FXColor color;
    FXString message;
    codeError = !parseColor(editing->getText(), color, message);
    error->setText(codeError ? message : FXString::null);
    if (!codeError) choose(color, SEL_CHANGED);
    editing = NULL;
    return 1;
}

long ColorTools::onCopy(FXObject*, FXSelector selector, void*)
{
    FXTextField* field = codes[FXSELID(selector) - ID_COPY];
    field->setSelection(0, field->getText().length());
    field->handle(this, FXSEL(SEL_COMMAND, FXTextField::ID_COPY_SEL), NULL);
    return 1;
}

long ColorTools::onChoose(FXObject*, FXSelector selectorId, void* pointer)
{
    FXint id = FXSELID(selectorId);
    if (id == ID_BEFORE) choose(original);
    else if (id == ID_GENERATED) choose(static_cast<FXColor>(reinterpret_cast<FXuval>(pointer)));
    else if (id == ID_SUGGEST)
        choose(themeContrast(FXRGB(0, 0, 0), contrastBackground) >= themeContrast(FXRGB(255, 255, 255), contrastBackground) ?
                 FXRGB(0, 0, 0) : FXRGB(255, 255, 255));
    else
    {
        FXuint seed = static_cast<FXuint>(FXThread::time());
        FXuint red = fxrandom(seed) & 255, green = fxrandom(seed) & 255, blue = fxrandom(seed) & 255;
        choose(FXRGBA(red, green, blue, FXALPHAVAL(current)));
    }
    return 1;
}

long ColorTools::onContrast(FXObject*, FXSelector, void* pointer)
{
    contrastBackground = static_cast<FXColor>(reinterpret_cast<FXuval>(pointer));
    setRGBA(current);
    return 1;
}

FXString ColorTools::encodeEntries() const
{
    FXString text;
    for (const Entry& entry : entries)
    {
        if (!text.empty()) text += '|';
        text += themeColorText(entry.color, true) + " " + entry.name;
    }
    return text;
}

FXbool ColorTools::decodeEntries(const FXString& text, FXString& error)
{
    std::vector<Entry> parsed;
    for (FXint i = 0; !text.empty() && i <= text.contains('|'); ++i)
    {
        FXString token = text.section('|', i);
        Entry entry;
        if (!themeColorParse(token.before(' '), entry.color))
        {
            error = _("The palette contains an invalid colour.");
            return false;
        }
        entry.name = token.after(' ');
        parsed.push_back(entry);
    }
    entries.swap(parsed);
    return true;
}

void ColorTools::showEntries()
{
    paletteColors->clearItems();
    for (const Entry& entry : entries)
        paletteColors->appendItem((entry.name.empty() ? themeColorText(entry.color, true) : entry.name), entry.color);
}

void ColorTools::refreshPalettes()
{
    storedPalettes.clear();
    if (scope->getCurrentItem() == 1 && theme)
    {
        for (const auto& entry : theme->properties)
            if (entry.first.left(8) == "palette.") storedPalettes[entry.first.mid(8, entry.first.length() - 8)] = entry.second;
    }
    else
    {
        FXint count = getApp()->reg().readIntEntry("COLOR_PALETTES", "count", 0);
        if (count < 0 || count > 128)
        {
            appLog(APP_LOG_ERRORS, "colour-palettes-invalid", "Invalid shared palette count");
            error->setText(_("The stored palette count is invalid; shared palettes could not be loaded."));
            palettes->clearItems();
            return;
        }
        for (FXint i = 0; i < count; ++i)
        {
            FXString key = FXStringVal(i);
            FXString name = getApp()->reg().readStringEntry("COLOR_PALETTES", (key + ".name").text(), "");
            if (!name.empty()) storedPalettes[name] = getApp()->reg().readStringEntry("COLOR_PALETTES", (key + ".colors").text(), "");
        }
    }
    palettes->clearItems();
    for (const auto& entry : storedPalettes) palettes->appendItem(entry.first);
    if (storedPalettes.empty()) { paletteName->setText(_("My palette")); entries.clear(); showEntries(); }
    else { palettes->setCurrentItem(0); onPalette(NULL, FXSEL(SEL_COMMAND, ID_PALETTE), NULL); }
}

long ColorTools::onPalette(FXObject*, FXSelector selectorId, void*)
{
    if (FXSELID(selectorId) == ID_SCOPE) refreshPalettes();
    else if (palettes->getCurrentItem() >= 0)
    {
        FXString name = palettes->getItemText(palettes->getCurrentItem()), error;
        auto found = storedPalettes.find(name);
        if (found != storedPalettes.end())
        {
            if (!decodeEntries(found->second, error)) MessageBox::error(getShell(), BOX_OK, _("Palette"), "%s", error.text());
            else { paletteName->setText(name); showEntries(); }
        }
    }
    return 1;
}

long ColorTools::onPaletteEdit(FXObject*, FXSelector selectorId, void* pointer)
{
    FXint id = FXSELID(selectorId);
    if (id == ID_PALETTE_COLOR)
    {
        FXint index = static_cast<FXint>(reinterpret_cast<FXival>(pointer));
        if (index >= 0 && index < static_cast<FXint>(entries.size())) choose(entries[index].color);
    }
    else if (id == ID_PALETTE_ADD)
    {
        FXString name = colorName->getText().trim();
        if (name.find_first_of("|\r\n") >= 0)
            MessageBox::error(getShell(), BOX_OK, _("Colour name"), _("Colour names cannot contain | or newlines."));
        else { entries.push_back(Entry{current, name}); showEntries(); }
    }
    else if (id == ID_PALETTE_REMOVE)
    {
        FXint index = paletteColors->getCurrentItem();
        if (index >= 0 && index < static_cast<FXint>(entries.size())) { entries.erase(entries.begin() + index); showEntries(); }
    }
    else
    {
        FXString name = paletteName->getText().trim();
        if (name.empty() || name.find_first_of("|;=\r\n") >= 0)
        {
            MessageBox::error(getShell(), BOX_OK, _("Palette name"), _("Enter a palette name without |, semicolons, = or newlines."));
            return 1;
        }
        if (storedPalettes.count(name) && MessageBox::question(getShell(), BOX_YES_NO, _("Replace palette"),
            _("Replace the saved palette \"%s\"?"), name.text()) != BOX_CLICKED_YES) return 1;
        std::map<FXString, FXString> previousPalettes = storedPalettes;
        storedPalettes[name] = encodeEntries();
        if (scope->getCurrentItem() == 1 && theme) theme->properties["palette." + name] = encodeEntries();
        else
        {
            if (storedPalettes.size() > 128)
            {
                storedPalettes = previousPalettes;
                MessageBox::error(getShell(), BOX_OK, _("Palette"), _("At most 128 shared palettes can be saved."));
                return 1;
            }
            struct Setting { FXString key, value; FXbool existed; };
            std::vector<Setting> previousSettings;
            auto remember = [&](const FXString& key)
            {
                previousSettings.push_back(Setting{key,
                    getApp()->reg().readStringEntry("COLOR_PALETTES", key.text(), ""),
                    getApp()->reg().existingEntry("COLOR_PALETTES", key.text())});
            };
            remember("count");
            FXint index = 0;
            for (const auto& entry : storedPalettes)
            {
                FXString key = FXStringVal(index++);
                remember(key + ".name");
                remember(key + ".colors");
                getApp()->reg().writeStringEntry("COLOR_PALETTES", (key + ".name").text(), entry.first.text());
                getApp()->reg().writeStringEntry("COLOR_PALETTES", (key + ".colors").text(), entry.second.text());
            }
            getApp()->reg().writeIntEntry("COLOR_PALETTES", "count", index);
            if (!getApp()->reg().write())
            {
                for (const Setting& setting : previousSettings)
                    if (setting.existed)
                        getApp()->reg().writeStringEntry("COLOR_PALETTES", setting.key.text(), setting.value.text());
                    else
                        getApp()->reg().deleteEntry("COLOR_PALETTES", setting.key.text());
                storedPalettes = previousPalettes;
                MessageBox::error(getShell(), BOX_OK, _("Palette"), _("The palette settings could not be written."));
                return 1;
            }
        }
        palettes->clearItems();
        for (const auto& entry : storedPalettes) palettes->appendItem(entry.first);
        palettes->setCurrentItem(palettes->findItem(name));
    }
    return 1;
}

long ColorTools::onPaletteFile(FXObject*, FXSelector selectorId, void*)
{
    FXint id = FXSELID(selectorId);
    FileDialog file(getShell(), id == ID_PALETTE_EXPORT ? _("Export palette") :
                                  id == ID_EXTRACT ? _("Extract palette from image") : _("Import palette"));
    const char* palettePatterns[] = {_("Swordfish / GIMP palettes"), "*.swordfish-palette,*.gpl", _("All files"), "*", NULL};
    const char* imagePatterns[] = {_("Images"), "*.png,*.jpg,*.jpeg,*.gif,*.bmp,*.tif,*.tiff", _("All files"), "*", NULL};
    file.setPatternList(id == ID_EXTRACT ? imagePatterns : palettePatterns);
    file.setSelectMode(id == ID_PALETTE_EXPORT ? SELECT_FILE_ANY : SELECT_FILE_EXISTING);
    if (!file.execute()) return 1;
    FXString path = file.getFilename();
    if (id == ID_PALETTE_EXPORT)
    {
        if (FXStat::exists(path) && MessageBox::question(getShell(), BOX_YES_NO, _("Overwrite"),
            _("Overwrite existing file: %s?"), path.text()) != BOX_CLICKED_YES) return 1;
        std::ofstream output(path.text());
        output << "# Swordfish palette\nname=" << paletteName->getText().text() << '\n';
        for (const Entry& entry : entries)
            output << "color=" << themeColorText(entry.color, true).text() << ' ' << entry.name.text() << '\n';
        output.close();
        if (!output) MessageBox::error(getShell(), BOX_OK, _("Export palette"), _("Cannot write palette: %s"), path.text());
    }
    else if (id == ID_EXTRACT)
    {
        FXIconSource source(getApp());
        std::unique_ptr<FXImage> image(source.loadScaledImageFile(path, 256, 1));
        if (!image || !image->getData())
        {
            MessageBox::error(getShell(), BOX_OK, _("Extract palette"), _("The image could not be decoded: %s"), path.text());
            return 1;
        }
        struct Bin { FXuint count = 0, red = 0, green = 0, blue = 0; };
        std::vector<Bin> bins(32768);
        for (FXint i = 0; i < image->getWidth() * image->getHeight(); ++i)
        {
            FXColor pixel = image->getData()[i];
            if (FXALPHAVAL(pixel) < 128) continue;
            FXuint red = FXREDVAL(pixel), green = FXGREENVAL(pixel), blue = FXBLUEVAL(pixel);
            Bin& bin = bins[(red >> 3) * 1024 + (green >> 3) * 32 + (blue >> 3)];
            ++bin.count; bin.red += red; bin.green += green; bin.blue += blue;
        }
        std::sort(bins.begin(), bins.end(), [](const Bin& a, const Bin& b) { return a.count > b.count; });
        std::vector<Entry> extracted;
        for (const Bin& bin : bins)
        {
            if (!bin.count || extracted.size() >= 12) break;
            FXColor color = FXRGB(bin.red / bin.count, bin.green / bin.count, bin.blue / bin.count);
            FXbool distinct = true;
            for (const Entry& entry : extracted)
            {
                int dr = FXREDVAL(color) - FXREDVAL(entry.color), dg = FXGREENVAL(color) - FXGREENVAL(entry.color);
                int db = FXBLUEVAL(color) - FXBLUEVAL(entry.color);
                if (dr * dr + dg * dg + db * db < 2304) distinct = false;
            }
            if (distinct) extracted.push_back(Entry{color, _("Image colour ") + FXStringVal(static_cast<FXint>(extracted.size()) + 1)});
        }
        if (extracted.empty())
            MessageBox::error(getShell(), BOX_OK, _("Extract palette"), _("The image has no opaque colours to extract."));
        else { entries.swap(extracted); paletteName->setText(FXPath::title(path)); showEntries(); }
    }
    else
    {
        std::ifstream input(path.text());
        std::string line;
        std::vector<Entry> imported;
        FXString name = FXPath::title(path), error;
        FXbool gimp = false, valid = input.good();
        while (valid && std::getline(input, line))
        {
            if (line.size() > 65536 || imported.size() > 1024) { valid = false; break; }
            FXString text = FXString(line.c_str()).trim();
            if (text.empty() || text[0] == '#') continue;
            if (text == "GIMP Palette") { gimp = true; continue; }
            if (text.left(5) == "name=") { name = text.after('='); continue; }
            if (gimp && (text.left(5) == "Name:" || text.left(8) == "Columns:"))
            {
                if (text.left(5) == "Name:") name = text.after(':').trim();
                continue;
            }
            Entry entry;
            if (text.left(6) == "color=")
            {
                text = text.after('=');
                valid = themeColorParse(text.before(' '), entry.color);
                entry.name = text.after(' ');
            }
            else if (gimp)
            {
                int red, green, blue, position = 0;
                valid = sscanf(text.text(), "%d %d %d %n", &red, &green, &blue, &position) == 3 &&
                        red >= 0 && red <= 255 && green >= 0 && green <= 255 && blue >= 0 && blue <= 255;
                if (valid) { entry.color = FXRGB(red, green, blue); entry.name = text.mid(position, text.length() - position).trim(); }
            }
            else valid = false;
            if (entry.name.find_first_of("|\r\n") >= 0) valid = false;
            if (valid) imported.push_back(entry);
        }
        valid = valid && !input.bad() && !imported.empty() && name.find_first_of("|;=\r\n") < 0;
        if (!valid) MessageBox::error(getShell(), BOX_OK, _("Import palette"), _("This is not a valid Swordfish or GIMP palette: %s"), path.text());
        else { entries.swap(imported); paletteName->setText(name); showEntries(); }
    }
    return 1;
}
