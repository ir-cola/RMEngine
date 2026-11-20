#pragma once
// 文字列が Windows のファイル名 / フォルダー名 1 つとして使えるか調べる (純粋)。

#include <string_view>

namespace rm {

enum class FileNameError {
    None,
    Empty,
    TooLong,
    InvalidChar,         // < > : " / \ | ? * か制御文字
    ReservedName,        // CON, PRN, AUX, NUL, COM1-9, LPT1-9 (拡張子が付いていても)
    LeadingSpace,
    TrailingDotOrSpace,  // Windows が黙って取り除く
};

// maxChars は UTF-16 のコード単位で数える。
FileNameError ValidateFileName(std::string_view utf8, size_t maxChars = 64);

}  // namespace rm
