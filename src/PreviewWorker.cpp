#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <poppler.h>
#include <cairo.h>
#include <webp/decode.h>
#include <libraw/libraw.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

static bool saveSurface(cairo_surface_t* surface, const char* output)
{
    cairo_status_t status = cairo_surface_write_to_png(surface, output);
    if (status != CAIRO_STATUS_SUCCESS) fprintf(stderr, "%s\n", cairo_status_to_string(status));
    cairo_surface_destroy(surface);
    return status == CAIRO_STATUS_SUCCESS;
}

static cairo_surface_t* canvas(double width, double height, int size, cairo_t** context)
{
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) return NULL;
    double scale = std::min(1.0, size / std::max(width, height));
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
        std::max(1, static_cast<int>(std::ceil(width * scale))),
        std::max(1, static_cast<int>(std::ceil(height * scale))));
    *context = cairo_create(surface);
    cairo_scale(*context, scale, scale);
    return surface;
}

static bool isRawImage(const char* path, const char* mime)
{
    static const char* const extensions[] =
    {
        "3fr", "ari", "arw", "bay", "cap", "cr2", "cr3", "crw", "dcr", "dcs", "dng", "drf", "eip", "erf",
        "fff", "iiq", "k25", "kdc", "mdc", "mef", "mos", "mrw", "nef", "nrw", "orf", "ori", "pef", "ptx",
        "pxn", "raf", "raw", "rw2", "rwl", "sr2", "srf", "srw", "x3f", NULL
    };
    const char* dot = strrchr(path, '.');
    if (dot && !strchr(dot, '/'))
    {
        for (int i = 0; extensions[i]; ++i)
        {
            if (g_ascii_strcasecmp(dot + 1, extensions[i]) == 0) return true;
        }
    }
    return g_content_type_is_a(mime, "image/x-dcraw") || strcmp(mime, "image/x-adobe-dng") == 0;
}

// Decode the preview the camera embedded in a RAW file, the RAW data itself is never developed
static GdkPixbuf* rawPreview(const char* path, int size)
{
    libraw_data_t* raw = libraw_init(0);
    if (!raw) return NULL;
    GdkPixbuf* image = NULL;
    int status = libraw_open_file(raw, path);
    if (status == LIBRAW_SUCCESS)
    {
        // Prefer the smallest JPEG preview that still fills the requested size, other formats decode slowly
        int chosen = -1, chosenSize = 0, largest = -1, largestSize = 0;
        for (int pass = 0; pass < 2 && chosen < 0 && largest < 0; ++pass)
        {
            for (int i = 0; i < raw->thumbs_list.thumbcount; ++i)
            {
                const libraw_thumbnail_item_t& item = raw->thumbs_list.thumblist[i];
                if (pass == 0 && item.tformat != LIBRAW_INTERNAL_THUMBNAIL_JPEG) continue;
                int longest = std::max(item.twidth, item.theight);
                if (longest > largestSize) { largest = i; largestSize = longest; }
                if (longest >= size && (chosen < 0 || longest < chosenSize)) { chosen = i; chosenSize = longest; }
            }
        }
        if (chosen < 0) chosen = largest;
        status = chosen >= 0 ? libraw_unpack_thumb_ex(raw, chosen) : libraw_unpack_thumb(raw);
    }
    if (status == LIBRAW_SUCCESS && raw->thumbnail.thumb && raw->thumbnail.tlength > 0)
    {
        const libraw_thumbnail_t& thumb = raw->thumbnail;
        GError* error = NULL;
        if (thumb.tformat == LIBRAW_THUMBNAIL_JPEG)
        {
            GdkPixbufLoader* loader = gdk_pixbuf_loader_new_with_type("jpeg", &error);
            if (loader)
            {
                int width = std::max<int>(1, thumb.twidth), height = std::max<int>(1, thumb.theight);
                double factor = std::min(1.0, double(size) / std::max(width, height));
                gdk_pixbuf_loader_set_size(loader, std::max(1, int(width * factor)), std::max(1, int(height * factor)));
                if (gdk_pixbuf_loader_write(loader, reinterpret_cast<const guchar*>(thumb.thumb), thumb.tlength, &error) &&
                    gdk_pixbuf_loader_close(loader, &error))
                {
                    image = gdk_pixbuf_loader_get_pixbuf(loader);
                    if (image) g_object_ref(image);
                }
                else gdk_pixbuf_loader_close(loader, NULL);
                g_object_unref(loader);
            }
        }
        else if (thumb.tformat == LIBRAW_THUMBNAIL_BITMAP && thumb.tcolors == 3 &&
                 thumb.tlength >= unsigned(thumb.twidth) * thumb.theight * 3)
        {
            GdkPixbuf* full = gdk_pixbuf_new_from_data(reinterpret_cast<const guchar*>(thumb.thumb), GDK_COLORSPACE_RGB,
                                                       false, 8, thumb.twidth, thumb.theight, thumb.twidth * 3, NULL, NULL);
            double factor = std::min(1.0, double(size) / std::max(thumb.twidth, thumb.theight));
            image = gdk_pixbuf_scale_simple(full, std::max(1, int(thumb.twidth * factor)),
                                            std::max(1, int(thumb.theight * factor)), GDK_INTERP_BILINEAR);
            g_object_unref(full);
        }
        if (error) { fprintf(stderr, "%s\n", error->message); g_error_free(error); }

        // Embedded previews are stored unrotated, apply the camera orientation
        const char* exif = image ? gdk_pixbuf_get_option(image, "orientation") : NULL;
        if (image && (!exif || strcmp(exif, "1") == 0))
        {
            GdkPixbufRotation rotation = GDK_PIXBUF_ROTATE_NONE;
            switch (raw->sizes.flip)
            {
                case 3: rotation = GDK_PIXBUF_ROTATE_UPSIDEDOWN; break;
                case 5: rotation = GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE; break;
                case 6: rotation = GDK_PIXBUF_ROTATE_CLOCKWISE; break;
            }
            if (rotation != GDK_PIXBUF_ROTATE_NONE)
            {
                GdkPixbuf* rotated = gdk_pixbuf_rotate_simple(image, rotation);
                if (rotated) { g_object_unref(image); image = rotated; }
            }
        }
    }
    else fprintf(stderr, "RAW preview: %s\n", libraw_strerror(status));
    libraw_close(raw);
    return image;
}

int main(int argc, char** argv)
{
    g_setenv("GIO_USE_VFS", "local", true);
    const char* modules = getenv("SWORDFISH_PREVIEW_PIXBUF_MODULEDIR");
    const char* cache = getenv("SWORDFISH_PREVIEW_PIXBUF_MODULE_FILE");
    const char* data = getenv("SWORDFISH_PREVIEW_DATA_DIRS");
    if (modules) g_setenv("GDK_PIXBUF_MODULEDIR", modules, true);
    if (cache) g_setenv("GDK_PIXBUF_MODULE_FILE", cache, true);
    if (data) g_setenv("XDG_DATA_DIRS", data, true);
    if (argc != 4) { fprintf(stderr, "Usage: swordfish-preview INPUT OUTPUT SIZE\n"); return 2; }
    char* end;
    long size = strtol(argv[3], &end, 10);
    if (*end || size < 32 || size > 512) { fprintf(stderr, "Invalid preview size\n"); return 2; }
    struct stat info;
    if (stat(argv[1], &info) != 0 || !S_ISREG(info.st_mode))
    { fprintf(stderr, "Preview input is not a readable regular file\n"); return 2; }
    GError* error = NULL;
    GFile* input = g_file_new_for_path(argv[1]);
    GFileInfo* metadata = g_file_query_info(input, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                          G_FILE_QUERY_INFO_NONE, NULL, &error);
    if (!metadata) { fprintf(stderr, "%s\n", error->message); g_error_free(error); g_object_unref(input); return 2; }
    const char* type = g_file_info_get_content_type(metadata);
    gchar* mime = type ? g_content_type_get_mime_type(type) : g_strdup("");
    bool success = false;
    if (strcmp(mime, "application/pdf") == 0)
    {
        char* uri = g_file_get_uri(input);
        PopplerDocument* document = poppler_document_new_from_file(uri, NULL, &error);
        g_free(uri);
        if (document)
        {
            PopplerPage* page = poppler_document_get_page(document, 0);
            if (page)
            {
                double width, height;
                poppler_page_get_size(page, &width, &height);
                cairo_t* context;
                cairo_surface_t* surface = canvas(width, height, size, &context);
                if (surface)
                {
                    cairo_set_source_rgb(context, 1, 1, 1);
                    cairo_paint(context);
                    poppler_page_render(page, context);
                    cairo_destroy(context);
                    success = saveSurface(surface, argv[2]);
                }
                g_object_unref(page);
            }
            g_object_unref(document);
        }
    }
    else if (strcmp(mime, "image/svg+xml") == 0)
    {
        RsvgHandle* svg = rsvg_handle_new_from_file(argv[1], &error);
        if (svg)
        {
            RsvgDimensionData dimensions;
#if LIBRSVG_CHECK_VERSION(2, 52, 0)
            double width, height;
            if (rsvg_handle_get_intrinsic_size_in_pixels(svg, &width, &height))
            {
                dimensions.width = std::max(1, int(std::ceil(width)));
                dimensions.height = std::max(1, int(std::ceil(height)));
            }
            else
            {
                dimensions.width = size;
                dimensions.height = size;
            }
#else
            rsvg_handle_get_dimensions(svg, &dimensions);
#endif
            cairo_t* context;
            cairo_surface_t* surface = canvas(dimensions.width, dimensions.height, size, &context);
            if (surface)
            {
#if LIBRSVG_CHECK_VERSION(2, 46, 0)
                RsvgRectangle viewport = {0, 0, double(dimensions.width), double(dimensions.height)};
                success = rsvg_handle_render_document(svg, context, &viewport, &error);
#else
                success = rsvg_handle_render_cairo(svg, context);
#endif
                cairo_destroy(context);
                if (success) success = saveSurface(surface, argv[2]);
                else cairo_surface_destroy(surface);
            }
            g_object_unref(svg);
        }
    }
    else if (g_str_has_prefix(mime, "video/"))
    {
        char dimensions[16];
        snprintf(dimensions, sizeof(dimensions), "%ld", size);
        execlp("ffmpegthumbnailer", "ffmpegthumbnailer", "-i", argv[1], "-o", argv[2],
               "-s", dimensions, "-c", "png", "-f", static_cast<char*>(NULL));
        perror("ffmpegthumbnailer");
    }
    else if (g_str_has_prefix(mime, "image/"))
    {
        GdkPixbuf* image = NULL;
        if (isRawImage(argv[1], mime)) image = rawPreview(argv[1], size);
        if (!image && strcmp(mime, "image/webp") == 0)
        {
            gchar* data = NULL;
            gsize length;
            if (g_file_get_contents(argv[1], &data, &length, &error))
            {
                WebPDecoderConfig config;
                if (WebPInitDecoderConfig(&config) && WebPGetFeatures(
                    reinterpret_cast<const uint8_t*>(data), length, &config.input) == VP8_STATUS_OK)
                {
                    double factor = std::min(1.0, double(size) / std::max(config.input.width, config.input.height));
                    config.options.use_scaling = 1;
                    config.options.scaled_width = std::max(1, int(config.input.width * factor));
                    config.options.scaled_height = std::max(1, int(config.input.height * factor));
                    config.output.colorspace = MODE_RGBA;
                    if (WebPDecode(reinterpret_cast<const uint8_t*>(data), length, &config) == VP8_STATUS_OK)
                    {
                        image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, true, 8,
                                              config.output.width, config.output.height);
                        for (int y = 0; y < config.output.height; ++y)
                            memcpy(gdk_pixbuf_get_pixels(image) + y * gdk_pixbuf_get_rowstride(image),
                                   config.output.u.RGBA.rgba + y * config.output.u.RGBA.stride,
                                   config.output.width * 4);
                    }
                    WebPFreeDecBuffer(&config.output);
                }
                g_free(data);
            }
        }
        else if (!image) image = gdk_pixbuf_new_from_file_at_scale(argv[1], size, size, true, &error);
        if (image)
        {
            GdkPixbuf* oriented = gdk_pixbuf_apply_embedded_orientation(image);
            success = gdk_pixbuf_save(oriented, argv[2], "png", &error, NULL);
            g_object_unref(oriented);
            g_object_unref(image);
        }
    }
    else fprintf(stderr, "Unsupported preview MIME type: %s\n", mime);
    if (error) { fprintf(stderr, "%s\n", error->message); g_error_free(error); }
    else if (!success) fprintf(stderr, "Unable to generate preview\n");
    g_free(mime);
    g_object_unref(metadata);
    g_object_unref(input);
    return success ? 0 : 2;
}
