#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace ASECM3::Client {
inline std::vector<uint8_t> ReadLooseAssetFile(const std::filesystem::path& path,
                                               size_t maximumBytes=128u*1024u*1024u) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)throw std::runtime_error("Cannot open loose asset dependency: "+path.string());
    const auto end=input.tellg();
    if(end<=0||uint64_t(end)>maximumBytes||uint64_t(end)>std::numeric_limits<size_t>::max())
        throw std::runtime_error("Loose asset dependency is empty or exceeds its snapshot limit: "+path.string());
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    input.seekg(0,std::ios::beg);
    if(!input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Incomplete loose asset read: "+path.string());
    return bytes;
}
}
