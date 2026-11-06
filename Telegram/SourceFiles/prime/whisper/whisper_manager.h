/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "prime/whisper/whisper_models.h"

#include <rpl/event_stream.h>

#include <atomic>
#include <map>
#include <memory>
#include <optional>

class HistoryItem;

namespace Prime::Whisper {

class Engine;

// Voice recognition that never leaves the computer.
class Manager final : public base::has_weak_ptr {
public:
	Manager();
	~Manager();

	[[nodiscard]] bool enabled() const;
	void setEnabled(bool value);

	// Index in the model table, -1 when none was chosen.
	[[nodiscard]] int model() const;
	void setModel(int index);

	// Empty for auto-detect.
	[[nodiscard]] QString language() const;
	void setLanguage(const QString &code);

	[[nodiscard]] int threads() const;
	void setThreads(int value);

	// Enabled and a model is installed: local recognition replaces the
	// server one, for everybody, Premium or not.
	[[nodiscard]] bool available() const;

	[[nodiscard]] Downloader &downloader() {
		return _downloader;
	}

	using Partial = Fn<void(const QString &text)>;
	using Done = Fn<void(const std::optional<QString> &text)>;
	void transcribe(
		not_null<HistoryItem*> item,
		Partial partial,
		Done done);
	void cancel(FullMsgId id);

	[[nodiscard]] rpl::producer<> changes() const {
		return _changes.events();
	}

private:
	struct Job {
		std::shared_ptr<std::atomic<bool>> cancel;
		rpl::lifetime waiting;
		Partial partial;
		Done done;
	};

	void run(FullMsgId id, const QString &path, const QByteArray &bytes);
	void finish(FullMsgId id, const std::optional<QString> &text);

	Downloader _downloader;
	std::shared_ptr<Engine> _engine;
	std::map<FullMsgId, std::unique_ptr<Job>> _jobs;
	base::Timer _idle;
	rpl::event_stream<> _changes;
	rpl::lifetime _lifetime;

};

} // namespace Prime::Whisper
