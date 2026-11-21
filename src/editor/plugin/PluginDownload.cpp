#include "editor/plugin/PluginDownload.h"

#include "base/Utf.h"
#include "editor/lang/Lang.h"
#include "editor/plugin/PluginIndex.h"
#include "platform/Crypto.h"
#include "platform/Shell.h"
#include "platform/Win.h"

#include <algorithm>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace rm::editor {

PackageFetch ExtractPackage(const std::string& zipBytes, const fs::path& into) {
    PackageFetch r;
    auto entries = zip::ReadDirectory(zipBytes, &r.zipProblem);
    if (!entries) {
        r.fail = PackageFetch::Fail::Zip;
        return r;
    }
    // 先に全部の名前と大きさを確かめる: 1 つでもだめなら何も書かない。
    uint64_t total = 0;
    bool manifest = false;
    for (zip::Entry& e : *entries) {
        e.name = NormalizeZipPath(e.name);
        if (const wchar_t* why = ZipPathProblem(e.name)) {
            r.fail = PackageFetch::Fail::Path;
            r.pathProblem = why;
            r.name = e.name;
            return r;
        }
        total += e.size;
        if (total > kMaxPackageBytes) {
            r.fail = PackageFetch::Fail::TooBig;
            return r;
        }
        manifest = manifest || (!e.directory && e.name == "plugin.json");
    }
    if (!manifest) {
        r.fail = PackageFetch::Fail::NoManifest;
        return r;
    }
    std::error_code ec;
    fs::remove_all(into, ec);
    fs::create_directories(into, ec);
    if (ec) {
        r.fail = PackageFetch::Fail::Disk;
        r.code = static_cast<unsigned long>(ec.value());
        return r;
    }
    for (const zip::Entry& e : *entries) {
        const fs::path target = into / PathFromUtf8(e.name);
        if (e.directory) {
            fs::create_directories(target, ec);
            continue;
        }
        auto bytes = zip::ReadEntry(zipBytes, e, kMaxPackageBytes, &r.zipProblem);
        if (!bytes) {
            r.fail = PackageFetch::Fail::Zip;
            r.name = e.name;
            return r;
        }
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(bytes->data(), static_cast<std::streamsize>(bytes->size()))) {
            r.fail = PackageFetch::Fail::Disk;
            r.code = ERROR_WRITE_FAULT;
            r.name = e.name;
            return r;
        }
    }
    return r;
}

PackageFetch FetchPackage(const std::string& url, uint64_t size, const std::string& sha256, const fs::path& into, std::stop_token stop) {
    PackageFetch r;
    r.fetch = platform::Fetch(url, std::min(kMaxPackageBytes, size + 1), stop);
    if (!r.fetch.body) {
        r.fail = PackageFetch::Fail::Fetch;
        return r;
    }
    const std::string& bytes = *r.fetch.body;
    if (bytes.size() != size) {
        r.fail = PackageFetch::Fail::Size;
        r.gotSize = bytes.size();
        return r;
    }
    if (!SameSha256(Sha256Hex(platform::Sha256(bytes)), sha256)) {
        r.fail = PackageFetch::Fail::Hash;
        return r;
    }
    return ExtractPackage(bytes, into);
}

std::wstring DescribeFetch(const platform::FetchResult& r) {
    using F = platform::FetchResult::Fail;
    switch (r.fail) {
        case F::None: return {};
        case F::BadUrl: return Tr(L"URL の形が正しくありません");
        case F::Network: return TrF(L"つながりませんでした ({})", Utf8ToWide(platform::ErrorText(r.code)));
        case F::Status: return TrF(L"サーバーが断りました (HTTP {})", r.status);
        case F::TooBig: return Tr(L"大きすぎます");
        case F::File: return TrF(L"ファイルを読めませんでした ({})", Utf8ToWide(platform::ErrorText(r.code)));
        case F::Stopped: return Tr(L"やめました");
    }
    return {};
}

std::wstring DescribePackageFetch(const PackageFetch& r) {
    using F = PackageFetch::Fail;
    auto zipWhy = [&] {
        switch (r.zipProblem) {
            case zip::Problem::NotZip: return Tr(L"zip ではありません");
            case zip::Problem::Unsupported: return Tr(L"読めない zip の形です (zip64・暗号・deflate 以外の圧縮)");
            case zip::Problem::TooBig: return Tr(L"中身が大きすぎます");
            case zip::Problem::Crc: return Tr(L"中身が壊れています (CRC が違います)");
            default: return Tr(L"zip が壊れています");
        }
    };
    switch (r.fail) {
        case F::None: return {};
        case F::Fetch: return DescribeFetch(r.fetch);
        case F::Size: return TrF(L"大きさが一覧と違います ({} バイト)", r.gotSize);
        case F::Hash: return Tr(L"中身が一覧と違います (SHA-256 が合いません)。途中で壊れたか、書き換えられています");
        case F::Zip: return r.name.empty() ? zipWhy() : TrF(L"{}: {}", Utf8ToWide(r.name), zipWhy());
        case F::Path: return TrF(L"zip の中に置けない名前があります ({}: {})", Utf8ToWide(r.name), Tr(r.pathProblem));
        case F::TooBig: return Tr(L"展開すると大きすぎます");
        case F::NoManifest: return Tr(L"zip の一番上に plugin.json がありません");
        case F::Disk: return TrF(L"書き出せませんでした ({})", Utf8ToWide(platform::ErrorText(r.code)));
    }
    return {};
}

}  // namespace rm::editor
