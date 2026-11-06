/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

struct whisper_context;

namespace Prime::Whisper {

// Wraps one loaded whisper.cpp model. All calls block, so they belong on
// a worker thread; calls are serialized.
class Engine final {
public:
	Engine();
	~Engine();

	struct Request {
		std::vector<float> pcm;
		QString language; // Empty for auto-detect.
		int threads = 4;
		// Called on the worker thread with the whole transcript so far.
		Fn<void(const QString &)> partial;
		const std::atomic<bool> *cancel = nullptr;
	};

	// Returns nothing when the model could not be loaded or decoding failed.
	[[nodiscard]] std::optional<QString> run(
		int model,
		const Request &request);

	// Frees the model memory.
	void unload();

private:
	std::mutex _mutex;
	whisper_context *_context = nullptr;
	int _loaded = -1;

};

} // namespace Prime::Whisper
