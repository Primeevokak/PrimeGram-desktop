/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/whisper/whisper_manager.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "prime/prime_prefs.h"
#include "prime/whisper/whisper_audio.h"
#include "prime/whisper/whisper_engine.h"

#include <QtCore/QThread>

namespace Prime::Whisper {
namespace {

constexpr auto kEnabledKey = std::string_view("prime_whisper_enabled");
constexpr auto kModelKey = std::string_view("prime_whisper_model");
constexpr auto kLanguageKey = std::string_view("prime_whisper_language");
constexpr auto kThreadsKey = std::string_view("prime_whisper_threads");
constexpr auto kDefaultModel = "small";
constexpr auto kIdleUnloadMs = 2 * 60 * 1000;
constexpr auto kLoadTimeoutMs = 90 * 1000;

[[nodiscard]] int DefaultThreads() {
	return std::clamp(QThread::idealThreadCount() / 2, 2, 8);
}

} // namespace

Manager::Manager()
: _engine(std::make_shared<Engine>())
, _idle([=] {
	crl::async([engine = _engine] {
		engine->unload();
	});
}) {
}

Manager::~Manager() {
	for (const auto &[id, job] : _jobs) {
		job->cancel->store(true);
	}
}

bool Manager::enabled() const {
	return Prefs::GetBool(kEnabledKey, true);
}

void Manager::setEnabled(bool value) {
	Prefs::SetBool(kEnabledKey, value);
	_changes.fire({});
}

int Manager::model() const {
	const auto id = Prefs::GetString(kModelKey);
	const auto found = id.isEmpty() ? -1 : FindModel(id);
	return (found >= 0) ? found : FindModel(QString::fromLatin1(kDefaultModel));
}

void Manager::setModel(int index) {
	Prefs::SetString(kModelKey, QString::fromLatin1(Model(index).id));
	_changes.fire({});
}

QString Manager::language() const {
	return Prefs::GetString(kLanguageKey);
}

void Manager::setLanguage(const QString &code) {
	Prefs::SetString(kLanguageKey, code);
	_changes.fire({});
}

int Manager::threads() const {
	return std::clamp(Prefs::GetInt(kThreadsKey, DefaultThreads()), 1, 32);
}

void Manager::setThreads(int value) {
	Prefs::SetInt(kThreadsKey, value);
	_changes.fire({});
}

bool Manager::available() const {
	return enabled() && IsInstalled(model());
}

void Manager::cancel(FullMsgId id) {
	const auto i = _jobs.find(id);
	if (i != _jobs.end()) {
		i->second->cancel->store(true);
		finish(id, std::nullopt);
	}
}

void Manager::transcribe(
		not_null<HistoryItem*> item,
		Partial partial,
		Done done) {
	const auto id = item->fullId();
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	if (!document || _jobs.contains(id)) {
		if (done) {
			done(std::nullopt);
		}
		return;
	}
	auto job = std::make_unique<Job>();
	job->cancel = std::make_shared<std::atomic<bool>>(false);
	job->partial = std::move(partial);
	job->done = std::move(done);
	const auto raw = job.get();
	_jobs.emplace(id, std::move(job));

	const auto view = document->createMediaView();
	const auto ready = [=] {
		auto bytes = view->bytes();
		auto path = bytes.isEmpty() ? document->filepath(true) : QString();
		if (bytes.isEmpty() && path.isEmpty()) {
			finish(id, std::nullopt);
			return;
		}
		run(id, path, bytes);
	};
	if (view->loaded(true)) {
		ready();
		return;
	}
	document->save(id, QString(), LoadFromCloudOrLocal, false);
	const auto session = &document->session();
	const auto started = crl::now();
	session->downloaderTaskFinished(
	) | rpl::on_next([=] {
		if (!_jobs.contains(id)) {
			return;
		}
		if (view->loaded(true)) {
			raw->waiting.destroy();
			crl::on_main(this, ready);
		} else if (!document->loading()
			|| crl::now() - started > kLoadTimeoutMs) {
			crl::on_main(this, [=] {
				finish(id, std::nullopt);
			});
		}
	}, raw->waiting);
}

void Manager::run(FullMsgId id, const QString &path, const QByteArray &bytes) {
	const auto i = _jobs.find(id);
	if (i == _jobs.end()) {
		return;
	}
	const auto cancel = i->second->cancel;
	const auto index = model();
	const auto language = this->language();
	const auto threads = this->threads();
	const auto engine = _engine;
	_idle.cancel();
	crl::async([=, weak = base::make_weak(this)] {
		auto pcm = DecodeToPcm16k(path, bytes, cancel.get());
		if (pcm.empty() || cancel->load()) {
			crl::on_main(weak, [=] {
				finish(id, std::nullopt);
			});
			return;
		}
		auto request = Engine::Request();
		request.pcm = std::move(pcm);
		request.language = language;
		request.threads = threads;
		request.cancel = cancel.get();
		request.partial = [=](const QString &text) {
			crl::on_main(weak, [=] {
				const auto i = _jobs.find(id);
				if (i != _jobs.end() && i->second->partial) {
					i->second->partial(text);
				}
			});
		};
		const auto text = engine->run(index, request);
		crl::on_main(weak, [=] {
			finish(id, text);
		});
	});
}

void Manager::finish(FullMsgId id, const std::optional<QString> &text) {
	const auto i = _jobs.find(id);
	if (i == _jobs.end()) {
		return;
	}
	const auto job = std::move(i->second);
	_jobs.erase(i);
	_idle.callOnce(kIdleUnloadMs);
	if (job->done) {
		job->done(text);
	}
}

} // namespace Prime::Whisper
