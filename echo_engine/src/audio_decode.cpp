#include "echo/audio_decode.hpp"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace echo {
PcmClip decodeAudio(const Bytes& bytes, std::size_t maxBytes) {
    std::vector<std::int16_t> input;
    int channels = 0, rate = 0;
    if (bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
        std::memcmp(bytes.data() + 8, "WAVE", 4) == 0) {
        int format = 0, bits = 0;
        std::size_t data = 0, size = 0;
        auto u16 = [&](std::size_t i) { return unsigned(bytes[i]) | (unsigned(bytes[i + 1]) << 8); };
        auto u32 = [&](std::size_t i) { return u16(i) | (u16(i + 2) << 16); };
        for (std::size_t p = 12; p + 8 <= bytes.size();) {
            std::uint32_t n = u32(p + 4);
            if (p + 8 + std::uint64_t(n) > bytes.size())
                throw std::runtime_error("Truncated WAV");
            if (!std::memcmp(bytes.data() + p, "fmt ", 4) && n >= 16) {
                format = u16(p + 8);
                channels = u16(p + 10);
                rate = u32(p + 12);
                bits = u16(p + 22);
            }
            if (!std::memcmp(bytes.data() + p, "data", 4)) {
                data = p + 8;
                size = n;
            }
            p += 8 + n + (n & 1);
        }
        if (format != 1 || (bits != 8 && bits != 16) || channels < 1 || channels > 2 || rate < 8000 ||
            rate > 192000 || !data || !size || size % (channels * bits / 8) != 0 || size > maxBytes)
            throw std::runtime_error(
                "WAV requires PCM mono/stereo, 8/16-bit, 8..192 kHz within audio budget");
        if (bits == 16) {
            input.resize(size / 2);
            std::memcpy(input.data(), bytes.data() + data, input.size() * 2);
        } else {
            input.resize(size);
            for (std::size_t i = 0; i < size; ++i)
                input[i] = std::int16_t((int(bytes[data + i]) - 128) * 256);
        }
    } else if (bytes.size() >= 4 && !std::memcmp(bytes.data(), "OggS", 4)) {
        int error = 0;
        stb_vorbis* vorbis = stb_vorbis_open_memory(bytes.data(), int(bytes.size()), &error, nullptr);
        if (!vorbis)
            throw std::runtime_error("Invalid OGG Vorbis stream");
        auto info = stb_vorbis_get_info(vorbis);
        channels = info.channels;
        rate = info.sample_rate;
        auto frames = stb_vorbis_stream_length_in_samples(vorbis);
        if (channels < 1 || channels > 2 || rate < 8000 || rate > 192000 || !frames ||
            std::uint64_t(frames) * channels * 2 > maxBytes) {
            stb_vorbis_close(vorbis);
            throw std::runtime_error("OGG PCM decode exceeds bounded audio budget");
        }
        input.resize(std::size_t(frames) * channels);
        int decoded =
            stb_vorbis_get_samples_short_interleaved(vorbis, channels, input.data(), int(input.size()));
        stb_vorbis_close(vorbis);
        input.resize(std::size_t(decoded) * channels);
    } else
        throw std::runtime_error("Supported audio formats: PCM WAV and OGG Vorbis");
    std::size_t frames = input.size() / channels;
    std::size_t count = std::size_t(std::ceil(double(frames) * 44100 / rate));
    if (!frames || count > maxBytes / 4)
        throw std::runtime_error("Resampled PCM exceeds audio budget");
    PcmClip output;
    output.samples.resize(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        double at = double(i) * rate / 44100;
        std::size_t a = std::min(frames - 1, std::size_t(at)), b = std::min(frames - 1, a + 1);
        float t = float(at - a);
        for (int channel = 0; channel < 2; ++channel) {
            int src = channels == 1 ? 0 : channel;
            float x = input[a * channels + src], y = input[b * channels + src];
            output.samples[i * 2 + channel] = std::int16_t(x + (y - x) * t);
        }
    }
    return output;
}
} // namespace echo
