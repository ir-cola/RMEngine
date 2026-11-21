#include "platform/Http.h"

#include "base/Utf.h"
#include "base/Version.h"
#include "platform/Win.h"

#include <winhttp.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace rm::platform {

namespace {

bool StartsWithNoCase(const std::string& s, const char* prefix) {
    for (size_t i = 0; prefix[i]; ++i) {
        if (i >= s.size() || std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) return false;
    }
    return true;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

FetchResult FromFile(const std::wstring& path, uint64_t maxBytes) {
    FetchResult r;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        r.fail = FetchResult::Fail::File;
        r.code = static_cast<unsigned long>(ec.value());
        return r;
    }
    if (size > maxBytes) {
        r.fail = FetchResult::Fail::TooBig;
        return r;
    }
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    std::string bytes(static_cast<size_t>(size), '\0');
    if (!in || !in.read(bytes.data(), static_cast<std::streamsize>(size))) {
        r.fail = FetchResult::Fail::File;
        r.code = ERROR_READ_FAULT;
        return r;
    }
    r.body = std::move(bytes);
    return r;
}

}  // namespace

std::wstring LocalPathOf(const std::string& url) {
    if (StartsWithNoCase(url, "file:")) {
        std::string rest = url.substr(5);
        size_t slashes = 0;
        while (slashes < rest.size() && rest[slashes] == '/') ++slashes;
        rest.erase(0, slashes);  // (file:///C:/x → C:/x)
        const bool drive = rest.size() >= 2 && rest[1] == ':';
        std::string decoded = !drive && slashes == 2 ? "\\\\" : "";  // (file://server/share は UNC のパスに)
        for (size_t i = 0; i < rest.size(); ++i) {
            if (rest[i] == '%' && i + 2 < rest.size() && HexValue(rest[i + 1]) >= 0 && HexValue(rest[i + 2]) >= 0) {
                decoded += static_cast<char>(HexValue(rest[i + 1]) * 16 + HexValue(rest[i + 2]));
                i += 2;
            } else {
                decoded += rest[i] == '/' ? '\\' : rest[i];
            }
        }
        return Utf8ToWide(decoded);
    }
    const bool drive = url.size() >= 3 && std::isalpha(static_cast<unsigned char>(url[0])) && url[1] == ':' && (url[2] == '\\' || url[2] == '/');
    const bool unc = url.size() >= 2 && url[0] == '\\' && url[1] == '\\';
    return drive || unc ? Utf8ToWide(url) : std::wstring{};
}

FetchResult Fetch(const std::string& url, uint64_t maxBytes, std::stop_token stop) {
    if (const std::wstring local = LocalPathOf(url); !local.empty()) return FromFile(local, maxBytes);
    FetchResult r;
    if (!StartsWithNoCase(url, "http://") && !StartsWithNoCase(url, "https://")) {
        r.fail = FetchResult::Fail::BadUrl;
        return r;
    }
    const std::wstring wide = Utf8ToWide(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &uc) || uc.dwHostNameLength == 0) {
        r.fail = FetchResult::Fail::BadUrl;
        return r;
    }
    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath ? std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) : L"/");
    if (uc.lpszExtraInfo) path += std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    auto network = [&r] {
        r.fail = FetchResult::Fail::Network;
        r.code = GetLastError();
        return r;
    };
    const std::wstring agent = L"RM Engine/" + std::wstring(kEngineVersionW);
    Handle session{WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) return network();
    WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 30000);
    Handle connect{WinHttpConnect(session.h, host.c_str(), uc.nPort, 0)};
    if (!connect.h) return network();
    Handle request{WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.h) return network();
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        return network();
    }
    DWORD status = 0, len = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len,
                             WINHTTP_NO_HEADER_INDEX)) {
        return network();
    }
    if (status != 200) {
        r.fail = FetchResult::Fail::Status;
        r.status = static_cast<int>(status);
        return r;
    }
    std::string body;
    std::vector<char> chunk;
    for (;;) {
        if (stop.stop_requested()) {
            r.fail = FetchResult::Fail::Stopped;
            return r;
        }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail)) return network();
        if (avail == 0) break;
        if (body.size() + avail > maxBytes) {
            r.fail = FetchResult::Fail::TooBig;
            return r;
        }
        chunk.resize(avail);
        DWORD got = 0;
        if (!WinHttpReadData(request.h, chunk.data(), avail, &got)) return network();
        body.append(chunk.data(), got);
    }
    r.body = std::move(body);
    return r;
}

}  // namespace rm::platform
