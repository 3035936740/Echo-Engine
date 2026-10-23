#pragma once

#ifndef SELF_CORE_HPP
#define SELF_CORE_HPP

#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
// #include "opencv2/opencv.hpp"

#ifdef _WIN32
#define DLL_EXPORT __declspec(dllexport)
#include <windows.h>
#else
#define DLL_EXPORT
#endif

namespace core {
    namespace type{
        using rgba = std::array<std::uint8_t, 4>;
    }
    namespace utils{
        // [[maybe_unused]] cv::Mat clearColor(const std::filesystem::path&, const core::type::rgba&);
    }
#ifdef _WIN32
    [[maybe_unused]] void hidden_console(bool enable = true);
#endif
}

#undef PRINT_FMT
#endif // !SELF_CORE_HPP
