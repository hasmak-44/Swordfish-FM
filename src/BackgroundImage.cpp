#include "config.h"
#include "i18n.h"
#include "BackgroundImage.h"
#include "BackgroundEffect.h"
#include "AppLogger.h"
#include "xfeutils.h"
#include <FXJPGImage.h>
#include <FXPNGImage.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <sys/stat.h>

extern FXString execpath;

namespace
{
const FXulong maxBytes = 32UL * 1024 * 1024;
const FXulong maxPixels = 32000000UL;
const FXuint maxDimension = 8192;

FXbool dimensionsAllowed(FXuint width, FXuint height)
{
    return width && height && width <= maxDimension && height <= maxDimension &&
           static_cast<FXulong>(width) * height <= maxPixels;
}

FXbool jpegDimensions(FILE* file, FXulong size, FXuint& width, FXuint& height)
{
    rewind(file);
    if (fgetc(file) != 0xff || fgetc(file) != 0xd8) return false;
    while (true)
    {
        int marker = fgetc(file);
        if (marker != 0xff) return false;
        do marker = fgetc(file); while (marker == 0xff);
        if (marker == EOF || marker == 0 || marker == 0xd9 || marker == 0xda) return false;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        int high = fgetc(file), low = fgetc(file);
        if (high == EOF || low == EOF) return false;
        int length = (high << 8) | low;
        long start = ftell(file);
        if (length < 2 || start < 0 || static_cast<FXulong>(start) + length - 2 > size) return false;
        FXbool frame = marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 &&
                       marker != 0xc8 && marker != 0xcc;
        if (frame)
        {
            if (length < 8 || fgetc(file) == EOF) return false;
            int h1 = fgetc(file), h2 = fgetc(file), w1 = fgetc(file), w2 = fgetc(file);
            if (h1 == EOF || h2 == EOF || w1 == EOF || w2 == EOF) return false;
            height = (h1 << 8) | h2;
            width = (w1 << 8) | w2;
            return dimensionsAllowed(width, height);
        }
        if (fseek(file, length - 2, SEEK_CUR) != 0) return false;
    }
}

FXbool imageHeader(const FXString& path, FXuint& width, FXuint& height, FXString& error)
{
    struct stat status;
    if (stat(path.text(), &status) != 0 || !S_ISREG(status.st_mode) || status.st_size <= 0 ||
        static_cast<FXulong>(status.st_size) > maxBytes)
    {
        error = _("An image must be a regular PNG or JPEG file of at most 32 MB.");
        return false;
    }
    FILE* file = fopen(path.text(), "rb");
    if (!file)
    {
        error = path + ": " + strerror(errno);
        return false;
    }
    unsigned char header[24];
    size_t count = fread(header, 1, sizeof(header), file);
    FXString extension = FXPath::extension(path).lower();
    FXbool valid = false;
    if (extension == "png" && count == sizeof(header) &&
        memcmp(header, "\x89PNG\r\n\x1a\n", 8) == 0 && memcmp(header + 12, "IHDR", 4) == 0)
    {
        width = static_cast<FXuint>(header[16]) << 24 | static_cast<FXuint>(header[17]) << 16 |
                static_cast<FXuint>(header[18]) << 8 | header[19];
        height = static_cast<FXuint>(header[20]) << 24 | static_cast<FXuint>(header[21]) << 16 |
                 static_cast<FXuint>(header[22]) << 8 | header[23];
        valid = dimensionsAllowed(width, height);
    }
    else if (extension == "jpg" || extension == "jpeg")
        valid = jpegDimensions(file, status.st_size, width, height);
    if (ferror(file)) valid = false;
    fclose(file);
    if (!valid)
        error = _("Use a valid PNG or JPEG image up to 8192 x 8192 pixels and 32 megapixels.");
    return valid;
}

FXbool regularGraphic(const FXString& root, const FXString& relative, FXString& result)
{
    if (root.empty()) return false;
    FXString path = FXPath::absolute(root);
    if (xf_islink(path)) return false;
    for (FXint i = 0; i <= relative.contains('/'); ++i)
    {
        path += PATHSEPSTRING + relative.section('/', i);
        if (xf_islink(path)) return false;
    }
    if (!xf_isfile(path)) return false;
    result = path;
    return true;
}

FXColor blendPixel(FXColor base, FXColor source)
{
    FXuint alpha = FXALPHAVAL(source);
    return FXRGB((FXREDVAL(source) * alpha + FXREDVAL(base) * (255 - alpha) + 127) / 255,
                 (FXGREENVAL(source) * alpha + FXGREENVAL(base) * (255 - alpha) + 127) / 255,
                 (FXBLUEVAL(source) * alpha + FXBLUEVAL(base) * (255 - alpha) + 127) / 255);
}

FXColor sampleImage(const FXImage& image, double x, double y, double opacity)
{
    const FXint width = image.getWidth(), height = image.getHeight();
    x = FXCLAMP(0.0, x, static_cast<double>(width - 1));
    y = FXCLAMP(0.0, y, static_cast<double>(height - 1));
    FXint x0 = static_cast<FXint>(x), y0 = static_cast<FXint>(y);
    FXint x1 = FXMIN(x0 + 1, width - 1), y1 = FXMIN(y0 + 1, height - 1);
    double dx = x - x0, dy = y - y0;
    double weights[] = {(1 - dx) * (1 - dy), dx * (1 - dy), (1 - dx) * dy, dx * dy};
    FXColor pixels[] = {image.getPixel(x0, y0), image.getPixel(x1, y0),
                        image.getPixel(x0, y1), image.getPixel(x1, y1)};
    double alpha = 0, red = 0, green = 0, blue = 0;
    for (FXint i = 0; i < 4; ++i)
    {
        double weight = weights[i] * FXALPHAVAL(pixels[i]) / 255.0;
        alpha += weight;
        red += weight * FXREDVAL(pixels[i]);
        green += weight * FXGREENVAL(pixels[i]);
        blue += weight * FXBLUEVAL(pixels[i]);
    }
    if (alpha <= 0) return 0;
    return FXRGBA(static_cast<FXint>(red / alpha + 0.5), static_cast<FXint>(green / alpha + 0.5),
                  static_cast<FXint>(blue / alpha + 0.5), static_cast<FXint>(alpha * opacity * 255 + 0.5));
}

struct WindowLayer
{
    FXWindow* anchor = NULL;
    FXString reference, mode, root;
    FXint opacity = 100;
    std::unique_ptr<FXImage> source;
    BackgroundOverlay overlay;
    FXbool failed = false;
    FXuint serial = 1;
    std::map<const FXWindow*, std::pair<FXint, FXint>> origins;
} windowLayer;

void refreshDescendants(FXWindow* window)
{
    for (FXWindow* child = window->getFirst(); child; child = child->getNext())
    {
        child->update();
        refreshDescendants(child);
    }
}
}

FXbool BackgroundImage::validReference(const FXString& reference)
{
    FXString relative;
    if (!BackgroundEffect::textureRelativePath(reference, relative)) return false;
    FXString extension = FXPath::extension(relative).lower();
    return extension == "png" || extension == "jpg" || extension == "jpeg";
}

FXString BackgroundImage::path(const FXString& reference, const FXString& assetRoot)
{
    FXString relative, result;
    if (!validReference(reference) || !BackgroundEffect::textureRelativePath(reference, relative))
        return FXString::null;
    FXString roots[] = {
        assetRoot.empty() ? FXString::null : assetRoot + PATHSEPSTRING "images",
        FXPath::directory(execpath) + "/../share/swordfish/images",
        xf_userdatahome() + PATHSEPSTRING "images"
    };
    for (const FXString& root : roots)
        if (regularGraphic(root, relative, result)) return result;
    return FXString::null;
}

std::unique_ptr<FXImage> BackgroundImage::load(FXApp* app, const FXString& file, FXString& error)
{
    FXuint width = 0, height = 0;
    if (!imageHeader(file, width, height, error)) return std::unique_ptr<FXImage>();
    std::unique_ptr<FXImage> image;
    if (FXPath::extension(file).lower() == "png")
        image.reset(new FXPNGImage(app, NULL, IMAGE_KEEP));
    else
        image.reset(new FXJPGImage(app, NULL, IMAGE_KEEP));
    FXFileStream stream;
    if (!stream.open(file, FXStreamLoad) || !image->loadPixels(stream) || !image->getData() ||
        image->getWidth() != static_cast<FXint>(width) || image->getHeight() != static_cast<FXint>(height))
    {
        error = _("The background image could not be decoded: ") + file;
        return std::unique_ptr<FXImage>();
    }
    return image;
}

FXbool BackgroundImage::importImage(FXApp* app, const FXString& source, const FXString& reference,
                                   FXString& error, FXbool replaceExisting)
{
    if (!validReference(reference))
    {
        error = _("Invalid image name or category.");
        return false;
    }
    std::unique_ptr<FXImage> image = load(app, source, error);
    if (!image) return false;
    FXString existing = path(reference);
    if (!existing.empty() && !replaceExisting)
    {
        error = _("Image name already exists. Please use another name.");
        return false;
    }
    FXString folder = xf_userdatahome() + PATHSEPSTRING "images", destination;
    if (!BackgroundEffect::prepareImageDestination(folder, reference, destination, error)) return false;
    if (!existing.empty() && FXPath::absolute(existing) != FXPath::absolute(destination))
    {
        error = _("Built-in or theme images cannot be replaced here. Please use another name.");
        return false;
    }
    if (!FXFile::copyFiles(source, destination, replaceExisting))
    {
        error = (replaceExisting ? _("Cannot replace image in ") : _("Cannot copy image into ")) +
                folder + ": " + strerror(errno);
        return false;
    }
    appLog(APP_LOG_SETTINGS, "theme-image-imported", reference + " from \"" + source + "\"");
    return true;
}

void BackgroundImage::composite(FXColor* pixels, FXint width, FXint height, const FXImage& source,
                                const FXString& mode)
{
    BackgroundOverlay overlay;
    overlay.render(source, width, height, mode, 100);
    overlay.blend(pixels, width, height, 0, 0);
}

void BackgroundOverlay::render(const FXImage& source, FXint w, FXint h, const FXString& mode, FXint percent)
{
    pixels.clear();
    width = height = 0;
    if (w <= 0 || h <= 0 || !source.getData() || source.getWidth() <= 0 || source.getHeight() <= 0) return;
    width = w;
    height = h;
    pixels.assign(static_cast<size_t>(w) * h, 0);
    const double opacity = FXCLAMP(0, percent, 100) / 100.0;
    if (mode == "tile")
    {
        for (FXint y = 0; y < h; ++y)
            for (FXint x = 0; x < w; ++x)
            {
                FXColor color = source.getPixel(x % source.getWidth(), y % source.getHeight());
                pixels[static_cast<size_t>(y) * w + x] = FXRGBA(FXREDVAL(color), FXGREENVAL(color), FXBLUEVAL(color),
                    static_cast<FXint>(FXALPHAVAL(color) * opacity + 0.5));
            }
        return;
    }
    double sx = w / static_cast<double>(source.getWidth());
    double sy = h / static_cast<double>(source.getHeight());
    double scale = mode == "fill" ? FXMAX(sx, sy) : FXMIN(sx, sy);
    double left = (w - source.getWidth() * scale) / 2.0;
    double top = (h - source.getHeight() * scale) / 2.0;
    for (FXint y = 0; y < h; ++y)
        for (FXint x = 0; x < w; ++x)
        {
            double px = x + 0.5, py = y + 0.5;
            if (px < left || py < top || px >= left + source.getWidth() * scale ||
                py >= top + source.getHeight() * scale) continue;
            pixels[static_cast<size_t>(y) * w + x] =
                sampleImage(source, (px - left) / scale - 0.5, (py - top) / scale - 0.5, opacity);
        }
}

void BackgroundOverlay::blend(FXColor* target, FXint w, FXint h, FXint x, FXint y) const
{
    if (!target || pixels.empty()) return;
    FXint x0 = FXMAX(0, -x), y0 = FXMAX(0, -y);
    FXint x1 = FXMIN(w, width - x), y1 = FXMIN(h, height - y);
    for (FXint row = y0; row < y1; ++row)
        for (FXint column = x0; column < x1; ++column)
        {
            FXColor color = pixels[static_cast<size_t>(row + y) * width + column + x];
            if (FXALPHAVAL(color))
            {
                FXColor& base = target[static_cast<size_t>(row) * w + column];
                base = blendPixel(base, color);
            }
        }
}

FXIcon* BackgroundImage::preview(FXApp* app, const FXString& file, FXColor background, FXint size, FXString& error)
{
    std::unique_ptr<FXImage> source = load(app, file, error);
    if (!source) return NULL;
    FXColor* pixels = NULL;
    if (!FXMALLOC(&pixels, FXColor, size * size)) throw FXMemoryException(_("Unable to create image preview"));
    std::fill(pixels, pixels + size * size, background);
    composite(pixels, size, size, *source, "fit");
    FXIcon* icon = new FXIcon(app, pixels, 0, IMAGE_KEEP | IMAGE_OWNED, size, size);
    icon->create();
    return icon;
}

FXint backgroundImageOpacity(const FXString& value)
{
    return value.empty() ? 100 : FXCLAMP(0, FXIntVal(value), 100);
}

void setWindowBackground(FXWindow* anchor, const FXString& reference, const FXString& placement,
                         FXint opacity, const FXString& root)
{
    WindowLayer& layer = windowLayer;
    FXString mode = placement.empty() ? FXString("fit") : placement;
    if (layer.anchor == anchor && layer.reference == reference && layer.mode == mode &&
        layer.opacity == opacity && layer.root == root) return;
    if (layer.reference != reference || layer.root != root) layer.source.reset();
    layer.anchor = anchor;
    layer.reference = reference;
    layer.mode = mode;
    layer.opacity = opacity;
    layer.root = root;
    layer.overlay = BackgroundOverlay();
    layer.failed = false;
    layer.origins.clear();
    ++layer.serial;
}

const BackgroundOverlay* windowBackground(const FXWindow* target, FXint& x, FXint& y, FXuint* serial)
{
    WindowLayer& layer = windowLayer;
    x = y = 0;
    if (!layer.anchor || layer.reference.empty() || layer.failed || layer.opacity <= 0) return NULL;
    // Child window positions are client-side, which avoids a server round trip on every paint
    const FXWindow* window = target;
    for (; window && window != layer.anchor; window = window->getParent())
    {
        x += window->getX();
        y += window->getY();
    }
    if (!window) return NULL;
    FXint width = FXMAX(1, layer.anchor->getWidth()), height = FXMAX(1, layer.anchor->getHeight());
    if (layer.overlay.width != width || layer.overlay.height != height)
    {
        std::unique_ptr<FXImage> loaded;
        FXImage* source = layer.source.get();
        if (!source)
        {
            FXString file = BackgroundImage::path(layer.reference, layer.root), error;
            if (!file.empty()) loaded = BackgroundImage::load(layer.anchor->getApp(), file, error);
            if (!loaded)
            {
                appLog(APP_LOG_ERRORS, "theme-image-unavailable", layer.reference + ": " +
                       (error.empty() ? _("Image was not found in the theme or its library.") : error));
                layer.failed = true;
                return NULL;
            }
            source = loaded.get();
        }
        FXbool resized = !layer.overlay.pixels.empty();
        layer.overlay.render(*source, width, height, layer.mode, layer.opacity);
        // Large originals are decoded again on the next window resize instead of being kept in memory
        if (loaded && static_cast<FXlong>(loaded->getWidth()) * loaded->getHeight() <= 16 * 1024 * 1024)
            layer.source = std::move(loaded);
        ++layer.serial;
        layer.origins.clear();
        if (resized) refreshDescendants(layer.anchor);
    }
    if (serial) *serial = layer.serial;
    return &layer.overlay;
}

void trackBackgroundOrigin(FXWindow* window)
{
    FXint x, y;
    if (!windowBackground(window, x, y)) return;
    auto known = windowLayer.origins.find(window);
    // Children that were moved without being resized are not repainted by the server
    if (known != windowLayer.origins.end() && (known->second.first != x || known->second.second != y))
        refreshDescendants(window);
    windowLayer.origins[window] = std::make_pair(x, y);
}

FXbool drawBackgroundImage(FXWindow* target, FXDC& dc, FXint x, FXint y, FXint w, FXint h, FXColor base)
{
    if (w <= 0 || h <= 0) return false;
    for (FXWindow* owner = target; owner; owner = owner->getParent())
        if (BackgroundImageOwner* imageOwner = dynamic_cast<BackgroundImageOwner*>(owner))
            if (!imageOwner->allowsBackgroundImage(target)) return false;
    FXint originX, originY;
    const BackgroundOverlay* overlay = windowBackground(target, originX, originY);
    if (!overlay) return false;
    FXColor* pixels = NULL;
    if (!FXMALLOC(&pixels, FXColor, static_cast<size_t>(w) * h))
        throw FXMemoryException(_("Unable to render background image"));
    std::fill(pixels, pixels + static_cast<size_t>(w) * h, FXALPHAVAL(base) ? base : target->getBackColor());
    overlay->blend(pixels, w, h, originX + x, originY + y);
    FXImage image(target->getApp(), pixels, IMAGE_OWNED, w, h);
    image.create();
    dc.drawImage(&image, x, y);
    return true;
}

FXbool drawBackgroundGaps(FXWindow* target, FXDC& dc, FXint x, FXint y, FXint w, FXint h)
{
    FXint originX, originY;
    if (w <= 0 || h <= 0 || !windowBackground(target, originX, originY)) return false;
    // Containers are mostly covered by their children, so only the visible gaps are rendered
    std::vector<FXRectangle> gaps(1, FXRectangle(x, y, w, h));
    for (FXWindow* child = target->getFirst(); child && !gaps.empty(); child = child->getNext())
    {
        if (!child->shown()) continue;
        FXint left = child->getX(), top = child->getY();
        FXint right = left + child->getWidth(), bottom = top + child->getHeight();
        std::vector<FXRectangle> remaining;
        for (const FXRectangle& gap : gaps)
        {
            FXint gapRight = gap.x + gap.w, gapBottom = gap.y + gap.h;
            if (left >= gapRight || right <= gap.x || top >= gapBottom || bottom <= gap.y)
            {
                remaining.push_back(gap);
                continue;
            }
            FXint coverTop = FXMAX(top, static_cast<FXint>(gap.y));
            FXint coverBottom = FXMIN(bottom, gapBottom);
            if (top > gap.y) remaining.push_back(FXRectangle(gap.x, gap.y, gap.w, top - gap.y));
            if (bottom < gapBottom) remaining.push_back(FXRectangle(gap.x, bottom, gap.w, gapBottom - bottom));
            if (left > gap.x) remaining.push_back(FXRectangle(gap.x, coverTop, left - gap.x, coverBottom - coverTop));
            if (right < gapRight) remaining.push_back(FXRectangle(right, coverTop, gapRight - right, coverBottom - coverTop));
        }
        gaps.swap(remaining);
    }
    for (const FXRectangle& gap : gaps)
        if (!drawBackgroundImage(target, dc, gap.x, gap.y, gap.w, gap.h))
        {
            dc.setForeground(target->getBackColor());
            dc.fillRectangle(gap.x, gap.y, gap.w, gap.h);
        }
    return true;
}

ThemeToolBar::ThemeToolBar(FXComposite* parent, FXComposite* shell, FXuint opts) :
    FXToolBar(parent, shell, opts) {}

ThemeToolBar::ThemeToolBar(FXComposite* parent, FXuint opts) :
    FXToolBar(parent, opts) {}

long ThemeToolBar::onPaint(FXObject* sender, FXSelector selector, void* pointer)
{
    return FXPacker::onPaint(sender, selector, pointer);
}

FXDEFMAP(ThemeToolBar) ThemeToolBarMap[] = {
    FXMAPFUNC(SEL_PAINT, 0, ThemeToolBar::onPaint)
};
FXIMPLEMENT(ThemeToolBar, FXToolBar, ThemeToolBarMap, ARRAYNUMBER(ThemeToolBarMap))
