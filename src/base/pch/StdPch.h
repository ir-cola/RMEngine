// エンジンがよく使う標準ライブラリのヘッダー。プロジェクトごとに 1 回コンパイルし (プリコンパイル済み
// ヘッダー、src/RM.Pch.props)、RM_Engine と RMTests の全ソースの前に読み込む (強制インクルード:
// ソース自身はこのファイルをインクルードせず、使うものは今まで通り自分でインクルードする)。標準
// ライブラリだけ: Windows・Direct3D・エンジン自身のヘッダーは決して入れない (それらは変わるし、
// 全ファイルから Windows が見えてしまう - layers.txt の層は、インクルードが示す通りのままにする)。
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
