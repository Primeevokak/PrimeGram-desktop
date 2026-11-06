/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "prime/tgws/tgws_crypto.h"

#include <optional>
#include <vector>

namespace Prime::TgWs {

// WHY: the server drops the connection when two packets share one
// WebSocket frame, so the stream is cut into whole packets here.
class MsgSplitter final {
public:
	MsgSplitter(const Handshake &relayInit, uint32_t protoInt);

	[[nodiscard]] std::vector<Bytes> split(const uint8_t *data, size_t size);
	[[nodiscard]] std::vector<Bytes> split(const Bytes &data);

	[[nodiscard]] std::vector<Bytes> flush();

private:
	[[nodiscard]] std::optional<size_t> nextPacketLen(
		size_t offset,
		size_t available) const;
	[[nodiscard]] std::optional<size_t> nextAbridgedLen(
		size_t offset,
		size_t available) const;
	[[nodiscard]] std::optional<size_t> nextIntermediateLen(
		size_t offset,
		size_t available) const;

	AesCtr _decrypt;
	uint32_t _proto = 0;
	Bytes _cipherBuffer;
	Bytes _plainBuffer;
	bool _disabled = false;

};

} // namespace Prime::TgWs
