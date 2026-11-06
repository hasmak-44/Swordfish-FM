#include "CustomActions.h"

bool CustomAction::operator==(const CustomAction& b) const
{
    return id == b.id && parent == b.parent && name == b.name && description == b.description &&
        icon == b.icon && command == b.command && program == b.program && parameters == b.parameters &&
        workingDirectory == b.workingDirectory && patterns == b.patterns && kind == b.kind &&
        toolbar == b.toolbar && files == b.files && folders == b.folders && single == b.single &&
        multiple == b.multiple && background == b.background && terminal == b.terminal &&
        confirm == b.confirm && showOutput == b.showOutput && programMode == b.programMode;
}

FXString CustomActions::duplicate(const FXString& id, const FXString& suffix, FXString& error)
{
    const CustomAction* original = find(id);
    if (!original || original->kind != CustomAction::ACTION)
    { error = "Select an action to duplicate."; return FXString::null; }
    if (items.size() >= MAX_ITEMS)
    { error = "At most 256 entries are supported."; return FXString::null; }
    unsigned position = original - &items[0];
    CustomAction copy = *original;
    copy.id = newId(); copy.name += suffix;
    items.insert(items.begin() + position + 1, copy);
    return copy.id;
}
#include <fnmatch.h>
#include <set>
#include <string>
#include <algorithm>
#include <ctime>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#include <sys/wait.h>
#include <cerrno>
#include <cstring>

namespace
{
const char* section = "CUSTOM ACTIONS";

FXString key(unsigned n, const char* field)
{
    FXString result;
    result.format("item%u.%s", n, field);
    return result;
}

bool integer(FXSettings& settings, const FXString& key, int maximum, int& value)
{
    FXString text = settings.readStringEntry(section, key.text(), "");
    text.trim();
    if (text.empty()) { return false; }
    value = 0;
    for (int i = 0; i < text.length(); ++i)
    {
        if (text[i] < '0' || text[i] > '9' || value > maximum / 10) { return false; }
        value = value * 10 + text[i] - '0';
        if (value > maximum) { return false; }
    }
    return true;
}

bool validId(const FXString& id)
{
    if (id.empty() || id.length() > 80) { return false; }
    for (int i = 0; i < id.length(); ++i)
    {
        char c = id[i];
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-') { return false; }
    }
    return true;
}

FXString quoted(const FXString& text, char quote)
{
    FXString result;
    if (!quote) { result += '\''; }
    for (int i = 0; i < text.length(); ++i)
    {
        char c = text[i];
        if (quote == '"')
        {
            if (c == '"' || c == '\\' || c == '$' || c == '`') { result += '\\'; }
            result += c;
        }
        else if (c == '\'') { result += "'\\''"; }
        else { result += c; }
    }
    if (!quote) { result += '\''; }
    return result;
}

bool boundary(char c)
{
    return !c || c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '|' || c == '&' ||
           c == '(' || c == ')' || c == '<' || c == '>';
}

FXString uri(const FXString& text)
{
    FXString result = "file://";
    const char* hex = "0123456789ABCDEF";
    for (int i = 0; i < text.length(); ++i)
    {
        unsigned char c = text[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
        {
            result += c;
        }
        else
        {
            result += '%'; result += hex[c >> 4]; result += hex[c & 15];
        }
    }
    return result;
}
}

const CustomAction* CustomActions::find(const FXString& id) const
{
    for (unsigned i = 0; i < items.size(); ++i) { if (items[i].id == id) { return &items[i]; } }
    return NULL;
}

FXString CustomActions::newId() const
{
    static unsigned serial = 0;
    FXString id;
    do { id.format("a-%lx-%lx-%x", (unsigned long)time(NULL), (unsigned long)getpid(), ++serial); }
    while (find(id));
    return id;
}

bool CustomActions::validate(FXString& error) const
{
    error.clear();
    if (items.size() > MAX_ITEMS) { error = "At most 256 custom action entries are supported."; return false; }
    std::set<std::string> ids;
    CustomActionContext sample;
    sample.directory = "/current"; sample.opposite = "/opposite";
    sample.paths.push_back("/current/example"); sample.directories.push_back(false);
    for (unsigned i = 0; i < items.size(); ++i)
    {
        const CustomAction& a = items[i];
        if (!validId(a.id) || !ids.insert(a.id.text()).second)
        { error = "Invalid or duplicate custom action ID."; return false; }
        if (a.kind < CustomAction::ACTION || a.kind > CustomAction::SEPARATOR ||
            a.name.length() > 256 || a.description.length() > 2048 || a.icon.length() > 4096 ||
            a.command.length() > 16384 || a.patterns.length() > 4096)
        { error = "Invalid entry type or an excessively long field."; return false; }
        if (a.kind != CustomAction::SEPARATOR &&
            (FXString(a.name).trim().empty() || a.name.contains('\n') || a.name.contains('\t') || a.name.contains('\r')))
        { error = "Actions and folders need a name without tabs or newlines."; return false; }
        if (!a.parent.empty())
        {
            const CustomAction* p = find(a.parent);
            if (!p || p->kind != CustomAction::FOLDER)
            { error = "An entry's parent must be an existing folder."; return false; }
        }
        FXString parent = a.parent;
        unsigned depth = 0;
        while (!parent.empty())
        {
            if (parent == a.id || ++depth > 16)
            { error = "Folders cannot contain themselves; nesting is limited to 16 levels."; return false; }
            const CustomAction* p = find(parent);
            if (!p) { error = "Missing parent folder."; return false; }
            parent = p->parent;
        }
        if (a.kind == CustomAction::ACTION)
        {
            if (a.program.length() > 4096 || a.parameters.length() > 16384 || a.workingDirectory.length() > 4096 ||
                (a.programMode && (a.program.empty() || a.program.contains('\n') || a.program.contains('\r'))))
            { error = "Program mode needs one executable name or path, without newlines; a field is too long."; return false; }
            bool usablePattern = false;
            for (int n = 0; n <= a.patterns.contains(';'); ++n)
            { if (!a.patterns.section(';', n).trim().empty()) { usablePattern = true; break; } }
            if ((!a.programMode && FXString(a.command).trim().empty()) || (!a.files && !a.folders && !a.background) ||
                (!a.single && !a.multiple && !a.background) || !usablePattern)
            { error = "Actions need a command, filename patterns, and at least one usable selection condition."; return false; }
            FXString expanded, directory;
            if (!prepare(a, sample, expanded, directory, error, false)) { error = a.name + ": " + error; return false; }
        }
    }
    return true;
}

bool CustomActions::load(FXSettings& settings, FXString& error)
{
    CustomActions loaded;
    if (!settings.existingEntry(section, "version"))
    {
        if (settings.existingEntry(section, "count"))
        { error = "Custom action settings have no version."; return false; }
        items.clear(); error.clear(); return true;
    }
    int count, version;
    if (!integer(settings, "version", 1, version) || version != 1 ||
        !integer(settings, "count", MAX_ITEMS, count))
    { error = "Unsupported or invalid custom action settings."; return false; }
    for (int i = 0; i < count; ++i)
    {
        CustomAction a;
#define READ_STRING(field) a.field = settings.readStringEntry(section, key(i, #field).text(), "")
        READ_STRING(id); READ_STRING(parent); READ_STRING(name); READ_STRING(description);
        READ_STRING(icon); READ_STRING(command); READ_STRING(patterns);
        READ_STRING(program); READ_STRING(parameters); READ_STRING(workingDirectory);
#undef READ_STRING
        if (!integer(settings, key(i, "kind"), CustomAction::SEPARATOR, a.kind))
        { error = "Invalid custom action entry type."; return false; }
#define READ_BOOL(field) \
        { int v; if (!integer(settings, key(i, #field), 1, v)) \
          { error = "Invalid custom action boolean setting."; return false; } a.field = v; }
        READ_BOOL(toolbar); READ_BOOL(files); READ_BOOL(folders); READ_BOOL(single);
        READ_BOOL(multiple); READ_BOOL(background); READ_BOOL(terminal); READ_BOOL(confirm);
        if (settings.existingEntry(section, key(i, "showOutput").text()))
        {
            READ_BOOL(showOutput);
        }
        if (settings.existingEntry(section, key(i, "programMode").text())) { READ_BOOL(programMode); }
#undef READ_BOOL
        loaded.items.push_back(a);
    }
    if (!loaded.validate(error)) { return false; }
    items.swap(loaded.items);
    return true;
}

void CustomActions::save(FXSettings& settings) const
{
    settings.deleteSection(section);
    settings.writeIntEntry(section, "version", 1);
    settings.writeIntEntry(section, "count", items.size());
    for (unsigned i = 0; i < items.size(); ++i)
    {
        const CustomAction& a = items[i];
#define WRITE_STRING(field) settings.writeStringEntry(section, key(i, #field).text(), a.field.text())
        WRITE_STRING(id); WRITE_STRING(parent); WRITE_STRING(name); WRITE_STRING(description);
        WRITE_STRING(icon); WRITE_STRING(command); WRITE_STRING(patterns);
        WRITE_STRING(program); WRITE_STRING(parameters); WRITE_STRING(workingDirectory);
#undef WRITE_STRING
        settings.writeIntEntry(section, key(i, "kind").text(), a.kind);
#define WRITE_BOOL(field) settings.writeIntEntry(section, key(i, #field).text(), a.field ? 1 : 0)
        WRITE_BOOL(toolbar); WRITE_BOOL(files); WRITE_BOOL(folders); WRITE_BOOL(single);
        WRITE_BOOL(multiple); WRITE_BOOL(background); WRITE_BOOL(terminal); WRITE_BOOL(confirm);
        WRITE_BOOL(showOutput);
        WRITE_BOOL(programMode);
#undef WRITE_BOOL
    }
}

std::vector<unsigned> CustomActions::children(const FXString& parent) const
{
    std::vector<unsigned> result;
    for (unsigned i = 0; i < items.size(); ++i) { if (items[i].parent == parent) { result.push_back(i); } }
    return result;
}

bool CustomActions::move(const FXString& id, const FXString& parent, FXString& error)
{
    for (unsigned i = 0; i < items.size(); ++i)
    {
        if (items[i].id != id) { continue; }
        FXString old = items[i].parent;
        items[i].parent = parent;
        if (!validate(error)) { items[i].parent = old; return false; }
        CustomAction moved = items[i];
        items.erase(items.begin() + i);
        items.push_back(moved);
        return true;
    }
    error = "Entry not found."; return false;
}

void CustomActions::remove(const FXString& id)
{
    std::vector<unsigned> nested = children(id);
    std::vector<FXString> ids;
    for (unsigned i = 0; i < nested.size(); ++i) { ids.push_back(items[nested[i]].id); }
    for (unsigned i = 0; i < ids.size(); ++i) { remove(ids[i]); }
    for (unsigned i = 0; i < items.size(); ++i)
    {
        if (items[i].id == id) { items.erase(items.begin() + i); break; }
    }
}

void CustomActions::reorder(const FXString& id, int direction)
{
    const CustomAction* a = find(id);
    if (!a) { return; }
    std::vector<unsigned> siblings = children(a->parent);
    for (unsigned i = 0; i < siblings.size(); ++i)
    {
        if (items[siblings[i]].id != id) { continue; }
        int next = (int)i + direction;
        if (next >= 0 && next < (int)siblings.size()) { std::swap(items[siblings[i]], items[siblings[next]]); }
        return;
    }
}

std::vector<unsigned> CustomActions::toolbarItems() const
{
    std::vector<unsigned> result;
    // Traverse hierarchy order; an exposed folder already includes its descendants.
    std::vector<FXString> parents(1, FXString::null);
    std::vector<unsigned> positions(1, 0);
    while (!parents.empty())
    {
        std::vector<unsigned> nodes = children(parents.back());
        if (positions.back() >= nodes.size()) { parents.pop_back(); positions.pop_back(); continue; }
        unsigned n = nodes[positions.back()++];
        if (items[n].toolbar) { result.push_back(n); }
        else if (items[n].kind == CustomAction::FOLDER) { parents.push_back(items[n].id); positions.push_back(0); }
    }
    return result;
}

bool CustomActions::matches(const CustomAction& a, const CustomActionContext& ctx) const
{
    if (!ctx.local || a.kind != CustomAction::ACTION || ctx.paths.size() != ctx.directories.size()) { return false; }
    if (ctx.paths.empty()) { return a.background || a.folders; }
    if ((ctx.paths.size() == 1 && !a.single) || (ctx.paths.size() > 1 && !a.multiple)) { return false; }
    for (unsigned i = 0; i < ctx.paths.size(); ++i)
    {
        if ((ctx.directories[i] && !a.folders) || (!ctx.directories[i] && !a.files)) { return false; }
        if (ctx.directories[i]) { continue; }
        FXString name = FXPath::name(ctx.paths[i]);
        bool matched = false;
        for (int p = 0; p <= a.patterns.contains(';'); ++p)
        {
            FXString pattern = a.patterns.section(';', p).trim();
            if (!pattern.empty() && pattern[0] == '.' && pattern.find('*') < 0 &&
                pattern.find('?') < 0 && pattern.find('[') < 0)
            { pattern.prepend('*'); }
            if (!pattern.empty() && fnmatch(pattern.text(), name.text(), 0) == 0) { matched = true; break; }
        }
        if (!matched) { return false; }
    }
    return true;
}

bool CustomActions::visible(unsigned i, const CustomActionContext& ctx) const
{
    if (items[i].kind == CustomAction::ACTION) { return matches(items[i], ctx); }
    if (items[i].kind == CustomAction::SEPARATOR) { return false; }
    std::vector<unsigned> nodes = children(items[i].id);
    for (unsigned n = 0; n < nodes.size(); ++n) { if (visible(nodes[n], ctx)) { return true; } }
    return false;
}

bool CustomActions::expand(const FXString& input, const CustomActionContext& ctx, FXString& output, FXString& error,
                           bool currentFolder)
{
    output.clear(); error.clear();
    // Nested shells have independent quoting rules. Refuse ambiguous substitution rather than guess.
    if (input.contains('`') || input.find("$(") >= 0 || input.find("${") >= 0 || input.find("<<") >= 0)
    { error = "Use a script for backticks, shell substitutions, or here-documents."; return false; }
    char quote = 0;
    for (int i = 0; i < input.length(); ++i)
    {
        char c = input[i];
        if (c == '\\' && quote != '\'')
        {
            if (i + 1 == input.length()) { error = "Trailing backslash in command."; return false; }
            output += c; output += input[++i]; continue;
        }
        if ((c == '\'' || c == '"') && (!quote || quote == c))
        {
            quote = quote ? 0 : c; output += c; continue;
        }
        if (c == '#' && !quote)
        { error = "Use a script for shell comments, or quote literal # characters."; return false; }
        if (c != '%') { output += c; continue; }
        if (++i == input.length()) { error = "A trailing % needs escaping as %%."; return false; }
        c = input[i];
        if (c == '%') { output += '%'; continue; }
        bool list = c == 'F' || c == 'N' || c == 'U' || c == 'D' || c == 'W' || c == 'X';
        if (list && (quote || (i > 1 && !boundary(input[i - 2])) || !boundary(input[i + 1])))
        { error = "List placeholders (%F, %N, %U, %D, %W, %X) must be separate, unquoted arguments."; return false; }
        std::vector<FXString> values;
        if (c == 'p') { values.push_back(ctx.directory); }
        else if (c == 'c')
        {
            FXString count; count.format("%u", (unsigned)ctx.paths.size()); values.push_back(count);
        }
        else if (c == 'o')
        {
            if (ctx.opposite.empty()) { error = "The opposite pane has no local directory."; return false; }
            values.push_back(ctx.opposite);
        }
        else if (c == 'd' || c == 'f' || c == 'n' || c == 'u' || c == 'w' || c == 'x' || list)
        {
            if (ctx.paths.empty())
            {
                if (c == 'd' || c == 'D') { values.push_back(ctx.directory); }
                else if (currentFolder)
                {
                    FXString value = ctx.directory;
                    if (c == 'n' || c == 'N') { value = FXPath::name(value); }
                    else if (c == 'u' || c == 'U') { value = uri(value); }
                    values.push_back(value);
                }
                else { error = "This command needs selected files; use %p for the current directory."; return false; }
            }
            for (unsigned n = 0; n < ctx.paths.size(); ++n)
            {
                FXString value = ctx.paths[n];
                if (c == 'n' || c == 'N') { value = FXPath::name(value); }
                else if (c == 'u' || c == 'U') { value = uri(value); }
                else if (c == 'd' || c == 'D') { value = FXPath::directory(value); }
                if (std::find(values.begin(), values.end(), value) == values.end() || (c != 'd' && c != 'D'))
                { values.push_back(value); }
                if (!list) { break; }
            }
            if (c == 'w' || c == 'W' || c == 'x' || c == 'X')
            {
                for (unsigned n = 0; n < values.size(); ++n)
                {
                    FXString base = FXPath::name(values[n]);
                    int dot = base.rfind('.');
                    if (dot <= 0) { dot = base.length(); }
                    values[n] = (c == 'w' || c == 'W') ? base.left(dot) :
                                (dot < base.length() ? base.mid(dot + 1, base.length() - dot - 1) : FXString::null);
                }
            }
        }
        else { error = "Unknown placeholder. Use %f %F %n %N %d %D %u %U %p %o %c %w %W %x %X or %%."; return false; }
        for (unsigned n = 0; n < values.size(); ++n)
        {
            if (n) { output += ' '; }
            output += quoted(values[n], quote);
        }
    }
    if (quote) { error = "Unclosed quote in command."; return false; }
    return true;
}

bool CustomActions::prepare(const CustomAction& action, const CustomActionContext& ctx, FXString& command,
                            FXString& directory, FXString& error, bool verify)
{
    command.clear(); directory.clear(); error.clear();
    const FXString& input = action.workingDirectory;
    for (int i = 0; i < input.length(); ++i)
    {
        if (input[i] != '%') { directory += input[i]; continue; }
        if (++i == input.length()) { error = "Trailing % in working directory; use %% for a literal percent."; return false; }
        char token = input[i];
        if (token == '%') { directory += '%'; }
        else if (token == 'p') { directory += ctx.directory; }
        else if (token == 'o')
        {
            if (ctx.opposite.empty()) { error = "The opposite pane has no local directory."; return false; }
            directory += ctx.opposite;
        }
        else if (token == 'd') { directory += ctx.paths.empty() ? ctx.directory : FXPath::directory(ctx.paths[0]); }
        else if (token == 'f')
        {
            if (ctx.paths.empty() && !action.folders)
            { error = "Working directory %f requires a selection, or Folders enabled."; return false; }
            directory += ctx.paths.empty() ? ctx.directory : ctx.paths[0];
        }
        else { error = "Working directory accepts only %p, %o, %d, %f and %% (one path, no shell evaluation)."; return false; }
    }
    if (directory.empty()) { directory = ctx.directory; }
    directory = FXPath::absolute(ctx.directory, directory);
    if (verify)
    {
        struct stat info;
        if (stat(directory.text(), &info) != 0 || !S_ISDIR(info.st_mode) || access(directory.text(), X_OK) != 0)
        { error = "Working directory is missing, not a directory, or inaccessible: " + directory; return false; }
    }
    if (!action.programMode) { return expand(action.command, ctx, command, error, action.folders); }
    if (action.program.empty()) { error = "Enter one program name or executable path."; return false; }
    FXString executable = action.program;
    if (verify)
    {
        executable.clear();
        if (action.program.contains('/')) { executable = FXPath::absolute(directory, action.program); }
        else
        {
            const char* path = getenv("SWORDFISH_HOST_PATH");
            if (!path || !*path) { path = getenv("PATH"); }
            FXString search = path ? path : "/usr/local/bin:/usr/bin:/bin";
            for (int n = 0; n <= search.contains(':'); ++n)
            {
                FXString entry = search.section(':', n);
                FXString candidate = FXPath::absolute(directory, entry.empty() ? "." : entry) + "/" + action.program;
                struct stat info;
                if (stat(candidate.text(), &info) == 0 && S_ISREG(info.st_mode) && access(candidate.text(), X_OK) == 0)
                { executable = candidate; break; }
            }
        }
        struct stat info;
        if (executable.empty() || stat(executable.text(), &info) != 0 || !S_ISREG(info.st_mode) ||
            access(executable.text(), X_OK) != 0)
        { error = "Program not found or not executable on the host: " + action.program; return false; }
    }
    FXString arguments;
    if (!expand(action.parameters, ctx, arguments, error, action.folders)) { return false; }
    command = quoted(executable, 0);
    if (!arguments.empty()) { command += " " + arguments; }
    return true;
}

bool CustomActions::check(const CustomAction& action, const CustomActionContext& ctx, FXString& error)
{
    FXString command, directory;
    if (!prepare(action, ctx, command, directory, error)) { return false; }
    int channel[2];
    if (pipe(channel) != 0)
    { error = FXString("Cannot start syntax check: ") + strerror(errno); return false; }
    pid_t child = fork();
    if (child == 0)
    {
        close(channel[0]);
        if (dup2(channel[1], STDERR_FILENO) < 0 || dup2(channel[1], STDOUT_FILENO) < 0) _exit(126);
        close(channel[1]);
        execl("/bin/sh", "sh", "-n", "-c", command.text(), (char*)NULL);
        _exit(126);
    }
    close(channel[1]);
    if (child < 0)
    {
        int failure = errno;
        close(channel[0]);
        error = FXString("Cannot start syntax check: ") + strerror(failure);
        return false;
    }
    char buffer[1024];
    FXString diagnostic;
    ssize_t count;
    while ((count = read(channel[0], buffer, sizeof(buffer))) != 0)
    {
        if (count < 0)
        {
            if (errno == EINTR) continue;
            error = FXString("Cannot read syntax check: ") + strerror(errno);
            break;
        }
        diagnostic.append(buffer, count);
    }
    close(channel[0]);
    int status;
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result < 0) { error = FXString("Cannot obtain syntax check status: ") + strerror(errno); return false; }
    if (!error.empty()) return false;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        error = diagnostic.empty() ? FXString("Shell syntax check failed.") : diagnostic;
        return false;
    }
    return true;
}
