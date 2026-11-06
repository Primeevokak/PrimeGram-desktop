/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/whisper/whisper_audio.h"

#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_utility.h"

namespace Prime::Whisper {
namespace {

using namespace FFmpeg;

constexpr auto kRate = 16000;
// Half an hour is far beyond a voice message, and keeps memory bounded.
constexpr auto kMaxSamples = int64(kRate) * 60 * 30;

class Decoder final {
public:
	[[nodiscard]] bool open(const QString &path, const QByteArray &bytes);
	[[nodiscard]] bool read(
		std::vector<float> &out,
		const std::atomic<bool> *cancel);

private:
	[[nodiscard]] bool convert(AVFrame *frame, std::vector<float> &out);
	[[nodiscard]] bool flush(std::vector<float> &out);

	ReadBytesWrap _bytesWrap;
	ReadFileWrap _fileWrap;
	FormatPointer _input;
	CodecPointer _decoder;
	SwresamplePointer _swr;
	FramePointer _decoded;
	Packet _packet;
	std::vector<float> _buffer;
	int _streamIndex = -1;

};

bool Decoder::open(const QString &path, const QByteArray &bytes) {
	if (!bytes.isEmpty()) {
		_bytesWrap = {
			.size = bytes.size(),
			.data = reinterpret_cast<const uchar*>(bytes.constData()),
		};
		_input = MakeFormatPointer(
			&_bytesWrap,
			&ReadBytesWrap::Read,
			nullptr,
			&ReadBytesWrap::Seek);
	} else {
		_fileWrap.file.setFileName(path);
		if (!_fileWrap.file.open(QIODevice::ReadOnly)) {
			return false;
		}
		_input = MakeFormatPointer(
			&_fileWrap,
			&ReadFileWrap::Read,
			nullptr,
			&ReadFileWrap::Seek);
	}
	if (!_input) {
		return false;
	}
	if (AvErrorWrap(avformat_find_stream_info(_input.get(), nullptr))) {
		return false;
	}
	_streamIndex = av_find_best_stream(
		_input.get(),
		AVMEDIA_TYPE_AUDIO,
		-1,
		-1,
		nullptr,
		0);
	if (_streamIndex < 0) {
		return false;
	}
	_decoder = MakeCodecPointer({ .stream = _input->streams[_streamIndex] });
	if (!_decoder) {
		return false;
	}
	_decoded = MakeFramePointer();
	return _decoded != nullptr;
}

bool Decoder::convert(AVFrame *frame, std::vector<float> &out) {
	auto mono = AVChannelLayout AV_CHANNEL_LAYOUT_MONO;
	_swr = MakeSwresamplePointer(
		&frame->ch_layout,
		AVSampleFormat(frame->format),
		frame->sample_rate,
		&mono,
		AV_SAMPLE_FMT_FLT,
		kRate,
		&_swr);
	if (!_swr) {
		return false;
	}
	const auto upper = int(swr_get_out_samples(
		_swr.get(),
		frame->nb_samples));
	if (upper <= 0) {
		return true;
	}
	_buffer.resize(upper);
	auto data = reinterpret_cast<uint8_t*>(_buffer.data());
	const auto samples = swr_convert(
		_swr.get(),
		&data,
		upper,
		(const uint8_t**)frame->extended_data,
		frame->nb_samples);
	if (samples < 0) {
		return false;
	} else if (samples > 0) {
		out.insert(out.end(), _buffer.begin(), _buffer.begin() + samples);
	}
	return int64(out.size()) < kMaxSamples;
}

bool Decoder::flush(std::vector<float> &out) {
	if (!_swr) {
		return true;
	}
	_buffer.resize(kRate);
	auto data = reinterpret_cast<uint8_t*>(_buffer.data());
	const auto samples = swr_convert(
		_swr.get(),
		&data,
		kRate,
		nullptr,
		0);
	if (samples > 0) {
		out.insert(out.end(), _buffer.begin(), _buffer.begin() + samples);
	}
	return true;
}

bool Decoder::read(
		std::vector<float> &out,
		const std::atomic<bool> *cancel) {
	auto eof = false;
	while (true) {
		if (cancel && cancel->load()) {
			return false;
		}
		const auto got = AvErrorWrap(avcodec_receive_frame(
			_decoder.get(),
			_decoded.get()));
		if (!got) {
			if (!convert(_decoded.get(), out)) {
				return false;
			}
			continue;
		} else if (got.code() == AVERROR_EOF) {
			return flush(out);
		} else if (got.code() != AVERROR(EAGAIN)) {
			return false;
		}
		if (eof) {
			const auto sent = AvErrorWrap(avcodec_send_packet(
				_decoder.get(),
				nullptr));
			if (sent && sent.code() != AVERROR_EOF) {
				return false;
			}
			continue;
		}
		av_packet_unref(&_packet.fields());
		const auto readResult = AvErrorWrap(av_read_frame(
			_input.get(),
			&_packet.fields()));
		if (readResult.code() == AVERROR_EOF) {
			eof = true;
			continue;
		} else if (readResult) {
			return false;
		} else if (_packet.fields().stream_index != _streamIndex) {
			continue;
		}
		const auto sent = AvErrorWrap(avcodec_send_packet(
			_decoder.get(),
			&_packet.fields()));
		if (sent && sent.code() != AVERROR_INVALIDDATA) {
			return false;
		}
	}
}

} // namespace

std::vector<float> DecodeToPcm16k(
		const QString &path,
		const QByteArray &bytes,
		const std::atomic<bool> *cancel) {
	auto result = std::vector<float>();
	auto decoder = Decoder();
	if (!decoder.open(path, bytes)) {
		return {};
	}
	if (!decoder.read(result, cancel)) {
		return {};
	}
	return result;
}

} // namespace Prime::Whisper
