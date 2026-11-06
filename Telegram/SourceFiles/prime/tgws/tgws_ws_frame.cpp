/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tgws/tgws_ws_frame.h"

#include <openssl/evp.h>

#include <algorithm>
#include <stdexcept>

namespace Prime::TgWs::Ws {
namespace {

[[nodiscard]] std::string Base64(const uint8_t *data, size_t size) {
	auto result = std::string(4 * ((size + 2) / 3) + 1, '\0');
	const auto written = EVP_EncodeBlock(
		reinterpret_cast<unsigned char*>(result.data()),
		data,
		int(size));
	result.resize(size_t(std::max(written, 0)));
	return result;
}

[[nodiscard]] std::string Lower(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](char c) {
		return (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
	});
	return value;
}

[[nodiscard]] std::string Trim(const std::string &value) {
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return {};
	}
	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

} // namespace

Bytes BuildFrame(uint8_t opcode, const uint8_t *data, size_t size) {
	auto mask = std::array<uint8_t, 4>();
	RandomBytes(mask.data(), mask.size());

	auto result = Bytes();
	result.reserve(size + 14);
	result.push_back(uint8_t(0x80 | opcode));
	if (size < 126) {
		result.push_back(uint8_t(0x80 | size));
	} else if (size < 65536) {
		result.push_back(0x80 | 126);
		result.push_back(uint8_t(size >> 8));
		result.push_back(uint8_t(size & 0xFF));
	} else {
		result.push_back(0x80 | 127);
		for (auto shift = 56; shift >= 0; shift -= 8) {
			result.push_back(uint8_t((uint64_t(size) >> shift) & 0xFF));
		}
	}
	result.insert(result.end(), mask.begin(), mask.end());
	const auto start = result.size();
	result.resize(start + size);
	for (auto i = size_t(0); i != size; ++i) {
		result[start + i] = data[i] ^ mask[i & 3];
	}
	return result;
}

Bytes BuildFrame(uint8_t opcode, const Bytes &data) {
	return BuildFrame(opcode, data.data(), data.size());
}

UpgradeRequest BuildUpgradeRequest(
		const std::string &host,
		const std::string &path) {
	auto nonce = std::array<uint8_t, 16>();
	RandomBytes(nonce.data(), nonce.size());
	auto result = UpgradeRequest();
	result.key = Base64(nonce.data(), nonce.size());
	result.text = "GET " + path + " HTTP/1.1\r\n"
		"Host: " + host + "\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: " + result.key + "\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"Sec-WebSocket-Protocol: binary\r\n"
		"\r\n";
	return result;
}

std::optional<UpgradeResponse> ParseUpgradeResponse(
		const uint8_t *data,
		size_t size) {
	const auto text = std::string(
		reinterpret_cast<const char*>(data),
		size);
	const auto end = text.find("\r\n\r\n");
	if (end == std::string::npos) {
		return std::nullopt;
	}
	auto result = UpgradeResponse();
	result.consumed = end + 4;

	const auto lineEnd = text.find("\r\n");
	result.statusLine = text.substr(0, lineEnd);
	const auto firstSpace = result.statusLine.find(' ');
	if (firstSpace != std::string::npos) {
		const auto rest = result.statusLine.substr(firstSpace + 1);
		try {
			result.status = std::stoi(rest);
		} catch (...) {
			result.status = 0;
		}
	}

	auto position = lineEnd + 2;
	while (position < end + 2) {
		const auto next = text.find("\r\n", position);
		if (next == std::string::npos || next == position) {
			break;
		}
		const auto line = text.substr(position, next - position);
		const auto colon = line.find(':');
		if (colon != std::string::npos
			&& Lower(line.substr(0, colon)) == "location") {
			result.location = Trim(line.substr(colon + 1));
		}
		position = next + 2;
	}
	return result;
}

void FrameParser::append(const uint8_t *data, size_t size) {
	if (_offset > 0 && _offset == _buffer.size()) {
		_buffer.clear();
		_offset = 0;
	} else if (_offset > (1U << 20)) {
		_buffer.erase(_buffer.begin(), _buffer.begin() + _offset);
		_offset = 0;
	}
	_buffer.insert(_buffer.end(), data, data + size);
}

std::optional<Frame> FrameParser::next() {
	const auto available = _buffer.size() - _offset;
	if (available < 2) {
		return std::nullopt;
	}
	const auto *p = _buffer.data() + _offset;
	const auto opcode = uint8_t(p[0] & 0x0F);
	const auto masked = (p[1] & 0x80) != 0;
	auto length = uint64_t(p[1] & 0x7F);
	auto header = size_t(2);
	if (length == 126) {
		if (available < 4) {
			return std::nullopt;
		}
		length = (uint64_t(p[2]) << 8) | uint64_t(p[3]);
		header = 4;
	} else if (length == 127) {
		if (available < 10) {
			return std::nullopt;
		}
		length = 0;
		for (auto i = 0; i != 8; ++i) {
			length = (length << 8) | uint64_t(p[2 + i]);
		}
		header = 10;
	}
	if (length > kMaxFrameLen) {
		throw std::runtime_error("TgWs: WebSocket frame is too large.");
	}
	const auto maskSize = masked ? size_t(4) : size_t(0);
	if (available < header + maskSize + length) {
		return std::nullopt;
	}

	auto result = Frame();
	result.opcode = opcode;
	result.payload.assign(
		p + header + maskSize,
		p + header + maskSize + length);
	if (masked) {
		const auto *mask = p + header;
		for (auto i = size_t(0); i != result.payload.size(); ++i) {
			result.payload[i] ^= mask[i & 3];
		}
	}
	_offset += header + maskSize + size_t(length);
	return result;
}

} // namespace Prime::TgWs::Ws
