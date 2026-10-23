#include "echo/snapshot_diff.hpp"
#include "echo/snapshot_fields.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdio>
#include <cstring>
namespace echo {
namespace {
template <class T> void format(Text<80>& out, T value) noexcept {
    out.data.fill(0);
    if constexpr (std::is_same_v<T, bool>) {
        out.set(value ? "true" : "false");
    } else if constexpr (std::is_floating_point_v<T>) {
        if constexpr (sizeof(T) == 4)
            std::snprintf(out.data.data(), out.data.size(), "%.9g [0x%08x]", double(value),
                          unsigned(std::bit_cast<std::uint32_t>(value)));
        else
            std::snprintf(out.data.data(), out.data.size(), "%.17g [0x%016llx]", double(value),
                          static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(value)));
    } else if constexpr (std::is_same_v<T, char>) {
        format(out, static_cast<unsigned char>(value));
    } else if constexpr (std::is_enum_v<T>) {
        format(out, static_cast<std::underlying_type_t<T>>(value));
    } else if constexpr (std::is_signed_v<T>) {
        std::to_chars(out.data.data(), out.data.data() + out.data.size() - 1,
                      static_cast<long long>(value));
    } else {
        std::to_chars(out.data.data(), out.data.data() + out.data.size() - 1,
                      static_cast<unsigned long long>(value));
    }
}
class Walker {
    SnapshotDiff& out_;
    std::string_view filter_;
    std::array<char, 256> path_{};
    std::size_t length_{};
    void append(std::string_view text) noexcept {
        auto n = std::min(text.size(), path_.size() - 1 - length_);
        if (n != text.size())
            out_.pathsTruncated = true;
        std::memcpy(path_.data() + length_, text.data(), n);
        length_ += n;
        path_[length_] = 0;
    }
    template <class F> void named(std::string_view name, F&& operation) noexcept {
        auto old = length_;
        if (length_)
            append(".");
        append(name);
        operation();
        length_ = old;
        path_[length_] = 0;
    }
    template <class T> void leaf(const T& a, const T& b) noexcept {
        ++out_.inspected;
        bool same;
        if constexpr (std::is_floating_point_v<T>) {
            if constexpr (sizeof(T) == 4)
                same = std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
            else
                same = std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
        } else
            same = a == b;
        if (same)
            return;
        ++out_.differences;
        std::string_view path(path_.data(), length_);
        if (!filter_.empty() && path.find(filter_) == std::string_view::npos)
            return;
        ++out_.matching;
        if (out_.entries.count == maxSnapshotDifferences)
            return;
        auto& entry = out_.entries.values[out_.entries.count++];
        if (path.size() >= entry.field.data.size())
            out_.pathsTruncated = true;
        entry.field.set(path);
        format(entry.before, a);
        format(entry.after, b);
    }

  public:
    Walker(SnapshotDiff& out, std::string_view filter) : out_(out), filter_(filter) {}
    template <class T> void visit(const T& a, const T& b) noexcept {
        if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>)
            leaf(a, b);
        else
            snapshot_detail::members(a, b, [&](std::string_view name, const auto& x, const auto& y) {
                named(name, [&] { visit(x, y); });
            });
    }
    template <class T, std::size_t N>
    void visit(const std::array<T, N>& a, const std::array<T, N>& b) noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            auto old = length_;
            char index[24];
            auto end = std::to_chars(index, index + sizeof(index), i).ptr;
            append("[");
            append({index, std::size_t(end - index)});
            append("]");
            visit(a[i], b[i]);
            length_ = old;
            path_[length_] = 0;
        }
    }
    template <class T, std::size_t N>
    void visit(const FixedList<T, N>& a, const FixedList<T, N>& b) noexcept {
        [[maybe_unused]] const auto& [values, count] = a;
        named("count", [&] { visit(a.count, b.count); });
        // Never trust active counts while diagnosing a damaged snapshot.
        named("values", [&] { visit(a.values, b.values); });
    }
    template <std::size_t N> void visit(const Text<N>& a, const Text<N>& b) noexcept {
        [[maybe_unused]] const auto& [data] = a;
        named("data", [&] { visit(a.data, b.data); });
    }
};
} // namespace
void compareSnapshots(const WorldState& a, const WorldState& b, SnapshotDiff& out,
                      std::string_view filter) noexcept {
    out.entries.count = 0;
    out.inspected = out.differences = out.matching = 0;
    out.pathsTruncated = false;
    Walker(out, filter).visit(a, b);
}
} // namespace echo
