#include "core.hpp"

/*
[[maybe_unused]] DLL_EXPORT cv::Mat core::utils::clearColor(const std::filesystem::path& filename, const core::type::rgba& rgba){
    cv::Mat image = cv::imread(filename.string(), cv::IMREAD_UNCHANGED);
    cv::Mat input_bgra;
    cv::cvtColor(image, input_bgra, cv::COLOR_BGR2BGRA);

    cv::Scalar detect_color(rgba.at(2), rgba.at(1), rgba.at(0), rgba.at(3));

    for (int deta_height{ 0 }; deta_height < input_bgra.size().height; ++deta_height) {
        for (int deta_width{ 0 }; deta_width < input_bgra.size().width; ++deta_width) {
            auto& pixel = input_bgra.at<cv::Vec4b>(deta_height, deta_width);

            if (pixel[0] == detect_color[0] && pixel[1] == detect_color[1] && pixel[2] == detect_color[2] && pixel[3] == detect_color[3]) {
                pixel[3] = 0;
            }
        }
    }
    return input_bgra;
}
*/

#ifdef _WIN32
[[maybe_unused]] DLL_EXPORT void core::hidden_console(bool enable) {
    if (enable)FreeConsole();
}
#endif