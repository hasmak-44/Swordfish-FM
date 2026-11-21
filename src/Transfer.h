#ifndef TRANSFER_H
#define TRANSFER_H

#include <fx.h>
#include <vector>

struct TransferItem
{
    FXString source;
    FXString target;
    FXbool restore;

    TransferItem(const FXString& from, const FXString& to, FXbool restoring = false) :
        source(from), target(to), restore(restoring) {}
};

struct TransferSelection
{
    FXString directory;
    std::vector<FXString> sources;
};

typedef FXbool (*TransferStarter)(FXWindow*, FXuint, const std::vector<TransferItem>&);

void xf_setTransferStarter(TransferStarter);
FXbool xf_hasTransferStarter();
FXbool xf_startTransfer(FXWindow*, FXuint, const std::vector<TransferItem>&);
FXbool xf_startDroppedTransfer(FXWindow*, FXuint, const FXString&, const FXString&, FXbool);

class XFileExplorer;
class TransferManager;
TransferManager* xf_createTransferManager(XFileExplorer*);
void xf_destroyTransferManager(TransferManager*);
int xf_runTransferWorker(int request, int channel, char* executable);

#endif
