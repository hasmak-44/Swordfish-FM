#include "CustomActions.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <fstream>
#include <sys/stat.h>
#include <cstdlib>

static CustomAction action(const char* id, const char* parent = "")
{
    CustomAction a;
    a.id = id; a.parent = parent; a.name = FXString("Action ") + id;
    a.command = "printf '%%s\\n' %F"; a.toolbar = true;
    return a;
}

static std::string shell(const FXString& command)
{
    int pipefd[2];
    assert(pipe(pipefd) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (!pid)
    {
        close(pipefd[0]);
        assert(dup2(pipefd[1], STDOUT_FILENO) >= 0);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", command.text(), (char*)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    std::string result;
    char buffer[4096];
    ssize_t count;
    while ((count = read(pipefd[0], buffer, sizeof(buffer))) > 0) { result.append(buffer, count); }
    close(pipefd[0]);
    int status;
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return result;
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    FXString error, expanded;
    {
        CustomActions clones;
        CustomAction folder = action("parent"); folder.kind = CustomAction::FOLDER;
        clones.items.push_back(folder);
        CustomAction source = action("source", "parent");
        source.description = "all fields"; source.icon = "theme:installed-application";
        source.programMode = true; source.program = "/usr/bin/printf"; source.parameters = "%F %c";
        source.workingDirectory = "%o"; source.patterns = "*.txt;*.md";
        source.files = false; source.folders = true; source.single = false; source.multiple = true;
        source.background = true; source.terminal = true; source.confirm = true; source.showOutput = true;
        clones.items.push_back(source); clones.items.push_back(action("after", "parent"));
        FXString id = clones.duplicate("source", " (copy)", error);
        assert(!id.empty() && id != source.id && clones.items.size() == 4);
        assert(clones.items[2].id == id && clones.items[3].id == "after");
        CustomAction expected = source; expected.id = id; expected.name += " (copy)";
        assert(clones.items[2] == expected);
        FXSettings saved; clones.save(saved); CustomActions reloaded;
        assert(reloaded.load(saved, error) && reloaded.items == clones.items);
        assert(clones.duplicate("parent", " (copy)", error).empty() && clones.items.size() == 4);
        clones.items[2].icon.clear(); assert(!(clones.items[2] == expected));
        clones.items[2] = expected;
        clones.reorder(id, -1); assert(clones.items[1].id == id);
    }
    puts("PASS: duplicate action clones every field beside fresh ID/name, keeps parent/order and survives save/reload");
    CustomActions model;
    CustomAction folder = action("folder");
    folder.kind = CustomAction::FOLDER; folder.icon = "/icon with space.png";
    folder.description = "Folder description\nsecond line";
    model.items.push_back(folder);
    CustomAction nested = folder;
    nested.id = "nested"; nested.parent = "folder";
    model.items.push_back(nested);
    CustomAction first = action("first", "nested");
    first.command = "printf '%%s\\n' %F %p %o"; first.patterns = "*.txt;*.md";
    first.confirm = true; first.terminal = true; first.showOutput = true;
    first.program = "stored but inactive program"; first.parameters = "stored inactive parameters";
    first.workingDirectory = "%p"; first.showOutput = true;
    model.items.push_back(first);
    CustomAction separator = action("separator");
    separator.kind = CustomAction::SEPARATOR;
    model.items.push_back(separator);
    model.items.push_back(action("last"));
    assert(model.validate(error));

    FXSettings settings;
    settings.writeStringEntry("OPTIONS", "unrelated", "preserved");
    model.save(settings);
    assert(settings.unparseFile(argv[1]));
    FXSettings disk;
    assert(disk.parseFile(argv[1], true));
    CustomActions loaded;
    assert(loaded.load(disk, error) && loaded.items.size() == model.items.size());
    for (unsigned i = 0; i < loaded.items.size(); ++i)
    {
        const CustomAction& a = model.items[i];
        const CustomAction& b = loaded.items[i];
        assert(a.id == b.id && a.parent == b.parent && a.name == b.name && a.description == b.description);
        assert(a.icon == b.icon && a.command == b.command && a.patterns == b.patterns && a.kind == b.kind);
        assert(a.program == b.program && a.parameters == b.parameters &&
               a.workingDirectory == b.workingDirectory && a.programMode == b.programMode);
        assert(a.toolbar == b.toolbar && a.files == b.files && a.folders == b.folders);
        assert(a.single == b.single && a.multiple == b.multiple && a.background == b.background);
        assert(a.terminal == b.terminal && a.confirm == b.confirm && a.showOutput == b.showOutput);
    }
    assert(FXString(disk.readStringEntry("OPTIONS", "unrelated", "")) == "preserved");
    loaded.remove("folder"); loaded.save(disk);
    assert(!disk.existingEntry("CUSTOM ACTIONS", "item4.id"));
    assert(loaded.load(disk, error) && loaded.items.size() == 2);
    puts("PASS: file-backed settings round trip, all fields/stable IDs/order, unrelated settings, stale entries removed");
    FXSettings legacy;
    model.save(legacy);
    legacy.deleteEntry("CUSTOM ACTIONS", "item2.showOutput");
    assert(loaded.load(legacy, error) && !loaded.items[2].showOutput);
    loaded.remove("folder");

    FXSettings bad;
    model.save(bad);
    bad.writeIntEntry("CUSTOM ACTIONS", "count", 257);
    assert(!loaded.load(bad, error) && loaded.items.size() == 2);
    model.save(bad); bad.writeStringEntry("CUSTOM ACTIONS", "item0.id", "INVALID ID");
    assert(!loaded.load(bad, error));
    model.save(bad); bad.writeIntEntry("CUSTOM ACTIONS", "item1.toolbar", 2);
    assert(!loaded.load(bad, error));
    model.save(bad); bad.writeIntEntry("CUSTOM ACTIONS", "version", 99);
    assert(!loaded.load(bad, error));
    model.save(bad); bad.writeStringEntry("CUSTOM ACTIONS", "count", "5garbage");
    assert(!loaded.load(bad, error));
    model.save(bad); bad.writeStringEntry("CUSTOM ACTIONS", "item0.kind", "0junk");
    assert(!loaded.load(bad, error));
    model.save(bad); bad.writeStringEntry("CUSTOM ACTIONS", "item0.toolbar", "1junk");
    assert(!loaded.load(bad, error));
    CustomActions invalid = model;
    invalid.items[1].id = "folder"; assert(!invalid.validate(error));
    invalid = model; invalid.items[0].parent = "nested"; assert(!invalid.validate(error));
    invalid = model; invalid.items[0].parent = "missing"; assert(!invalid.validate(error));
    invalid = model; invalid.items[0].parent = "last"; assert(!invalid.validate(error));
    invalid = model; invalid.items[2].name = "bad\tmenu"; assert(!invalid.validate(error));
    invalid = model; invalid.items[2].name = "   "; assert(!invalid.validate(error));
    invalid = model; invalid.items[2].patterns = "; ;"; assert(!invalid.validate(error));
    invalid = model; invalid.items[2].command = "   "; assert(!invalid.validate(error));
    invalid = model; invalid.items[2].command = "echo %z"; assert(!invalid.validate(error));
    invalid = model; invalid.items[2].files = false; invalid.items[2].folders = false;
    assert(!invalid.validate(error));
    invalid.items.clear();
    for (unsigned n = 0; n < 18; ++n)
    {
        CustomAction a = folder;
        a.id.format("depth-%u", n);
        if (n) { a.parent.format("depth-%u", n - 1); }
        invalid.items.push_back(a);
    }
    assert(!invalid.validate(error));
    puts("PASS: invalid schema/count/booleans/IDs/parents/types/commands/conditions rejected without replacing live model");

    loaded = model;
    assert(!loaded.move("folder", "nested", error));
    assert(!loaded.move("folder", "folder", error));
    assert(!loaded.move("first", "last", error));
    assert(loaded.move("first", "", error) && loaded.find("first")->parent.empty());
    loaded.reorder("first", -1);
    std::vector<unsigned> roots = loaded.children("");
    assert(loaded.items[roots[roots.size() - 2]].id == "first");
    loaded.remove("folder"); assert(!loaded.find("nested") && loaded.find("first"));
    std::vector<unsigned> exposed = model.toolbarItems();
    assert(exposed.size() == 3 && model.items[exposed[0]].id == "folder" &&
           model.items[exposed[1]].kind == CustomAction::SEPARATOR && model.items[exposed[2]].id == "last");
    model.items[0].toolbar = false;
    exposed = model.toolbarItems();
    assert(model.items[exposed[0]].id == "nested");
    model.items[1].toolbar = false;
    exposed = model.toolbarItems();
    assert(model.items[exposed[0]].id == "first");
    assert(model.newId() != model.newId());
    puts("PASS: move/reorder/remove, recursive deletion, cycle prevention, toolbar folder exposure and no duplicates");

    CustomActionContext ctx;
    ctx.directory = "/work/it's $HOME; `touch bad`";
    ctx.opposite = "/other/\"$(touch bad)\"";
    const char* tricky[] = {
        "/work/a space.txt", "/work/a'quote.txt", "/work/\"; echo BAD; #.txt",
        "/work/$(echo BAD)`echo BAD`$HOME\\end.txt", "/work/line\nbreak.txt", "/work/ünicode.txt"
    };
    std::string expected;
    for (unsigned i = 0; i < sizeof(tricky) / sizeof(tricky[0]); ++i)
    {
        ctx.paths.push_back(tricky[i]); ctx.directories.push_back(false);
        expected += tricky[i]; expected += '\0';
    }
    assert(CustomActions::expand("printf '%%s\\0' %F", ctx, expanded, error));
    assert(shell(expanded) == expected);
    expected = ctx.paths[1].text(); expected += '\0';
    CustomActionContext one = ctx;
    one.paths.assign(1, ctx.paths[1]); one.directories.assign(1, false);
    const char* commands[] = { "printf '%%s\\0' %f", "printf '%%s\\0' \"%f\"", "printf '%%s\\0' '%f'" };
    for (unsigned i = 0; i < 3; ++i)
    {
        assert(CustomActions::expand(commands[i], one, expanded, error));
        assert(shell(expanded) == expected);
    }
    for (unsigned n = 0; n < ctx.paths.size(); ++n)
    {
        one.paths.assign(1, ctx.paths[n]);
        expected = ctx.paths[n].text(); expected += '\0';
        for (unsigned i = 0; i < 3; ++i)
        {
            assert(CustomActions::expand(commands[i], one, expanded, error));
            assert(shell(expanded) == expected);
        }
        assert(CustomActions::expand("printf '%%s' \"prefix%f:suffix\"", one, expanded, error));
        assert(shell(expanded) == std::string("prefix") + ctx.paths[n].text() + ":suffix");
    }
    one.paths.assign(1, ctx.paths[1]);
    assert(CustomActions::expand("printf '%%s\\n' %D", ctx, expanded, error));
    assert(shell(expanded) == "/work\n");
    assert(CustomActions::expand("printf '%%s\\0' %p %o", ctx, expanded, error));
    expected = ctx.directory.text(); expected += '\0'; expected += ctx.opposite.text(); expected += '\0';
    assert(shell(expanded) == expected);
    assert(CustomActions::expand("printf '%%s\\n' %n %d %u %%", one, expanded, error));
    assert(shell(expanded) == "a'quote.txt\n/work\nfile:///work/a%27quote.txt\n%\n");
    const char* refused[] = { "echo \"%F\"", "echo prefix%F", "echo %F.suffix", "echo %z", "echo %",
        "echo 'unclosed", "echo `echo %f`", "echo $(echo %f)", "echo ${x:-%f}", "cat <<EOF\n%f\nEOF",
        "echo # '\necho %f", "echo \\" };
    for (unsigned i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
    { assert(!CustomActions::expand(refused[i], one, expanded, error)); }
    puts("PASS: real /bin/sh preserves hostile filenames in unquoted/single/double-quoted placeholders and lists; ambiguous syntax refused");

    CustomAction match = action("match");
    match.patterns = "*.txt;*.md"; match.folders = false;
    assert(model.matches(match, ctx));
    ctx.paths.push_back("/work/not-matched.png"); ctx.directories.push_back(false);
    assert(!model.matches(match, ctx)); ctx.paths.pop_back(); ctx.directories.pop_back();
    match.multiple = false; assert(!model.matches(match, ctx));
    assert(model.matches(match, one));
    match.single = false; assert(!model.matches(match, one));
    match.single = true; one.directories[0] = true; assert(!model.matches(match, one));
    match.folders = true; assert(model.matches(match, one));
    one.paths[0] = "/work/folder-without-extension";
    assert(model.matches(match, one));
    one.local = false; assert(!model.matches(match, one));
    CustomActionContext empty;
    empty.directory = "/work";
    assert(model.matches(match, empty));
    match.folders = false;
    assert(!model.matches(match, empty));
    match.background = true; assert(model.matches(match, empty));
    assert(!CustomActions::expand("echo %f", empty, expanded, error));
    assert(CustomActions::expand("printf '%%s' %d %p", empty, expanded, error));
    assert(shell(expanded) == "/work/work");
    empty.directory = "/work/current folder's";
    assert(CustomActions::expand("printf '%%s\\n' %f %F %n %N %d %D %u %U", empty, expanded, error, true));
    assert(shell(expanded) ==
        "/work/current folder's\n/work/current folder's\ncurrent folder's\ncurrent folder's\n"
        "/work/current folder's\n/work/current folder's\nfile:///work/current%20folder%27s\n"
        "file:///work/current%20folder%27s\n");
    match.patterns = ".txt;.md;.tar.gz";
    match.multiple = true;
    assert(model.matches(match, ctx));
    CustomActionContext archive;
    archive.paths.push_back("/work/archive.tar.gz"); archive.directories.push_back(false);
    assert(model.matches(match, archive));
    archive.paths[0] = "/work/archive.gz"; assert(!model.matches(match, archive));
    archive.paths[0] = "/work/PHOTO.TXT"; assert(!model.matches(match, archive));
    match.patterns = "*.TXT;.tar.gz";
    assert(model.matches(match, archive));
    archive.paths[0] = "/work/README"; assert(!model.matches(match, archive));
    match.patterns = "*"; assert(model.matches(match, archive));
    archive.paths[0] = "/work/photo.txt.bak"; match.patterns = ".txt";
    assert(!model.matches(match, archive));
    match.patterns = ".*";
    archive.paths[0] = "/work/photo.txt"; assert(!model.matches(match, archive));
    archive.paths[0] = "/work/.hidden"; assert(model.matches(match, archive));
    puts("PASS: extension shorthand, compound suffixes, explicit globs and folder/current-folder eligibility");
    assert(!CustomActions::expand("echo %o", empty, expanded, error));
    ctx.paths.pop_back(); ctx.directories.pop_back();
    model.items[2].patterns = "*.txt";
    assert(model.visible(0, ctx));
    ctx.paths.push_back("/work/no.png"); ctx.directories.push_back(false);
    assert(!model.visible(0, ctx));
    puts("PASS: files/folders, all-item globs, single/multiple/background/local contexts and recursive folder visibility");
    {
        FXString base = FXPath::absolute(FXPath::directory(argv[1]));
        FXString cwd = base + "/work ' $() ; percent%";
        assert(mkdir(cwd.text(), 0700) == 0);
        FXString executable = base + "/program ' $() ; spaced";
        {
            std::ofstream script(executable.text());
            script << "#!/bin/sh\nprintf '%s\\n' \"$PWD\"\nprintf '%s\\0' \"$@\"\n";
        }
        assert(chmod(executable.text(), 0700) == 0);
        CustomAction launch = action("program-test");
        launch.programMode = true; launch.program = executable; launch.parameters = "%F %c"; launch.folders = false;
        launch.command = "  printf 'legacy unchanged'  ";
        launch.workingDirectory = cwd;
        launch.workingDirectory.substitute("%", "%%");
        CustomActionContext context;
        context.directory = base; context.opposite = cwd;
        context.paths.push_back(base + "/a '$HOME;`bad`.tar.gz");
        context.paths.push_back(base + "/.hidden");
        context.directories.assign(2, false);
        FXString command, directory;
        assert(CustomActions::prepare(launch, context, command, directory, error));
        assert(directory == cwd);
        FXString qcwd = cwd; qcwd.substitute("'", "'\\''"); qcwd = "'" + qcwd + "'";
        std::string expected = cwd.text(); expected += '\n';
        for (const FXString& path : context.paths) { expected += path.text(); expected += '\0'; }
        expected += '2'; expected += '\0';
        assert(shell("cd " + qcwd + " && " + command) == expected);
        launch.workingDirectory.clear();
        assert(CustomActions::prepare(launch, context, command, directory, error) && directory == base);
        launch.workingDirectory = "%o";
        assert(CustomActions::prepare(launch, context, command, directory, error) && directory == cwd);
        launch.workingDirectory = "%p/../missing-swordfish-action-cwd";
        assert(!CustomActions::prepare(launch, context, command, directory, error));
        launch.workingDirectory = "$(touch do-not-evaluate)";
        assert(!CustomActions::prepare(launch, context, command, directory, error));
        launch.workingDirectory = "%F";
        assert(!CustomActions::prepare(launch, context, command, directory, error, false));
        launch.workingDirectory = "%d";
        assert(CustomActions::prepare(launch, context, command, directory, error) && directory == base);
        launch.workingDirectory = "%f";
        assert(!CustomActions::prepare(launch, context, command, directory, error));
        CustomActionContext backgroundContext = context;
        backgroundContext.paths.clear(); backgroundContext.directories.clear();
        assert(!CustomActions::prepare(launch, backgroundContext, command, directory, error, false));
        launch.folders = true;
        assert(CustomActions::prepare(launch, backgroundContext, command, directory, error) && directory == base);
        launch.folders = false;
        launch.workingDirectory.clear();
        launch.program = "swordfish-missing-executable-test";
        assert(!CustomActions::prepare(launch, context, command, directory, error));
        launch.program = base;
        assert(!CustomActions::prepare(launch, context, command, directory, error));
        launch.program = executable;
        assert(chmod(executable.text(), 0600) == 0);
        assert(!CustomActions::prepare(launch, context, command, directory, error));
        assert(chmod(executable.text(), 0700) == 0);
        const char* oldPath = getenv("SWORDFISH_HOST_PATH");
        bool hadPath = oldPath != NULL;
        std::string savedPath = oldPath ? oldPath : "";
        assert(setenv("SWORDFISH_HOST_PATH", "/usr/bin:/bin", 1) == 0);
        launch.program = "sh";
        assert(CustomActions::prepare(launch, context, command, directory, error));
        launch.program = "sh -c";
        assert(!CustomActions::prepare(launch, context, command, directory, error));
        if (hadPath) { setenv("SWORDFISH_HOST_PATH", savedPath.c_str(), 1); }
        else { unsetenv("SWORDFISH_HOST_PATH"); }
        launch.programMode = false;
        assert(CustomActions::prepare(launch, context, command, directory, error));
        assert(command == launch.command);
        CustomActions modes;
        modes.items.push_back(launch);
        launch.id = "program-new"; launch.programMode = true; launch.program = executable;
        modes.items.push_back(launch);
        FXSettings saved; modes.save(saved);
        CustomActions restored; assert(restored.load(saved, error) && restored.items.size() == 2);
        assert(!restored.items[0].programMode && restored.items[0].command == "  printf 'legacy unchanged'  ");
        assert(restored.items[1].programMode && restored.items[1].program == executable &&
               restored.items[1].parameters == "%F %c");
        saved.deleteEntry("CUSTOM ACTIONS", "item0.programMode");
        saved.deleteEntry("CUSTOM ACTIONS", "item0.program");
        saved.deleteEntry("CUSTOM ACTIONS", "item0.parameters");
        saved.deleteEntry("CUSTOM ACTIONS", "item0.workingDirectory");
        assert(restored.load(saved, error) && !restored.items[0].programMode &&
               restored.items[0].command == "  printf 'legacy unchanged'  ");
        saved.writeStringEntry("CUSTOM ACTIONS", "item1.programMode", "invalid");
        assert(!restored.load(saved, error));
        restored.items[1].program.clear();
        assert(!restored.validate(error));
        restored.items[1].program = executable + "\n";
        assert(!restored.validate(error));
        restored.items[1].program = executable;
        restored.items[1].parameters = "%Q";
        assert(!restored.validate(error));
        restored.items[1].parameters.clear();
        restored.items[1].workingDirectory = "%F";
        assert(!restored.validate(error));
        assert(unlink(executable.text()) == 0 && rmdir(cwd.text()) == 0);
        puts("PASS: optional program-mode persistence, old command bytes, host executable lookup, hostile executable/cwd paths, cwd defaults/overrides and explicit failures");
    }
    {
        CustomActionContext context;
        context.directory = "/work/folder.tar.gz"; context.opposite = "/other";
        const char* names[] = { "archive.tar.gz", ".hidden", ".hidden.txt", "name.", "nodot", "quote'$.txt" };
        const char* stems[] = { "archive.tar", ".hidden", ".hidden", "name", "nodot", "quote'$" };
        const char* extensions[] = { "gz", "", "txt", "", "", "txt" };
        std::string expected;
        for (unsigned n = 0; n < 6; ++n)
        {
            context.paths.assign(1, FXString("/work/") + names[n]); context.directories.assign(1, false);
            assert(CustomActions::expand("printf '%%s\\0' %w %x %c", context, expanded, error));
            expected = stems[n]; expected += '\0'; expected += extensions[n]; expected += '\0';
            expected += '1'; expected += '\0';
            assert(shell(expanded) == expected);
            assert(CustomActions::expand("printf '%%s\\0' '%w' \"%x\"", context, expanded, error));
            expected = stems[n]; expected += '\0'; expected += extensions[n]; expected += '\0';
            assert(shell(expanded) == expected);
        }
        context.paths.clear(); context.directories.clear();
        for (const char* name : names) { context.paths.push_back(FXString("/work/") + name); context.directories.push_back(false); }
        assert(CustomActions::expand("printf '%%s\\0' %W %X %c", context, expanded, error));
        expected.clear();
        for (const char* stem : stems) { expected += stem; expected += '\0'; }
        for (const char* extension : extensions) { expected += extension; expected += '\0'; }
        expected += '6'; expected += '\0';
        assert(shell(expanded) == expected);
        assert(!CustomActions::expand("echo \"%W\"", context, expanded, error));
        context.paths.clear(); context.directories.clear();
        assert(CustomActions::expand("printf '%%s\\0' %w %x %c %d", context, expanded, error, true));
        expected = "folder.tar"; expected += '\0'; expected += "gz"; expected += '\0'; expected += '0';
        expected += '\0'; expected += context.directory.text(); expected += '\0';
        assert(shell(expanded) == expected);
        assert(!CustomActions::expand("echo %w", context, expanded, error, false));
        puts("PASS: %c/%w/%W/%x/%X quoting, empty extensions, dotfiles, compound extensions, multi-selection and background semantics");
    }
    {
        CustomActionContext context;
        context.directory = FXPath::directory(FXPath::absolute(argv[1]));
        CustomAction checkAction = action("check");
        checkAction.programMode = true;
        checkAction.program = "/bin/sh";
        FXString marker = context.directory + "/check-must-not-execute";
        checkAction.parameters = "-c 'touch " + marker + "'";
        assert(CustomActions::check(checkAction, context, error));
        assert(access(marker.text(), F_OK) != 0);
        checkAction.parameters = "value )";
        assert(!CustomActions::check(checkAction, context, error) && !error.empty());
        checkAction.parameters = "%Q";
        assert(!CustomActions::check(checkAction, context, error));
        checkAction.parameters.clear();
        checkAction.program = "/missing/swordfish-test-program";
        assert(!CustomActions::check(checkAction, context, error));
        checkAction.program = "/bin/true";
        checkAction.workingDirectory = "/missing/swordfish-test-directory";
        assert(!CustomActions::check(checkAction, context, error));
        checkAction.workingDirectory.clear();
        checkAction.programMode = false;
        checkAction.command = "touch " + marker;
        assert(CustomActions::check(checkAction, context, error));
        assert(access(marker.text(), F_OK) != 0);
        checkAction.command = "if then";
        assert(!CustomActions::check(checkAction, context, error));
        puts("PASS: non-executing checks validate preparation and shell syntax, report errors, and never run side effects");
    }
    return 0;
}
