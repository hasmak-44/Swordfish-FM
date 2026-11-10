#include <gio/gio.h>
#include <cassert>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

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
    assert(output.find("SERVICE\tprinter\tprinter@127.0.0.3\t127.0.0.3\t127.0.0.3\thttp://127.0.0.3/\tws-discovery\n") !=
           std::string::npos);
    assert(output.find("SERVER\t127.0.0.3") == std::string::npos);
    assert(output.find("duplicate") == std::string::npos);
    assert(state->hosts.at("127.0.0.2").port == CLOSED);
    std::fclose(out);
    g_socket_listener_close(smb);
    g_socket_listener_close(http);
    g_object_unref(smb);
    g_object_unref(http);
    puts("PASS: GVFS+WSD merge by address, metadata name wins, same-host metadata only, WSD printers listed as services, closed port hidden, DONE last");
}

static std::string dnsName(const std::string& name)
{
    std::string result;
    gchar** labels = g_strsplit(name.c_str(), ".", -1);
    for (gchar** label = labels; *label; ++label)
    {
        result += static_cast<char>(strlen(*label));
        result += *label;
    }
    g_strfreev(labels);
    return result + '\0';
}

static std::string u16(unsigned value)
{
    return std::string(1, static_cast<char>(value >> 8)) + static_cast<char>(value & 0xff);
}

static std::string record(const std::string& owner, unsigned type, const std::string& data)
{
    return dnsName(owner) + u16(type) + u16(0x8001) + u16(0) + u16(120) + u16(data.size()) + data;
}

static const std::string MEDIA_DESCRIPTION =
    "<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><device>"
    "<deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType><friendlyName>Living Room NAS</friendlyName>"
    "<serviceList><service><serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>"
    "<controlURL>/cm</controlURL></service><service>"
    "<serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>"
    "<controlURL>ctl</controlURL></service></serviceList></device></root>";

static void serviceParsers()
{
    assert(ssdpLocation("HTTP/1.1 200 OK\r\nST: ssdp:all\r\nlocation:  http://192.168.1.5:8200/rootDesc.xml \r\n\r\n") ==
           "http://192.168.1.5:8200/rootDesc.xml");
    assert(ssdpLocation("NOTIFY * HTTP/1.1\r\nLOCATION: http://x/\r\n\r\n") == "");
    assert(ssdpLocation("HTTP/1.1 404 Not Found\r\nLOCATION: http://x/\r\n\r\n") == "");

    UpnpDevice device;
    assert(parseDescription(MEDIA_DESCRIPTION, device));
    assert(classifyDevice(device) == "media" && device.name == "Living Room NAS");
    UpnpService directory = contentDirectory(device, "http://192.168.1.5:8200/desc/root.xml");
    assert(directory.type == "urn:schemas-upnp-org:service:ContentDirectory:1");
    assert(directory.control == "http://192.168.1.5:8200/desc/ctl");
    device = UpnpDevice();
    assert(parseDescription("<root><URLBase>http://10.0.0.1:5000/</URLBase><device>"
        "<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:2</deviceType><friendlyName>Router</friendlyName>"
        "<presentationURL>/admin</presentationURL><deviceList><device>"
        "<deviceType>urn:schemas-upnp-org:device:WANDevice:2</deviceType></device></deviceList></device></root>", device));
    assert(classifyDevice(device) == "router" && device.types.size() == 2 && device.presentation == "/admin");
    assert(contentDirectory(device, "http://10.0.0.1/").control.empty());
    device = UpnpDevice();
    assert(parseDescription("<root><device><deviceType>urn:schemas-upnp-org:device:Printer:1</deviceType>"
        "</device></root>", device) && classifyDevice(device) == "printer");
    device = UpnpDevice();
    assert(parseDescription("<root><device><deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>"
        "</device></root>", device) && classifyDevice(device) == "");
    assert(!parseDescription("<root/>", device));
    assert(resolveUrl("http://10.0.0.1:80/a/b.xml", "ftp://evil/") == "");
    assert(resolveUrl("http://10.0.0.1/a/b.xml", "c?x=1") == "http://10.0.0.1:80/a/c?x=1");

    assert(sameHostPage("http://10.0.0.1/admin", "10.0.0.1"));
    assert(sameHostPage("https://10.0.0.1:8443/", "10.0.0.1"));
    assert(!sameHostPage("http://10.0.0.10/", "10.0.0.1"));
    assert(!sameHostPage("http://10.0.0.1.evil.example/", "10.0.0.1"));
    assert(!sameHostPage("javascript:alert(1)", "10.0.0.1"));
    assert(!sameHostPage("http://10.0.0.1/a b", "10.0.0.1"));

    HttpUrl url;
    assert(parseHttpUrl("http://10.0.0.1:8200/x?y#z", url) && url.host == "10.0.0.1" && url.port == 8200 &&
           url.path == "/x?y");
    assert(!parseHttpUrl("http://user@10.0.0.1/", url));
    assert(!parseHttpUrl("http://10.0.0.1:99999/", url));
    assert(!parseHttpUrl("https://10.0.0.1/", url));

    const std::string query = mdnsQuery(0x1234);
    assert(query.compare(0, 6, std::string("\x12\x34\0\0\0", 5) + static_cast<char>(G_N_ELEMENTS(MDNS_TYPES))) == 0);
    assert(query.find(dnsName("_ipp._tcp.local")) != std::string::npos);

    const std::string instance = "Office Printer._ipp._tcp.local";
    std::string txt = std::string(1, 30) + "adminurl=http://192.168.1.9/ui";
    txt += std::string(1, 7) + "note=ok";
    const std::string packet = u16(0) + u16(0x8400) + u16(0) + u16(5) + u16(0) + u16(0) +
        record("_ipp._tcp.local", 12, dnsName(instance)) +
        record(instance, 33, u16(0) + u16(0) + u16(8631) + dnsName("printer.local")) +
        record(instance, 16, txt) + record("printer.local", 1, std::string("\xc0\xa8\x01\x09", 4)) +
        record("_ftp._tcp.local", 12, dnsName("Files._ftp._tcp.local"));
    std::vector<MdnsService> services = parseMdnsReply(packet, "192.168.1.200");
    assert(services.size() == 2);
    assert(services[0].kind == "ftp" && services[0].name == "Files" && services[0].port == 21 &&
           services[0].address == "192.168.1.200");
    assert(services[1].kind == "printer" && services[1].name == "Office Printer" && services[1].port == 8631 &&
           services[1].address == "192.168.1.9" && services[1].adminUrl == "http://192.168.1.9/ui");
    const std::string loop = u16(0) + u16(0x8400) + u16(0) + u16(1) + u16(0) + u16(0) + std::string("\xc0\x0c", 2);
    assert(parseMdnsReply(loop, "192.168.1.200").empty());
    assert(parseMdnsReply(packet.substr(0, 40), "192.168.1.200").empty());
    assert(parseMdnsReply(u16(0) + u16(0) + packet.substr(4), "192.168.1.200").empty());

    std::vector<media::Object> objects = media::parseDidl(
        "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
        " xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">"
        "<container id=\"64\" parentID=\"0\"><dc:title>AC/DC</dc:title></container>"
        "<item id=\"a\"><dc:title>Song</dc:title><dc:date>2020-05-17T10:20:30</dc:date>"
        "<res protocolInfo=\"rtsp-rtp-udp:*:audio/mpeg:*\">rtsp://x/1</res>"
        "<res protocolInfo=\"http-get:*:audio/mpeg:*\" size=\"1234\">http://10.0.0.5:8200/MediaItems/22.mp3</res></item>"
        "<item id=\"b\"><dc:title>Song.MP3</dc:title><res protocolInfo=\"http-get:*:audio/mpeg:*\">"
        "http://10.0.0.5:8200/MediaItems/23</res></item>"
        "<item id=\"c\"><dc:title>Photo</dc:title><res protocolInfo=\"http-get:*:image/jpeg:*\">"
        "http://10.0.0.5:8200/get?id=7</res></item>"
        "<item id=\"d\"><dc:title>No stream</dc:title><res protocolInfo=\"rtsp:*:video/mp4:*\">rtsp://x/</res></item>"
        "<item id=\"e\"><dc:title>..</dc:title><res protocolInfo=\"http-get:*:video/x-matroska:*\">"
        "http://10.0.0.5/v.bin</res></item>"
        "</DIDL-Lite>");
    assert(objects.size() == 5);
    media::assignNames(objects);
    assert(objects[0].container && objects[0].name == "AC-DC");
    assert(objects[1].name == "Song.mp3" && objects[1].size == 1234 && objects[1].mime == "audio/mpeg" &&
           objects[1].url == "http://10.0.0.5:8200/MediaItems/22.mp3");
    assert(objects[2].name == "Song (2).mp3");
    assert(objects[3].name == "Photo.jpg" && objects[4].name == "Untitled.mkv");
    assert(media::parseDate("2020-05-17T10:20:30") == 1589710830 && media::parseDate("2020-05-17") == 1589673600);
    assert(media::parseDate("unknown") == 0);
    assert(media::authority("http://10.0.0.5/desc.xml") == "10.0.0.5:80");
    puts("PASS: SSDP replies, UPnP descriptions and classes, same-host pages, mDNS packets and loops, DIDL names");
}

static guint16 mediaPort;
static std::string browsed;

static std::string between(const std::string& text, const std::string& open, const std::string& close)
{
    const size_t start = text.find(open);
    if (start == std::string::npos) return "";
    const size_t end = text.find(close, start + open.size());
    return end == std::string::npos ? "" : text.substr(start + open.size(), end - start - open.size());
}

static std::string browseReply(const std::string& id, unsigned start, unsigned requested)
{
    const std::string base = "http://127.0.0.1:" + std::to_string(mediaPort);
    std::string didl = "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
                       "xmlns:dc=\"http://purl.org/dc/elements/1.1/\">";
    unsigned total = 0, returned = 0;
    if (id == "0")
    {
        total = returned = 2;
        didl += "<container id=\"1\"><dc:title>Music</dc:title></container>"
                "<container id=\"2\"><dc:title>Video/Clips</dc:title></container>";
    }
    else if (id == "1")
    {
        total = 150;
        for (unsigned i = start; i < total && returned < requested; ++i, ++returned)
            didl += "<item id=\"t" + std::to_string(i) + "\"><dc:title>" + (i < 2 ? std::string("Same") :
                    "Track " + std::to_string(i)) + "</dc:title><res protocolInfo=\"http-get:*:audio/mpeg:*\" size=\"" +
                    std::to_string(100 + i) + "\">" + base + "/media/" + std::to_string(i) + ".mp3</res></item>";
    }
    else if (id != "2")
    {
        return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/xml\r\n\r\n<s:Envelope xmlns:s=\"x\"><s:Body>"
               "<s:Fault><detail><UPnPError><errorDescription>No such object</errorDescription></UPnPError></detail>"
               "</s:Fault></s:Body></s:Envelope>";
    }
    didl += "</DIDL-Lite>";
    gchar* escaped = g_markup_escape_text(didl.c_str(), -1);
    const std::string body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
        "<s:Body><u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>" +
        std::string(escaped) + "</Result><NumberReturned>" + std::to_string(returned) +
        "</NumberReturned><TotalMatches>" + std::to_string(total) + "</TotalMatches><UpdateID>1</UpdateID>"
        "</u:BrowseResponse></s:Body></s:Envelope>";
    g_free(escaped);
    return "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nTransfer-Encoding: chunked\r\n\r\n" + chunked(body);
}

static gpointer serveMedia(gpointer data)
{
    GSocketListener* listener = G_SOCKET_LISTENER(data);
    for (;;)
    {
        GSocketConnection* connection = g_socket_listener_accept(listener, NULL, NULL, NULL);
        assert(connection);
        GInputStream* input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
        std::string text;
        char buffer[4096];
        for (;;)
        {
            const size_t end = text.find("\r\n\r\n");
            if (end != std::string::npos)
            {
                const std::string length = between(text, "Content-Length: ", "\r\n");
                if (text.size() >= end + 4 + (length.empty() ? 0 : std::stoul(length))) break;
            }
            const gssize bytes = g_input_stream_read(input, buffer, sizeof(buffer), NULL, NULL);
            if (bytes <= 0) break;
            text.append(buffer, bytes);
        }
        const std::string path = between(text, " ", " HTTP/1.1");
        std::string reply;
        if (path == "/desc/root.xml")
        {
            reply = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(MEDIA_DESCRIPTION.size()) + "\r\n\r\n" +
                    MEDIA_DESCRIPTION;
        }
        else if (path == "/desc/ctl")
        {
            assert(text.find("SOAPAction: \"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"") != std::string::npos);
            const std::string id = between(text, "<ObjectID>", "</ObjectID>");
            const std::string start = between(text, "<StartingIndex>", "</StartingIndex>");
            browsed += id + "@" + start + " ";
            reply = browseReply(id, std::stoul(start), std::stoul(between(text, "<RequestedCount>", "</RequestedCount>")));
        }
        else if (path == "/media/song")
        {
            reply = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" + chunked("streamed song bytes");
        }
        else if (path == "/media/sized")
        {
            reply = "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nsized bytes";
        }
        else if (path == "/media/slow")
        {
            const std::string head = "HTTP/1.1 200 OK\r\nContent-Length: 1000\r\n\r\nfirst part";
            g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(connection)),
                                      head.data(), head.size(), NULL, NULL, NULL);
            g_usleep(1500000);
        }
        else if (path == "/media/short")
        {
            reply = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nonly part";
        }
        else
        {
            reply = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        }
        g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(connection)),
                                  reply.data(), reply.size(), NULL, NULL, NULL);
        g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
        g_object_unref(connection);
        if (path == "/quit") return NULL;
    }
}

static int captured(const std::function<int()>& run, std::string& output)
{
    std::fflush(stdout);
    FILE* file = tmpfile();
    const int saved = dup(STDOUT_FILENO);
    assert(file && saved >= 0 && dup2(fileno(file), STDOUT_FILENO) >= 0);
    const int result = run();
    std::fflush(stdout);
    assert(dup2(saved, STDOUT_FILENO) >= 0);
    close(saved);
    std::rewind(file);
    output.clear();
    char line[4096];
    while (std::fgets(line, sizeof(line), file)) output += line;
    std::fclose(file);
    return result;
}

static std::vector<std::string> entryNames(const std::string& output)
{
    std::vector<std::string> names;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line))
    {
        if (line == "DONE") break;
        GVariant* value = g_variant_parse(G_VARIANT_TYPE("(suuuxxxsbii)"), line.c_str(), NULL, NULL, NULL);
        assert(value);
        const gchar* name = NULL;
        guint32 mode = 0;
        g_variant_get(value, "(&suuuxxxsbii)", &name, &mode, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        gsize size = 0;
        guchar* decoded = g_base64_decode(name, &size);
        names.push_back(std::string(reinterpret_cast<char*>(decoded), size) + (S_ISDIR(mode) ? "/" : ""));
        g_free(decoded);
        g_variant_unref(value);
    }
    return names;
}

static void mediaServer()
{
    GSocketListener* listener = g_socket_listener_new();
    mediaPort = listen(listener);
    GThread* server = g_thread_new("media", serveMedia, listener);
    const std::string root = "http://127.0.0.1:" + std::to_string(mediaPort);
    const std::string location = root + "/desc/root.xml";
    const std::string top = "dlna://127.0.0.1:" + std::to_string(mediaPort);
    std::string output;

    assert(captured([&] { return media::list(location, top); }, output) == 0);
    std::vector<std::string> names = entryNames(output);
    assert(names.size() == 2 && names[0] == "Music/" && names[1] == "Video-Clips/");
    assert(output.size() > 5 && output.compare(output.size() - 5, 5, "DONE\n") == 0);
    assert(browsed == "0@0 ");

    browsed.clear();
    assert(captured([&] { return media::list(location, top + "/Music/"); }, output) == 0);
    names = entryNames(output);
    assert(names.size() == 150 && names[0] == "Same.mp3" && names[1] == "Same (2).mp3" && names[149] == "Track 149.mp3");
    assert(browsed == "0@0 1@0 1@100 ");
    const std::string stream = root + "/media/7.mp3";
    gchar* encoded = g_base64_encode(reinterpret_cast<const guchar*>(stream.data()), stream.size());
    assert(output.find(std::string("'") + encoded + "'") != std::string::npos);
    g_free(encoded);

    assert(captured([&] { return media::list(location, top + "/Video-Clips"); }, output) == 0 && output == "DONE\n");
    assert(captured([&] { return media::list(location, top + "/Video%2DClips"); }, output) == 0 && output == "DONE\n");
    assert(captured([&] { return media::list(location, top + "/Missing"); }, output) == 1 &&
           output == "ERROR Folder not found: Missing\n");
    assert(captured([&] { return media::list(location, "dlna://127.0.0.1:1/"); }, output) == 1 &&
           output.compare(0, 6, "ERROR ") == 0);
    assert(captured([&] { return media::list(location, top + "0/"); }, output) == 1);
    assert(captured([&] { return media::list(root + "/nothing.xml", top); }, output) == 1 &&
           output.find("404") != std::string::npos);

    gchar* directory = g_dir_make_tmp("swordfish-media-XXXXXX", NULL);
    assert(directory);
    const std::string target = std::string(directory) + "/song.mp3";
    assert(captured([&] { return media::download(root + "/media/song", target); }, output) == 0 &&
           output == "PROGRESS 0 -1\nPROGRESS 19 -1\nDONE\n");
    gchar* contents = NULL;
    assert(g_file_get_contents(target.c_str(), &contents, NULL, NULL) &&
           std::string(contents) == "streamed song bytes");
    g_free(contents);
    assert(captured([&] { return media::download(root + "/media/song", target); }, output) == 1 &&
           output.find("already exists") != std::string::npos);
    const std::string partial = std::string(directory) + "/short.mp3";
    assert(captured([&] { return media::download(root + "/media/short", partial); }, output) == 1 &&
           output.find("interrupted") != std::string::npos);
    assert(captured([&] { return media::download(root + "/media/none", partial); }, output) == 1 &&
           output.find("404") != std::string::npos);
    assert(captured([&] { return media::download("ftp://127.0.0.1/x", partial); }, output) == 1);
    assert(access(partial.c_str(), F_OK) != 0 && access((partial + ".part").c_str(), F_OK) != 0 &&
           access((target + ".part").c_str(), F_OK) != 0);
    const std::string sized = std::string(directory) + "/sized.mp3";
    assert(captured([&] { return media::download(root + "/media/sized", sized); }, output) == 0 &&
           output == "PROGRESS 0 11\nPROGRESS 11 11\nDONE\n");
    unlink(sized.c_str());
    // Cancelling (SIGTERM) removes the unfinished file
    const std::string slow = std::string(directory) + "/slow.mp3";
    const pid_t child = fork();
    if (child == 0)
    {
        const int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, STDOUT_FILENO);
        _exit(media::download(root + "/media/slow", slow));
    }
    for (int waited = 0; waited < 100; ++waited)
    {
        struct stat info;
        if (stat((slow + ".part").c_str(), &info) == 0 && info.st_size == 10) break;
        g_usleep(20000);
    }
    assert(kill(child, SIGTERM) == 0);
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 130);
    assert(access((slow + ".part").c_str(), F_OK) != 0 && access(slow.c_str(), F_OK) != 0);
    unlink(target.c_str());
    assert(rmdir(directory) == 0);
    g_free(directory);

    std::string ignored;
    httpGet(root + "/quit", ignored);
    g_thread_join(server);
    g_socket_listener_close(listener);
    g_object_unref(listener);
    puts("PASS: media browsing by folder path, paging, unique names, path checks, safe streaming downloads");
}

static void shareAuthentication()
{
    GMountOperation* operation = g_mount_operation_new();
    int reply = -1;
    g_signal_connect(operation, "reply", G_CALLBACK(+[](GMountOperation*, GMountOperationResult result, gpointer data)
    {
        *static_cast<int*>(data) = result;
    }), &reply);
    int stage = 0;
    sharePassword(operation, "", "", "", G_ASK_PASSWORD_ANONYMOUS_SUPPORTED, &stage);
    assert(stage == 1 && g_mount_operation_get_anonymous(operation) && reply == G_MOUNT_OPERATION_HANDLED);
    sharePassword(operation, "", "", "", G_ASK_PASSWORD_ANONYMOUS_SUPPORTED, &stage);
    assert(stage == 2 && !g_mount_operation_get_anonymous(operation) && reply == G_MOUNT_OPERATION_HANDLED);
    assert(strcmp(g_mount_operation_get_username(operation), "guest") == 0);

    FILE* input = tmpfile();
    assert(input);
    std::fputs("dGVzdC11c2Vy\tV09SS0dST1VQ\t\n", input);
    std::fputs("dGVzdC11c2Vy\tV09SS0dST1VQ\tcGEkcyd3b3Jk\n", input);
    std::rewind(input);
    const int savedInput = dup(STDIN_FILENO);
    assert(savedInput >= 0 && dup2(fileno(input), STDIN_FILENO) >= 0);
    for (const char* password : {"", "pa$s'word"})
    {
        sharePassword(operation, "", "", "", G_ASK_PASSWORD_ANONYMOUS_SUPPORTED, &stage);
        assert(!g_mount_operation_get_anonymous(operation) && reply == G_MOUNT_OPERATION_HANDLED);
        assert(strcmp(g_mount_operation_get_username(operation), "test-user") == 0);
        assert(strcmp(g_mount_operation_get_domain(operation), "WORKGROUP") == 0);
        assert(strcmp(g_mount_operation_get_password(operation), password) == 0);
    }
    assert(dup2(savedInput, STDIN_FILENO) >= 0);
    close(savedInput);
    std::fclose(input);
    g_object_unref(operation);
    puts("PASS: anonymous-first share browsing, guest then credential fallback, blank and shell-special passwords");
}

int main()
{
    parsers();
    serviceParsers();
    mediaServer();
    shareAuthentication();
    mergeAndChecks();
}
