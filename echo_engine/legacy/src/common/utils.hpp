//
// Created by Bing on 2023/11/20.
//
#pragma once

#ifndef UTILS_HPP
#define UTILS_HPP
#include <iostream>
#include <chrono>

#define PRINT_FMT(function_name)  template <typename... Tn>inline void function_name(std::format_string<Tn...> fmt, Tn&&... args)

using namespace std::chrono_literals;

namespace echo::utils{
    namespace type{
        using rgba = std::array<std::uint8_t, 4>;
    }

    template <typename T>
    [[maybe_unused]] inline void print(const T& arg) {
        std::cout << arg; std::cout.flush();
    }
    template <typename T, typename... Tn>
    [[maybe_unused]] inline void print(const T& arg, const Tn&... args) {
        if constexpr (sizeof...(args) > 0) {
            std::cout << arg << ' ';
        } print(args...);
    }
    template <typename T, typename... Tn>
    [[maybe_unused]] inline void println(const T& arg, const Tn&... args) {
        print(arg, args...); std::cout << std::endl;
    }
    template <typename T>
    [[maybe_unused]] inline void print(T&& arg) {
        std::cout << arg; std::cout.flush();
    }
    template <typename T, typename... Tn>
    [[maybe_unused]] inline void print(T&& arg, Tn&&... args) {
        if constexpr (sizeof...(args) > 0) {
            std::cout << arg << ' ';
        }
        print(args...);
    }
    template <typename T, typename... Tn>
    [[maybe_unused]] inline void println(T&& arg, Tn&&... args) {
        print(arg, args...); std::cout << std::endl;
    }

    PRINT_FMT(print_fmt) {
        std::cout << std::format(fmt, std::forward<Tn>(args)...);
        std::cout.flush();
    }

    PRINT_FMT(println_fmt) {
        print_fmt(fmt, std::forward<Tn>(args)...);
        std::cout << std::endl;
    }

    [[maybe_unused]] inline constexpr auto operator""_f(const char* fmt, size_t) {
        return[=]<typename... T>(T&&... Args) { return std::vformat(fmt, std::make_format_args(std::forward<T>(Args)...)); };
    }

    [[maybe_unused]] inline constexpr type::rgba RGBA(uint8_t red, uint8_t green,uint8_t blue, uint8_t alpha){
        return type::rgba{red,green,blue,alpha};
    }
}

#undef PRINT_FMT
#endif //UTILS_HPP
