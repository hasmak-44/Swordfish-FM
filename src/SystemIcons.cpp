#include "config.h"
#include "i18n.h"
#include <fx.h>
#include <FXPNGIcon.h>
#include <string>
#include <dirent.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include "xfeutils.h"
#include "AppLogger.h"
#include "SystemIcons.h"

extern char** environ;
extern FXString execpath;

namespace
{
    struct IconFile
    {
        int size;       // 0 for scalable
        bool svg;
        FXString path;
    };

    typedef std::map<std::string, std::vector<IconFile> > ThemeIndex;

    const char* const SUFFIX = "-system";

    void addRoot(std::vector<FXString>& roots, const FXString& dir)
    {
        if (dir.empty() || !xf_isdirectory(dir))
        {
            return;
        }
        FXString real = xf_realpath(dir);
        if (std::find(roots.begin(), roots.end(), real) == roots.end())
        {
            roots.push_back(real);
        }
    }

    // Folders that can contain icon themes
    std::vector<FXString> themeRoots()
    {
        std::vector<FXString> roots;
        FXString home = FXSystem::getHomeDirectory();

        const char* hostdata = getenv("SWORDFISH_HOST_XDG_DATA_HOME");
        if (!hostdata || !*hostdata)
        {
            hostdata = getenv("XDG_DATA_HOME");
        }
        if (hostdata && *hostdata)
        {
            addRoot(roots, FXString(hostdata) + "/icons");
        }
        addRoot(roots, home + "/.local/share/icons");
        addRoot(roots, home + "/.icons");

        const char* dirs = getenv("SWORDFISH_HOST_XDG_DATA_DIRS");
        if (!dirs || !*dirs)
        {
            dirs = getenv("XDG_DATA_DIRS");
        }
        FXString list = (dirs && *dirs) ? dirs : "/usr/local/share:/usr/share";
        for (int i = 0; i < list.contains(':') + 1; i++)
        {
            addRoot(roots, list.section(':', i) + "/icons");
        }
        addRoot(roots, "/usr/local/share/icons");
        addRoot(roots, "/usr/share/icons");
        return roots;
    }

    // Read a key from the [Icon Theme] section of an index.theme file
    FXString themeKey(const FXString& indexfile, const char* key)
    {
        FXString value;
        FILE* fp = fopen(indexfile.text(), "r");
        if (!fp)
        {
            return value;
        }
        char line[4096];
        FXString prefix = FXString(key) + "=";
        while (fgets(line, sizeof(line), fp))
        {
            if (line[0] == '[' && !FXString(line).contains("[Icon Theme]"))
            {
                break;
            }
            if (strncmp(line, prefix.text(), prefix.length()) == 0)
            {
                value = FXString(line + prefix.length()).trim();
                break;
            }
        }
        fclose(fp);
        return value;
    }

    // Size encoded in a directory name, -1 if none, 0 for scalable
    int sizeFromComponent(const char* name)
    {
        if (strcmp(name, "scalable") == 0)
        {
            return 0;
        }
        if (strchr(name, '@'))
        {
            return -2;
        }
        int w = 0, h = 0;
        if (sscanf(name, "%dx%d", &w, &h) == 2 && w == h)
        {
            return w;
        }
        char* end = NULL;
        long n = strtol(name, &end, 10);
        if (end && *end == '\0' && n > 0)
        {
            return (int)n;
        }
        return -1;
    }

    // Walk a theme and record the wanted icon names
    void indexTheme(const FXString& dir, int size, bool insize, const std::set<std::string>& wanted, ThemeIndex& index, int depth)
    {
        if (depth > 4)
        {
            return;
        }
        DIR* d = opendir(dir.text());
        if (!d)
        {
            return;
        }
        struct dirent* entry;
        while ((entry = readdir(d)) != NULL)
        {
            const char* name = entry->d_name;
            if (name[0] == '.')
            {
                continue;
            }
            FXString path = dir + "/" + name;
            bool isdir = (entry->d_type == DT_DIR);
            if (entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK)
            {
                isdir = xf_isdirectory(path);
            }
            if (isdir)
            {
                if (strstr(name, "symbolic") || strcmp(name, "cursors") == 0)
                {
                    continue;
                }
                int s = sizeFromComponent(name);
                if (s == -2 || s > 128)
                {
                    continue;
                }
                if (s >= 0)
                {
                    indexTheme(path, s, true, wanted, index, depth + 1);
                }
                else
                {
                    indexTheme(path, size, insize, wanted, index, depth + 1);
                }
                continue;
            }
            if (!insize)
            {
                continue;
            }
            const char* dot = strrchr(name, '.');
            if (!dot || (strcmp(dot, ".png") != 0 && strcmp(dot, ".svg") != 0))
            {
                continue;
            }
            std::string base(name, dot - name);
            if (wanted.find(base) == wanted.end())
            {
                continue;
            }
            IconFile file;
            file.size = size;
            file.svg = (strcmp(dot, ".svg") == 0);
            if (file.svg)
            {
                file.size = 0;
            }
            file.path = path;
            index[base].push_back(file);
        }
        closedir(d);
    }

    // Theme folder by id in any root
    FXString locateTheme(const std::vector<FXString>& roots, const FXString& id)
    {
        for (size_t i = 0; i < roots.size(); i++)
        {
            FXString dir = roots[i] + "/" + id;
            if (xf_existfile(dir + "/index.theme"))
            {
                return dir;
            }
        }
        return FXString();
    }

    // The theme followed by everything it inherits
    void inheritanceChain(const std::vector<FXString>& roots, const FXString& id, std::vector<FXString>& chain)
    {
        if (std::find(chain.begin(), chain.end(), id) != chain.end() || chain.size() > 12)
        {
            return;
        }
        FXString dir = locateTheme(roots, id);
        if (dir.empty())
        {
            return;
        }
        chain.push_back(id);
        FXString parents = themeKey(dir + "/index.theme", "Inherits");
        for (int i = 0; i < parents.contains(',') + 1; i++)
        {
            FXString parent = parents.section(',', i).trim();
            if (!parent.empty())
            {
                inheritanceChain(roots, parent, chain);
            }
        }
    }

    // Pick the best file of one icon for a target size, svg = rendered
    const IconFile* pickFile(const std::vector<IconFile>& files, int side)
    {
        const IconFile* exact = NULL;
        const IconFile* svg = NULL;
        const IconFile* larger = NULL;
        const IconFile* smaller = NULL;
        for (size_t i = 0; i < files.size(); i++)
        {
            const IconFile& f = files[i];
            if (f.svg)
            {
                if (!svg)
                {
                    svg = &f;
                }
            }
            else if (f.size == side)
            {
                exact = &f;
            }
            else if (f.size > side)
            {
                if (!larger || f.size < larger->size)
                {
                    larger = &f;
                }
            }
            else if (!smaller || f.size > smaller->size)
            {
                smaller = &f;
            }
        }
        if (exact)
        {
            return exact;
        }
        if (svg)
        {
            return svg;
        }
        return larger ? larger : smaller;
    }

    // Width and height of a PNG file
    bool pngSize(const FXString& path, int& w, int& h)
    {
        FILE* fp = fopen(path.text(), "rb");
        if (!fp)
        {
            return false;
        }
        unsigned char head[24];
        size_t n = fread(head, 1, sizeof(head), fp);
        fclose(fp);
        if (n != sizeof(head) || memcmp(head + 1, "PNG", 3) != 0)
        {
            return false;
        }
        w = (head[16] << 24) | (head[17] << 16) | (head[18] << 8) | head[19];
        h = (head[20] << 24) | (head[21] << 16) | (head[22] << 8) | head[23];
        return w > 0 && h > 0;
    }

    bool runConverter(const FXString& converter, const FXString& svg, const FXString& out, int side)
    {
        FXString size = FXStringVal(side);
        char* argv[] = {(char*)converter.text(), (char*)"-w", (char*)size.text(), (char*)"-h", (char*)size.text(),
                        (char*)"-o", (char*)out.text(), (char*)svg.text(), NULL};
        pid_t pid;
        if (posix_spawn(&pid, converter.text(), NULL, NULL, argv, environ) != 0)
        {
            return false;
        }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0)
        {
            return false;
        }
        return WIFEXITED(status) && WEXITSTATUS(status) == 0 && xf_existfile(out);
    }

    // Copy a PNG, resizing it when it does not already have the target size
    bool resizePng(FXApp* app, const FXString& src, const FXString& out, int side, int srcsize)
    {
        if (srcsize == side)
        {
            return FXFile::copy(src, out, true);
        }
        FXPNGIcon icon(app);
        FXFileStream in;
        if (!in.open(src, FXStreamLoad))
        {
            return false;
        }
        bool ok = icon.loadPixels(in);
        in.close();
        if (!ok)
        {
            return false;
        }
        icon.scale(side, side, 1);
        FXFileStream outstream;
        if (!outstream.open(out, FXStreamSave))
        {
            return false;
        }
        ok = icon.savePixels(outstream);
        outstream.close();
        return ok;
    }
}


const char* systemThemeSuffix()
{
    return SUFFIX;
}


void scanSystemThemes(std::vector<SystemTheme>& themes)
{
    std::vector<FXString> roots = themeRoots();
    std::set<std::string> seen;
    for (size_t r = 0; r < roots.size(); r++)
    {
        DIR* d = opendir(roots[r].text());
        if (!d)
        {
            continue;
        }
        std::vector<SystemTheme> found;
        struct dirent* entry;
        while ((entry = readdir(d)) != NULL)
        {
            if (entry->d_name[0] == '.' || seen.count(entry->d_name))
            {
                continue;
            }
            FXString id = entry->d_name;
            if (id == "hicolor" || id == "locolor" || id == "default")
            {
                continue;
            }
            FXString index = roots[r] + "/" + id + "/index.theme";
            if (!xf_existfile(index) || themeKey(index, "Hidden") == "true" || themeKey(index, "Directories").empty())
            {
                continue;
            }
            SystemTheme theme;
            theme.id = id;
            theme.name = themeKey(index, "Name");
            if (theme.name.empty())
            {
                theme.name = id;
            }
            theme.dir = roots[r] + "/" + id;
            seen.insert(entry->d_name);
            found.push_back(theme);
        }
        closedir(d);
        themes.insert(themes.end(), found.begin(), found.end());
    }
    std::sort(themes.begin(), themes.end(), [](const SystemTheme& a, const SystemTheme& b)
              { return FXString(a.name).lower() < FXString(b.name).lower(); });
}


FXString systemThemeFolder(const FXString& id)
{
    FXString base;
    const char* portabledata = getenv("SWORDFISH_PORTABLE_DATA");
    if (portabledata && *portabledata)
    {
        base = FXString(portabledata) + "/icons";
    }
    else
    {
        const char* config = getenv("XDG_CONFIG_HOME");
        base = ((config && *config) ? FXString(config) : FXSystem::getHomeDirectory() + "/.config") + "/swordfish/icons";
    }
    return base + "/" + id + SUFFIX;
}


FXString findIconConverter()
{
    FXString local = FXPath::directory(execpath) + "/rsvg-convert";
    if (xf_existfile(local))
    {
        return local;
    }
    const char* path = getenv("PATH");
    FXString list = path ? path : "";
    for (int i = 0; i < list.contains(':') + 1; i++)
    {
        FXString candidate = list.section(':', i) + "/rsvg-convert";
        if (!list.section(':', i).empty() && xf_existfile(candidate))
        {
            return candidate;
        }
    }
    return FXString();
}


int convertSystemTheme(FXApp* app, const SystemTheme& theme, const FXString& mapfile,
                       const FXString& defaultdir, const FXString& dest, FXString& error,
                       SystemIconProgress progress, void* userdata)
{
    // Parse the mapping file: swordfish name = freedesktop name, alternatives...
    std::vector<std::pair<std::string, std::vector<std::string> > > mapping;
    std::set<std::string> wanted;
    FILE* fp = fopen(mapfile.text(), "r");
    if (!fp)
    {
        error = "Icon mapping file not found: " + mapfile;
        return -1;
    }
    char line[4096];
    while (fgets(line, sizeof(line), fp))
    {
        FXString text = FXString(line).trim();
        if (text.empty() || text[0] == '#' || !text.contains('='))
        {
            continue;
        }
        std::string name = text.before('=').trim().text();
        FXString alts = text.after('=');
        std::vector<std::string> names;
        for (int i = 0; i < alts.contains(',') + 1; i++)
        {
            FXString alt = alts.section(',', i).trim();
            if (!alt.empty())
            {
                names.push_back(alt.text());
                wanted.insert(alt.text());
            }
        }
        mapping.push_back(std::make_pair(name, names));
    }
    fclose(fp);

    // Index the theme and what it inherits
    std::vector<FXString> roots = themeRoots();
    std::vector<FXString> chain;
    inheritanceChain(roots, theme.id, chain);
    if (chain.empty())
    {
        error = "Icon theme not found: " + theme.id;
        return -1;
    }
    std::vector<ThemeIndex> indexes(chain.size());
    for (size_t i = 0; i < chain.size(); i++)
    {
        indexTheme(locateTheme(roots, chain[i]), -1, false, wanted, indexes[i], 0);
    }

    FXString converter = findIconConverter();

    // Build in a temporary folder and rename when complete
    FXString tmp = dest + ".tmp";
    FXFile::removeFiles(tmp, true);
    FXString parent = FXPath::directory(dest);
    if (!xf_isdirectory(parent) && !FXDir::create(parent))
    {
        error = "Cannot create folder: " + parent;
        return -1;
    }
    if (!FXDir::create(tmp))
    {
        error = "Cannot create folder: " + tmp;
        return -1;
    }

    // Start with a complete copy of the default theme
    DIR* d = opendir(defaultdir.text());
    if (!d)
    {
        error = "Default icons not found: " + defaultdir;
        FXFile::removeFiles(tmp, true);
        return -1;
    }
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL)
    {
        const char* dot = strrchr(entry->d_name, '.');
        if (dot && strcmp(dot, ".png") == 0)
        {
            FXFile::copy(defaultdir + "/" + entry->d_name, tmp + "/" + entry->d_name, true);
        }
    }
    closedir(d);

    int converted = 0, failedsvg = 0;
    int total = (int)mapping.size();
    for (int m = 0; m < total; m++)
    {
        const std::string& name = mapping[m].first;
        const std::vector<std::string>& alts = mapping[m].second;
        FXString reference = defaultdir + "/" + name.c_str() + ".png";
        int w, h;
        if (progress && (m % 3) == 0)
        {
            progress(m, total, userdata);
        }
        if (!pngSize(reference, w, h))
        {
            continue;
        }
        int side = FXMIN(w, h);
        FXString out = tmp + "/" + name.c_str() + ".png";
        bool done = false;
        for (size_t a = 0; a < alts.size() && !done; a++)
        {
            for (size_t t = 0; t < indexes.size() && !done; t++)
            {
                ThemeIndex::const_iterator it = indexes[t].find(alts[a]);
                if (it == indexes[t].end())
                {
                    continue;
                }
                const IconFile* file = pickFile(it->second, side);
                if (!file)
                {
                    continue;
                }
                if (file->svg)
                {
                    if (converter.empty())
                    {
                        failedsvg++;
                        continue;
                    }
                    done = runConverter(converter, file->path, out, side);
                }
                else
                {
                    done = resizePng(app, file->path, out, side, file->size);
                }
            }
        }
        if (done)
        {
            converted++;
        }
        else if (!xf_existfile(out))
        {
            FXFile::copy(reference, out, true);
        }
    }
    if (progress)
    {
        progress(total, total, userdata);
    }

    if (converted == 0)
    {
        error = converter.empty() && failedsvg > 0 ? "The SVG converter (rsvg-convert) is not available" : "No matching icons were found in this theme";
        FXFile::removeFiles(tmp, true);
        return -1;
    }

    FXFile::removeFiles(dest, true);
    if (!FXDir::rename(tmp, dest))
    {
        error = "Cannot store the converted theme in " + dest;
        FXFile::removeFiles(tmp, true);
        return -1;
    }
    appLog(APP_LOG_SETTINGS, "system-icons-converted",
           "theme=\"" + theme.id + "\" icons=" + FXStringVal(converted) + "/" + FXStringVal(total) + " dest=\"" + dest + "\"");
    return converted;
}
