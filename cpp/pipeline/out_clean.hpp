#pragma once
#include <filesystem>
#include <string_view>

namespace pipeline::out {
inline bool user_temporary(std::string_view name) {
    constexpr std::string_view prefix=".part-",suffix=".parquet.writing";
    if(!name.starts_with(prefix)||!name.ends_with(suffix)||name.size()<=prefix.size()+suffix.size()) return false;
    const auto id=name.substr(prefix.size(),name.size()-prefix.size()-suffix.size());
    return id.find_first_not_of("0123456789")==id.npos;
}
inline void clear_output_files(const std::filesystem::path& dir) {
    for(const auto& entry:std::filesystem::directory_iterator(dir)) {
        if(entry.symlink_status().type()!=std::filesystem::file_type::regular) continue;
        const auto name=entry.path().filename().native();
        const bool visible=name.front()!='.' && name.front()!='_' && entry.path().extension()==".parquet";
        if(visible || user_temporary(name)) std::filesystem::remove(entry.path());
    }
}
}
