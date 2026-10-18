#ifndef REMOTEDIRECTORY_H
#define REMOTEDIRECTORY_H

#include <fx.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

namespace RemoteDirectory
{
struct Entry
{
    FXString name, link;
    struct stat info;
    FXbool symlink = false;
    int readable = -1, executable = -1;
    FXulong revision = 0;
};

FXbool handles(const FXString& path);
FXbool isServer(const FXString& path);
const std::vector<Entry>& entries(FXApp*, const FXString&, FXObject*, FXSelector);
FXbool loading(const FXString&);
FXbool loading(FXObject*);
FXString error(const FXString&);
void refresh(const FXString&);
void release(FXObject*, const FXString& subtree = FXString::null);
FXbool harvest(pid_t, int);
}
#endif
