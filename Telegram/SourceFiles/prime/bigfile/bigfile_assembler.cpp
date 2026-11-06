/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/bigfile/bigfile_assembler.h"

#include "api/api_messages_search.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "prime/prime_session.h"
#include "prime/prime_text.h"
#include "storage/storage_account.h"
#include "window/window_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace Prime::BigFile {
namespace {

constexpr auto kPollMs = 300;
constexpr auto kMaxSearchRounds = 12;

[[nodiscard]] QString KeyFor(uint64 peerId, const QString &alias) {
	return QString::number(peerId) + ':' + alias;
}

[[nodiscard]] QString DownloadDirectory(not_null<Main::Session*> session) {
	const auto chosen = Core::App().settings().downloadPath();
	auto result = QString();
	if (chosen.isEmpty()) {
		result = File::DefaultDownloadPath(session);
	} else if (chosen == u"tmp"_q) {
		result = session->local().tempDirectory();
	} else {
		result = chosen;
	}
	if (!result.endsWith('/') && !result.endsWith('\\')) {
		result += '/';
	}
	QDir().mkpath(result);
	return result;
}

} // namespace

Assembler::Assembler()
: _poll([=] { check(); }) {
}

Assembler::~Assembler() = default;

std::optional<Header> Assembler::HeaderOf(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	if (!document) {
		return std::nullopt;
	}
	return ParseHeader(document->filename().toUtf8().toStdString());
}

std::vector<Assembler::JobInfo> Assembler::jobs() const {
	auto result = std::vector<JobInfo>();
	for (const auto &job : _jobs) {
		result.push_back({
			.alias = job->alias,
			.fileName = job->fileName,
			.total = job->total,
			.done = job->next - 1,
			.partProgress = job->partProgress,
			.searching = job->searching,
		});
	}
	return result;
}

Assembler::Job *Assembler::find(const QString &key) {
	for (const auto &job : _jobs) {
		if (job->key == key) {
			return job.get();
		}
	}
	return nullptr;
}

bool Assembler::complete(const Job &job) const {
	for (auto i = 1; i <= job.total; ++i) {
		if (!job.chunks.contains(i)) {
			return false;
		}
	}
	return true;
}

void Assembler::toast(const QString &text) const {
	if (const auto window = Core::App().activeWindow()) {
		window->showToast(text);
	}
}

void Assembler::assemble(not_null<HistoryItem*> chunk) {
	const auto header = HeaderOf(chunk);
	if (!header) {
		return;
	}
	const auto peerId = chunk->history()->peer->id.value;
	const auto alias = QString::fromStdString(header->alias);
	const auto key = KeyFor(peerId, alias);
	if (find(key)) {
		toast(Text(
			"This file is already being assembled",
			"Этот файл уже собирается"));
		return;
	}
	auto job = std::make_unique<Job>();
	job->key = key;
	job->alias = alias;
	job->fileName = QString::fromStdString(header->fileName);
	job->totalSize = header->totalSize;
	job->total = header->total;
	job->userId = chunk->history()->session().userId().bare;
	job->peerId = peerId;
	const auto raw = job.get();
	_jobs.push_back(std::move(job));
	_changes.fire({});

	collectLoaded(*raw);
	if (complete(*raw)) {
		proceed(key);
	} else {
		toast(Text(
			"Looking for the other parts…",
			"Ищем остальные части…"));
		startSearch(*raw);
	}
}

void Assembler::collectItem(Job &job, not_null<HistoryItem*> item) {
	const auto header = HeaderOf(item);
	if (!header
		|| QString::fromStdString(header->alias) != job.alias
		|| header->total != job.total
		|| item->history()->peer->id.value != job.peerId) {
		return;
	}
	job.chunks[header->index] = item->fullId();
}

void Assembler::collectLoaded(Job &job) {
	const auto session = SessionByUserId(job.userId);
	if (!session) {
		return;
	}
	const auto peer = session->data().peerLoaded(PeerId(job.peerId));
	if (!peer) {
		return;
	}
	const auto history = session->data().history(peer);
	for (const auto &block : history->blocks) {
		for (const auto &view : block->messages) {
			collectItem(job, view->data());
		}
	}
}

void Assembler::startSearch(Job &job) {
	const auto session = SessionByUserId(job.userId);
	if (!session) {
		fail(job.key, Text("The account is not available",
			"Аккаунт недоступен"));
		return;
	}
	const auto peer = session->data().peer(PeerId(job.peerId));
	const auto history = session->data().history(peer);
	const auto key = job.key;
	job.searching = true;
	job.search = std::make_unique<Api::MessagesSearch>(history);
	job.search->messagesFounds(
	) | rpl::on_next([=](const Api::FoundMessages &found) {
		const auto job = find(key);
		if (!job) {
			return;
		}
		const auto session = SessionByUserId(job->userId);
		if (!session) {
			return;
		}
		for (const auto &id : found.messages) {
			if (const auto item = session->data().message(id)) {
				collectItem(*job, item);
			}
		}
		if (complete(*job)) {
			job->searching = false;
			crl::on_main(this, [=] {
				proceed(key);
			});
		} else if (!found.nextToken.isEmpty()
			&& ++job->searchRounds < kMaxSearchRounds) {
			job->search->searchMore();
		} else {
			crl::on_main(this, [=] {
				fail(key, Text(
					"Not all parts of the file were found in the chat",
					"В чате найдены не все части файла"));
			});
		}
	}, job.searchLifetime);
	auto request = Api::MessagesSearch::Request();
	request.query = job.alias;
	job.search->searchMessages(std::move(request));
}

void Assembler::proceed(const QString &key) {
	const auto job = find(key);
	if (!job) {
		return;
	}
	const auto session = SessionByUserId(job->userId);
	if (!session) {
		fail(key, Text("The account is not available",
			"Аккаунт недоступен"));
		return;
	}
	job->searching = false;
	const auto directory = DownloadDirectory(session);
	job->destination = filedialogNextFilename(
		job->fileName,
		QString(),
		directory);
	if (job->destination.isEmpty()) {
		job->destination = directory + job->fileName;
	} else if (QFileInfo(job->destination).isRelative()) {
		job->destination = directory + job->destination;
	}
	job->partPath = directory + job->alias + u".prime-part"_q;
	QFile::remove(job->destination);
	auto created = QFile(job->destination);
	if (!created.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		fail(key, Text("Cannot create the file", "Не удалось создать файл"));
		return;
	}
	created.close();
	toast(Text("Downloading the file in parts…",
		"Скачиваем файл по частям…"));
	downloadNext(*job);
}

void Assembler::downloadNext(Job &job) {
	const auto session = SessionByUserId(job.userId);
	const auto i = job.chunks.find(job.next);
	const auto item = (session && i != job.chunks.end())
		? session->data().message(i->second)
		: nullptr;
	const auto media = item ? item->media() : nullptr;
	const auto document = media ? media->document() : nullptr;
	if (!document) {
		fail(job.key, Text(
			"A part of the file is no longer available",
			"Одна из частей файла больше недоступна"));
		return;
	}
	QFile::remove(job.partPath);
	job.downloading = true;
	job.partProgress = 0.;
	document->save(item->fullId(), job.partPath, LoadFromCloudOrLocal, false);
	_poll.callEach(kPollMs);
	_changes.fire({});
}

void Assembler::check() {
	auto any = false;
	for (const auto &job : _jobs) {
		if (!job->downloading) {
			continue;
		}
		any = true;
		const auto session = SessionByUserId(job->userId);
		const auto i = job->chunks.find(job->next);
		const auto item = (session && i != job->chunks.end())
			? session->data().message(i->second)
			: nullptr;
		const auto media = item ? item->media() : nullptr;
		const auto document = media ? media->document() : nullptr;
		if (!document) {
			const auto key = job->key;
			fail(key, Text(
				"A part of the file is no longer available",
				"Одна из частей файла больше недоступна"));
			return;
		}
		const auto size = QFileInfo(job->partPath).size();
		if (document->loading()) {
			job->partProgress = document->progress();
		} else if (size > 0 && size >= document->size) {
			job->downloading = false;
			partDownloaded(*job);
			return;
		} else {
			const auto key = job->key;
			fail(key, Text(
				"A part of the file failed to download",
				"Не удалось скачать часть файла"));
			return;
		}
	}
	if (!any) {
		_poll.cancel();
	}
	_changes.fire({});
}

void Assembler::partDownloaded(Job &job) {
	auto in = QFile(job.partPath);
	auto out = QFile(job.destination);
	auto ok = in.open(QIODevice::ReadOnly)
		&& out.open(QIODevice::WriteOnly | QIODevice::Append);
	if (ok) {
		auto buffer = QByteArray(4 * 1024 * 1024, Qt::Uninitialized);
		while (!in.atEnd()) {
			const auto read = in.read(buffer.data(), buffer.size());
			if (read < 0 || out.write(buffer.constData(), read) != read) {
				ok = false;
				break;
			}
		}
	}
	in.close();
	out.close();
	QFile::remove(job.partPath);
	if (!ok) {
		fail(job.key, Text(
			"Not enough space or the disk is not writable",
			"Недостаточно места или диск недоступен для записи"));
		return;
	}
	job.partProgress = 0.;
	if (job.next >= job.total) {
		const auto name = job.fileName;
		const auto destination = job.destination;
		remove(job.key);
		toast(Text("File assembled: %1", "Файл собран: %1").arg(name));
		File::ShowInFolder(destination);
		return;
	}
	++job.next;
	downloadNext(job);
}

void Assembler::fail(const QString &key, const QString &text) {
	const auto job = find(key);
	if (job) {
		if (job->downloading) {
			QFile::remove(job->partPath);
		}
		if (!job->destination.isEmpty()) {
			QFile::remove(job->destination);
		}
	}
	remove(key);
	toast(text);
}

void Assembler::cancel(const QString &alias) {
	for (const auto &job : _jobs) {
		if (job->alias == alias) {
			const auto key = job->key;
			if (job->downloading) {
				QFile::remove(job->partPath);
			}
			if (!job->destination.isEmpty()) {
				QFile::remove(job->destination);
			}
			remove(key);
			return;
		}
	}
}

void Assembler::remove(const QString &key) {
	const auto i = ranges::find(_jobs, key, [](const auto &job) {
		return job->key;
	});
	if (i != end(_jobs)) {
		_jobs.erase(i);
	}
	_changes.fire({});
}

} // namespace Prime::BigFile
