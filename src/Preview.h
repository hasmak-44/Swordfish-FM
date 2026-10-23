#ifndef PREVIEW_H
#define PREVIEW_H

#include <fx.h>
#include <sys/types.h>

namespace Preview
{
// Returns an empty path while queued or when the format cannot be previewed.
// waiting is set when an empty result may still turn into a preview later.
FXString request(FXApp* app, const FXString& path, FXint size, FXbool priority = false, FXbool* waiting = NULL);
FXString mimeType(const FXString& path);
FXbool textType(const FXString& mime);
FXulong revision();
FXbool pending();
FXbool harvest(pid_t pid, int status);
void shutdown();
}

#endif
