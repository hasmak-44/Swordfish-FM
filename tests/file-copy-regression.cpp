#include "config.h"

#include <fx.h>
#include "i18n.h"
#include "xfedefs.h"
#include "File.h"
#include "xfeutils.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static File* activeCopy = NULL;
static FXString watchedTarget;
static std::string expectedTarget;
static FXbool partSeen = false;
static FXbool cancelAtPart = false;

static void writeFile(const FXString& path, const std::string& data)
{
    std::ofstream file(path.text(), std::ios::binary | std::ios::trunc);
    assert(file);
    file.write(data.data(), data.size());
    file.close();
    assert(file);
}

static std::string readFile(const FXString& path)
{
    std::ifstream file(path.text(), std::ios::binary);
    assert(file);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static void observeCopy(void*, const FXString& changed)
{
    FXString part = watchedTarget + ".part";
    if (changed != part || access(part.text(), F_OK) != 0) return;

    partSeen = true;
    if (!expectedTarget.empty()) assert(readFile(watchedTarget) == expectedTarget);
    if (cancelAtPart)
    {
        cancelAtPart = false;
        activeCopy->onCmdCancel(NULL, 0, NULL);
    }
}

static int copyFile(FXApp* app, const FXString& source, const FXString& target,
                    FXulong size, FXbool cancel)
{
    File transfer(app, _("Copy regression"), COPY);
    transfer.create();
    activeCopy = &transfer;
    transfer.setChangeCallback(observeCopy, NULL);
    watchedTarget = target;
    partSeen = false;
    cancelAtPart = cancel;
    return transfer.copy(source, target, "", size, xf_getcurrenttime(), false, false);
}

int main(int argc, char* argv[])
{
    FXApp app("SwordfishCopyRegression", "Swordfish");
    app.init(argc, argv);
    app.create();

    char directory[] = "/tmp/swordfish-file-copy-XXXXXX";
    assert(mkdtemp(directory));
    FXString root(directory);
    FXString source = root + "/source.bin";
    FXString target = root + "/destination.bin";
    FXString part = target + ".part";

    const std::string original = "keep this destination until the replacement is complete";
    const std::string replacement(4 * 1024 * 1024, 'R');
    writeFile(source, replacement);

    expectedTarget.clear();
    assert(copyFile(&app, source, target, replacement.size(), false) == true);
    assert(partSeen);
    assert(readFile(target) == replacement);
    assert(access(part.text(), F_OK) != 0);

    writeFile(target, original);
    expectedTarget = original;
    assert(copyFile(&app, source, target, replacement.size(), false) == true);
    assert(partSeen);
    assert(readFile(target) == replacement);
    assert(access(part.text(), F_OK) != 0);

    writeFile(target, original);
    expectedTarget = original;
    assert(copyFile(&app, source, target, replacement.size(), true) == false);
    assert(partSeen);
    assert(readFile(target) == original);
    assert(access(part.text(), F_OK) != 0);

    FXString oldTarget = root + "/old-target";
    writeFile(oldTarget, original);
    assert(unlink(target.text()) == 0);
    assert(symlink(oldTarget.text(), target.text()) == 0);
    expectedTarget = original;
    assert(copyFile(&app, source, target, replacement.size(), true) == false);
    struct stat targetInfo;
    assert(partSeen && lstat(target.text(), &targetInfo) == 0 && S_ISLNK(targetInfo.st_mode));
    assert(readFile(target) == original);
    assert(access(part.text(), F_OK) != 0);

    unlink(source.text());
    unlink(target.text());
    unlink(oldTarget.text());
    rmdir(root.text());
    return 0;
}
