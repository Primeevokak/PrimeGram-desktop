/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/whisper/whisper_engine.h"

#include "prime/whisper/whisper_models.h"

#include <whisper.h>

#include <string>

namespace Prime::Whisper {
namespace {

struct Sink {
	std::string text;
	const Engine::Request *request = nullptr;
};

void OnSegment(
		whisper_context *context,
		whisper_state *state,
		int count,
		void *userData) {
	const auto sink = static_cast<Sink*>(userData);
	if (!sink || !sink->request || !sink->request->partial) {
		return;
	}
	const auto total = whisper_full_n_segments(context);
	for (auto i = total - count; i < total; ++i) {
		if (i < 0) {
			continue;
		}
		if (const auto segment = whisper_full_get_segment_text(context, i)) {
			sink->text += segment;
		}
	}
	sink->request->partial(
		QString::fromUtf8(sink->text.c_str()).trimmed());
}

bool AbortRequested(void *userData) {
	const auto request = static_cast<const Engine::Request*>(userData);
	return request && request->cancel && request->cancel->load();
}

} // namespace

Engine::Engine() = default;

Engine::~Engine() {
	unload();
}

std::optional<QString> Engine::run(int model, const Request &request) {
	if (request.pcm.empty()) {
		return std::nullopt;
	}
	auto lock = std::unique_lock(_mutex);
	if (_loaded != model) {
		if (_context) {
			whisper_free(_context);
			_context = nullptr;
			_loaded = -1;
		}
		auto params = whisper_context_default_params();
		params.use_gpu = false;
		const auto path = ModelPath(model).toUtf8();
		_context = whisper_init_from_file_with_params(path.constData(), params);
		if (!_context) {
			return std::nullopt;
		}
		_loaded = model;
	}

	auto params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
	params.print_realtime = false;
	params.print_progress = false;
	params.print_timestamps = false;
	params.print_special = false;
	params.translate = false;
	params.no_context = true;
	params.single_segment = false;
	params.n_threads = request.threads > 0 ? request.threads : 4;
	// Voice messages are short and conversational; suppressing non-speech
	// tokens keeps "(music)"-style artefacts out of the transcript.
	params.suppress_nst = true;
	const auto language = request.language.toUtf8();
	params.language = language.isEmpty() ? "auto" : language.constData();
	params.detect_language = false;

	auto sink = Sink{ .request = &request };
	params.new_segment_callback = OnSegment;
	params.new_segment_callback_user_data = &sink;
	params.abort_callback = AbortRequested;
	params.abort_callback_user_data = const_cast<Request*>(&request);

	const auto result = whisper_full(
		_context,
		params,
		request.pcm.data(),
		int(request.pcm.size()));
	if (result != 0) {
		return std::nullopt;
	}
	auto text = std::string();
	const auto segments = whisper_full_n_segments(_context);
	for (auto i = 0; i != segments; ++i) {
		if (const auto segment = whisper_full_get_segment_text(_context, i)) {
			text += segment;
		}
	}
	return QString::fromUtf8(text.c_str()).trimmed();
}

void Engine::unload() {
	auto lock = std::unique_lock(_mutex);
	if (_context) {
		whisper_free(_context);
		_context = nullptr;
		_loaded = -1;
	}
}

} // namespace Prime::Whisper
