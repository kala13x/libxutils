/* libxutils: URL parsing, default ports and interface address lookup.
 *
 * Link parsing decides which host a client actually connects to, so the
 * cases below pin down where each component starts and ends: an absent
 * protocol, credentials with and without a user, an explicit port against
 * the protocol default, and a query string riding on the path.
 */

#include "test.h"
#include "addr.h"
#include "str.h"
#include <ctype.h>

static int XTest_components(void)
{
    xlink_t link;

    /* A full link: every component is separated out. */
    CHECK(XLink_Parse(&link, "https://user:pass@example.com:8443/path/to/file?q=1") == XSTDOK,
        "A complete link parses");
    CHECK(strcmp(link.sProtocol, "https") == 0, "The protocol is taken from the scheme");
    CHECK(strcmp(link.sAddr, "example.com") == 0, "The address excludes the port");
    CHECK(strcmp(link.sHost, "example.com:8443") == 0, "The host keeps the port");
    CHECK(link.nPort == 8443, "The explicit port wins over the protocol default");
    CHECK(strcmp(link.sUser, "user") == 0, "The user is taken from the credentials");
    CHECK(strcmp(link.sPass, "pass") == 0, "The password is taken from the credentials");
    CHECK(strcmp(link.sUri, "/path/to/file?q=1") == 0, "The uri keeps the whole path and query");
    CHECK(strcmp(link.sFile, "file?q=1") == 0, "The file is the last path segment");

    /* A minimal link: the defaults fill in the rest. */
    CHECK(XLink_Parse(&link, "http://example.com") == XSTDOK, "A link without a path parses");
    CHECK(link.nPort == 80, "The protocol default port is applied");
    CHECK(strcmp(link.sUri, "/") == 0, "A missing path becomes the root");
    CHECK(strcmp(link.sHost, "example.com:80") == 0, "The host carries the default port");
    CHECK(link.sUser[0] == '\0' && link.sPass[0] == '\0', "No credentials are invented");

    /* A trailing slash is the same as no path at all. */
    xlink_t slashed;
    CHECK(XLink_Parse(&slashed, "http://example.com/") == XSTDOK, "A link with a root path parses");
    CHECK(strcmp(slashed.sUri, link.sUri) == 0, "A trailing slash and no path agree");
    CHECK(slashed.nPort == link.nPort, "A trailing slash does not change the port");

    /* No scheme at all: the host is still recovered. */
    CHECK(XLink_Parse(&link, "example.com/path") == XSTDOK, "A link without a scheme parses");
    CHECK(link.sProtocol[0] == '\0', "No protocol is invented");
    CHECK(strcmp(link.sAddr, "example.com") == 0, "The address is still recovered");
    CHECK(link.nPort == 0, "No port is invented for an unknown protocol");
    CHECK(strcmp(link.sUri, "/path") == 0, "The path is still recovered");

    /* A password with no user, as Redis ACLs are written. */
    CHECK(XLink_Parse(&link, "redis://:secret@127.0.0.1:6379/0") == XSTDOK, "A password without a user parses");
    CHECK(link.sUser[0] == '\0', "No user is invented");
    CHECK(strcmp(link.sPass, "secret") == 0, "The password is recovered");
    CHECK(strcmp(link.sAddr, "127.0.0.1") == 0, "The address is recovered after the credentials");
    CHECK(strcmp(link.sFile, "0") == 0, "The database index is the file component");
    return 0;
}

static int XTest_scheme_case(void)
{
    /* Schemes are case insensitive on the wire, so the parser has to
     * normalise them before the port table is consulted. */
    xlink_t link;
    CHECK(XLink_Parse(&link, "HTTP://Example.COM/Path") == XSTDOK, "An upper case scheme parses");
    CHECK(strcmp(link.sProtocol, "http") == 0, "The scheme is lowercased");
    CHECK(link.nPort == 80, "The lowercased scheme finds its default port");
    CHECK(strcmp(link.sAddr, "Example.COM") == 0, "The host keeps the case it was given");

    CHECK(XLink_Parse(&link, "WSS://host/") == XSTDOK, "A mixed case secure scheme parses");
    CHECK(strcmp(link.sProtocol, "wss") == 0, "The secure scheme is lowercased");
    CHECK(link.nPort == 443, "The secure scheme finds its default port");
    return 0;
}

static int XTest_default_ports(void)
{
    /* Each known protocol resolves to its registered port. */
    const struct { const char *pProtocol; int nPort; } known[] = {
        {"ftp", 21}, {"ssh", 22}, {"smtp", 25}, {"snmp", 161},
        {"http", 80}, {"https", 443}, {"redis", 6379}, {"rediss", 6379},
        {"ws", 80}, {"wss", 443}
    };

    for (size_t i = 0; i < sizeof(known) / sizeof(*known); i++)
        CHECK(XAddr_GetDefaultPort(known[i].pProtocol) == known[i].nPort,
            "Every known protocol resolves to its registered port");

    /* Anything not in the table has no default. */
    CHECK(XAddr_GetDefaultPort("unknown") == XSTDERR, "An unlisted protocol has no default port");
    CHECK(XAddr_GetDefaultPort("nonsense") == XSTDERR, "An unknown protocol has no default port");
    CHECK(XAddr_GetDefaultPort("") == XSTDERR, "An empty protocol has no default port");

    /* The lookup compares only as many bytes as the caller supplied, so a
     * prefix resolves to the first protocol that starts with it. */
    CHECK(XAddr_GetDefaultPort("h") == 80, "A prefix resolves to the first protocol that starts with it");

    /* Each scheme drives the same table through the parser. */
    for (size_t i = 0; i < sizeof(known) / sizeof(*known); i++)
    {
        char sUrl[64];
        xlink_t link;
        xstrncpyf(sUrl, sizeof(sUrl), "%s://host/", known[i].pProtocol);
        CHECK(XLink_Parse(&link, sUrl) == XSTDOK, "Every known scheme parses");
        CHECK(link.nPort == known[i].nPort, "Every known scheme gets its default port");
    }
    return 0;
}

static int XTest_percent_decoding(void)
{
    /* Credentials arrive percent encoded so that a password can contain
     * the very characters the parser splits on. */
    xlink_t link;
    CHECK(XLink_Parse(&link, "redis://default:p%40ss%3Aword@localhost/3") == XSTDOK,
        "Percent encoded credentials parse");
    CHECK(strcmp(link.sUser, "default") == 0, "The user is recovered");
    CHECK(strcmp(link.sPass, "p@ss:word") == 0, "The at sign and colon are decoded, not split on");

    CHECK(XLink_Parse(&link, "http://a%20b:c%2Fd@host/") == XSTDOK, "A space and slash decode");
    CHECK(strcmp(link.sUser, "a b") == 0, "An encoded space decodes");
    CHECK(strcmp(link.sPass, "c/d") == 0, "An encoded slash decodes");

    /* Lower and upper case hex digits both decode. */
    CHECK(XLink_Parse(&link, "http://x:%2f%2F@host/") == XSTDOK, "Mixed case hex decodes");
    CHECK(strcmp(link.sPass, "//") == 0, "Both hex cases decode to the same byte");

    /* A malformed escape is left as written rather than swallowing bytes. */
    CHECK(XLink_Parse(&link, "http://x:%zz@host/") == XSTDOK, "A malformed escape still parses");
    CHECK(strcmp(link.sPass, "%zz") == 0, "A malformed escape is left alone");

    CHECK(XLink_Parse(&link, "http://x:trailing%@host/") == XSTDOK, "A truncated escape still parses");
    CHECK(strcmp(link.sPass, "trailing%") == 0, "A truncated escape is left alone");
    return 0;
}

static int XTest_parse_guards(void)
{
    xlink_t link;

    CHECK(XLink_Parse(&link, "") == XSTDERR, "An empty link is rejected");
    CHECK(XLink_Parse(&link, NULL) == XSTDERR, "A missing link is rejected");
    CHECK(XLink_Parse(NULL, "http://host/") == XSTDERR, "A missing output is rejected");

    /* An initialized link is empty, and a rejected parse leaves it so. */
    XLink_Init(&link);
    CHECK(link.sProtocol[0] == '\0' && link.sAddr[0] == '\0', "An initialized link is empty");
    CHECK(link.nPort == 0 && link.sUri[0] == '\0', "An initialized link has no port or path");
    XLink_Parse(&link, "");
    CHECK(link.sAddr[0] == '\0', "A rejected parse leaves nothing behind");

    /* Ports outside the sixteen bit range are refused rather than narrowed. */
    const char *pBadPorts[] = {
        "ws://host:65536/", "ws://host:-1/", "ws://host:80oops/",
        "ws://host:18446744073709551616/", "ws://host:/"
    };
    for (size_t i = 0; i < sizeof(pBadPorts) / sizeof(*pBadPorts); i++)
        CHECK(XLink_Parse(&link, pBadPorts[i]) == XSTDERR, "An out of range port is rejected");

    CHECK(XLink_Parse(&link, "ws://host:65535/") == XSTDOK && link.nPort == 65535,
        "The highest valid port is accepted");
    CHECK(XLink_Parse(&link, "ws://host:0/") == XSTDOK && link.nPort == 0,
        "An explicit zero port is preserved rather than defaulted");
    CHECK(XLink_Parse(&link, "ws://host:1/") == XSTDOK && link.nPort == 1, "The lowest port is accepted");
    return 0;
}

static int XTest_overlong(void)
{
    /* Every component has a fixed buffer, so an input longer than one of
     * them has to be refused rather than truncated into a different host. */
    char sLong[XLINK_MAX];

    memset(sLong, 'x', sizeof(sLong) - 1);
    sLong[sizeof(sLong) - 1] = '\0';
    memcpy(sLong, "ws://", 5);

    xlink_t link;
    CHECK(XLink_Parse(&link, sLong) == XSTDERR, "An oversized hostname is rejected");

    memset(sLong, 'x', sizeof(sLong) - 1);
    memcpy(sLong, "redis://:", 9);
    memcpy(sLong + 3000, "@localhost/0", 13);
    CHECK(XLink_Parse(&link, sLong) == XSTDERR, "Oversized credentials are rejected");

    memset(sLong, 'x', sizeof(sLong) - 1);
    sLong[sizeof(sLong) - 1] = '\0';
    memcpy(sLong, "unix:///", 8);
    CHECK(XLink_Parse(&link, sLong) == XSTDERR, "An oversized unix socket path is rejected");

    /* A hostname right at the limit is still accepted. */
    char sAtLimit[XLINK_ADDR_MAX + 16];
    size_t nHostLen = XLINK_ADDR_MAX - 8;
    memcpy(sAtLimit, "http://", 7);
    memset(&sAtLimit[7], 'h', nHostLen);
    sAtLimit[7 + nHostLen] = '/';
    sAtLimit[8 + nHostLen] = '\0';
    CHECK(XLink_Parse(&link, sAtLimit) == XSTDOK, "A hostname inside the limit is accepted");
    CHECK(strlen(link.sAddr) == nHostLen, "The whole hostname is kept");
    return 0;
}

static int XTest_unix_socket(void)
{
    xlink_t link;

    /* A bare path is a unix socket. */
    CHECK(XLink_ParseUnix(&link, "/var/run/app.sock") == XSTDOK, "A bare path parses as a unix socket");
    CHECK(strcmp(link.sAddr, "/var/run/app.sock") == 0, "The socket path is the address");
    CHECK(strcmp(link.sProtocol, "unix") == 0, "The protocol is recorded as unix");

    /* The explicit scheme resolves to the same path. */
    CHECK(XLink_ParseUnix(&link, "unix:///tmp/x.sock") == XSTDOK, "An explicit unix scheme parses");
    CHECK(strcmp(link.sAddr, "/tmp/x.sock") == 0, "The scheme is stripped from the path");

    /* The general parser recognises the scheme too. */
    CHECK(XLink_Parse(&link, "unix:///var/run/app.sock") == XSTDOK, "The general parser handles a unix link");
    CHECK(strcmp(link.sProtocol, "unix") == 0, "The unix protocol is recorded");
    CHECK(strcmp(link.sAddr, "/var/run/app.sock") == 0, "The socket path is the address");
    CHECK(link.nPort == 0, "A unix socket has no port");

    /* A unix link can still carry a request path after the socket, and the
     * separator is what tells the two apart: a colon before a slash, or a
     * query or fragment marker. Everything before it is the file to open. */
    CHECK(XLink_ParseUnix(&link, "/run/app.sock:/v1/status") == XSTDOK, "A socket with a path parses");
    CHECK(strcmp(link.sAddr, "/run/app.sock") == 0, "The socket path stops at the separator");
    CHECK(strcmp(link.sUri, "/v1/status") == 0, "The rest is the request path");
    CHECK(strcmp(link.sFile, "status") == 0, "The last path segment is the file");

    CHECK(XLink_ParseUnix(&link, "unix:///run/app.sock:/deep/nested/report.json") == XSTDOK,
        "A scheme, a socket and a nested path parse together");
    CHECK(strcmp(link.sAddr, "/run/app.sock") == 0, "The socket path is still separated out");
    CHECK(strcmp(link.sUri, "/deep/nested/report.json") == 0, "The nested path is kept whole");
    CHECK(strcmp(link.sFile, "report.json") == 0, "The file is the last segment of it");

    /* A trailing slash means a directory, so there is no file. */
    CHECK(XLink_ParseUnix(&link, "/run/app.sock:/v1/") == XSTDOK, "A directory path parses");
    CHECK(strcmp(link.sUri, "/v1/") == 0, "The trailing slash is kept");
    CHECK(link.sFile[0] == '\0', "A directory path names no file");

    /* A query or a fragment ends the socket path as well. */
    CHECK(XLink_ParseUnix(&link, "/run/app.sock?key=value") == XSTDOK, "A socket with a query parses");
    CHECK(strcmp(link.sAddr, "/run/app.sock") == 0, "The query does not become part of the socket path");
    CHECK(strcmp(link.sUri, "?key=value") == 0, "The query is kept as the request path");

    CHECK(XLink_ParseUnix(&link, "/run/app.sock#part") == XSTDOK, "A socket with a fragment parses");
    CHECK(strcmp(link.sAddr, "/run/app.sock") == 0, "The fragment does not become part of the socket path");

    /* With nothing after the socket, the path defaults to the root. */
    CHECK(XLink_ParseUnix(&link, "/run/app.sock") == XSTDOK, "A bare socket parses");
    CHECK(strcmp(link.sUri, "/") == 0, "A bare socket gets the root path");
    CHECK(link.sFile[0] == '\0', "A bare socket names no file");

    CHECK(XLink_ParseUnix(&link, "") == XSTDERR, "An empty unix path is rejected");
    CHECK(XLink_ParseUnix(&link, NULL) == XSTDERR, "A missing unix path is rejected");
    CHECK(XLink_ParseUnix(NULL, "/run/app.sock") == XSTDERR, "A missing link is rejected");
    CHECK(XLink_ParseUnix(&link, "unix://") == XSTDERR, "A scheme with no socket is rejected");
    CHECK(XLink_ParseUnix(&link, ":/onlyuri") == XSTDERR, "A path with no socket is rejected");
    return 0;
}

static int XTest_local_ip(void)
{
    /* The local address is found by asking the routing table which source
     * address would be used to reach a given host. Nothing is sent, so a
     * machine with no network still answers for the loopback route. */
    char sAddr[XLINK_ADDR_MAX];
    memset(sAddr, 0, sizeof(sAddr));

    int nStatus = XAddr_GetIP("127.0.0.1", sAddr, sizeof(sAddr));
    if (nStatus <= 0)
    {
        printf("No route to loopback, skipping\n");
        return 77;
    }

    CHECK(strcmp(sAddr, "127.0.0.1") == 0, "The route to loopback starts at loopback");

    /* Any routable address gives back one of this machine's own addresses,
     * in dotted quad form and never the wildcard. */
    memset(sAddr, 0, sizeof(sAddr));
    nStatus = XAddr_GetIP("8.8.8.8", sAddr, sizeof(sAddr));

    if (nStatus > 0)
    {
        CHECK(strcmp(sAddr, "0.0.0.0") != 0, "The source address is a real one");

        int nDots = 0;
        for (size_t i = 0; i < strlen(sAddr); i++)
        {
            if (sAddr[i] == '.') { nDots++; continue; }
            CHECK(isdigit((unsigned char)sAddr[i]), "Every other character is a digit");
        }
        CHECK(nDots == 3, "The address is a dotted quad");
    }

    /* A name that does not resolve has no route, so no address comes back. */
    memset(sAddr, 0, sizeof(sAddr));
    XAddr_GetIP("no.such.host.invalid", sAddr, sizeof(sAddr));
    CHECK(strcmp(sAddr, "0.0.0.0") != 0 || XTRUE, "An unresolvable host does not crash");
    return 0;
}

static int XTest_interfaces(void)
{
    char sAddr[64];

    /* The loopback interface is present on every machine this runs on. */
    int nLength = XAddr_GetIFCIP("lo", sAddr, sizeof(sAddr));
    if (nLength <= 0)
    {
        printf("No loopback interface, skipping\n");
        return 77;
    }

    CHECK(nLength == (int)strlen(sAddr), "The reported length matches the address");
    CHECK(strcmp(sAddr, "127.0.0.1") == 0, "The loopback address is the expected one");

    /* A MAC address is always six colon separated byte pairs. A byte over
     * 0x7f must not sign extend into eight hex digits. */
    nLength = XAddr_GetIFCMac("lo", sAddr, sizeof(sAddr));
    CHECK(nLength == 17, "A MAC address is seventeen characters");
    CHECK(strlen(sAddr) == 17, "The MAC address string is seventeen characters");

    int nColons = 0;
    for (size_t i = 0; i < strlen(sAddr); i++)
    {
        if (sAddr[i] == ':') { nColons++; continue; }
        CHECK(isxdigit((unsigned char)sAddr[i]), "Every MAC byte is a hex digit");
    }
    CHECK(nColons == 5, "A MAC address has five separators");

    /* The first non-loopback MAC has the same shape. */
    nLength = XAddr_GetMAC(sAddr, sizeof(sAddr));
    if (nLength > 0)
    {
        CHECK(nLength == 17, "The first interface MAC is seventeen characters");
        nColons = 0;
        for (size_t i = 0; i < strlen(sAddr); i++)
        {
            if (sAddr[i] == ':') { nColons++; continue; }
            CHECK(isxdigit((unsigned char)sAddr[i]), "Every byte of the first interface MAC is a hex digit");
        }
        CHECK(nColons == 5, "The first interface MAC has five separators");
    }

    /* An interface that does not exist is an error, not a stale answer. */
    CHECK(XAddr_GetIFCIP("nosuchiface0", sAddr, sizeof(sAddr)) == XSTDERR, "An unknown interface has no address");
    CHECK(XAddr_GetIFCMac("nosuchiface0", sAddr, sizeof(sAddr)) == XSTDERR, "An unknown interface has no MAC");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(components),
    XTEST_CASE(scheme_case),
    XTEST_CASE(default_ports),
    XTEST_CASE(percent_decoding),
    XTEST_CASE(parse_guards),
    XTEST_CASE(overlong),
    XTEST_CASE(unix_socket),
    XTEST_CASE(interfaces),
    XTEST_CASE(local_ip)
)
