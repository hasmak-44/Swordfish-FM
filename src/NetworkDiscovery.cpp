#include <gio/gio.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <map>
#include <net/if.h>
#include <netinet/in.h>
#include <set>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
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

// Windows administrative pseudo-shares (ADMIN$, IPC$, PRINT$, C$...) cannot be opened from
// other systems, and "nobody" is Samba's service account rather than a real share.
bool unusableShare(const char* name)
{
    std::string share(name);
    for (char& c : share) c = static_cast<char>(g_ascii_tolower(c));
    return share == "admin$" || share == "ipc$" || share == "print$" || share == "nobody" ||
           (share.length() == 2 && g_ascii_isalpha(share[0]) && share[1] == '$');
}

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
            if (unusableShare(name)) continue;
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
    // Try anonymous, then a "guest" login with no password (as smb4k does), then ask the user
    int* stage = static_cast<int*>(data);
    if (*stage == 0)
    {
        *stage = 1;
        if (flags & G_ASK_PASSWORD_ANONYMOUS_SUPPORTED)
        {
            g_mount_operation_set_anonymous(operation, true);
            g_mount_operation_reply(operation, G_MOUNT_OPERATION_HANDLED);
            return;
        }
    }
    if (*stage == 1)
    {
        *stage = 2;
        g_mount_operation_set_anonymous(operation, false);
        g_mount_operation_set_username(operation, "guest");
        g_mount_operation_set_domain(operation, "WORKGROUP");
        g_mount_operation_set_password(operation, "");
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
        g_signal_connect_data(operation, "ask-password", G_CALLBACK(sharePassword), new int(0),
            [](gpointer value, GClosure*) { delete static_cast<int*>(value); }, GConnectFlags(0));
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
const char* const MDNS_GROUP = "224.0.0.251";
const guint16 MDNS_PORT = 5353;
const guint16 SSDP_PORT = 1900;
const guint SERVICE_RETRY_MS = 1500;
const guint SERVICE_WINDOW_MS = 5000;
const size_t MAX_SERVICES = 256;

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

// Advertised non-SMB service: FTP/SFTP, media server, printer or router/access point.
struct Service
{
    std::string kind, key, address, name, url;
    std::set<std::string> sources;
    std::string printed;
};

struct ServiceSocket
{
    GSocket* socket;
    GSource* source;
    bool mdns;
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
    std::map<std::string, Service> services;
    std::set<std::string> ssdpLocations;
    std::vector<ServiceSocket> serviceSockets;
    std::string mdnsQuery;
    int pending = 0;
    bool wsdOpen = false;
    bool servicesOpen = false;
    bool serviceLimitReported = false;
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

bool hasType(const std::string& types, const char* wanted)
{
    gchar** tokens = g_strsplit_set(types.c_str(), " \t\r\n", -1);
    bool found = false;
    for (gchar** token = tokens; *token && !found; ++token)
        found = localName(*token).find(wanted) != std::string::npos;
    g_strfreev(tokens);
    return found;
}

bool isPrinter(const std::string& types)
{
    return hasType(types, "PrintDeviceType") || hasType(types, "PrinterServiceType");
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

struct HttpUrl
{
    std::string host, path;
    guint16 port = 80;
};

// Plain http URLs only: no user information, IPv6 literals or control characters.
bool parseHttpUrl(const std::string& url, HttpUrl& parsed)
{
    if (g_ascii_strncasecmp(url.c_str(), "http://", 7) != 0) return false;
    const size_t slash = url.find('/', 7);
    const std::string authority = url.substr(7, slash == std::string::npos ? std::string::npos : slash - 7);
    parsed.path = slash == std::string::npos ? "/" : url.substr(slash);
    const size_t hash = parsed.path.find('#');
    if (hash != std::string::npos) parsed.path.resize(hash);
    if (authority.empty() || authority.find_first_of("@[]") != std::string::npos) return false;
    const size_t colon = authority.find(':');
    parsed.host = authority.substr(0, colon);
    guint64 port = 80;
    if (colon != std::string::npos)
    {
        const std::string digits = authority.substr(colon + 1);
        if (digits.empty() || digits.size() > 5 || digits.find_first_not_of("0123456789") != std::string::npos)
            return false;
        port = g_ascii_strtoull(digits.c_str(), NULL, 10);
    }
    if (parsed.host.empty() || port == 0 || port > 65535) return false;
    for (char c : parsed.host)
        if (!(g_ascii_isalnum(c) || c == '.' || c == '-' || c == '_')) return false;
    for (char c : parsed.path)
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f) return false;
    parsed.port = static_cast<guint16>(port);
    return true;
}

// Resolve a URL from a UPnP description against the description's own URL.
std::string resolveUrl(const std::string& base, const std::string& reference)
{
    const std::string ref = trim(reference);
    if (ref.empty()) return "";
    if (g_ascii_strncasecmp(ref.c_str(), "http://", 7) == 0 || g_ascii_strncasecmp(ref.c_str(), "https://", 8) == 0)
        return ref;
    if (ref.find("://") != std::string::npos) return "";
    HttpUrl url;
    if (!parseHttpUrl(base, url)) return "";
    const std::string root = "http://" + url.host + ":" + std::to_string(url.port);
    if (ref[0] == '/') return root + ref;
    std::string directory = url.path.substr(0, url.path.find('?'));
    directory = directory.substr(0, directory.rfind('/') + 1);
    return root + directory + ref;
}

// One HTTP/1.1 exchange with Connection: close; the reply is capped at MAX_REPLY.
std::string httpExchange(const HttpUrl& url, const std::string& method, const std::string& headers,
                         const std::string& content, std::string& error, guint timeout = METADATA_TIMEOUT_S)
{
    std::string request = method + " " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" +
                          std::to_string(url.port) + "\r\nUser-Agent: swordfish-network\r\nConnection: close\r\n" +
                          headers;
    if (method == "POST") request += "Content-Length: " + std::to_string(content.size()) + "\r\n";
    request += "\r\n" + content;
    GSocketClient* client = g_socket_client_new();
    g_socket_client_set_timeout(client, timeout);
    GError* failure = NULL;
    GSocketConnection* connection = g_socket_client_connect_to_host(client, url.host.c_str(), url.port, NULL, &failure);
    std::string reply;
    if (connection &&
        g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(connection)),
                                  request.data(), request.size(), NULL, NULL, &failure))
    {
        GInputStream* input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
        char buffer[8192];
        gssize bytes;
        while ((bytes = g_input_stream_read(input, buffer, sizeof(buffer), NULL, &failure)) > 0)
        {
            reply.append(buffer, bytes);
            if (reply.size() > MAX_REPLY)
            {
                error = "reply too large";
                break;
            }
        }
    }
    if (failure && error.empty()) error = failure->message;
    g_clear_error(&failure);
    if (connection)
    {
        g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
        g_object_unref(connection);
    }
    g_object_unref(client);
    return reply;
}

// Body of a complete "200" reply, with chunked transfer encoding removed.
bool httpBody(const std::string& reply, std::string& body, std::string& error)
{
    const size_t headerEnd = reply.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
    {
        if (error.empty()) error = "incomplete HTTP reply";
        return false;
    }
    const std::string statusLine = reply.substr(0, reply.find("\r\n"));
    if (statusLine.compare(0, 5, "HTTP/") != 0 || statusLine.find(" 200") == std::string::npos)
    {
        error = statusLine;
        return false;
    }
    body = reply.substr(headerEnd + 4);
    if (lower(reply.substr(0, headerEnd)).find("\r\ntransfer-encoding: chunked") != std::string::npos)
        body = dechunk(body);
    return true;
}

std::string httpGet(const std::string& address, std::string& body, guint timeout = METADATA_TIMEOUT_S)
{
    HttpUrl url;
    if (!parseHttpUrl(address, url)) return "not a plain http URL";
    std::string error;
    const std::string reply = httpExchange(url, "GET", "", "", error, timeout);
    return httpBody(reply, body, error) ? "" : error;
}

struct UpnpService
{
    std::string type, control;
};

struct UpnpDevice
{
    std::vector<std::string> types;
    std::string name, presentation, base;
    std::vector<UpnpService> services;
};

struct DescriptionParse
{
    std::vector<std::string> stack;
    std::string text;
    UpnpService service;
    UpnpDevice device;
};

void descriptionStart(GMarkupParseContext*, const gchar* element, const gchar**, const gchar**,
                      gpointer data, GError**)
{
    DescriptionParse* parse = static_cast<DescriptionParse*>(data);
    parse->stack.push_back(localName(element));
    parse->text.clear();
    if (parse->stack.back() == "service") parse->service = UpnpService();
}

void descriptionEnd(GMarkupParseContext*, const gchar*, gpointer data, GError**)
{
    DescriptionParse* parse = static_cast<DescriptionParse*>(data);
    const std::string element = parse->stack.back();
    const std::string value = trim(parse->text);
    const std::string parent = parse->stack.size() >= 2 ? parse->stack[parse->stack.size() - 2] : "";
    UpnpDevice& device = parse->device;
    if (element == "deviceType" && parent == "device") device.types.push_back(value);
    else if (element == "friendlyName" && parent == "device" && device.name.empty()) device.name = value;
    else if (element == "presentationURL" && parent == "device" && device.presentation.empty())
        device.presentation = value;
    else if (element == "URLBase") device.base = value;
    else if (element == "serviceType" && parent == "service") parse->service.type = value;
    else if (element == "controlURL" && parent == "service") parse->service.control = value;
    else if (element == "service" && !parse->service.type.empty() && !parse->service.control.empty())
        device.services.push_back(parse->service);
    parse->stack.pop_back();
    parse->text.clear();
}

void descriptionText(GMarkupParseContext*, const gchar* text, gsize length, gpointer data, GError**)
{
    static_cast<DescriptionParse*>(data)->text.append(text, length);
}

bool parseDescription(const std::string& xml, UpnpDevice& device)
{
    DescriptionParse parse;
    GMarkupParser parser = {descriptionStart, descriptionEnd, descriptionText, NULL, NULL};
    if (!parseXml(xml, parser, &parse) || parse.device.types.empty()) return false;
    device = parse.device;
    return true;
}

// Control URL of the device's ContentDirectory (media servers only).
UpnpService contentDirectory(const UpnpDevice& device, const std::string& location)
{
    for (const UpnpService& service : device.services)
        if (service.type.find(":service:ContentDirectory:") != std::string::npos)
            return UpnpService{service.type, resolveUrl(device.base.empty() ? location : device.base, service.control)};
    return UpnpService();
}

// media, printer, router (gateways and Wi-Fi access points) or "" for anything else.
std::string classifyDevice(const UpnpDevice& device)
{
    std::string result;
    for (const std::string& type : device.types)
    {
        if (type.find(":device:MediaServer:") != std::string::npos) return "media";
        if (type.find(":device:Printer:") != std::string::npos) result = "printer";
        else if (result.empty() &&
                 (type.find(":device:InternetGatewayDevice:") != std::string::npos ||
                  type.find(":device:WANDevice:") != std::string::npos ||
                  type.find(":device:WANConnectionDevice:") != std::string::npos ||
                  type.find(":device:LANDevice:") != std::string::npos ||
                  type.find(":device:WFADevice:") != std::string::npos ||
                  type.find(":device:WLANAccessPointDevice:") != std::string::npos))
            result = "router";
    }
    return result;
}

// LOCATION of an SSDP M-SEARCH "200 OK" reply.
std::string ssdpLocation(const std::string& reply)
{
    const size_t end = reply.find("\r\n");
    if (end == std::string::npos || reply.compare(0, 9, "HTTP/1.1 ") != 0 ||
        reply.substr(0, end).find(" 200") == std::string::npos)
        return "";
    size_t position = end + 2;
    while (position < reply.size())
    {
        size_t next = reply.find("\r\n", position);
        if (next == std::string::npos) next = reply.size();
        const std::string line = reply.substr(position, next - position);
        const size_t colon = line.find(':');
        if (colon != std::string::npos && lower(trim(line.substr(0, colon))) == "location")
            return trim(line.substr(colon + 1));
        if (line.empty()) break;
        position = next + 2;
    }
    return "";
}

struct MdnsType
{
    const char* type;
    const char* kind;
    guint16 port;
};

const MdnsType MDNS_TYPES[] = {
    {"_ftp._tcp.local", "ftp", 21},
    {"_sftp-ssh._tcp.local", "sftp", 22},
    {"_ssh._tcp.local", "sftp", 22},
    {"_ipp._tcp.local", "printer", 631},
    {"_ipps._tcp.local", "printer", 631},
    {"_printer._tcp.local", "printer", 515},
    {"_pdl-datastream._tcp.local", "printer", 9100},
};

struct MdnsService
{
    std::string kind, name, address, adminUrl;
    guint16 port = 0;
};

// One legacy-unicast query (ephemeral source port) asking for every service type above.
std::string mdnsQuery(guint16 id)
{
    std::string packet;
    auto put16 = [&packet](guint16 value)
    {
        packet += static_cast<char>(value >> 8);
        packet += static_cast<char>(value & 0xff);
    };
    put16(id);
    put16(0);
    put16(G_N_ELEMENTS(MDNS_TYPES));
    put16(0);
    put16(0);
    put16(0);
    for (const MdnsType& type : MDNS_TYPES)
    {
        gchar** labels = g_strsplit(type.type, ".", -1);
        for (gchar** label = labels; *label; ++label)
        {
            packet += static_cast<char>(strlen(*label));
            packet += *label;
        }
        g_strfreev(labels);
        packet += '\0';
        put16(12);
        put16(1);
    }
    return packet;
}

// DNS name with compression; bounded so a hostile packet cannot loop or overrun.
bool readName(const std::string& packet, size_t& offset, std::vector<std::string>& labels)
{
    size_t position = offset;
    bool jumped = false;
    int jumps = 0;
    size_t total = 0;
    for (;;)
    {
        if (position >= packet.size()) return false;
        const unsigned length = static_cast<unsigned char>(packet[position]);
        if ((length & 0xc0) == 0xc0)
        {
            if (position + 1 >= packet.size() || ++jumps > 32) return false;
            const size_t target = ((length & 0x3f) << 8) | static_cast<unsigned char>(packet[position + 1]);
            if (!jumped) offset = position + 2;
            jumped = true;
            position = target;
            continue;
        }
        if (length & 0xc0) return false;
        if (length == 0)
        {
            if (!jumped) offset = position + 1;
            return true;
        }
        total += length + 1;
        if (position + 1 + length > packet.size() || total > 255) return false;
        labels.push_back(packet.substr(position + 1, length));
        position += 1 + length;
    }
}

std::string joinName(const std::vector<std::string>& labels)
{
    std::string name;
    for (const std::string& label : labels) name += (name.empty() ? "" : ".") + label;
    return lower(name);
}

std::vector<MdnsService> parseMdnsReply(const std::string& packet, const std::string& peer)
{
    std::vector<MdnsService> result;
    if (packet.size() < 12 || !(static_cast<unsigned char>(packet[2]) & 0x80)) return result;
    auto get16 = [&packet](size_t at) -> unsigned
    {
        return (static_cast<unsigned char>(packet[at]) << 8) | static_cast<unsigned char>(packet[at + 1]);
    };
    const unsigned questions = get16(4);
    const unsigned records = get16(6) + get16(8) + get16(10);
    size_t offset = 12;
    for (unsigned i = 0; i < questions; ++i)
    {
        std::vector<std::string> ignored;
        if (!readName(packet, offset, ignored) || offset + 4 > packet.size()) return result;
        offset += 4;
    }
    std::map<std::string, std::vector<std::vector<std::string>>> pointers;
    std::map<std::string, std::pair<guint16, std::string>> targets;
    std::map<std::string, std::string> admin, addresses;
    for (unsigned i = 0; i < records && i < 512; ++i)
    {
        std::vector<std::string> owner;
        if (!readName(packet, offset, owner) || offset + 10 > packet.size()) break;
        const unsigned type = get16(offset);
        const size_t length = get16(offset + 8);
        const size_t data = offset + 10;
        if (data + length > packet.size()) break;
        offset = data + length;
        const std::string name = joinName(owner);
        size_t cursor = data;
        std::vector<std::string> target;
        if (type == 12 && readName(packet, cursor, target)) pointers[name].push_back(target);
        else if (type == 33 && length > 6)
        {
            cursor = data + 6;
            if (readName(packet, cursor, target)) targets[name] = std::make_pair(get16(data + 4), joinName(target));
        }
        else if (type == 16)
        {
            while (cursor < data + length)
            {
                const size_t size = static_cast<unsigned char>(packet[cursor]);
                if (cursor + 1 + size > data + length) break;
                const std::string entry = packet.substr(cursor + 1, size);
                if (lower(entry.substr(0, 9)) == "adminurl=") admin[name] = entry.substr(9);
                cursor += 1 + size;
            }
        }
        else if (type == 1 && length == 4)
        {
            char text[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, packet.data() + data, text, sizeof(text));
            addresses[name] = text;
        }
    }
    for (const MdnsType& type : MDNS_TYPES)
    {
        auto found = pointers.find(type.type);
        if (found == pointers.end()) continue;
        for (const std::vector<std::string>& instance : found->second)
        {
            if (instance.empty()) continue;
            const std::string key = joinName(instance);
            MdnsService service;
            service.kind = type.kind;
            service.name = instance[0];
            service.port = type.port;
            service.address = peer;
            auto srv = targets.find(key);
            if (srv != targets.end())
            {
                service.port = srv->second.first;
                auto address = addresses.find(srv->second.second);
                if (address != addresses.end()) service.address = address->second;
            }
            auto url = admin.find(key);
            if (url != admin.end()) service.adminUrl = url->second;
            result.push_back(service);
        }
    }
    return result;
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
    if (!state->wsdOpen && !state->servicesOpen && state->pending == 0) finish();
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

// SERVICE kind key address name url sources
void emitService(Service& service)
{
    if (state->finished) return;
    std::string sources;
    for (const std::string& source : service.sources) sources += (sources.empty() ? "" : ",") + source;
    const std::string line = "SERVICE\t" + service.kind + "\t" + clean(service.key, 512) + "\t" +
                             clean(service.address) + "\t" +
                             clean(service.name.empty() ? service.address : service.name) + "\t" +
                             clean(service.url, 512) + "\t" + sources;
    if (line == service.printed) return;
    service.printed = line;
    std::fprintf(out, "%s\n", line.c_str());
    std::fflush(out);
}

void addService(const std::string& kind, const std::string& key, const std::string& address,
                const std::string& name, const std::string& url, const char* source)
{
    if (!isIPv4(address) || state->localAddresses.count(address)) return;
    auto found = state->services.find(key);
    if (found == state->services.end())
    {
        if (state->services.size() >= MAX_SERVICES)
        {
            if (!state->serviceLimitReported) status("discovery", "device limit reached; further devices ignored");
            state->serviceLimitReported = true;
            return;
        }
        Service service;
        service.kind = kind;
        service.key = key;
        service.address = address;
        found = state->services.insert(std::make_pair(key, service)).first;
    }
    Service& service = found->second;
    service.sources.insert(source);
    if (service.name.empty()) service.name = trim(name);
    if (service.url.empty()) service.url = url;
    emitService(service);
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
    if (isPrinter(match.types))
    {
        addService("printer", "printer@" + peer, peer, "", "http://" + peer + "/", "ws-discovery");
        return;
    }
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

// A device's web page must be served by that device itself.
bool sameHostPage(const std::string& page, const std::string& peer)
{
    const size_t scheme = g_ascii_strncasecmp(page.c_str(), "http://", 7) == 0 ? 7 :
                          g_ascii_strncasecmp(page.c_str(), "https://", 8) == 0 ? 8 : 0;
    if (!scheme || page.compare(scheme, peer.size(), peer) != 0) return false;
    const size_t end = scheme + peer.size();
    if (end != page.size() && page[end] != '/' && page[end] != ':') return false;
    for (char c : page)
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f) return false;
    return true;
}

struct DescriptionJob
{
    std::string location, peer, error, body;
};

void describeDevice(const std::string& location, const std::string& peer)
{
    HttpUrl url;
    if (!parseHttpUrl(location, url) || url.host != peer || state->ssdpLocations.size() >= MAX_SERVICES ||
        !state->ssdpLocations.insert(location).second)
        return;
    runThread<DescriptionJob>(new DescriptionJob{location, peer, "", ""},
        [](DescriptionJob* job) { job->error = httpGet(job->location, job->body); },
        [](DescriptionJob* job)
        {
            UpnpDevice device;
            if (!job->error.empty()) status("ssdp", job->location + ": " + job->error);
            else if (!parseDescription(job->body, device)) status("ssdp", job->location + ": invalid description");
            else
            {
                const std::string kind = classifyDevice(device);
                const std::string base = device.base.empty() ? job->location : device.base;
                std::string page = resolveUrl(base, device.presentation);
                if (!sameHostPage(page, job->peer)) page = "http://" + job->peer + "/";
                if (kind == "media")
                {
                    HttpUrl control;
                    if (parseHttpUrl(contentDirectory(device, job->location).control, control) &&
                        control.host == job->peer)
                        addService(kind, "media@" + job->location, job->peer, device.name, job->location, "ssdp");
                }
                else if (!kind.empty())
                {
                    addService(kind, kind + "@" + job->peer, job->peer, device.name, page, "ssdp");
                }
            }
            delete job;
        });
}

void addMdnsService(const MdnsService& found)
{
    const std::string port = std::to_string(found.port);
    if (found.kind == "printer")
    {
        const std::string admin = trim(found.adminUrl);
        addService("printer", "printer@" + found.address, found.address, found.name,
                   sameHostPage(admin, found.address) ? admin : "http://" + found.address + "/", "mdns");
    }
    else
    {
        addService(found.kind, found.kind + "@" + found.address + ":" + port, found.address, found.name,
                   found.kind + "://" + found.address + ":" + port + "/", "mdns");
    }
}

gboolean serviceReadable(GSocket* socket, GIOCondition, gpointer data)
{
    const bool mdns = data != NULL;
    char buffer[9000];
    for (;;)
    {
        GSocketAddress* sender = NULL;
        GError* error = NULL;
        const gssize bytes = g_socket_receive_from(socket, &sender, buffer, sizeof(buffer), NULL, &error);
        if (bytes < 0)
        {
            if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
                status(mdns ? "mdns" : "ssdp", std::string("receive failed: ") + error->message);
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
        if (!isIPv4(peer)) continue;
        const std::string reply(buffer, bytes);
        if (mdns)
            for (const MdnsService& service : parseMdnsReply(reply, peer)) addMdnsService(service);
        else
            describeDevice(ssdpLocation(reply), peer);
    }
    return G_SOURCE_CONTINUE;
}

void sendServiceQueries()
{
    const std::string search = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n"
                               "MX: 2\r\nST: ssdp:all\r\nUSER-AGENT: Linux UPnP/1.1 swordfish-network/1\r\n\r\n";
    for (const ServiceSocket& item : state->serviceSockets)
    {
        GInetAddress* group = g_inet_address_new_from_string(item.mdns ? MDNS_GROUP : WSD_GROUP);
        GSocketAddress* destination = g_inet_socket_address_new(group, item.mdns ? MDNS_PORT : SSDP_PORT);
        const std::string& packet = item.mdns ? state->mdnsQuery : search;
        GError* error = NULL;
        if (g_socket_send_to(item.socket, destination, packet.data(), packet.size(), NULL, &error) < 0)
        {
            status(item.mdns ? "mdns" : "ssdp", std::string("send failed: ") + error->message);
            g_error_free(error);
        }
        g_object_unref(destination);
        g_object_unref(group);
    }
}

void stopServices()
{
    for (ServiceSocket& item : state->serviceSockets)
    {
        g_source_destroy(item.source);
        g_source_unref(item.source);
        g_socket_close(item.socket, NULL);
        g_object_unref(item.socket);
    }
    state->serviceSockets.clear();
    state->servicesOpen = false;
    maybeFinish();
}

// mDNS (FTP, SFTP, printers) and SSDP (media servers, printers, routers): advertised services only.
void startServices()
{
    state->mdnsQuery = mdnsQuery(static_cast<guint16>(g_random_int_range(1, 65536)));
    for (const Interface& iface : state->interfaces)
        for (bool mdns : {true, false})
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
                status(mdns ? "mdns" : "ssdp", iface.name + ": " + (error ? error->message : strerror(errno)));
                g_clear_error(&error);
                if (socket) g_object_unref(socket);
                continue;
            }
            g_socket_set_multicast_ttl(socket, mdns ? 255 : 2);
            g_socket_set_blocking(socket, false);
            GSource* source = g_socket_create_source(socket, G_IO_IN, NULL);
            g_source_set_callback(source, reinterpret_cast<GSourceFunc>(reinterpret_cast<void (*)()>(serviceReadable)),
                                  mdns ? GINT_TO_POINTER(1) : NULL, NULL);
            g_source_attach(source, NULL);
            state->serviceSockets.push_back(ServiceSocket{socket, source, mdns});
        }
    if (state->serviceSockets.empty()) return;
    state->servicesOpen = true;
    sendServiceQueries();
    g_timeout_add(SERVICE_RETRY_MS, [](gpointer) -> gboolean
    {
        if (state->servicesOpen) sendServiceQueries();
        return G_SOURCE_REMOVE;
    }, NULL);
    g_timeout_add(SERVICE_WINDOW_MS, [](gpointer) -> gboolean
    {
        stopServices();
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
    startServices();
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

// Read-only UPnP ContentDirectory browsing for dlna://HOST:PORT/Folder/Sub paths.
namespace media
{
using namespace discovery;

const guint BROWSE_TIMEOUT_S = 15;
const guint PAGE = 100;
const size_t MAX_OBJECTS = 10000;

struct Object
{
    bool container = false;
    std::string id, title, url, mime, date, name;
    gint64 size = -1;
};

struct DidlParse
{
    std::vector<std::string> stack;
    std::vector<Object> objects;
    std::string text, resMime;
    gint64 resSize = -1;
    bool resHttp = false;
};

void didlStart(GMarkupParseContext*, const gchar* element, const gchar** names, const gchar** values,
               gpointer data, GError**)
{
    DidlParse* parse = static_cast<DidlParse*>(data);
    const std::string name = localName(element);
    parse->text.clear();
    if ((name == "container" || name == "item") && parse->stack.size() == 1)
    {
        Object object;
        object.container = name == "container";
        for (int i = 0; names[i]; ++i)
            if (strcmp(names[i], "id") == 0) object.id = values[i];
        parse->objects.push_back(object);
    }
    else if (name == "res")
    {
        parse->resHttp = false;
        parse->resMime.clear();
        parse->resSize = -1;
        for (int i = 0; names[i]; ++i)
        {
            if (strcmp(names[i], "protocolInfo") == 0)
            {
                gchar** fields = g_strsplit(values[i], ":", 4);
                parse->resHttp = g_strv_length(fields) == 4 && strcmp(fields[0], "http-get") == 0;
                if (parse->resHttp) parse->resMime = fields[2];
                g_strfreev(fields);
            }
            else if (strcmp(names[i], "size") == 0)
            {
                parse->resSize = g_ascii_strtoll(values[i], NULL, 10);
            }
        }
    }
    parse->stack.push_back(name);
}

void didlEnd(GMarkupParseContext*, const gchar*, gpointer data, GError**)
{
    DidlParse* parse = static_cast<DidlParse*>(data);
    const std::string element = parse->stack.back();
    parse->stack.pop_back();
    if (parse->stack.size() == 2 && !parse->objects.empty())
    {
        Object& object = parse->objects.back();
        const std::string value = trim(parse->text);
        if (element == "title" && object.title.empty()) object.title = value;
        else if (element == "date" && object.date.empty()) object.date = value;
        else if (element == "res" && parse->resHttp && object.url.empty())
        {
            HttpUrl url;
            if (parseHttpUrl(value, url))
            {
                object.url = value;
                object.mime = parse->resMime;
                object.size = parse->resSize;
            }
        }
    }
    parse->text.clear();
}

void didlText(GMarkupParseContext*, const gchar* text, gsize length, gpointer data, GError**)
{
    static_cast<DidlParse*>(data)->text.append(text, length);
}

std::vector<Object> parseDidl(const std::string& xml)
{
    DidlParse parse;
    GMarkupParser parser = {didlStart, didlEnd, didlText, NULL, NULL};
    if (!parseXml(xml, parser, &parse)) return {};
    std::vector<Object> result;
    for (Object& object : parse.objects)
        if (!object.id.empty() && (object.container || !object.url.empty())) result.push_back(object);
    return result;
}

struct BrowseParse
{
    std::vector<std::string> stack;
    std::string result, returned, total, fault;
};

void browseStart(GMarkupParseContext*, const gchar* element, const gchar**, const gchar**, gpointer data, GError**)
{
    static_cast<BrowseParse*>(data)->stack.push_back(localName(element));
}

void browseEnd(GMarkupParseContext*, const gchar*, gpointer data, GError**)
{
    static_cast<BrowseParse*>(data)->stack.pop_back();
}

void browseText(GMarkupParseContext*, const gchar* text, gsize length, gpointer data, GError**)
{
    BrowseParse* parse = static_cast<BrowseParse*>(data);
    if (parse->stack.empty()) return;
    const std::string& element = parse->stack.back();
    if (element == "Result") parse->result.append(text, length);
    else if (element == "NumberReturned") parse->returned.append(text, length);
    else if (element == "TotalMatches") parse->total.append(text, length);
    else if (element == "errorDescription" || element == "faultstring") parse->fault.append(text, length);
}

std::string xmlEscape(const std::string& text)
{
    gchar* escaped = g_markup_escape_text(text.c_str(), text.size());
    std::string result = escaped;
    g_free(escaped);
    return result;
}

// Children of one container, all pages.
bool browse(const UpnpService& directory, const std::string& id, std::vector<Object>& objects, std::string& error)
{
    HttpUrl url;
    if (!parseHttpUrl(directory.control, url))
    {
        error = "the media server has no usable ContentDirectory address";
        return false;
    }
    objects.clear();
    guint64 start = 0;
    for (;;)
    {
        const std::string body =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
            " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:Browse xmlns:u=\"" +
            xmlEscape(directory.type) + "\"><ObjectID>" + xmlEscape(id) +
            "</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag><Filter>*</Filter><StartingIndex>" +
            std::to_string(start) + "</StartingIndex><RequestedCount>" + std::to_string(PAGE) +
            "</RequestedCount><SortCriteria></SortCriteria></u:Browse></s:Body></s:Envelope>";
        std::string failure;
        const std::string reply = httpExchange(url, "POST",
            "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + directory.type + "#Browse\"\r\n",
            body, failure, BROWSE_TIMEOUT_S);
        std::string content;
        const bool ok = httpBody(reply, content, failure);
        BrowseParse parse;
        GMarkupParser parser = {browseStart, browseEnd, browseText, NULL, NULL};
        const size_t headerEnd = reply.find("\r\n\r\n");
        std::string xml = ok ? content : (headerEnd == std::string::npos ? "" : reply.substr(headerEnd + 4));
        const bool parsed = parseXml(xml, parser, &parse);
        if (!ok || !parsed)
        {
            error = !trim(parse.fault).empty() ? "media server: " + trim(parse.fault) :
                    !failure.empty() ? failure : "invalid reply from the media server";
            return false;
        }
        const std::vector<Object> page = parseDidl(parse.result);
        objects.insert(objects.end(), page.begin(), page.end());
        const guint64 returned = g_ascii_strtoull(trim(parse.returned).c_str(), NULL, 10);
        const guint64 total = g_ascii_strtoull(trim(parse.total).c_str(), NULL, 10);
        start += returned;
        if (returned == 0 || start >= total || objects.size() >= MAX_OBJECTS) return true;
    }
}

std::string extensionFor(const Object& object)
{
    static const std::map<std::string, std::string> types = {
        {"audio/mpeg", "mp3"}, {"audio/mp3", "mp3"}, {"audio/flac", "flac"}, {"audio/x-flac", "flac"},
        {"audio/ogg", "ogg"}, {"audio/x-wav", "wav"}, {"audio/wav", "wav"}, {"audio/mp4", "m4a"},
        {"audio/x-m4a", "m4a"}, {"audio/aac", "aac"}, {"audio/x-ms-wma", "wma"}, {"video/mp4", "mp4"},
        {"video/x-matroska", "mkv"}, {"video/x-msvideo", "avi"}, {"video/avi", "avi"}, {"video/mpeg", "mpg"},
        {"video/quicktime", "mov"}, {"video/webm", "webm"}, {"video/x-ms-wmv", "wmv"}, {"video/mp2t", "ts"},
        {"image/jpeg", "jpg"}, {"image/png", "png"}, {"image/gif", "gif"}, {"image/webp", "webp"},
        {"image/bmp", "bmp"}, {"image/tiff", "tif"}};
    auto found = types.find(lower(object.mime.substr(0, object.mime.find(';'))));
    if (found != types.end()) return found->second;
    HttpUrl url;
    if (parseHttpUrl(object.url, url))
    {
        std::string path = url.path.substr(0, url.path.find('?'));
        path = path.substr(path.rfind('/') + 1);
        const size_t dot = path.rfind('.');
        if (dot != std::string::npos && path.size() - dot >= 2 && path.size() - dot <= 6)
        {
            const std::string extension = lower(path.substr(dot + 1));
            bool plain = true;
            for (char c : extension) plain = plain && g_ascii_isalnum(c);
            if (plain) return extension;
        }
    }
    return "";
}

// File names shown for a container's children: no slashes, unique, items keep a real extension.
void assignNames(std::vector<Object>& objects)
{
    std::set<std::string> used;
    for (Object& object : objects)
    {
        std::string base = clean(object.title, 200);
        for (char& c : base)
            if (c == '/') c = '-';
        base = trim(base);
        if (base.empty() || base == "." || base == "..") base = object.container ? "Folder" : "Untitled";
        std::string extension = object.container ? "" : extensionFor(object);
        if (!extension.empty())
        {
            const std::string suffix = "." + extension;
            if (base.size() > suffix.size() && lower(base.substr(base.size() - suffix.size())) == suffix)
                base.resize(base.size() - suffix.size());
        }
        const std::string suffix = extension.empty() ? "" : "." + extension;
        std::string name = base + suffix;
        for (int copy = 2; used.count(name); ++copy) name = base + " (" + std::to_string(copy) + ")" + suffix;
        used.insert(name);
        object.name = name;
    }
}

gint64 parseDate(const std::string& date)
{
    int year, month, day, hour = 0, minute = 0, second = 0;
    if (std::sscanf(date.c_str(), "%4d-%2d-%2d", &year, &month, &day) != 3) return 0;
    if (date.size() >= 19) std::sscanf(date.c_str() + 11, "%2d:%2d:%2d", &hour, &minute, &second);
    GDateTime* time = g_date_time_new_utc(year, month, day, hour, minute, second);
    if (!time) return 0;
    const gint64 result = g_date_time_to_unix(time);
    g_date_time_unref(time);
    return result;
}

void printObject(const Object& object)
{
    gchar* name = g_base64_encode(reinterpret_cast<const guchar*>(object.name.data()), object.name.size());
    gchar* url = g_base64_encode(reinterpret_cast<const guchar*>(object.url.data()), object.url.size());
    const gint64 modified = parseDate(object.date);
    GVariant* value = g_variant_ref_sink(g_variant_new("(suuuxxxsbii)", name,
        object.container ? (S_IFDIR | 0555) : (S_IFREG | 0444), G_MAXUINT32, G_MAXUINT32,
        modified, modified, object.size > 0 ? object.size : 0, url, false, 1, object.container ? 1 : 0));
    gchar* record = g_variant_print(value, true);
    std::puts(record);
    g_free(record);
    g_variant_unref(value);
    g_free(name);
    g_free(url);
}

int fail(const std::string& message)
{
    gchar* escaped = g_strescape(message.c_str(), NULL);
    std::printf("ERROR %s\n", escaped);
    g_free(escaped);
    return 1;
}

// Canonical dlna:// authority of a description URL: host:port.
std::string authority(const std::string& location)
{
    HttpUrl url;
    return parseHttpUrl(location, url) ? url.host + ":" + std::to_string(url.port) : "";
}

int list(const std::string& location, const std::string& path)
{
    const std::string server = authority(location);
    const std::string prefix = "dlna://" + server;
    if (server.empty() || path.compare(0, prefix.size(), prefix) != 0 ||
        (path.size() > prefix.size() && path[prefix.size()] != '/'))
        return fail("The media server address does not match its description");
    std::vector<std::string> segments;
    gchar** parts = g_strsplit(path.c_str() + prefix.size(), "/", -1);
    for (gchar** part = parts; *part; ++part)
    {
        if (!**part) continue;
        gchar* segment = g_uri_unescape_string(*part, "/");
        if (!segment)
        {
            g_strfreev(parts);
            return fail("Invalid media folder name");
        }
        segments.push_back(segment);
        g_free(segment);
    }
    g_strfreev(parts);
    std::string body;
    std::string error = httpGet(location, body);
    if (!error.empty()) return fail("Cannot read the media server description: " + error);
    UpnpDevice device;
    if (!parseDescription(body, device)) return fail("Invalid media server description");
    const UpnpService directory = contentDirectory(device, location);
    HttpUrl control;
    if (!parseHttpUrl(directory.control, control))
        return fail("The media server has no ContentDirectory service");
    HttpUrl description;
    parseHttpUrl(location, description);
    if (control.host != description.host) return fail("The media server's control address is on another host");
    std::string id = "0";
    std::vector<Object> objects;
    for (const std::string& segment : segments)
    {
        if (!browse(directory, id, objects, error)) return fail(error);
        assignNames(objects);
        std::string next;
        for (const Object& object : objects)
            if (object.container && object.name == segment) next = object.id;
        if (next.empty()) return fail("Folder not found: " + segment);
        id = next;
    }
    if (!browse(directory, id, objects, error)) return fail(error);
    assignNames(objects);
    for (const Object& object : objects) printObject(object);
    std::puts("DONE");
    std::fflush(stdout);
    return 0;
}

// Streams one http URL into DESTINATION.part, then moves it into place without overwriting.
// Removes the unfinished file when Swordfish cancels the download (SIGTERM).
char cancelPart[4096];
void cancelDownload(int)
{
    if (cancelPart[0]) unlink(cancelPart);
    _exit(130);
}

int download(const std::string& address, const std::string& destination)
{
    HttpUrl url;
    if (!parseHttpUrl(address, url)) return fail("Not a plain http address");
    const std::string partial = destination + ".part";
    int fd = open(partial.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) return fail(partial + ": " + strerror(errno));
    gint64 expected = -1, written = 0, chunk = -1;
    if (partial.size() < sizeof(cancelPart))
    {
        memcpy(cancelPart, partial.c_str(), partial.size() + 1);
        struct sigaction action = {};
        action.sa_handler = cancelDownload;
        sigaction(SIGTERM, &action, NULL);
        sigaction(SIGINT, &action, NULL);
    }
    gint64 reported = 0;
    // "PROGRESS received total" (total -1 when the server does not say)
    auto progress = [&](bool force)
    {
        const gint64 now = g_get_monotonic_time();
        if (!force && now - reported < 250000) return;
        reported = now;
        std::printf("PROGRESS %" G_GINT64_FORMAT " %" G_GINT64_FORMAT "\n", written, expected);
        std::fflush(stdout);
    };
    GSocketClient* client = g_socket_client_new();
    g_socket_client_set_timeout(client, 60);
    GError* failure = NULL;
    GSocketConnection* connection = g_socket_client_connect_to_host(client, url.host.c_str(), url.port, NULL, &failure);
    const std::string request = "GET " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" + std::to_string(url.port) +
                                "\r\nUser-Agent: swordfish-network\r\nConnection: close\r\n\r\n";
    std::string error, buffer;
    bool headers = true, chunked = false, finished = false;
    if (connection && g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(connection)),
                                                 request.data(), request.size(), NULL, NULL, &failure))
    {
        GInputStream* input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
        char data[65536];
        gssize bytes;
        auto store = [&](const char* text, size_t size) -> bool
        {
            while (size > 0)
            {
                const ssize_t count = write(fd, text, size);
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0)
                {
                    error = strerror(errno);
                    return false;
                }
                text += count;
                size -= count;
                written += count;
            }
            progress(false);
            return true;
        };
        while (error.empty() && !finished && (bytes = g_input_stream_read(input, data, sizeof(data), NULL, &failure)) > 0)
        {
            buffer.append(data, bytes);
            if (headers)
            {
                const size_t end = buffer.find("\r\n\r\n");
                if (end == std::string::npos)
                {
                    if (buffer.size() > 65536) error = "invalid HTTP reply";
                    continue;
                }
                const std::string head = lower(buffer.substr(0, end));
                const std::string statusLine = buffer.substr(0, buffer.find("\r\n"));
                if (statusLine.compare(0, 5, "HTTP/") != 0 || statusLine.find(" 200") == std::string::npos)
                {
                    error = statusLine;
                    break;
                }
                chunked = head.find("\r\ntransfer-encoding: chunked") != std::string::npos;
                const size_t length = head.find("\r\ncontent-length:");
                if (length != std::string::npos) expected = g_ascii_strtoll(head.c_str() + length + 17, NULL, 10);
                buffer.erase(0, end + 4);
                headers = false;
                progress(true);
            }
            if (!chunked)
            {
                if (!store(buffer.data(), buffer.size())) break;
                buffer.clear();
                finished = expected >= 0 && written >= expected;
                continue;
            }
            for (;;)
            {
                if (chunk < 0)
                {
                    const size_t line = buffer.find("\r\n");
                    if (line == std::string::npos) break;
                    chunk = static_cast<gint64>(strtoull(buffer.substr(0, line).c_str(), NULL, 16));
                    buffer.erase(0, line + 2);
                    if (chunk == 0)
                    {
                        finished = true;
                        break;
                    }
                }
                if (static_cast<gint64>(buffer.size()) < chunk + 2) break;
                if (!store(buffer.data(), chunk)) break;
                buffer.erase(0, chunk + 2);
                chunk = -1;
            }
        }
        if (headers && error.empty() && !failure) error = "incomplete HTTP reply";
        if (error.empty() && !failure && ((chunked && !finished) || (!chunked && expected >= 0 && written != expected)))
            error = "the download was interrupted";
    }
    if (failure && error.empty()) error = failure->message;
    g_clear_error(&failure);
    if (connection)
    {
        g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
        g_object_unref(connection);
    }
    g_object_unref(client);
    if (close(fd) != 0 && error.empty()) error = strerror(errno);
    if (error.empty())
    {
        if (link(partial.c_str(), destination.c_str()) == 0) unlink(partial.c_str());
        else if (errno == EEXIST) error = destination + " already exists";
        else if (access(destination.c_str(), F_OK) == 0) error = destination + " already exists";
        else if (rename(partial.c_str(), destination.c_str()) != 0) error = strerror(errno);
    }
    if (!error.empty())
    {
        unlink(partial.c_str());
        return fail(error);
    }
    progress(true);
    std::puts("DONE");
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
    if (argc == 4 && strcmp(argv[1], "--media") == 0) return media::list(argv[2], argv[3]);
    if (argc == 4 && strcmp(argv[1], "--download") == 0) return media::download(argv[2], argv[3]);
    std::fprintf(stderr, "Usage: swordfish-network --discover | --shares smb://HOST/ | --list PATH |\n"
                         "       --media DESCRIPTION-URL dlna://HOST:PORT/PATH | --download URL FILE\n");
    return 2;
}
