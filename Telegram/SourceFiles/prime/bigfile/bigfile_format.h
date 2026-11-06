/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.

The chunk naming format is shared with PrimeGram for Android: a file that
is too big for Telegram travels as several files named
"[PG1 <alias> <index>-<total> <size>M] <name>".
*/
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Prime::BigFile {

inline constexpr auto kMarker = "PG1";

inline constexpr int64_t kMiB = 1024LL * 1024;
inline constexpr int64_t kGiB = 1024 * kMiB;

inline constexpr int64_t kChunkFree = 1990 * kMiB;
inline constexpr int64_t kChunkPremium = 3990 * kMiB;
inline constexpr int64_t kMaxNormal = 8 * kGiB;
inline constexpr int64_t kMaxExperimental = 50 * kGiB;

inline constexpr int64_t kJitterMin = 10 * kMiB;
inline constexpr int64_t kJitterMax = 70 * kMiB;

inline constexpr int kDelayMinMs = 5000;
inline constexpr int kDelayMaxMs = 15000;

struct Header {
	std::string alias;
	int index = 0; // 1-based.
	int total = 0;
	int64_t totalSize = 0; // Whole megabytes on the wire.
	std::string fileName;
};

struct Part {
	int64_t offset = 0;
	int64_t length = 0;
};

struct Plan {
	std::string alias;
	std::string fileName;
	int64_t totalSize = 0;
	std::vector<Part> parts;

	[[nodiscard]] int count() const {
		return int(parts.size());
	}
};

// Splits the file in advance; every part but the last is the limit minus
// a random 10-70 MB, so that no two parts have the same size.
[[nodiscard]] Plan MakePlan(
	const std::string &fileName,
	int64_t totalSize,
	int64_t chunkLimit);

[[nodiscard]] std::string NewAlias();
[[nodiscard]] int NextDelayMs();

// Name of the chunk, file name is trimmed in the middle so that the whole
// thing fits 250 bytes and the extension survives.
[[nodiscard]] std::string MakeHeader(
	const std::string &alias,
	int index,
	int total,
	int64_t totalSize,
	const std::string &fileName);
[[nodiscard]] std::string MakeChunkName(const Plan &plan, int index);

// Returns nothing when the name is not one of ours.
[[nodiscard]] std::optional<Header> ParseHeader(const std::string &name);

} // namespace Prime::BigFile
