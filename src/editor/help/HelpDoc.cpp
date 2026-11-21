#include "editor/help/HelpDoc.h"

#include "base/Utf.h"

#include <optional>
#include <utility>

namespace rm::editor::help {

namespace {

std::vector<std::wstring> SplitLines(std::string_view utf8) {
    if (utf8.starts_with("\xEF\xBB\xBF")) utf8.remove_prefix(3);  // (BOM)
    const std::wstring all = Utf8ToWide(utf8);
    std::vector<std::wstring> lines;
    size_t start = 0;
    for (;;) {
        const size_t nl = all.find(L'\n', start);
        std::wstring line = all.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(std::move(line));
        if (nl == std::wstring::npos) break;
        start = nl + 1;
    }
    return lines;
}

std::wstring_view Trim(std::wstring_view s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.remove_suffix(1);
    return s;
}

size_t Indent(std::wstring_view s) {
    size_t n = 0;
    for (const wchar_t ch : s) {
        if (ch == L' ') {
            ++n;
        } else if (ch == L'\t') {
            n += 4;
        } else {
            break;
        }
    }
    return n;
}

bool IsAsciiWordEnd(wchar_t ch) { return ch < 0x80 && ch != L' '; }

// 段落の続きの行をつなぐ: 英語の単語の間には空白、日本語どうしはそのまま。
void JoinLine(std::wstring& text, std::wstring_view more) {
    if (more.empty()) return;
    if (!text.empty() && IsAsciiWordEnd(text.back()) && IsAsciiWordEnd(more.front())) text += L' ';
    text += more;
}

// "- 文字" / "* 文字" なら文字の始まり。
std::optional<size_t> BulletAt(std::wstring_view t) {
    if (t.size() >= 2 && (t[0] == L'-' || t[0] == L'*') && t[1] == L' ') return 2;
    return std::nullopt;
}

// "12. 文字" なら (番号, 文字の始まり)。
std::optional<std::pair<int, size_t>> NumberAt(std::wstring_view t) {
    size_t i = 0;
    int n = 0;
    while (i < t.size() && i < 6 && t[i] >= L'0' && t[i] <= L'9') n = n * 10 + (t[i++] - L'0');
    if (i == 0 || i + 1 >= t.size() || t[i] != L'.' || t[i + 1] != L' ') return std::nullopt;
    return std::make_pair(n, i + 2);
}

bool IsRule(std::wstring_view t) {
    if (t.size() < 3) return false;
    for (const wchar_t ch : t) {
        if (ch != L'-') return false;
    }
    return true;
}

// "|a|b|" -> ["a", "b"]。
std::vector<std::wstring> Cells(std::wstring_view t) {
    if (t.starts_with(L'|')) t.remove_prefix(1);
    if (t.ends_with(L'|')) t.remove_suffix(1);
    std::vector<std::wstring> out;
    size_t start = 0;
    for (;;) {
        const size_t bar = t.find(L'|', start);
        out.emplace_back(Trim(t.substr(start, bar == std::wstring_view::npos ? std::wstring_view::npos : bar - start)));
        if (bar == std::wstring_view::npos) break;
        start = bar + 1;
    }
    return out;
}

bool IsTableSeparator(const std::vector<std::wstring>& cells) {
    for (const std::wstring& c : cells) {
        if (c.empty()) return false;
        for (const wchar_t ch : c) {
            if (ch != L'-' && ch != L':') return false;
        }
    }
    return !cells.empty();
}

// "![説明](パス)" ならそのブロック。
std::optional<Block> ImageAt(std::wstring_view t) {
    if (!t.starts_with(L"![")) return std::nullopt;
    const size_t close = t.find(L"](");
    if (close == std::wstring_view::npos || !t.ends_with(L')')) return std::nullopt;
    Block b;
    b.kind = Block::Kind::Image;
    b.alt = std::wstring(t.substr(2, close - 2));
    b.image = WideToUtf8(Trim(t.substr(close + 2, t.size() - close - 3)));
    return b;
}

}  // namespace

std::vector<Span> ParseInline(std::wstring_view text) {
    std::vector<Span> out;
    std::wstring plain;
    auto flush = [&] {
        if (!plain.empty()) out.push_back({Span::Kind::Text, std::move(plain), {}});
        plain.clear();
    };
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == L'\\' && i + 1 < text.size()) {  // (\* \` \[ \\: 印ではなく文字)
            plain += text[i + 1];
            i += 2;
            continue;
        }
        if (text.substr(i).starts_with(L"**")) {
            const size_t end = text.find(L"**", i + 2);
            if (end != std::wstring_view::npos && end > i + 2) {
                flush();
                out.push_back({Span::Kind::Bold, std::wstring(text.substr(i + 2, end - i - 2)), {}});
                i = end + 2;
                continue;
            }
        }
        if (text[i] == L'`') {
            const size_t end = text.find(L'`', i + 1);
            if (end != std::wstring_view::npos && end > i + 1) {
                flush();
                out.push_back({Span::Kind::Code, std::wstring(text.substr(i + 1, end - i - 1)), {}});
                i = end + 1;
                continue;
            }
        }
        if (text[i] == L'[') {
            const size_t mid = text.find(L"](", i + 1);
            const size_t end = mid == std::wstring_view::npos ? mid : text.find(L')', mid + 2);
            if (mid != std::wstring_view::npos && end != std::wstring_view::npos && mid > i + 1) {
                flush();
                out.push_back({Span::Kind::Link, std::wstring(text.substr(i + 1, mid - i - 1)), WideToUtf8(Trim(text.substr(mid + 2, end - mid - 2)))});
                i = end + 1;
                continue;
            }
        }
        plain += text[i++];
    }
    flush();
    return out;
}

std::wstring PlainText(const std::vector<Span>& spans) {
    std::wstring s;
    for (const Span& sp : spans) s += sp.text;
    return s;
}

std::wstring TitleOf(std::string_view utf8) {
    for (const std::wstring& line : SplitLines(utf8)) {
        const std::wstring_view t = Trim(line);
        if (t.starts_with(L"# ")) return PlainText(ParseInline(Trim(t.substr(2))));
    }
    return {};
}

Doc ParseDoc(std::string_view utf8) {
    Doc doc;
    const std::vector<std::wstring> lines = SplitLines(utf8);
    // 書いている途中の段落 (同じ種類が続く間は、行をつなぐ)。
    std::wstring open;
    Block::Kind openKind = Block::Kind::Paragraph;
    int openLevel = 0, openNumber = 0;
    bool hasOpen = false;
    auto close = [&] {
        if (!hasOpen) return;
        Block b;
        b.kind = openKind;
        b.level = openLevel;
        b.number = openNumber;
        b.spans = ParseInline(open);
        doc.blocks.push_back(std::move(b));
        open.clear();
        hasOpen = false;
    };
    auto start = [&](Block::Kind kind, int level, int number, std::wstring_view text) {
        close();
        openKind = kind;
        openLevel = level;
        openNumber = number;
        open = std::wstring(text);
        hasOpen = true;
    };

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::wstring& line = lines[i];
        const std::wstring_view t = Trim(line);
        if (t.starts_with(L"```")) {  // コード: 閉じる ``` まで、そのまま
            close();
            Block b;
            b.kind = Block::Kind::Code;
            for (++i; i < lines.size() && !Trim(lines[i]).starts_with(L"```"); ++i) {
                if (!b.code.empty()) b.code += L'\n';
                b.code += lines[i];
            }
            doc.blocks.push_back(std::move(b));
            continue;
        }
        if (t.empty()) {
            close();
            continue;
        }
        if (t.starts_with(L"#")) {
            size_t n = 0;
            while (n < t.size() && t[n] == L'#') ++n;
            if (n <= 3 && n < t.size() && t[n] == L' ') {
                close();
                Block b;
                b.kind = Block::Kind::Heading;
                b.level = static_cast<int>(n);
                b.spans = ParseInline(Trim(t.substr(n + 1)));
                if (n == 1 && doc.title.empty()) doc.title = PlainText(b.spans);
                doc.blocks.push_back(std::move(b));
                continue;
            }
        }
        if (IsRule(t)) {
            close();
            doc.blocks.push_back({Block::Kind::Rule});
            continue;
        }
        if (auto img = ImageAt(t)) {
            close();
            doc.blocks.push_back(std::move(*img));
            continue;
        }
        if (t.starts_with(L'|')) {
            close();
            Block b;
            b.kind = Block::Kind::Table;
            for (; i < lines.size() && Trim(lines[i]).starts_with(L'|'); ++i) {
                const std::vector<std::wstring> cells = Cells(Trim(lines[i]));
                if (b.rows.size() == 1 && IsTableSeparator(cells)) continue;
                std::vector<std::vector<Span>> row;
                for (const std::wstring& cell : cells) row.push_back(ParseInline(cell));
                b.rows.push_back(std::move(row));
            }
            --i;
            doc.blocks.push_back(std::move(b));
            continue;
        }
        if (t.starts_with(L'>')) {
            const std::wstring_view rest = Trim(t.substr(1));
            if (hasOpen && openKind == Block::Kind::Note) {
                JoinLine(open, rest);
            } else {
                start(Block::Kind::Note, 0, 0, rest);
            }
            continue;
        }
        const int depth = static_cast<int>(Indent(line) / 2);
        if (const auto b = BulletAt(t)) {
            start(Block::Kind::Bullet, depth, 0, Trim(t.substr(*b)));
            continue;
        }
        if (const auto n = NumberAt(t)) {
            start(Block::Kind::Number, depth, n->first, Trim(t.substr(n->second)));
            continue;
        }
        // ふつうの文: 前の段落 (と箇条書きの続きの行) につなぐ。
        if (hasOpen && openKind != Block::Kind::Note) {
            JoinLine(open, t);
        } else {
            start(Block::Kind::Paragraph, 0, 0, t);
        }
    }
    close();
    return doc;
}

std::string ResolveLink(std::string_view book, std::string_view folder, std::string_view target) {
    if (target.empty()) return {};
    if (IsWebLink(target)) return std::string(target);
    if (target.ends_with(".md")) target.remove_suffix(3);
    if (target.starts_with('/')) return std::string(target.substr(1));
    // 本の中のフォルダー (本の名前の後ろ) から、区切りごとに進む。
    std::vector<std::string> parts;
    if (folder.size() > book.size() && folder.starts_with(book) && folder[book.size()] == '/') {
        std::string_view rest = folder.substr(book.size() + 1);
        while (!rest.empty()) {
            const size_t slash = rest.find('/');
            parts.emplace_back(rest.substr(0, slash));
            if (slash == std::string_view::npos) break;
            rest.remove_prefix(slash + 1);
        }
    }
    while (!target.empty()) {
        const size_t slash = target.find('/');
        const std::string_view part = target.substr(0, slash);
        if (part == "..") {
            if (parts.empty()) return {};  // (本の外)
            parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.emplace_back(part);
        }
        if (slash == std::string_view::npos) break;
        target.remove_prefix(slash + 1);
    }
    if (!parts.empty() && parts.back() == "index") parts.pop_back();  // (フォルダーのページ)
    std::string id(book);
    for (const std::string& p : parts) id += "/" + p;
    return id;
}

}  // namespace rm::editor::help
