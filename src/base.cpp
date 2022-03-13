#include "base.hpp"

namespace fn {

bool source_loc::operator==(const source_loc& other) {
    return this->line == other.line
        && this->col == other.col;
}

bool source_loc::operator!=(const source_loc& other) {
    return !(*this == other);
}

// Hashes for std::string and integers use FNV-1a
template<> u64 hash<string>(const string& s) {
    static const u64 prime = 0x100000001b3;
    u64 res = 0xcbf29ce484222325;
    for (u32 i=0; i<s.length(); ++i) {
        res ^= s[i];
        res *= prime;
    }
    return res;
}

template<> u64 hash<u64>(const u64& u) {
    static const u64 prime = 0x100000001b3;
    u64 res = 0xcbf29ce484222325;
    auto bytes = u;
    for (int i = 0; i < 8; ++i) {
        res ^= (bytes & 0xff);
        res *= prime;
        bytes = bytes >> 8;
    }
    return res;
}

template<> u64 hash<u32>(const u32& u) {
    static const u64 prime = 0x100000001b3;
    u64 res = 0xcbf29ce484222325;
    auto bytes = u;
    for (int i = 0; i < 4; ++i) {
        res ^= (bytes & 0xff);
        res *= prime;
        bytes = bytes >> 8;
    }
    return res;
}

template<> u64 hash<u16>(const u16& u) {
    static const u64 prime = 0x100000001b3;
    u64 res = 0xcbf29ce484222325;
    auto bytes = u;
    for (int i = 0; i < 2; ++i) {
        res ^= (bytes & 0xff);
        res *= prime;
        bytes = bytes >> 8;
    }
    return res;
}

template<> u64 hash<u8>(const u8& u) {
    static const u64 prime = 0x100000001b3;
    u64 res = 0xcbf29ce484222325;
    res ^= u;
    res *= prime;
    return res;
}

}
