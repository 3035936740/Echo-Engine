//
// Created by Bing on 2023/11/23.
//
#pragma once

#ifndef FRAMEPERSECOND_HPP
#define FRAMEPERSECOND_HPP

#include <chrono>
#include <memory>
#include <mutex>

using namespace std::chrono_literals;
namespace echo::utils{
    namespace fps_literals{
        [[maybe_unused]] inline constexpr std::chrono::microseconds operator"" _fps(std::uint64_t fps){
            return 1000000us / fps;
        }

        [[maybe_unused]] inline constexpr std::chrono::microseconds FPS(std::uint64_t fps){
            return 1000000us / fps;
        }
    }

    using namespace fps_literals;

    class FramePerSecond {
    private:
        std::chrono::microseconds m_frames {60_fps};
        std::shared_ptr<std::mutex> m_mutex{nullptr};
    public:
        FramePerSecond();

        explicit FramePerSecond(std::shared_ptr<std::mutex>);

        explicit FramePerSecond(std::chrono::microseconds&&);
        explicit FramePerSecond(const std::chrono::microseconds&);

        explicit FramePerSecond(std::chrono::microseconds, std::shared_ptr<std::mutex>);

        virtual ~FramePerSecond();

        FramePerSecond(const FramePerSecond &);

        FramePerSecond(FramePerSecond &&) noexcept;

        virtual FramePerSecond &operator=(const FramePerSecond &);

        virtual FramePerSecond &operator=(FramePerSecond &&) noexcept;

        void setFrames(std::chrono::microseconds fps);

        void setMutex(std::shared_ptr<std::mutex>);

        const std::chrono::microseconds& getFrames();
    };
}


#endif //FRAMEPERSECOND_HPP
