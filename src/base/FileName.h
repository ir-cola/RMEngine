#pragma once
// Checks whether a string can be used as one Windows file / folder name (pure).

#include <string_view>

namespace rm {

enum class FileNameError {
    None,
    Empty,
    TooLong,
    InvalidChar,         // < > : " / \ | ? * or a control character
    ReservedName,        // CON, PRN, AUX, NUL, COM1-9, LPT1-9 (also with an extension)
    LeadingSpace,
    TrailingDotOrSpace,  // Windows silently strips these
};

// maxChars counts UTF-16 code units.
FileNameError ValidateFileName(std::string_view utf8, size_t maxChars = 64);

}  // namespace rm
