// The standard library headers the engine uses most, compiled once per project (a precompiled header,
// src/RM.Pch.props) and read before every source file of RM_Engine and RMTests (forced include: the
// sources do not include this file themselves and still include what they use). Only the standard
// library: never Windows, Direct3D or the engine's own headers (those change, or would let every file
// see Windows - the layers in layers.txt stay what the includes say).
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
