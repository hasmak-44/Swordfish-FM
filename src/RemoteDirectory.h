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
// Discovered computer names, so tabs and titles can show them instead of an address
void setHostName(const FXString& host, const FXString& name);
FXString hostName(const FXString& host);
FXString displayPath(const FXString& path);
// GVFS mount name as share@computer, user@host or device model (empty if not a GVFS mount)
FXString mountLabel(const FXString& gvfsname);
FXString titlePath(const FXString& path);
// UPnP/DLNA media servers browsed read-only as dlna://HOST:PORT/Folder/Sub (segments URI-escaped)
FXbool isMedia(const FXString& path);
// Remembers a discovered server's description URL; returns its dlna://HOST:PORT root or "" if invalid
FXString addMediaServer(const FXString& location, const FXString& name);
FXString mediaRoot(const FXString& location);
FXString mediaServerLocation(const FXString& path);
FXString mediaParent(const FXString& path);
// Stream address of a file listed in an open media folder ("" if unknown)
FXString mediaLink(const FXString& path);
FXString mediaLabel(const FXString& path);
// Background copy of a media stream with a progress window; failures are reported when the queue is empty
void download(FXWindow* owner, const FXString& url, const FXString& destination);
FXuint downloadsPending();
// Stops the current download (removing its unfinished file) and drops the rest of the queue
void cancelDownloads();
}
#endif
