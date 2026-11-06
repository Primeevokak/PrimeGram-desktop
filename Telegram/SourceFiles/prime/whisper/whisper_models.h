/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "base/weak_ptr.h"

#include <rpl/event_stream.h>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace Prime::Whisper {

struct ModelInfo {
	const char *id = nullptr;
	const char *file = nullptr;
	const char *title = nullptr;
	int64 bytes = 0;
	const char *noteEn = nullptr;
	const char *noteRu = nullptr;
	bool heavy = false;
};

[[nodiscard]] int ModelCount();
[[nodiscard]] const ModelInfo &Model(int index);
[[nodiscard]] int FindModel(const QString &id);

// Where the models live and what is already there.
[[nodiscard]] QString ModelsDirectory();
[[nodiscard]] QString ModelPath(int index);
[[nodiscard]] bool IsInstalled(int index);
[[nodiscard]] int64 InstalledSize(int index);
void Remove(int index);

// Downloads one model at a time, with resume.
class Downloader final : public base::has_weak_ptr {
public:
	Downloader();
	~Downloader();

	void start(int index);
	void cancel();

	[[nodiscard]] int current() const {
		return _index;
	}
	[[nodiscard]] bool active() const {
		return _index >= 0;
	}
	[[nodiscard]] float64 progress() const;
	[[nodiscard]] QString error() const {
		return _error;
	}

	[[nodiscard]] rpl::producer<> changes() const {
		return _changes.events();
	}

private:
	void finish(bool ok);
	void flush();

	std::unique_ptr<QNetworkAccessManager> _network;
	QNetworkReply *_reply = nullptr;
	std::unique_ptr<QFile> _file;
	int _index = -1;
	int64 _resumed = 0;
	int64 _total = 0;
	int64 _received = 0;
	QString _error;
	rpl::event_stream<> _changes;

};

} // namespace Prime::Whisper
