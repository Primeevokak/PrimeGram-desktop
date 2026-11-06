/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "prime/tgws/tgws_crypto.h"

#include <optional>
#include <string>

namespace Prime::TgWs::Ws {

inline constexpr auto kOpText = uint8_t(0x1);
inline constexpr auto kOpBinary = uint8_t(0x2);
inline constexpr auto kOpClose = uint8_t(0x8);
inline constexpr auto kOpPing = uint8_t(0x9);
inline constexpr auto kOpPong = uint8_t(0xA);

struct Frame {
	uint8_t opcode = 0;
	Bytes payload;
};

[[nodiscard]] Bytes BuildFrame(
	uint8_t opcode,
	const uint8_t *data,
	size_t size);
[[nodiscard]] Bytes BuildFrame(uint8_t opcode, const Bytes &data);

struct UpgradeRequest {
	std::string text;
	std::string key;
};
[[nodiscard]] UpgradeRequest BuildUpgradeRequest(
	const std::string &host,
	const std::string &path);

struct UpgradeResponse {
	int status = 0;
	std::string statusLine;
	std::string location;
	size_t consumed = 0; // Bytes used, including the blank line.

	[[nodiscard]] bool isRedirect() const {
		return status == 301
			|| status == 302
			|| status == 303
			|| status == 307
			|| status == 308;
	}
};
[[nodiscard]] std::optional<UpgradeResponse> ParseUpgradeResponse(
	const uint8_t *data,
	size_t size);

class FrameParser final {
public:
	void append(const uint8_t *data, size_t size);

	[[nodiscard]] std::optional<Frame> next();

private:
	Bytes _buffer;
	size_t _offset = 0;

};

} // namespace Prime::TgWs::Ws
