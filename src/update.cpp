#include "update.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include <winhttp.h>

#include "log.h"

#pragma comment(lib, "winhttp.lib")

namespace idps {

namespace {

// Compare two dotted-numeric version strings. Returns >0 if a > b, 0 if
// equal, <0 if a < b. Non-numeric segments compare zero. Tolerant of
// missing trailing components ("0.4" == "0.4.0").
int compare_semver(const char* a, const char* b) {
    while (*a || *b) {
        unsigned na = 0, nb = 0;
        while (*a >= '0' && *a <= '9') { na = na * 10 + (*a - '0'); ++a; }
        while (*b >= '0' && *b <= '9') { nb = nb * 10 + (*b - '0'); ++b; }
        if (na != nb) return na > nb ? 1 : -1;
        if (*a == '.') ++a;
        if (*b == '.') ++b;
        if (!*a && !*b) break;
        if (*a && (*a < '0' || *a > '9') && *a != '.') break;
        if (*b && (*b < '0' || *b > '9') && *b != '.') break;
    }
    return 0;
}

bool fetch_latest_release_json(std::string& out) {
    HINTERNET session = WinHttpOpen(
        L"arcdps_individual_dps/auto-update",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    // 2s connect / 2s send / 2s receive — keep this fast since arc calls
    // the export during plugin load and a sluggish API check would stall
    // the game's startup window.
    WinHttpSetTimeouts(session, 2000, 2000, 2000, 2000);

    bool ok = false;
    HINTERNET conn = WinHttpConnect(session, L"api.github.com",
                                    INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (conn) {
        HINTERNET req = WinHttpOpenRequest(
            conn, L"GET",
            L"/repos/iTroy0/arcdps_individual_dps/releases/latest",
            nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (req) {
            const wchar_t* hdrs =
                L"Accept: application/vnd.github+json\r\n"
                L"X-GitHub-Api-Version: 2022-11-28\r\n";
            WinHttpAddRequestHeaders(req, hdrs, static_cast<DWORD>(-1),
                                     WINHTTP_ADDREQ_FLAG_ADD);

            if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                DWORD status = 0, size = sizeof(status);
                WinHttpQueryHeaders(req,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                    WINHTTP_NO_HEADER_INDEX);
                if (status == 200) {
                    DWORD avail = 0;
                    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                        std::vector<char> buf(avail);
                        DWORD read = 0;
                        if (!WinHttpReadData(req, buf.data(), avail, &read)) break;
                        out.append(buf.data(), read);
                        if (out.size() > 64 * 1024) break; // sanity cap
                    }
                    ok = !out.empty();
                } else {
                    log_line("update check: HTTP %lu", status);
                }
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(session);
    return ok;
}

bool extract_tag_name(const std::string& json, std::string& out) {
    // GitHub JSON has "tag_name":"vX.Y.Z" near the top of the object.
    // Skipping a real JSON parser keeps the dependency surface tiny —
    // the field is well-defined and we only need a literal substring.
    const char* needle = "\"tag_name\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos += std::strlen(needle);
    auto end = json.find('"', pos);
    if (end == std::string::npos) return false;
    out.assign(json, pos, end - pos);
    return !out.empty();
}

} // namespace

const wchar_t* check_for_update(const char* current_version) {
    static wchar_t url_buf[256];
    url_buf[0] = L'\0';

    std::string json;
    if (!fetch_latest_release_json(json)) return nullptr;

    std::string tag;
    if (!extract_tag_name(json, tag)) return nullptr;

    const char* remote = tag.c_str();
    if (*remote == 'v' || *remote == 'V') ++remote;

    if (compare_semver(remote, current_version) <= 0) {
        log_line("update check: up-to-date (current=%s remote=%s)",
                 current_version, remote);
        return nullptr;
    }

    int written = std::swprintf(
        url_buf, sizeof(url_buf) / sizeof(url_buf[0]),
        L"https://github.com/iTroy0/arcdps_individual_dps/releases/download/%hs/arcdps_individual_dps.dll",
        tag.c_str());
    if (written <= 0) return nullptr;

    log_line("update check: newer release %s available (current=%s)",
             tag.c_str(), current_version);
    return url_buf;
}

} // namespace idps
