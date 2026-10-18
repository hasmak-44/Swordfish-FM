#include <gio/gio.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ifaddrs.h>
#include <map>
#include <net/if.h>
#include <netinet/in.h>
#include <set>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <vector>

namespace
{
struct DirectoryLoad
{
    GMainLoop* loop;
    GFileEnumerator* enumerator;
    GError* error;
    GFile* directory = NULL;
    bool shares = false;
    bool mounted = false;
};

void nextEntries(GObject* source, GAsyncResult* result, gpointer data)
{
    DirectoryLoad* load = static_cast<DirectoryLoad*>(data);
    GList* entries = g_file_enumerator_next_files_finish(G_FILE_ENUMERATOR(source), result, &load->error);
    if (load->error || !entries)
    {
        g_main_loop_quit(load->loop);
        return;
    }
    for (GList* node = entries; node; node = node->next)
    {
        GFileInfo* info = G_FILE_INFO(node->data);
        const char* name = g_file_info_get_name(info);
        const char* target = g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET) ?
                             g_file_info_get_symlink_target(info) : NULL;
        if (!name) continue;
        if (load->shares)
        {
            const char* targetUri = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_STANDARD_TARGET_URI);
            if (g_file_info_get_file_type(info) != G_FILE_TYPE_MOUNTABLE &&
                (!targetUri || !g_str_has_prefix(targetUri, "smb://"))) continue;
        }
        guint32 mode = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE);
        GFileType type = g_file_info_get_file_type(info);
        if (load->shares) mode = S_IFDIR | 0555;
        // Remote backends need not expose Unix permission bits.
        if (!mode)
            mode = (type == G_FILE_TYPE_DIRECTORY ? S_IFDIR | 0755 : S_IFREG | 0644);
        if (g_file_info_get_is_symlink(info))
            mode = (mode & ~S_IFMT) | (type == G_FILE_TYPE_DIRECTORY ? S_IFDIR :
                                     type == G_FILE_TYPE_SYMBOLIC_LINK ? S_IFLNK : S_IFREG);
        gchar* encodedName = g_base64_encode(reinterpret_cast<const guchar*>(name), strlen(name));
        gchar* encodedTarget = g_base64_encode(reinterpret_cast<const guchar*>(target ? target : ""),
                                                target ? strlen(target) : 0);
        GVariant* value = g_variant_ref_sink(g_variant_new("(suuuxxxsbii)",
            encodedName, mode,
            g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_UNIX_UID) ?
                g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_UID) : G_MAXUINT32,
            g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_UNIX_GID) ?
                g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_GID) : G_MAXUINT32,
            static_cast<gint64>(g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED)),
            static_cast<gint64>(g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_CHANGED)),
            static_cast<gint64>(g_file_info_get_size(info)), encodedTarget,
            g_file_info_get_is_symlink(info),
            g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ) ?
                static_cast<int>(g_file_info_get_attribute_boolean(info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ)) : -1,
            g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE) ?
                static_cast<int>(g_file_info_get_attribute_boolean(info, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE)) : -1));
        gchar* record = g_variant_print(value, true);
        std::puts(record);
        g_free(record);
        g_variant_unref(value);
        g_free(encodedName);
        g_free(encodedTarget);
    }
    g_list_free_full(entries, g_object_unref);
    std::fflush(stdout);
    g_file_enumerator_next_files_async(load->enumerator, 100, G_PRIORITY_DEFAULT, NULL, nextEntries, load);
}

void enumerated(GObject* source, GAsyncResult* result, gpointer data);

void enumerateDirectory(DirectoryLoad* load)
{
    std::string attributes =
        "standard::name,standard::type,standard::size,standard::is-symlink,standard::symlink-target,"
        "unix::mode,unix::uid,unix::gid,time::modified,time::changed,access::can-read,access::can-execute";
    if (load->shares) attributes += ",standard::target-uri";
    g_file_enumerate_children_async(load->directory, attributes.c_str(),
        G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT, NULL, enumerated, load);
}

void sharePassword(GMountOperation* operation, const char*, const char*, const char*, GAskPasswordFlags flags,
                   gpointer data)
{
    bool* anonymous = static_cast<bool*>(data);
    if (!*anonymous && (flags & G_ASK_PASSWORD_ANONYMOUS_SUPPORTED))
    {
        *anonymous = true;
        g_mount_operation_set_anonymous(operation, true);
        g_mount_operation_reply(operation, G_MOUNT_OPERATION_HANDLED);
        return;
    }
    std::puts("AUTH");
    std::fflush(stdout);
    char buffer[4098];
    if (!std::fgets(buffer, sizeof(buffer), stdin) || !strchr(buffer, '\n'))
    {
        g_mount_operation_reply(operation, G_MOUNT_OPERATION_ABORTED);
        return;
    }
    *strchr(buffer, '\n') = '\0';
    gchar** fields = g_strsplit(buffer, "\t", -1);
    if (g_strv_length(fields) != 3)
    {
        g_strfreev(fields);
        g_mount_operation_reply(operation, G_MOUNT_OPERATION_ABORTED);
        return;
    }
    std::string decoded[3];
    for (int i = 0; i < 3; ++i)
    {
        gsize size;
        guchar* value = g_base64_decode(fields[i], &size);
        decoded[i].assign(reinterpret_cast<char*>(value), size);
        g_free(value);
    }
    g_strfreev(fields);
    g_mount_operation_set_anonymous(operation, false);
    g_mount_operation_set_username(operation, decoded[0].c_str());
    g_mount_operation_set_domain(operation, decoded[1].c_str());
    g_mount_operation_set_password(operation, decoded[2].c_str());
    g_mount_operation_set_password_save(operation, G_PASSWORD_SAVE_FOR_SESSION);
    g_mount_operation_reply(operation, G_MOUNT_OPERATION_HANDLED);
}

void shareServerMounted(GObject* source, GAsyncResult* result, gpointer data)
{
    DirectoryLoad* load = static_cast<DirectoryLoad*>(data);
    if (!g_file_mount_enclosing_volume_finish(G_FILE(source), result, &load->error) &&
        !g_error_matches(load->error, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED))
    {
        g_main_loop_quit(load->loop);
        return;
    }
    g_clear_error(&load->error);
    enumerateDirectory(load);
}

void enumerated(GObject* source, GAsyncResult* result, gpointer data)
{
    DirectoryLoad* load = static_cast<DirectoryLoad*>(data);
    load->enumerator = g_file_enumerate_children_finish(G_FILE(source), result, &load->error);
    if (!load->enumerator && load->shares && !load->mounted &&
        g_error_matches(load->error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED))
    {
        load->mounted = true;
        g_clear_error(&load->error);
        GMountOperation* operation = g_mount_operation_new();
        g_signal_connect_data(operation, "ask-password", G_CALLBACK(sharePassword), new bool(false),
            [](gpointer value, GClosure*) { delete static_cast<bool*>(value); }, GConnectFlags(0));
        g_file_mount_enclosing_volume(load->directory, G_MOUNT_MOUNT_NONE, operation, NULL,
                                      shareServerMounted, load);
        g_object_unref(operation);
    }
    else if (!load->enumerator) g_main_loop_quit(load->loop);
    else g_file_enumerator_next_files_async(load->enumerator, 100, G_PRIORITY_DEFAULT, NULL, nextEntries, load);
}

int listDirectory(const char* path, bool shares = false)
{
    GFile* directory = shares ? g_file_new_for_uri(path) : g_file_new_for_path(path);
    gchar* uri = g_file_get_uri(directory);
    // GVFS's native VFS maps its FUSE path back to the remote URI.
    // Refuse a local-FUSE fallback: it would recreate per-entry round trips.
    if (strstr(path, "/gvfs/") && g_str_has_prefix(uri, "file:"))
    {
        std::puts("ERROR Host GVFS could not resolve the mounted share to its native URI");
        g_free(uri);
        g_object_unref(directory);
        return 1;
    }
    g_free(uri);
    DirectoryLoad load;
    load.loop = g_main_loop_new(NULL, false);
    load.enumerator = NULL;
    load.error = NULL;
    load.directory = directory;
    load.shares = shares;
    enumerateDirectory(&load);
    g_main_loop_run(load.loop);
    if (load.enumerator)
    {
        GError* closeError = NULL;
        if (!g_file_enumerator_close(load.enumerator, NULL, &closeError))
        {
            if (!load.error) load.error = closeError;
            else g_clear_error(&closeError);
        }
        g_object_unref(load.enumerator);
    }
    g_main_loop_unref(load.loop);
    g_object_unref(directory);
    if (load.error)
    {
        gchar* message = g_strescape(load.error->message, NULL);
        std::printf("ERROR %s\n", message);
        g_free(message);
        g_error_free(load.error);
        return 1;
    }
    std::puts("DONE");
    return 0;
}
}

// Discovery: host GVFS (Linux/Samba, mDNS) plus WS-Discovery (Windows), with a
// TCP-connect-only check of port 445. It never mounts, authenticates or uses SMB1.
namespace discovery
{
const char* const WSD_GROUP = "239.255.255.250";
const guint16 WSD_PORT = 3702;
const guint WSD_RETRY_MS = 3000;
const guint WSD_WINDOW_MS = 8000;
const guint DEADLINE_MS = 12000;
const guint CONNECT_TIMEOUT_S = 3;
const guint METADATA_TIMEOUT_S = 4;
const gsize MAX_REPLY = 1024 * 1024;
const size_t MAX_HOSTS = 256;
const char* const ADDRESSING = "http://schemas.xmlsoap.org/ws/2004/08/addressing";
const char* const WSD = "http://schemas.xmlsoap.org/ws/2005/04/discovery";

guint16 smbPort = 445;
FILE* out = stdout;

enum PortState { UNCHECKED, CHECKING, OPEN, CLOSED };
enum MetadataState { NO_METADATA, METADATA_PENDING, METADATA_DONE };

struct Interface
{
    std::string name;
    std::string address;
};

struct Host
{
    std::string key, address, connect, name, group;
    std::set<std::string> sources;
    bool local = false;
    bool nameFromMetadata = false;
    PortState port = UNCHECKED;
    MetadataState metadata = NO_METADATA;
    std::string printed;
};

struct ProbeMatch
{
    std::string endpoint, types, xaddrs;
};

struct Computer
{
    std::string name, group;
};

struct State
{
    GMainLoop* loop = NULL;
    std::vector<Interface> interfaces;
    std::set<std::string> localAddresses;
    std::map<std::string, Host> hosts;
    std::set<std::string> gvfsHosts;
    std::vector<GSocket*> sockets;
    std::vector<GSource*> sources;
    std::string probeId, probe;
    int pending = 0;
    bool wsdOpen = false;
    bool finished = false;
    bool hostLimitReported = false;
};
State* state = NULL;

std::string lower(std::string text)
{
    for (char& c : text) c = g_ascii_tolower(c);
    return text;
}

std::string localName(const char* element)
{
    const char* colon = strrchr(element, ':');
    return colon ? colon + 1 : element;
}

// Records are tab-separated; strip control characters and keep valid UTF-8.
std::string clean(const std::string& text, size_t limit = 128)
{
    std::string result;
    for (unsigned char c : text)
    {
        result += (c < 0x20 || c == 0x7f) ? ' ' : static_cast<char>(c);
        if (result.size() >= limit) break;
    }
    const gchar* end = NULL;
    g_utf8_validate(result.c_str(), result.size(), &end);
    result.resize(end - result.c_str());
    return result;
}

void status(const char* source, const std::string& message)
{
    std::fprintf(out, "STATUS\t%s\t%s\n", source, clean(message, 300).c_str());
    std::fflush(out);
}

bool isIPv4(const std::string& text)
{
    GInetAddress* address = g_inet_address_new_from_string(text.c_str());
    bool result = address && g_inet_address_get_family(address) == G_SOCKET_FAMILY_IPV4;
    if (address) g_object_unref(address);
    return result;
}

// Host part of smb://[user@]host[:port]/...; empty for the smb:/// browse root.
std::string smbHost(const std::string& uri)
{
    if (g_ascii_strncasecmp(uri.c_str(), "smb://", 6) != 0) return "";
    std::string authority = uri.substr(6, uri.find('/', 6) == std::string::npos ?
                                          std::string::npos : uri.find('/', 6) - 6);
    const size_t at = authority.rfind('@');
    if (at != std::string::npos) authority = authority.substr(at + 1);
    if (!authority.empty() && authority[0] == '[') return "";
    const size_t colon = authority.find(':');
    if (colon != std::string::npos) authority = authority.substr(0, colon);
    gchar* unescaped = g_uri_unescape_string(authority.c_str(), NULL);
    std::string host = unescaped ? unescaped : "";
    g_free(unescaped);
    for (char c : host)
        if (!(g_ascii_isalnum(c) || c == '.' || c == '-' || c == '_')) return "";
    return host;
}

// Windows pub:Computer values: NAME/Workgroup:GROUP, NAME\Domain:DOMAIN or NAME/NotJoined.
Computer parseComputer(const std::string& value)
{
    Computer computer;
    std::string text = clean(value, 256);
    const size_t split = text.find_first_of("/\\");
    computer.name = text.substr(0, split);
    if (split != std::string::npos) computer.group = text.substr(split + 1);
    while (!computer.name.empty() && computer.name.back() == ' ') computer.name.pop_back();
    return computer;
}

struct ProbeParse
{
    std::vector<std::string> stack;
    std::string relatesTo;
    std::vector<ProbeMatch> matches;
    bool inMatch = false;
};

void probeStart(GMarkupParseContext*, const gchar* element, const gchar**, const gchar**,
                gpointer data, GError**)
{
    ProbeParse* parse = static_cast<ProbeParse*>(data);
    parse->stack.push_back(localName(element));
    if (parse->stack.back() == "ProbeMatch")
    {
        parse->matches.push_back(ProbeMatch());
        parse->inMatch = true;
    }
}

void probeEnd(GMarkupParseContext*, const gchar*, gpointer data, GError**)
{
    ProbeParse* parse = static_cast<ProbeParse*>(data);
    if (parse->stack.back() == "ProbeMatch") parse->inMatch = false;
    parse->stack.pop_back();
}

void probeText(GMarkupParseContext*, const gchar* text, gsize length, gpointer data, GError**)
{
    ProbeParse* parse = static_cast<ProbeParse*>(data);
    if (parse->stack.empty()) return;
    const std::string& element = parse->stack.back();
    std::string value(text, length);
    if (element == "RelatesTo") parse->relatesTo += value;
    else if (parse->inMatch && element == "Address") parse->matches.back().endpoint += value;
    else if (parse->inMatch && element == "Types") parse->matches.back().types += value;
    else if (parse->inMatch && element == "XAddrs") parse->matches.back().xaddrs += value;
}

std::string trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

// GMarkup never loads DTDs or expands custom entities.
bool parseXml(const std::string& text, const GMarkupParser& parser, gpointer data)
{
    GMarkupParseContext* context = g_markup_parse_context_new(&parser, (GMarkupParseFlags)0, data, NULL);
    bool ok = g_markup_parse_context_parse(context, text.data(), text.size(), NULL) &&
              g_markup_parse_context_end_parse(context, NULL);
    g_markup_parse_context_free(context);
    return ok;
}

std::vector<ProbeMatch> parseProbeMatches(const std::string& reply, const std::string& messageId)
{
    ProbeParse parse;
    GMarkupParser parser = {probeStart, probeEnd, probeText, NULL, NULL};
    if (!parseXml(reply, parser, &parse) || trim(parse.relatesTo) != messageId) return {};
    for (ProbeMatch& match : parse.matches)
    {
        match.endpoint = trim(match.endpoint);
        match.types = trim(match.types);
        match.xaddrs = trim(match.xaddrs);
    }
    return parse.matches;
}

bool isComputer(const std::string& types)
{
    gchar** tokens = g_strsplit_set(types.c_str(), " \t\r\n", -1);
    bool found = false;
    for (gchar** token = tokens; *token && !found; ++token)
        found = localName(*token) == "Computer";
    g_strfreev(tokens);
    return found;
}

struct MetadataParse
{
    std::vector<std::string> stack;
    std::string computer;
};

void metadataStart(GMarkupParseContext*, const gchar* element, const gchar**, const gchar**,
                   gpointer data, GError**)
{
    static_cast<MetadataParse*>(data)->stack.push_back(localName(element));
}

void metadataEnd(GMarkupParseContext*, const gchar*, gpointer data, GError**)
{
    static_cast<MetadataParse*>(data)->stack.pop_back();
}

void metadataText(GMarkupParseContext*, const gchar* text, gsize length, gpointer data, GError**)
{
    MetadataParse* parse = static_cast<MetadataParse*>(data);
    const size_t depth = parse->stack.size();
    if (depth >= 2 && parse->stack[depth - 1] == "Computer" && parse->stack[depth - 2] == "Host")
        parse->computer += std::string(text, length);
}

std::string dechunk(const std::string& body)
{
    std::string result;
    size_t position = 0;
    while (position < body.size())
    {
        const size_t line = body.find("\r\n", position);
        if (line == std::string::npos) return "";
        const unsigned long size = strtoul(body.substr(position, line - position).c_str(), NULL, 16);
        if (size == 0) return result;
        position = line + 2;
        if (position + size > body.size()) return "";
        result.append(body, position, size);
        position += size + 2;
    }
    return "";
}

// Parse an HTTP reply carrying WS-Transfer Get metadata; returns false on any error.
bool parseMetadataReply(const std::string& reply, Computer& computer, std::string& error)
{
    const size_t headerEnd = reply.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
    {
        error = "incomplete HTTP reply";
        return false;
    }
    const std::string headers = lower(reply.substr(0, headerEnd));
    const std::string statusLine = reply.substr(0, reply.find("\r\n"));
    if (statusLine.compare(0, 5, "HTTP/") != 0 || statusLine.find(" 200") == std::string::npos)
    {
        error = statusLine;
        return false;
    }
    std::string body = reply.substr(headerEnd + 4);
    if (headers.find("\r\ntransfer-encoding: chunked") != std::string::npos) body = dechunk(body);
    MetadataParse parse;
    GMarkupParser parser = {metadataStart, metadataEnd, metadataText, NULL, NULL};
    if (!parseXml(body, parser, &parse))
    {
        error = "invalid metadata XML";
        return false;
    }
    computer = parseComputer(trim(parse.computer));
    if (computer.name.empty())
    {
        error = "metadata has no computer name";
        return false;
    }
    return true;
}

std::string metadataRequest(const std::string& endpoint)
{
    gchar* to = g_markup_escape_text(endpoint.c_str(), -1);
    gchar* message = g_uuid_string_random();
    gchar* source = g_uuid_string_random();
    std::string body = std::string("<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:a=\"") +
                       ADDRESSING + "\"><s:Header><a:To>" + to + "</a:To>"
                       "<a:Action>http://schemas.xmlsoap.org/ws/2004/09/transfer/Get</a:Action>"
                       "<a:MessageID>urn:uuid:" + message + "</a:MessageID>"
                       "<a:ReplyTo><a:Address>" + ADDRESSING + "/role/anonymous</a:Address></a:ReplyTo>"
                       "<a:From><a:Address>urn:uuid:" + source + "</a:Address></a:From>"
                       "</s:Header><s:Body/></s:Envelope>";
    g_free(to);
    g_free(message);
    g_free(source);
    return body;
}

struct MetadataJob
{
    std::string key, url, endpoint, peer;
    Computer computer;
    std::string error;
    bool ok = false;
};

// Fetch only http URLs whose host is exactly the replying address.
void fetchMetadata(MetadataJob* job)
{
    const std::string prefix = "http://";
    if (job->url.compare(0, prefix.size(), prefix) != 0)
    {
        job->error = "not an http metadata URL";
        return;
    }
    const size_t slash = job->url.find('/', prefix.size());
    const std::string authority = job->url.substr(prefix.size(), slash == std::string::npos ?
                                                  std::string::npos : slash - prefix.size());
    const std::string path = slash == std::string::npos ? "/" : job->url.substr(slash);
    const size_t colon = authority.find(':');
    const std::string host = authority.substr(0, colon);
    const guint64 port = colon == std::string::npos ? 80 :
                         g_ascii_strtoull(authority.c_str() + colon + 1, NULL, 10);
    if (host != job->peer || port == 0 || port > 65535 || path.find_first_of("\r\n ") != std::string::npos)
    {
        job->error = "metadata URL is not on the replying address";
        return;
    }
    const std::string body = metadataRequest(job->endpoint);
    const std::string request = "POST " + path + " HTTP/1.1\r\nHost: " + authority +
                                "\r\nContent-Type: application/soap+xml; charset=utf-8"
                                "\r\nUser-Agent: swordfish-network\r\nContent-Length: " +
                                std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    GSocketClient* client = g_socket_client_new();
    g_socket_client_set_timeout(client, METADATA_TIMEOUT_S);
    GError* error = NULL;
    GSocketConnection* connection = g_socket_client_connect_to_host(client, host.c_str(),
                                                                    static_cast<guint16>(port), NULL, &error);
    std::string reply;
    if (connection &&
        g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(connection)),
                                  request.data(), request.size(), NULL, NULL, &error))
    {
        GInputStream* input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
        char buffer[8192];
        gssize bytes;
        while ((bytes = g_input_stream_read(input, buffer, sizeof(buffer), NULL, &error)) > 0)
        {
            reply.append(buffer, bytes);
            if (reply.size() > MAX_REPLY)
            {
                job->error = "metadata reply too large";
                break;
            }
        }
    }
    if (error) job->error = error->message;
    g_clear_error(&error);
    if (connection)
    {
        g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
        g_object_unref(connection);
    }
    g_object_unref(client);
    if (job->error.empty()) job->ok = parseMetadataReply(reply, job->computer, job->error);
}

bool portOpen(const std::string& target)
{
    GSocketClient* client = g_socket_client_new();
    g_socket_client_set_timeout(client, CONNECT_TIMEOUT_S);
    GSocketConnection* connection = g_socket_client_connect_to_host(client, target.c_str(), smbPort, NULL, NULL);
    if (connection)
    {
        g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
        g_object_unref(connection);
    }
    g_object_unref(client);
    return connection != NULL;
}

void finish()
{
    if (state->finished) return;
    state->finished = true;
    std::fputs("DONE\n", out);
    std::fflush(out);
    g_main_loop_quit(state->loop);
}

void maybeFinish()
{
    if (!state->wsdOpen && state->pending == 0) finish();
}

void emit(Host& host)
{
    if (host.port != OPEN || host.metadata == METADATA_PENDING || state->finished) return;
    std::string sources;
    for (const std::string& source : host.sources) sources += (sources.empty() ? "" : ",") + source;
    const std::string line = "SERVER\t" + clean(host.key) + "\t" + clean(host.connect) + "\t" +
                             clean(host.name.empty() ? host.connect : host.name) + "\t" +
                             clean(host.group) + "\t" + sources + "\t" + (host.local ? "1" : "0");
    if (line == host.printed) return;
    host.printed = line;
    std::fprintf(out, "%s\n", line.c_str());
    std::fflush(out);
}

template <typename Job>
void runThread(Job* job, void (*work)(Job*), void (*done)(Job*))
{
    ++state->pending;
    struct Call
    {
        Job* job;
        void (*work)(Job*);
        void (*done)(Job*);
    };
    Call* call = new Call{job, work, done};
    GTask* task = g_task_new(NULL, NULL, [](GObject*, GAsyncResult*, gpointer data)
    {
        Call* call = static_cast<Call*>(data);
        --state->pending;
        call->done(call->job);
        delete call;
        maybeFinish();
    }, call);
    g_task_set_task_data(task, call, NULL);
    g_task_run_in_thread(task, [](GTask* task, gpointer, gpointer data, GCancellable*)
    {
        Call* call = static_cast<Call*>(data);
        call->work(call->job);
        g_task_return_boolean(task, true);
    });
    g_object_unref(task);
}

struct PortJob
{
    std::string key, target;
    bool open;
};

void checkPort(Host& host)
{
    host.port = CHECKING;
    runThread<PortJob>(new PortJob{host.key, host.address.empty() ? host.connect : host.address, false},
        [](PortJob* job) { job->open = portOpen(job->target); },
        [](PortJob* job)
        {
            auto found = state->hosts.find(job->key);
            if (found != state->hosts.end())
            {
                found->second.port = job->open ? OPEN : CLOSED;
                emit(found->second);
            }
            delete job;
        });
}

// Merge only identical IPv4 addresses (or identical names when unresolved).
Host* addHost(const std::string& address, const std::string& hostname, const char* source)
{
    const std::string key = address.empty() ? lower(hostname) : address;
    if (key.empty()) return NULL;
    auto found = state->hosts.find(key);
    if (found == state->hosts.end())
    {
        if (state->hosts.size() >= MAX_HOSTS)
        {
            if (!state->hostLimitReported) status("discovery", "host limit reached; further hosts ignored");
            state->hostLimitReported = true;
            return NULL;
        }
        Host host;
        host.key = key;
        host.address = address;
        host.local = !address.empty() && state->localAddresses.count(address);
        found = state->hosts.insert(std::make_pair(key, host)).first;
    }
    Host& host = found->second;
    host.sources.insert(source);
    // A working mDNS/GVFS name is preferred over a bare address for connecting.
    if (!hostname.empty() && (host.connect.empty() || host.connect == host.address)) host.connect = hostname;
    if (host.connect.empty()) host.connect = address;
    if (host.port == UNCHECKED) checkPort(host);
    else emit(host);
    return &host;
}

struct ResolveJob
{
    std::string host, name, address;
};

void addGvfsHost(const std::string& hostname, const std::string& name)
{
    if (!state->gvfsHosts.insert(lower(hostname)).second) return;
    runThread<ResolveJob>(new ResolveJob{hostname, name, ""},
        [](ResolveJob* job)
        {
            if (isIPv4(job->host))
            {
                job->address = job->host;
                return;
            }
            GList* addresses = g_resolver_lookup_by_name(g_resolver_get_default(), job->host.c_str(), NULL, NULL);
            for (GList* node = addresses; node && job->address.empty(); node = node->next)
                if (g_inet_address_get_family(G_INET_ADDRESS(node->data)) == G_SOCKET_FAMILY_IPV4)
                {
                    gchar* text = g_inet_address_to_string(G_INET_ADDRESS(node->data));
                    job->address = text;
                    g_free(text);
                }
            g_resolver_free_addresses(addresses);
        },
        [](ResolveJob* job)
        {
            Host* host = addHost(job->address, job->host, "gvfs");
            if (host && !host->nameFromMetadata && host->name.empty()) host->name = job->name;
            if (host) emit(*host);
            delete job;
        });
}

struct GvfsRoot
{
    std::string uri;
    GFileEnumerator* enumerator = NULL;
};

void gvfsFinished(GvfsRoot* root, GError* error)
{
    if (error)
    {
        status("gvfs", root->uri + ": " + error->message);
        g_error_free(error);
    }
    if (root->enumerator)
    {
        g_file_enumerator_close(root->enumerator, NULL, NULL);
        g_object_unref(root->enumerator);
    }
    delete root;
    --state->pending;
    maybeFinish();
}

void gvfsBatch(GObject* source, GAsyncResult* result, gpointer data)
{
    GvfsRoot* root = static_cast<GvfsRoot*>(data);
    GError* error = NULL;
    GList* entries = g_file_enumerator_next_files_finish(G_FILE_ENUMERATOR(source), result, &error);
    if (error || !entries)
    {
        gvfsFinished(root, error);
        return;
    }
    for (GList* node = entries; node; node = node->next)
    {
        GFileInfo* info = G_FILE_INFO(node->data);
        const char* target = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_STANDARD_TARGET_URI);
        const std::string host = target ? smbHost(target) : "";
        if (host.empty()) continue;
        const char* display = g_file_info_get_display_name(info);
        addGvfsHost(host, display ? display : host);
    }
    g_list_free_full(entries, g_object_unref);
    g_file_enumerator_next_files_async(root->enumerator, 50, G_PRIORITY_DEFAULT, NULL, gvfsBatch, root);
}

void gvfsEnumerated(GObject* source, GAsyncResult* result, gpointer data)
{
    GvfsRoot* root = static_cast<GvfsRoot*>(data);
    GError* error = NULL;
    root->enumerator = g_file_enumerate_children_finish(G_FILE(source), result, &error);
    g_object_unref(source);
    if (!root->enumerator) gvfsFinished(root, error);
    else g_file_enumerator_next_files_async(root->enumerator, 50, G_PRIORITY_DEFAULT, NULL, gvfsBatch, root);
}

// Existing desktop roots only; NOT_MOUNTED is reported, never mounted.
void startGvfs()
{
    for (const char* uri : {"network:///", "dns-sd://local/"})
    {
        ++state->pending;
        GvfsRoot* root = new GvfsRoot();
        root->uri = uri;
        g_file_enumerate_children_async(g_file_new_for_uri(uri),
            G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
            G_FILE_ATTRIBUTE_STANDARD_TARGET_URI,
            G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT, NULL, gvfsEnumerated, root);
    }
}

void handleProbeMatch(const ProbeMatch& match, const std::string& peer)
{
    if (!isComputer(match.types)) return;
    auto known = state->hosts.find(peer);
    if (known != state->hosts.end() && known->second.sources.count("ws-discovery")) return;
    Host* host = addHost(peer, "", "ws-discovery");
    if (!host || match.endpoint.empty()) return;
    gchar** urls = g_strsplit_set(match.xaddrs.c_str(), " \t\r\n", -1);
    std::string url;
    for (gchar** item = urls; *item && url.empty(); ++item)
        if (g_str_has_prefix(*item, ("http://" + peer + ":").c_str()) ||
            g_str_has_prefix(*item, ("http://" + peer + "/").c_str()))
            url = *item;
    g_strfreev(urls);
    if (url.empty()) return;
    host->metadata = METADATA_PENDING;
    MetadataJob* job = new MetadataJob();
    job->key = host->key;
    job->url = url;
    job->endpoint = match.endpoint;
    job->peer = peer;
    runThread<MetadataJob>(job, fetchMetadata, [](MetadataJob* job)
    {
        auto found = state->hosts.find(job->key);
        if (found != state->hosts.end())
        {
            Host& host = found->second;
            host.metadata = METADATA_DONE;
            if (job->ok)
            {
                host.name = job->computer.name;
                host.group = job->computer.group;
                host.nameFromMetadata = true;
            }
            else
            {
                status("ws-discovery", job->peer + ": metadata: " + job->error);
            }
            emit(host);
        }
        delete job;
    });
}

gboolean wsdReadable(GSocket* socket, GIOCondition, gpointer)
{
    char buffer[65536];
    for (;;)
    {
        GSocketAddress* sender = NULL;
        GError* error = NULL;
        const gssize bytes = g_socket_receive_from(socket, &sender, buffer, sizeof(buffer), NULL, &error);
        if (bytes < 0)
        {
            if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
                status("ws-discovery", std::string("receive failed: ") + error->message);
            g_error_free(error);
            break;
        }
        std::string peer;
        if (G_IS_INET_SOCKET_ADDRESS(sender))
        {
            gchar* text = g_inet_address_to_string(
                g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(sender)));
            peer = text;
            g_free(text);
        }
        if (sender) g_object_unref(sender);
        if (isIPv4(peer))
            for (const ProbeMatch& match : parseProbeMatches(std::string(buffer, bytes), state->probeId))
                handleProbeMatch(match, peer);
    }
    return G_SOURCE_CONTINUE;
}

void sendProbes()
{
    GInetAddress* group = g_inet_address_new_from_string(WSD_GROUP);
    GSocketAddress* destination = g_inet_socket_address_new(group, WSD_PORT);
    for (GSocket* socket : state->sockets)
    {
        GError* error = NULL;
        if (g_socket_send_to(socket, destination, state->probe.data(), state->probe.size(), NULL, &error) < 0)
        {
            status("ws-discovery", std::string("send failed: ") + error->message);
            g_error_free(error);
        }
    }
    g_object_unref(destination);
    g_object_unref(group);
}

void stopWsd()
{
    for (GSource* source : state->sources)
    {
        g_source_destroy(source);
        g_source_unref(source);
    }
    for (GSocket* socket : state->sockets)
    {
        g_socket_close(socket, NULL);
        g_object_unref(socket);
    }
    state->sources.clear();
    state->sockets.clear();
    state->wsdOpen = false;
    maybeFinish();
}

void startWsd()
{
    gchar* id = g_uuid_string_random();
    state->probeId = std::string("urn:uuid:") + id;
    g_free(id);
    state->probe = std::string("<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:a=\"") +
                   ADDRESSING + "\" xmlns:d=\"" + WSD +
                   "\" xmlns:p=\"http://schemas.xmlsoap.org/ws/2006/02/devprof\"><s:Header>"
                   "<a:Action>" + WSD + "/Probe</a:Action><a:MessageID>" + state->probeId +
                   "</a:MessageID><a:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To></s:Header>"
                   "<s:Body><d:Probe><d:Types>p:Device</d:Types></d:Probe></s:Body></s:Envelope>";
    for (const Interface& iface : state->interfaces)
    {
        GError* error = NULL;
        GSocket* socket = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM,
                                       G_SOCKET_PROTOCOL_UDP, &error);
        GInetAddress* local = g_inet_address_new_from_string(iface.address.c_str());
        GSocketAddress* bound = g_inet_socket_address_new(local, 0);
        struct in_addr multicast;
        inet_pton(AF_INET, iface.address.c_str(), &multicast);
        const bool ok = socket && g_socket_bind(socket, bound, false, &error) &&
                        setsockopt(g_socket_get_fd(socket), IPPROTO_IP, IP_MULTICAST_IF,
                                   &multicast, sizeof(multicast)) == 0;
        g_object_unref(bound);
        g_object_unref(local);
        if (!ok)
        {
            status("ws-discovery", iface.name + ": " + (error ? error->message : strerror(errno)));
            g_clear_error(&error);
            if (socket) g_object_unref(socket);
            continue;
        }
        g_socket_set_multicast_ttl(socket, 1);
        g_socket_set_blocking(socket, false);
        GSource* source = g_socket_create_source(socket, G_IO_IN, NULL);
        g_source_set_callback(source, reinterpret_cast<GSourceFunc>(reinterpret_cast<void (*)()>(wsdReadable)), NULL, NULL);
        g_source_attach(source, NULL);
        state->sockets.push_back(socket);
        state->sources.push_back(source);
    }
    if (state->sockets.empty()) return;
    state->wsdOpen = true;
    sendProbes();
    g_timeout_add(WSD_RETRY_MS, [](gpointer) -> gboolean
    {
        if (state->wsdOpen) sendProbes();
        return G_SOURCE_REMOVE;
    }, NULL);
    g_timeout_add(WSD_WINDOW_MS, [](gpointer) -> gboolean
    {
        stopWsd();
        return G_SOURCE_REMOVE;
    }, NULL);
}

void readInterfaces()
{
    struct ifaddrs* list = NULL;
    if (getifaddrs(&list) != 0)
    {
        status("discovery", std::string("cannot list interfaces: ") + strerror(errno));
        return;
    }
    for (struct ifaddrs* item = list; item; item = item->ifa_next)
    {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET) continue;
        char text[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in*>(item->ifa_addr)->sin_addr, text, sizeof(text));
        state->localAddresses.insert(text);
        if ((item->ifa_flags & IFF_UP) && !(item->ifa_flags & IFF_LOOPBACK) && (item->ifa_flags & IFF_MULTICAST))
            state->interfaces.push_back(Interface{item->ifa_name, text});
    }
    freeifaddrs(list);
}

int run()
{
    state = new State();
    state->loop = g_main_loop_new(NULL, false);
    readInterfaces();
    if (state->interfaces.empty()) status("ws-discovery", "no active IPv4 network interface");
    startGvfs();
    startWsd();
    g_timeout_add(DEADLINE_MS, [](gpointer) -> gboolean
    {
        status("discovery", "time limit reached");
        finish();
        return G_SOURCE_REMOVE;
    }, NULL);
    maybeFinish();
    if (!state->finished) g_main_loop_run(state->loop);
    // Worker threads may still be blocked after the deadline; leave state allocated.
    return 0;
}
}

int main(int argc, char** argv)
{
    if (argc == 3 && strcmp(argv[1], "--list") == 0) return listDirectory(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--shares") == 0)
    {
        const std::string host = discovery::smbHost(argv[2]);
        const std::string uri = argv[2];
        if (host.empty() || (uri != "smb://" + host && uri != "smb://" + host + "/"))
        {
            std::puts("ERROR Invalid SMB server address");
            return 2;
        }
        return listDirectory(argv[2], true);
    }
    if (argc == 2 && strcmp(argv[1], "--discover") == 0) return discovery::run();
    std::fprintf(stderr, "Usage: swordfish-network --discover | --shares smb://HOST/ | --list PATH\n");
    return 2;
}
