#pragma once
// core::sha256 -- the archive's fingerprint (s013a). A plain FIPS 180-4
// SHA-256, no library: the core links no crypto, and a file's hash is the
// proof s013b reads back from the media. READ-ONLY: it opens files O_RDONLY.
#include <cstdint>
#include <string>

namespace tidy::core::sha256 {

class Hasher {
public:
    Hasher();
    void add(const void* data, std::size_t n);
    std::string hex();             // finishes; 64 lowercase hex chars
private:
    void block(const std::uint8_t* p);
    std::uint32_t m_h[8];
    std::uint8_t  m_buf[64];
    std::size_t   m_fill = 0;
    std::uint64_t m_bits = 0;
};

std::string of_text(const std::string& s);
// The file's hash (never following a symlink, O_NOATIME: its read time is
// left as it was); "" if it cannot be read.
// `size` (optional) = the bytes read.
std::string of_file(const std::string& path, std::uint64_t* size = nullptr);

}  // namespace tidy::core::sha256
