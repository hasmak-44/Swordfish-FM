#include <fx.h>
#include "Preview.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <webp/encode.h>
#include <cairo-pdf.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

static void number(std::string& out, unsigned value, unsigned bytes = 4)
{
    for (unsigned i = 0; i < bytes; ++i) out += char(value >> (8 * i));
}

static std::string chunk(const char* tag, const std::string& data)
{
    std::string out(tag, 4);
    number(out, data.size());
    out += data;
    if (data.size() % 2) out += '\0';
    return out;
}

static void video(const FXString& filename)
{
    std::string header;
    number(header, 100000);
    number(header, 7680);
    number(header, 0);
    number(header, 0x10);
    number(header, 20);
    number(header, 0);
    number(header, 1);
    number(header, 768);
    number(header, 32);
    number(header, 8);
    header.append(16, '\0');
    std::string stream("vidsDIB ", 8);
    number(stream, 0);
    number(stream, 0, 2);
    number(stream, 0, 2);
    number(stream, 0);
    number(stream, 1);
    number(stream, 10);
    number(stream, 0);
    number(stream, 20);
    number(stream, 768);
    number(stream, 0xffffffff);
    number(stream, 0);
    stream.append(8, '\0');
    std::string bitmap;
    number(bitmap, 40);
    number(bitmap, 32);
    number(bitmap, 8);
    number(bitmap, 1, 2);
    number(bitmap, 24, 2);
    number(bitmap, 0);
    number(bitmap, 768);
    bitmap.append(16, '\0');
    std::string frames, index;
    for (int i = 0; i < 20; ++i)
    {
        index += "00db";
        number(index, 0x10);
        number(index, frames.size() + 4);
        number(index, 768);
        std::string pixels;
        for (int n = 0; n < 256; ++n) { pixels += char(30); pixels += char(80); pixels += char(180); }
        frames += chunk("00db", pixels);
    }
    std::string headers = chunk("avih", header) +
        chunk("LIST", std::string("strl") + chunk("strh", stream) + chunk("strf", bitmap));
    std::string avi = chunk("RIFF", std::string("AVI ") + chunk("LIST", std::string("hdrl") + headers) +
                             chunk("LIST", std::string("movi") + frames) + chunk("idx1", index));
    std::ofstream file(filename.text(), std::ios::binary);
    file.write(avi.data(), avi.size());
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    FXString directory = argv[1];
    FXApp app("SwordfishPreviewTest", "SwordfishPreviewTest");
    app.init(argc, argv);
    app.create();
    GdkPixbuf* image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, true, 8, 240, 60);
    gdk_pixbuf_fill(image, 0xd04060ff);
    const char* formats[] = {"png", "jpeg", "tiff", "bmp"};
    for (const char* format : formats)
        assert(gdk_pixbuf_save(image, (directory + "/image." + format).text(), format, NULL, NULL));
    uint8_t* encoded;
    size_t length = WebPEncodeRGBA(gdk_pixbuf_get_pixels(image), 240, 60,
                                  gdk_pixbuf_get_rowstride(image), 80, &encoded);
    assert(length > 0);
    std::ofstream webp((directory + "/image.webp").text(), std::ios::binary);
    webp.write(reinterpret_cast<char*>(encoded), length);
    webp.close();
    WebPFree(encoded);
    g_object_unref(image);
    std::ofstream((directory + "/image.svg").text()) <<
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"240\" height=\"60\">"
        "<rect width=\"240\" height=\"60\" fill=\"#d04060\"/></svg>";
    cairo_surface_t* pdf = cairo_pdf_surface_create((directory + "/document.pdf").text(), 240, 60);
    cairo_t* context = cairo_create(pdf);
    cairo_set_source_rgb(context, 0.8, 0.2, 0.3);
    cairo_paint(context);
    cairo_destroy(context);
    cairo_surface_destroy(pdf);
    video(directory + "/video.avi");
    std::ofstream((directory + "/broken.png").text()) << "\x89PNG\r\n\x1a\nbroken";
    std::ofstream((directory + "/plain.txt").text()) << "not an image";

    auto pump = [&](int milliseconds) {
        for (int n = 0; n < milliseconds / 5; ++n)
        {
            app.runWhileEvents();
            int status;
            pid_t child;
            while ((child = waitpid(-1, &status, WNOHANG)) > 0)
                assert(Preview::harvest(child, status));
            usleep(5000);
        }
    };
    auto ready = [&](const FXString& source, int size) {
        FXString result;
        for (int n = 0; n < 1000 && result.empty(); ++n)
        {
            result = Preview::request(&app, source, size, true);
            pump(5);
        }
        assert(!result.empty());
        GdkPixbuf* thumbnail = gdk_pixbuf_new_from_file(result.text(), NULL);
        assert(thumbnail);
        int width = gdk_pixbuf_get_width(thumbnail), height = gdk_pixbuf_get_height(thumbnail);
        assert(width <= size && height <= size && width > 0 && height > 0);
        assert(abs(width - height * 4) <= 4);
        g_object_unref(thumbnail);
        return result;
    };
    for (const char* name : {"image.png", "image.jpeg", "image.tiff", "image.bmp", "image.webp",
                             "image.svg", "document.pdf", "video.avi"})
    {
        FXString source = directory + "/" + name;
        FXlong start = FXThread::time();
        assert(Preview::request(&app, source, 96).empty());
        assert(FXThread::time() - start < 100000000);
        FXString result = ready(source, 96);
        FXulong revision = Preview::revision();
        assert(Preview::request(&app, source, 96) == result);
        assert(Preview::revision() == revision);
        printf("PASS: asynchronous %s decode, proportional fit, persistent cache\n", name);
    }
    FXString original = ready(directory + "/image.png", 96);
    assert(ready(directory + "/image.png", 128) != original);
    std::ofstream((directory + "/image.png").text(), std::ios::app) << "changed";
    assert(ready(directory + "/image.png", 96) != original);
    puts("PASS: cache invalidation for source changes and zoom sizes");
    FXulong previous = Preview::revision();
    Preview::request(&app, directory + "/broken.png", 96);
    for (int n = 0; n < 1000 && previous == Preview::revision(); ++n) pump(5);
    assert(previous != Preview::revision());
    assert(Preview::request(&app, directory + "/broken.png", 96).empty());
    assert(Preview::request(&app, directory + "/plain.txt", 96).empty());
    assert(Preview::request(&app, directory, 96).empty());
    puts("PASS: corrupt and unsupported files never yield placeholder previews");

    std::ofstream((directory + "/slow.png").text()) << "\x89PNG\r\n\x1a\nslow";
    previous = Preview::revision();
    FXlong timeoutStarted = FXThread::time();
    Preview::request(&app, directory + "/slow.png", 96);
    int iterations = 0;
    for (; iterations < 3000 && previous == Preview::revision(); ++iterations) pump(5);
    assert(previous != Preview::revision() && iterations > 10);
    assert(FXThread::time() - timeoutStarted >= 12 * 1000000000LL);
    assert(Preview::request(&app, directory + "/slow.png", 96).empty());
    puts("PASS: decoder time limit, responsive event loop and failed-output cleanup");
    Preview::shutdown();
    int status;
    assert(waitpid(-1, &status, WNOHANG) == -1);
    puts("PASS: centralized child harvesting and shutdown leave no zombies");
}
