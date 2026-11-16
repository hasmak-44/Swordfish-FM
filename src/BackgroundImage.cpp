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

FXColor sampleImage(const FXImage& image, double x, double y, FXColor base)
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
    return FXRGB(static_cast<FXint>(red + (1 - alpha) * FXREDVAL(base) + 0.5),
                 static_cast<FXint>(green + (1 - alpha) * FXGREENVAL(base) + 0.5),
                 static_cast<FXint>(blue + (1 - alpha) * FXBLUEVAL(base) + 0.5));
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

FXbool BackgroundImage::importImage(FXApp* app, const FXString& source, const FXString& reference, FXString& error)
{
    if (!validReference(reference))
    {
        error = _("Invalid image name or category.");
        return false;
    }
    std::unique_ptr<FXImage> image = load(app, source, error);
    if (!image) return false;
    if (!path(reference).empty())
    {
        error = _("Image name already exists. Please use another name.");
        return false;
    }
    FXString folder = xf_userdatahome() + PATHSEPSTRING "images", destination;
    if (!BackgroundEffect::prepareImageDestination(folder, reference, destination, error)) return false;
    if (!FXFile::copyFiles(source, destination, false))
    {
        error = _("Cannot copy image into ") + folder + ": " + strerror(errno);
        return false;
    }
    appLog(APP_LOG_SETTINGS, "theme-image-imported", reference + " from \"" + source + "\"");
    return true;
}

void BackgroundImage::composite(FXColor* pixels, FXint width, FXint height, const FXImage& source,
                                const FXString& mode)
{
    if (!pixels || width <= 0 || height <= 0 || !source.getData() ||
        source.getWidth() <= 0 || source.getHeight() <= 0) return;
    if (mode == "tile")
    {
        for (FXint y = 0; y < height; ++y)
            for (FXint x = 0; x < width; ++x)
            {
                FXint index = y * width + x;
                pixels[index] = blendPixel(pixels[index], source.getPixel(x % source.getWidth(), y % source.getHeight()));
            }
        return;
    }
    double sx = width / static_cast<double>(source.getWidth());
    double sy = height / static_cast<double>(source.getHeight());
    double scale = mode == "fill" ? FXMAX(sx, sy) : FXMIN(sx, sy);
    double left = (width - source.getWidth() * scale) / 2.0;
    double top = (height - source.getHeight() * scale) / 2.0;
    for (FXint y = 0; y < height; ++y)
        for (FXint x = 0; x < width; ++x)
        {
            double px = x + 0.5, py = y + 0.5;
            if (px < left || py < top || px >= left + source.getWidth() * scale ||
                py >= top + source.getHeight() * scale) continue;
            FXint index = y * width + x;
            pixels[index] = sampleImage(source, (px - left) / scale - 0.5,
                                         (py - top) / scale - 0.5, pixels[index]);
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

void BackgroundImageLayer::set(const FXString& image, const FXString& placement, const FXString& assetRoot)
{
    FXString selectedMode = placement.empty() ? FXString("fit") : placement;
    if (reference == image && mode == selectedMode && root == assetRoot) return;
    reference = image;
    mode = selectedMode;
    root = assetRoot;
    rendered.reset();
    width = height = 0;
    failed = false;
}

FXbool BackgroundImageLayer::draw(FXWindow* owner, FXWindow* target, FXDC& dc,
                                  FXint x, FXint y, FXint w, FXint h)
{
    if (reference.empty() || failed || w <= 0 || h <= 0) return false;
    FXint newWidth = FXMAX(1, owner->getWidth()), newHeight = FXMAX(1, owner->getHeight());
    if (!rendered || newWidth != width || newHeight != height || background != owner->getBackColor())
    {
        FXString file = BackgroundImage::path(reference, root), error;
        std::unique_ptr<FXImage> source;
        if (!file.empty()) source = BackgroundImage::load(owner->getApp(), file, error);
        if (!source)
        {
            appLog(APP_LOG_ERRORS, "theme-image-unavailable", reference + ": " +
                   (error.empty() ? _("Image was not found in the theme or its library.") : error));
            rendered.reset();
            failed = true;
            return false;
        }
        width = newWidth;
        height = newHeight;
        background = owner->getBackColor();
        FXColor* pixels = NULL;
        if (!FXMALLOC(&pixels, FXColor, static_cast<size_t>(width) * height))
            throw FXMemoryException(_("Unable to render background image"));
        std::fill(pixels, pixels + static_cast<size_t>(width) * height, background);
        BackgroundImage::composite(pixels, width, height, *source, mode);
        rendered.reset(new FXImage(owner->getApp(), pixels, IMAGE_OWNED, width, height));
        rendered->create();
    }
    FXint originX = 0, originY = 0;
    if (target != owner) target->translateCoordinatesTo(originX, originY, owner, 0, 0);
    FXint x0 = FXMAX(x, -originX), y0 = FXMAX(y, -originY);
    FXint x1 = FXMIN(x + w, width - originX), y1 = FXMIN(y + h, height - originY);
    if (x0 != x || y0 != y || x1 != x + w || y1 != y + h)
    {
        dc.setForeground(target->getBackColor());
        dc.fillRectangle(x, y, w, h);
    }
    if (x1 > x0 && y1 > y0)
        dc.drawArea(rendered.get(), x0 + originX, y0 + originY, x1 - x0, y1 - y0, x0, y0);
    return true;
}

FXbool drawBackgroundImage(FXWindow* target, FXDC& dc, FXint x, FXint y, FXint w, FXint h)
{
    for (FXWindow* owner = target; owner; owner = owner->getParent())
        if (BackgroundImageOwner* imageOwner = dynamic_cast<BackgroundImageOwner*>(owner))
            return imageOwner->allowsBackgroundImage(target) &&
                   imageOwner->backgroundImage.draw(owner, target, dc, x, y, w, h);
    return false;
}

ThemeToolBar::ThemeToolBar(FXComposite* parent, FXComposite* shell, FXuint opts) :
    FXToolBar(parent, shell, opts) {}

ThemeToolBar::ThemeToolBar(FXComposite* parent, FXuint opts) :
    FXToolBar(parent, opts) {}

long ThemeToolBar::onPaint(FXObject* sender, FXSelector selector, void* pointer)
{
    long result = FXPacker::onPaint(sender, selector, pointer);
    FXDCWindow dc(this, static_cast<FXEvent*>(pointer));
    drawBackgroundImage(this, dc, border, border, width - 2 * border, height - 2 * border);
    return result;
}

FXDEFMAP(ThemeToolBar) ThemeToolBarMap[] = {
    FXMAPFUNC(SEL_PAINT, 0, ThemeToolBar::onPaint)
};
FXIMPLEMENT(ThemeToolBar, FXToolBar, ThemeToolBarMap, ARRAYNUMBER(ThemeToolBarMap))
