/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tgws/tgws_splitter.h"

namespace Prime::TgWs {

MsgSplitter::MsgSplitter(const Handshake &relayInit, uint32_t protoInt)
: _decrypt(
	relayInit.data() + kSkipLen,
	relayInit.data() + kSkipLen + kPrekeyLen)
, _proto(protoInt) {
	_decrypt.skip(kHandshakeLen);
}

std::vector<Bytes> MsgSplitter::split(const Bytes &data) {
	return split(data.data(), data.size());
}

std::vector<Bytes> MsgSplitter::split(const uint8_t *data, size_t size) {
	if (!size) {
		return {};
	} else if (_disabled) {
		return { Bytes(data, data + size) };
	}

	_cipherBuffer.insert(_cipherBuffer.end(), data, data + size);
	const auto plain = _decrypt.process(data, size);
	_plainBuffer.insert(_plainBuffer.end(), plain.begin(), plain.end());

	auto parts = std::vector<Bytes>();
	auto offset = size_t(0);
	const auto total = _cipherBuffer.size();
	// WHY: erasing from the front per packet is quadratic for a chunk
	// full of small packets, so walk with an offset and erase once.
	while (offset < total) {
		const auto length = nextPacketLen(offset, total - offset);
		if (!length) {
			break;
		} else if (*length == 0) {
			parts.emplace_back(
				_cipherBuffer.begin() + offset,
				_cipherBuffer.end());
			offset = total;
			_disabled = true;
			break;
		}
		parts.emplace_back(
			_cipherBuffer.begin() + offset,
			_cipherBuffer.begin() + offset + *length);
		offset += *length;
	}
	if (offset) {
		_cipherBuffer.erase(_cipherBuffer.begin(), _cipherBuffer.begin() + offset);
		_plainBuffer.erase(_plainBuffer.begin(), _plainBuffer.begin() + offset);
	}
	return parts;
}

std::vector<Bytes> MsgSplitter::flush() {
	if (_cipherBuffer.empty()) {
		return {};
	}
	auto tail = std::move(_cipherBuffer);
	_cipherBuffer.clear();
	_plainBuffer.clear();
	return { std::move(tail) };
}

std::optional<size_t> MsgSplitter::nextPacketLen(
		size_t offset,
		size_t available) const {
	if (!available) {
		return std::nullopt;
	}
	switch (_proto) {
	case kProtoAbridgedInt:
		return nextAbridgedLen(offset, available);
	case kProtoIntermediateInt:
	case kProtoPaddedInt:
		return nextIntermediateLen(offset, available);
	}
	return size_t(0);
}

std::optional<size_t> MsgSplitter::nextAbridgedLen(
		size_t offset,
		size_t available) const {
	const auto first = _plainBuffer[offset];
	auto payload = size_t(0);
	auto header = size_t(0);
	if (first == 0x7F || first == 0xFF) {
		if (available < 4) {
			return std::nullopt;
		}
		payload = (size_t(_plainBuffer[offset + 1])
			| (size_t(_plainBuffer[offset + 2]) << 8)
			| (size_t(_plainBuffer[offset + 3]) << 16)) * 4;
		header = 4;
	} else {
		payload = size_t(first & 0x7F) * 4;
		header = 1;
	}
	if (!payload) {
		return size_t(0);
	}
	const auto packet = header + payload;
	if (available < packet) {
		return std::nullopt;
	}
	return packet;
}

std::optional<size_t> MsgSplitter::nextIntermediateLen(
		size_t offset,
		size_t available) const {
	if (available < 4) {
		return std::nullopt;
	}
	const auto payload = (uint32_t(_plainBuffer[offset])
		| (uint32_t(_plainBuffer[offset + 1]) << 8)
		| (uint32_t(_plainBuffer[offset + 2]) << 16)
		| (uint32_t(_plainBuffer[offset + 3]) << 24)) & 0x7FFFFFFFU;
	if (!payload) {
		return size_t(0);
	}
	const auto packet = size_t(4) + payload;
	if (available < packet) {
		return std::nullopt;
	}
	return packet;
}

} // namespace Prime::TgWs
