#include <gio/gio.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <unistd.h>

#define main networkDiscoveryMain
#include "../src/NetworkDiscovery.cpp"
#undef main

using namespace discovery;

static const char* ENDPOINT = "urn:uuid:ce41ebfe-0a8d-457e-be20-a5965a99559e";

static std::string probeReply(const std::string& relatesTo, const std::string& types, const std::string& xaddrs)
{
    return std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
        "xmlns:wsd=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
        "xmlns:wsdp=\"http://schemas.xmlsoap.org/ws/2006/02/devprof\" "
        "xmlns:pub=\"http://schemas.microsoft.com/windows/pub/2005/07\"><soap:Header>"
        "<wsa:To>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</wsa:To>"
        "<wsa:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/ProbeMatches</wsa:Action>"
        "<wsa:RelatesTo>") + relatesTo + "</wsa:RelatesTo></soap:Header><soap:Body><wsd:ProbeMatches>"
        "<wsd:ProbeMatch><wsa:EndpointReference><wsa:Address>" + ENDPOINT +
        "</wsa:Address></wsa:EndpointReference><wsd:Types>" + types + "</wsd:Types>"
        "<wsd:XAddrs>" + xaddrs + "</wsd:XAddrs><wsd:MetadataVersion>2</wsd:MetadataVersion>"
        "</wsd:ProbeMatch></wsd:ProbeMatches></soap:Body></soap:Envelope>";
}

static const std::string METADATA =
    "<soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" "
    "xmlns:wsx=\"http://schemas.xmlsoap.org/ws/2004/09/mex\" "
    "xmlns:wsdp=\"http://schemas.xmlsoap.org/ws/2006/02/devprof\" "
    "xmlns:pub=\"http://schemas.microsoft.com/windows/pub/2005/07\"><soap:Body><wsx:Metadata>"
    "<wsx:MetadataSection><wsdp:ThisModel><wsdp:FriendlyName>Microsoft Publication Service Device Host"
    "</wsdp:FriendlyName></wsdp:ThisModel></wsx:MetadataSection><wsx:MetadataSection><wsdp:Relationship>"
    "<wsdp:Host><wsdp:Types>pub:Computer</wsdp:Types><pub:Computer>DELL-740/Workgroup:MAKHLOUF"
    "</pub:Computer></wsdp:Host></wsdp:Relationship></wsx:MetadataSection></wsx:Metadata></soap:Body>"
    "</soap:Envelope>";

static std::string chunked(const std::string& body)
{
    char size[32];
    std::string first = body.substr(0, body.size() / 2);
    std::snprintf(size, sizeof(size), "%zx", first.size());
    std::string result = std::string(size) + "\r\n" + first + "\r\n";
    std::string second = body.substr(first.size());
    std::snprintf(size, sizeof(size), "%zx", second.size());
    return result + size + "\r\n" + second + "\r\n0\r\n\r\n";
}

static void parsers()
{
    assert(smbHost("smb://mx-hp.local:445/") == "mx-hp.local");
    assert(smbHost("smb://user@192.168.1.104/share") == "192.168.1.104");
    assert(smbHost("smb:///") == "");
    assert(smbHost("sftp://host/") == "");
    assert(smbHost("smb://bad;host/") == "");
    assert(smbHost("smb://[fe80::1]/") == "");

    Computer computer = parseComputer("DELL-740/Workgroup:MAKHLOUF");
    assert(computer.name == "DELL-740" && computer.group == "Workgroup:MAKHLOUF");
    computer = parseComputer("PC\\Domain:CORP");
    assert(computer.name == "PC" && computer.group == "Domain:CORP");
    assert(parseComputer("PC/NotJoined").name == "PC");

    std::vector<ProbeMatch> matches = parseProbeMatches(probeReply("urn:uuid:probe", "wsdp:Device pub:Computer",
        "http://192.168.1.104:5357/ce41ebfe-0a8d-457e-be20-a5965a99559e/"), "urn:uuid:probe");
    assert(matches.size() == 1 && matches[0].endpoint == ENDPOINT);
    assert(matches[0].xaddrs == "http://192.168.1.104:5357/ce41ebfe-0a8d-457e-be20-a5965a99559e/");
    assert(isComputer(matches[0].types));
    assert(!isComputer("wsdp:Device wprt:PrintDeviceType"));
    assert(parseProbeMatches(probeReply("urn:uuid:other", "pub:Computer", ""), "urn:uuid:probe").empty());
    assert(parseProbeMatches("<!DOCTYPE x [<!ENTITY a \"b\">]><x>&a;</x>", "urn:uuid:probe").empty());
    assert(parseProbeMatches("not xml", "urn:uuid:probe").empty());

    std::string error;
    assert(parseMetadataReply("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n" + METADATA, computer, error));
    assert(computer.name == "DELL-740" && computer.group == "Workgroup:MAKHLOUF");
    computer = Computer();
    assert(parseMetadataReply("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" + chunked(METADATA),
                              computer, error));
    assert(computer.name == "DELL-740");
    assert(!parseMetadataReply("HTTP/1.1 500 Internal Server Error\r\n\r\n", computer, error));
    assert(!parseMetadataReply("HTTP/1.1 200 OK\r\n\r\n<a><b>no computer</b></a>", computer, error));

    assert(clean("a\tb\nc") == "a b c");
    assert(clean("ok\xff\xfe") == "ok");
    assert(clean(std::string(300, 'x')).size() == 128);
    puts("PASS: smb URI hosts, Windows computer values, probe matches, DTD refusal, metadata HTTP/chunked, record cleaning");
}

static std::string request;

static gpointer serveMetadata(gpointer data)
{
    GSocketListener* listener = G_SOCKET_LISTENER(data);
    GSocketConnection* connection = g_socket_listener_accept(listener, NULL, NULL, NULL);
    assert(connection);
    GInputStream* input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    char buffer[4096];
    while (request.find("</s:Envelope>") == std::string::npos)
    {
        gssize bytes = g_input_stream_read(input, buffer, sizeof(buffer), NULL, NULL);
        if (bytes <= 0) break;
        request.append(buffer, bytes);
    }
    std::string reply = "HTTP/1.1 200 OK\r\nContent-Type: application/soap+xml\r\n"
                        "Transfer-Encoding: chunked\r\n\r\n" + chunked(METADATA);
    g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(connection)),
                              reply.data(), reply.size(), NULL, NULL, NULL);
    g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
    g_object_unref(connection);
    return NULL;
}

static guint16 listen(GSocketListener* listener)
{
    GInetAddress* loopback = g_inet_address_new_from_string("127.0.0.1");
    GSocketAddress* address = g_inet_socket_address_new(loopback, 0);
    GSocketAddress* effective = NULL;
    assert(g_socket_listener_add_address(listener, address, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP,
                                         NULL, &effective, NULL));
    guint16 port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(effective));
    g_object_unref(effective);
    g_object_unref(address);
    g_object_unref(loopback);
    return port;
}

static void mergeAndChecks()
{
    GSocketListener* smb = g_socket_listener_new();
    smbPort = listen(smb);
    GSocketListener* http = g_socket_listener_new();
    const guint16 httpPort = listen(http);
    GThread* server = g_thread_new("metadata", serveMetadata, http);

    out = tmpfile();
    state = new State();
    state->loop = g_main_loop_new(NULL, false);
    state->localAddresses.insert("127.0.0.1");
    addGvfsHost("127.0.0.1", "LINUX-NAME");
    addGvfsHost("127.0.0.1", "duplicate");
    ProbeMatch match;
    match.endpoint = ENDPOINT;
    match.types = "wsdp:Device pub:Computer";
    match.xaddrs = "http://198.51.100.7:5357/elsewhere/ http://127.0.0.1:" + std::to_string(httpPort) + "/device/";
    handleProbeMatch(match, "127.0.0.1");
    handleProbeMatch(match, "127.0.0.1");
    ProbeMatch printer = match;
    printer.types = "wsdp:Device wprt:PrintDeviceType";
    handleProbeMatch(printer, "127.0.0.3");
    addHost("127.0.0.2", "", "ws-discovery");
    g_timeout_add(10000, [](gpointer) -> gboolean { assert(!"discovery test timed out"); return G_SOURCE_REMOVE; }, NULL);
    maybeFinish();
    if (!state->finished) g_main_loop_run(state->loop);
    g_thread_join(server);

    assert(request.find("POST /device/ HTTP/1.1\r\n") == 0);
    assert(request.find(std::string("<a:To>") + ENDPOINT + "</a:To>") != std::string::npos);
    assert(request.find("transfer/Get</a:Action>") != std::string::npos);

    std::rewind(out);
    std::string output;
    char line[1024];
    while (std::fgets(line, sizeof(line), out)) output += line;
    const std::string expected = "SERVER\t127.0.0.1\t127.0.0.1\tDELL-740\tWorkgroup:MAKHLOUF\tgvfs,ws-discovery\t1\n";
    assert(output.size() >= expected.size() + 5);
    assert(output.compare(output.size() - 5, 5, "DONE\n") == 0);
    assert(output.compare(output.size() - 5 - expected.size(), expected.size(), expected) == 0);
    assert(output.find("127.0.0.2") == std::string::npos);
    assert(output.find("127.0.0.3") == std::string::npos);
    assert(output.find("duplicate") == std::string::npos);
    assert(state->hosts.at("127.0.0.2").port == CLOSED);
    std::fclose(out);
    g_socket_listener_close(smb);
    g_socket_listener_close(http);
    g_object_unref(smb);
    g_object_unref(http);
    puts("PASS: GVFS+WSD merge by address, metadata name wins, same-host metadata only, printers ignored, closed port hidden, DONE last");
}

static void shareAuthentication()
{
    GMountOperation* operation = g_mount_operation_new();
    int reply = -1;
    g_signal_connect(operation, "reply", G_CALLBACK(+[](GMountOperation*, GMountOperationResult result, gpointer data)
    {
        *static_cast<int*>(data) = result;
    }), &reply);
    bool anonymous = false;
    sharePassword(operation, "", "", "", G_ASK_PASSWORD_ANONYMOUS_SUPPORTED, &anonymous);
    assert(anonymous && g_mount_operation_get_anonymous(operation) && reply == G_MOUNT_OPERATION_HANDLED);

    FILE* input = tmpfile();
    assert(input);
    std::fputs("dGVzdC11c2Vy\tV09SS0dST1VQ\t\n", input);
    std::fputs("dGVzdC11c2Vy\tV09SS0dST1VQ\tcGEkcyd3b3Jk\n", input);
    std::rewind(input);
    const int savedInput = dup(STDIN_FILENO);
    assert(savedInput >= 0 && dup2(fileno(input), STDIN_FILENO) >= 0);
    for (const char* password : {"", "pa$s'word"})
    {
        sharePassword(operation, "", "", "", G_ASK_PASSWORD_ANONYMOUS_SUPPORTED, &anonymous);
        assert(!g_mount_operation_get_anonymous(operation) && reply == G_MOUNT_OPERATION_HANDLED);
        assert(strcmp(g_mount_operation_get_username(operation), "test-user") == 0);
        assert(strcmp(g_mount_operation_get_domain(operation), "WORKGROUP") == 0);
        assert(strcmp(g_mount_operation_get_password(operation), password) == 0);
    }
    assert(dup2(savedInput, STDIN_FILENO) >= 0);
    close(savedInput);
    std::fclose(input);
    g_object_unref(operation);
    puts("PASS: anonymous-first share browsing, credential fallback, blank and shell-special passwords");
}

int main()
{
    parsers();
    shareAuthentication();
    mergeAndChecks();
}
