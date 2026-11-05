// Durable file helpers for packs (POSIX: Linux and macOS).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tradelab::desktop::packs {

// Writes `bytes` to `<path>.tmp`, fsyncs it, renames it over `path` and fsyncs the folder: readers see
// the old file or the whole new one, never a torn write.
bool write_file_atomic(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);

// Appends and fsyncs (resumable downloads); false on any error.
bool append_file_durable(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);

std::optional<std::vector<std::uint8_t>> read_file(const std::filesystem::path& path);

// Lowercase hex SHA-256 (the engine's implementation).
std::string sha256_hex(std::span<const std::uint8_t> bytes);

}  // namespace tradelab::desktop::packs
