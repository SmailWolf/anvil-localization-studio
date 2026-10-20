#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <windows.h>
using Decoder = int64_t(WINAPI *)(const void *, int64_t, void *, int64_t, int, int, int, void *, int64_t,
                                  void *, void *, void *, int64_t, int);
static uint32_t read32(std::istream &f) {
    uint32_t n = 0;
    if (!f.read(reinterpret_cast<char *>(&n), 4))
        throw std::runtime_error("short job input");
    return n;
}
using Compressor = int64_t(WINAPI *)(int, const void *, int64_t, void *, int, const void *, const void *,
                                     const void *, void *, int64_t);
// Oodle 7 ABI verified in the owned DLL: the bound function takes rawLen only.
using Bound = int64_t(WINAPI *)(int64_t);
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc != 4 && argc != 5)
            throw std::runtime_error("args: DLL jobs output [compress]");
        const bool compress = argc == 5 && std::wstring(argv[4]) == L"compress";
        if (argc == 5 && !compress)
            throw std::runtime_error("Unknown codec mode");
        HMODULE lib = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!lib)
            throw std::runtime_error("Cannot load codec DLL: " + std::to_string(GetLastError()));
        auto decode = reinterpret_cast<Decoder>(GetProcAddress(lib, "OodleLZ_Decompress"));
        if (!decode)
            throw std::runtime_error("Missing decoder export");
        auto encoder = reinterpret_cast<Compressor>(GetProcAddress(lib, "OodleLZ_Compress"));
        auto bound = reinterpret_cast<Bound>(GetProcAddress(lib, "OodleLZ_GetCompressedBufferSizeNeeded"));
        if (compress && (!encoder || !bound))
            throw std::runtime_error("Missing compressor exports");
        std::ifstream in(std::filesystem::path(argv[2]), std::ios::binary);
        std::ofstream out(std::filesystem::path(argv[3]), std::ios::binary | std::ios::trunc);
        if (!in || !out)
            throw std::runtime_error("Cannot open research input/output");
        uint32_t count = read32(in);
        if (count > 100000)
            throw std::runtime_error("Too many blocks");
        out.write(reinterpret_cast<char *>(&count), 4);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t raw = read32(in), stored = read32(in);
            if (raw == 0 || raw > 32768 || stored == 0 || stored > 65536)
                throw std::runtime_error("Invalid block size");
            std::vector<char> src(stored), dst(raw + 64);
            if (!in.read(src.data(), stored))
                throw std::runtime_error("Truncated compressed block");
            if (compress) {
                if (raw != stored)
                    throw std::runtime_error("Compressor jobs must contain raw blocks");
                const auto capacity = bound(raw);
                if (capacity <= 0 || capacity > 1024 * 1024)
                    throw std::runtime_error("Invalid compression bound");
                std::vector<char> encoded(capacity);
                const auto size =
                    encoder(9, src.data(), raw, encoded.data(), 1, nullptr, nullptr, nullptr, nullptr, 0);
                if (size <= 0 || size > capacity || size > 65536)
                    throw std::runtime_error("Compression failed");
                // Verify the codec's output before exposing it to a caller.
                if (decode(encoded.data(), size, dst.data(), raw, 1, 0, 0, nullptr, 0, nullptr, nullptr,
                           nullptr, 0, 3) != raw ||
                    !std::equal(src.begin(), src.end(), dst.begin()))
                    throw std::runtime_error("Compression round trip failed");
                // Original GRB containers store incompressible blocks verbatim.
                // Equal sizes mark a raw block; compressed output can grow by a
                // codec header and must not exceed the 32 KiB container bound.
                const bool storeRaw = size >= raw;
                const auto n = storeRaw ? raw : uint32_t(size);
                out.write(reinterpret_cast<const char *>(&raw), 4);
                out.write(reinterpret_cast<const char *>(&n), 4);
                out.write(storeRaw ? src.data() : encoded.data(), n);
                continue;
            }
            if (raw == stored)
                std::copy(src.begin(), src.end(), dst.begin());
            else if (decode(src.data(), stored, dst.data(), raw, 1, 0, 0, nullptr, 0, nullptr, nullptr,
                            nullptr, 0, 3) != raw)
                throw std::runtime_error("Codec size mismatch");
            out.write(reinterpret_cast<char *>(&raw), 4);
            out.write(dst.data(), raw);
        }
        if (in.peek() != std::char_traits<char>::eof() || !out)
            throw std::runtime_error("Trailing input or write error");
        std::cout << (compress ? "Encoded " : "Decoded ") << count << " blocks\n";
        FreeLibrary(lib);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
