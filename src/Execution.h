#ifndef EXECUTION_H
#define EXECUTION_H

#include <fx.h>
#include <stdio.h>
#include <string.h>
#include "xfeutils.h"

enum ExecutionKind
{
    EXECUTION_DOCUMENT,
    EXECUTION_PROGRAM,
    EXECUTION_SCRIPT
};

// Keep classification available to all applications using FileDict.
static inline ExecutionKind xf_executionKind(const FXString& path)
{
    if (!xf_isfile(path))
    {
        return EXECUTION_DOCUMENT;
    }
    FILE* file = fopen(path.text(), "rb");
    unsigned char header[256] = {0};
    size_t size = file ? fread(header, 1, sizeof(header), file) : 0;
    if (file)
    {
        fclose(file);
    }
    FXString ext = FXPath::extension(path).lower();
    FXbool pe = false;
    if (size >= 64 && header[0] == 'M' && header[1] == 'Z')
    {
        unsigned long offset = (unsigned long)header[60] | ((unsigned long)header[61] << 8) |
                               ((unsigned long)header[62] << 16) | ((unsigned long)header[63] << 24);
        file = fopen(path.text(), "rb");
        unsigned char signature[4];
        pe = file && fseek(file, offset, SEEK_SET) == 0 &&
             fread(signature, 1, 4, file) == 4 && memcmp(signature, "PE\000\000", 4) == 0;
        if (file)
        {
            fclose(file);
        }
    }
    // Windows Installer packages are OLE compound files; other OLE files are documents.
    FXbool msi = ext == "msi" && size >= 8 &&
                 memcmp(header, "\320\317\021\340\241\261\032\341", 8) == 0;
    if ((size >= 4 && memcmp(header, "\177ELF", 4) == 0) || pe || msi)
    {
        return EXECUTION_PROGRAM;
    }
    if ((size >= 2 && header[0] == '#' && header[1] == '!') ||
        ext == "sh" || ext == "bash" || ext == "zsh" || ext == "ksh" ||
        ext == "fish" || ext == "csh" || ext == "tcsh" || ext == "py" ||
        ext == "pyw" || ext == "pl" || ext == "rb" || ext == "lua" ||
        ext == "php" || ext == "js" || ext == "tcl")
    {
        return EXECUTION_SCRIPT;
    }
    if (ext == "jar" && size >= 4 && memcmp(header, "PK\003\004", 4) == 0)
    {
        // A library JAR is a document, not an application.
        FXString manifest = xf_getcommandoutput("unzip -p " + xf_quote(path) +
                                               " META-INF/MANIFEST.MF 2>/dev/null");
        manifest.lower();
        if (manifest.left(11) == "main-class:" || manifest.contains("\nmain-class:"))
        {
            return EXECUTION_PROGRAM;
        }
    }
    return EXECUTION_DOCUMENT;
}

// Run as Administrator is offered only for one selected program or script.
static inline FXbool xf_administratorActionAvailable(FXint selected, const FXString& path)
{
    return selected == 1 && xf_executionKind(path) != EXECUTION_DOCUMENT;
}

enum ExecutionOrigin
{
    EXECUTION_OPEN,
    EXECUTION_EXPLICIT,
    EXECUTION_ADMINISTRATOR
};

// Returns false when Open With is needed for a missing preferred editor.
FXbool xf_executeFile(FXWindow* owner, const FXString& path, const FXString& directory,
                      const FXString& startDirectory, ExecutionOrigin origin = EXECUTION_OPEN,
                      FXbool terminal = false);

FXbool xf_editFiles(FXWindow* owner, const vector_FXString& paths, const FXString& directory,
                    const FXString& startDirectory, FXuint openWithId,
                    FXObject* preferencesTarget, FXuint preferencesId);

// Returns false when the launch was cancelled or handled here (Open With, Settings, system default once).
FXbool xf_prepareAssociation(FXWindow* owner, const FXString& command, const FXString& path,
                             FXuint openWithId, FXObject* preferencesTarget, FXuint preferencesId);

#endif
