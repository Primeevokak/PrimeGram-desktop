/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/bigfile/bigfile_format.h"

#include <algorithm>
#include <cstdio>
#include <random>

namespace Prime::BigFile {
namespace {

constexpr auto kMaxNameBytes = 250;

[[nodiscard]] std::mt19937_64 &Random() {
	static auto result = std::mt19937_64(std::random_device()());
	return result;
}

[[nodiscard]] bool IsContinuation(unsigned char byte) {
	return (byte & 0xC0) == 0x80;
}

// Removes the last code point of a UTF-8 string.
void PopCodePoint(std::string &text) {
	while (!text.empty() && IsContinuation((unsigned char)text.back())) {
		text.pop_back();
	}
	if (!text.empty()) {
		text.pop_back();
	}
}

[[nodiscard]] bool ParseNumber(
		const std::string &text,
		int64_t max,
		int64_t &result) {
	if (text.empty() || text.size() > 18) {
		return false;
	}
	result = 0;
	for (const auto ch : text) {
		if (ch < '0' || ch > '9') {
			return false;
		}
		result = result * 10 + (ch - '0');
	}
	return result <= max;
}

} // namespace

Plan MakePlan(
		const std::string &fileName,
		int64_t totalSize,
		int64_t chunkLimit) {
	auto result = Plan{ NewAlias(), fileName, totalSize };
	auto offset = int64_t(0);
	while (offset < totalSize) {
		const auto remaining = totalSize - offset;
		auto length = int64_t(0);
		if (remaining <= chunkLimit) {
			length = remaining;
		} else {
			const auto jitter = std::uniform_int_distribution<int64_t>(
				kJitterMin,
				kJitterMax)(Random());
			length = chunkLimit - jitter;
			if (length <= 0 || length >= remaining) {
				length = std::min(chunkLimit, remaining);
			}
		}
		result.parts.push_back({ offset, length });
		offset += length;
	}
	if (result.parts.empty()) {
		result.parts.push_back({ 0, 0 });
	}
	return result;
}

std::string NewAlias() {
	static const auto alphabet = std::string(
		"abcdefghijklmnopqrstuvwxyz0123456789");
	auto result = std::string();
	auto dist = std::uniform_int_distribution<size_t>(
		0,
		alphabet.size() - 1);
	for (auto i = 0; i != 8; ++i) {
		result += alphabet[dist(Random())];
	}
	return result;
}

int NextDelayMs() {
	return std::uniform_int_distribution<int>(
		kDelayMinMs,
		kDelayMaxMs)(Random());
}

std::string MakeHeader(
		const std::string &alias,
		int index,
		int total,
		int64_t totalSize,
		const std::string &fileName) {
	const auto width = int(std::to_string(total).size());
	const auto megabytes = std::max<int64_t>(
		1,
		(totalSize + kMiB - 1) / kMiB);
	char buffer[128];
	std::snprintf(
		buffer,
		sizeof(buffer),
		"[%s %s %0*d-%d %lldM] ",
		kMarker,
		alias.c_str(),
		width,
		index,
		total,
		(long long)megabytes);
	const auto prefix = std::string(buffer);

	auto name = fileName.empty() ? std::string("file") : fileName;
	const auto budget = kMaxNameBytes - int(prefix.size());
	if (int(name.size()) > budget) {
		const auto dot = name.rfind('.');
		const auto extension = (dot != std::string::npos && dot > 0)
			? name.substr(dot)
			: std::string();
		const auto keep = std::max(4, budget - int(extension.size()) - 1);
		auto base = extension.empty() ? name : name.substr(0, dot);
		while (int(base.size()) > keep && base.size() > 1) {
			PopCodePoint(base);
		}
		name = base + extension;
	}
	return prefix + name;
}

std::string MakeChunkName(const Plan &plan, int index) {
	return MakeHeader(
		plan.alias,
		index + 1,
		plan.count(),
		plan.totalSize,
		plan.fileName);
}

std::optional<Header> ParseHeader(const std::string &name) {
	const auto prefix = std::string("[") + kMarker + " ";
	if (name.compare(0, prefix.size(), prefix) != 0) {
		return std::nullopt;
	}
	auto pos = prefix.size();

	const auto aliasEnd = name.find(' ', pos);
	if (aliasEnd == std::string::npos) {
		return std::nullopt;
	}
	auto result = Header();
	result.alias = name.substr(pos, aliasEnd - pos);
	if (result.alias.size() < 4 || result.alias.size() > 16) {
		return std::nullopt;
	}
	for (const auto ch : result.alias) {
		const auto ok = (ch >= 'a' && ch <= 'z')
			|| (ch >= 'A' && ch <= 'Z')
			|| (ch >= '0' && ch <= '9');
		if (!ok) {
			return std::nullopt;
		}
	}
	pos = aliasEnd + 1;

	const auto dash = name.find('-', pos);
	const auto space = name.find(' ', pos);
	if (dash == std::string::npos
		|| space == std::string::npos
		|| dash > space) {
		return std::nullopt;
	}
	const auto sizeEnd = name.find("M] ", space + 1);
	if (sizeEnd == std::string::npos) {
		return std::nullopt;
	}
	auto index = int64_t(0);
	auto total = int64_t(0);
	auto megabytes = int64_t(0);
	if (!ParseNumber(name.substr(pos, dash - pos), INT32_MAX, index)
		|| !ParseNumber(
			name.substr(dash + 1, space - dash - 1),
			INT32_MAX,
			total)
		|| !ParseNumber(
			name.substr(space + 1, sizeEnd - space - 1),
			int64_t(1) << 40,
			megabytes)) {
		return std::nullopt;
	}
	if (index < 1 || total < 1 || index > total) {
		return std::nullopt;
	}
	result.fileName = name.substr(sizeEnd + 3);
	if (result.fileName.empty()) {
		return std::nullopt;
	}
	result.index = int(index);
	result.total = int(total);
	result.totalSize = megabytes * kMiB;
	return result;
}

} // namespace Prime::BigFile
