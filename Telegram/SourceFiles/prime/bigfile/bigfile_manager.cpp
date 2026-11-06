/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/bigfile/bigfile_manager.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "core/application.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "prime/prime_prefs.h"
#include "prime/prime_text.h"
#include "settings.h"
#include "storage/localimageloader.h"
#include "ui/chat/attach/attach_prepare.h"
#include "window/window_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Prime::BigFile {
namespace {

constexpr auto kEnabledKey = std::string_view("prime_bigfile_enabled");
constexpr auto kExperimentalKey = std::string_view(
	"prime_bigfile_experimental");
constexpr auto kPollMs = 500;
constexpr auto kAppearTimeoutMs = 90 * 1000;
constexpr auto kRetryDelayMs = 5000;
constexpr auto kMaxFailures = 5;

} // namespace

Manager::Manager()
: _poll([=] { checkAwaited(); })
, _next([=] { pump(); }) {
}

Manager::~Manager() = default;

void Manager::start() {
	if (_started) {
		return;
	}
	_started = true;
	load();
	Core::App().domain().activeSessionValue(
	) | rpl::on_next([=](Main::Session *session) {
		if (session) {
			pump();
		}
	}, _lifetime);
}

bool Manager::sendingEnabled() const {
	return Prefs::GetBool(kEnabledKey, true);
}

void Manager::setSendingEnabled(bool value) {
	Prefs::SetBool(kEnabledKey, value);
	_changes.fire({});
}

bool Manager::experimental() const {
	return Prefs::GetBool(kExperimentalKey, false);
}

void Manager::setExperimental(bool value) {
	Prefs::SetBool(kExperimentalKey, value);
	_changes.fire({});
}

int64 Manager::maxSendableSize() const {
	return experimental() ? kMaxExperimental : kMaxNormal;
}

bool Manager::trySend(not_null<PeerData*> peer, const QString &path) {
	const auto info = QFileInfo(path);
	const auto size = int64(info.size());
	if (!sendingEnabled()
		|| !info.isFile()
		|| size <= 0
		|| size > maxSendableSize()) {
		return false;
	}
	auto &session = peer->session();
	const auto limit = session.premium() ? kChunkPremium : kChunkFree;
	const auto name = info.fileName().toUtf8().toStdString();
	const auto plan = MakePlan(name, size, limit);

	auto transfer = Transfer();
	transfer.alias = QString::fromStdString(plan.alias);
	transfer.sourcePath = info.absoluteFilePath();
	transfer.fileName = info.fileName();
	transfer.totalSize = size;
	transfer.userId = session.userId().bare;
	transfer.peerId = peer->id.value;
	transfer.parts = plan.parts;
	_transfers.push_back(std::move(transfer));
	save();
	_changes.fire({});
	toast(Text(
		"The file is too big for Telegram, sending it in parts",
		"Файл больше лимита Telegram, отправляется по частям"));
	pump();
	return true;
}

std::vector<Manager::TransferInfo> Manager::transfers() const {
	auto result = std::vector<TransferInfo>();
	result.reserve(_transfers.size());
	for (const auto &transfer : _transfers) {
		result.push_back({
			.alias = transfer.alias,
			.fileName = transfer.fileName,
			.totalSize = transfer.totalSize,
			.total = int(transfer.parts.size()),
			.sent = transfer.sent,
			.active = (transfer.alias == _currentAlias),
		});
	}
	return result;
}

void Manager::cancel(const QString &alias) {
	const auto i = ranges::find(_transfers, alias, &Transfer::alias);
	if (i == end(_transfers)) {
		return;
	}
	if (alias == _currentAlias) {
		if (const auto session = sessionFor(i->userId)) {
			if (const auto item = session->data().message(_currentMessage)) {
				if (item->isSending()) {
					session->api().cancelLocalItem(item);
				}
			}
		}
		_poll.cancel();
		_watch.destroy();
		clearChunkFile();
		_currentAlias = QString();
		_currentMessage = FullMsgId();
		_busy = false;
	}
	_transfers.erase(i);
	save();
	_changes.fire({});
	pump();
}

QString Manager::statePath() const {
	return cWorkingDir() + u"tdata/prime_bigfile.json"_q;
}

QString Manager::chunkDirectory() const {
	const auto path = cWorkingDir() + u"tdata/prime_bigfile/"_q;
	QDir().mkpath(path);
	return path;
}

void Manager::load() {
	auto file = QFile(statePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	for (const auto &value : document.object().value("transfers").toArray()) {
		const auto object = value.toObject();
		auto transfer = Transfer();
		transfer.alias = object.value("alias").toString();
		transfer.sourcePath = object.value("source").toString();
		transfer.fileName = object.value("name").toString();
		transfer.totalSize = object.value("size").toString().toLongLong();
		transfer.userId = object.value("user").toString().toULongLong();
		transfer.peerId = object.value("peer").toString().toULongLong();
		transfer.sent = object.value("sent").toInt();
		for (const auto &part : object.value("parts").toArray()) {
			const auto pair = part.toArray();
			if (pair.size() == 2) {
				transfer.parts.push_back({
					pair.at(0).toString().toLongLong(),
					pair.at(1).toString().toLongLong(),
				});
			}
		}
		if (!transfer.alias.isEmpty()
			&& !transfer.parts.empty()
			&& transfer.sent <= int(transfer.parts.size())) {
			_transfers.push_back(std::move(transfer));
		}
	}
}

void Manager::save() const {
	auto array = QJsonArray();
	for (const auto &transfer : _transfers) {
		auto parts = QJsonArray();
		for (const auto &part : transfer.parts) {
			parts.append(QJsonArray{
				QString::number(part.offset),
				QString::number(part.length),
			});
		}
		array.append(QJsonObject{
			{ "alias", transfer.alias },
			{ "source", transfer.sourcePath },
			{ "name", transfer.fileName },
			{ "size", QString::number(transfer.totalSize) },
			{ "user", QString::number(transfer.userId) },
			{ "peer", QString::number(transfer.peerId) },
			{ "sent", transfer.sent },
			{ "parts", parts },
		});
	}
	auto file = QFile(statePath());
	if (_transfers.empty()) {
		file.remove();
		return;
	}
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(QJsonObject{
			{ "transfers", array },
		}).toJson(QJsonDocument::Compact));
	}
}

Main::Session *Manager::sessionFor(uint64 userId) const {
	for (const auto &entry : Core::App().domain().accounts()) {
		if (const auto session = entry.account->maybeSession()) {
			if (session->userId().bare == userId) {
				return session;
			}
		}
	}
	return nullptr;
}

Manager::Transfer *Manager::find(const QString &alias) {
	const auto i = ranges::find(_transfers, alias, &Transfer::alias);
	return (i != end(_transfers)) ? &*i : nullptr;
}

void Manager::toast(const QString &text) const {
	if (const auto window = Core::App().activeWindow()) {
		window->showToast(text);
	}
}

void Manager::pump() {
	if (_busy) {
		return;
	}
	for (auto &transfer : _transfers) {
		if (!sessionFor(transfer.userId)) {
			continue;
		}
		if (transfer.sent >= int(transfer.parts.size())) {
			finish(transfer.alias);
			return;
		}
		_busy = true;
		_currentAlias = transfer.alias;
		sendChunk(transfer);
		return;
	}
}

void Manager::sendChunk(Transfer &transfer) {
	const auto alias = transfer.alias;
	const auto index = transfer.sent;
	const auto part = transfer.parts[index];
	const auto name = QString::fromStdString(MakeHeader(
		alias.toStdString(),
		index + 1,
		int(transfer.parts.size()),
		transfer.totalSize,
		transfer.fileName.toUtf8().toStdString()));
	const auto source = transfer.sourcePath;
	const auto target = chunkDirectory() + name;
	_currentChunkName = name;
	_currentChunkPath = target;

	crl::async([=, weak = base::make_weak(this)] {
		auto ok = false;
		if (QFileInfo(target).size() == part.length) {
			ok = true;
		} else {
			auto in = QFile(source);
			auto out = QFile(target);
			if (in.open(QIODevice::ReadOnly)
				&& in.seek(part.offset)
				&& out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
				auto left = part.length;
				auto buffer = QByteArray(1024 * 1024, Qt::Uninitialized);
				ok = true;
				while (left > 0) {
					const auto read = in.read(
						buffer.data(),
						std::min<int64>(buffer.size(), left));
					if (read <= 0
						|| out.write(buffer.constData(), read) != read) {
						ok = false;
						break;
					}
					left -= read;
				}
			}
		}
		crl::on_main(weak, [=] {
			const auto transfer = find(alias);
			if (!transfer || alias != _currentAlias) {
				return;
			}
			const auto session = sessionFor(transfer->userId);
			if (!ok || !session) {
				toast(Text(
					"Could not prepare a part of the file",
					"Не удалось подготовить часть файла"));
				cancel(alias);
				return;
			}
			const auto peer = session->data().peer(
				PeerId(transfer->peerId));
			const auto history = session->data().history(peer);

			auto list = Ui::PreparedList();
			auto file = Ui::PreparedFile(target);
			file.size = part.length;
			file.type = Ui::PreparedFile::Type::File;
			file.information = std::make_unique<
				Ui::PreparedFileInformation>();
			file.information->filemime = u"application/octet-stream"_q;
			list.files.push_back(std::move(file));

			awaitChunk(alias, name);
			session->api().sendFiles(
				std::move(list),
				SendMediaType::File,
				nullptr,
				Api::SendAction(history));
		});
	});
}

void Manager::awaitChunk(const QString &alias, const QString &chunkName) {
	const auto transfer = find(alias);
	if (!transfer) {
		return;
	}
	const auto session = sessionFor(transfer->userId);
	if (!session) {
		return;
	}
	const auto peerId = PeerId(transfer->peerId);
	_currentMessage = FullMsgId();
	_awaitingSince = crl::now();
	_watch.destroy();

	session->data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		if (_currentMessage || !item->out()
			|| item->history()->peer->id != peerId) {
			return;
		}
		const auto media = item->media();
		const auto document = media ? media->document() : nullptr;
		if (document && document->filename() == chunkName) {
			_currentMessage = item->fullId();
		}
	}, _watch);
	session->data().itemIdChanged(
	) | rpl::on_next([=](const Data::Session::IdChange &change) {
		if (_currentMessage
			&& change.oldId == _currentMessage.msg
			&& change.newId.peer == _currentMessage.peer) {
			_currentMessage = change.newId;
		}
	}, _watch);
	_poll.callEach(kPollMs);
}

void Manager::checkAwaited() {
	const auto transfer = find(_currentAlias);
	const auto session = transfer ? sessionFor(transfer->userId) : nullptr;
	if (!session) {
		return;
	}
	if (!_currentMessage) {
		if (crl::now() - _awaitingSince > kAppearTimeoutMs) {
			chunkFinished(false);
		}
		return;
	}
	const auto item = session->data().message(_currentMessage);
	if (!item || item->hasFailed()) {
		if (item) {
			item->destroy();
		}
		chunkFinished(false);
	} else if (!item->isSending() && IsServerMsgId(item->id)) {
		chunkFinished(true);
	}
}

void Manager::chunkFinished(bool success) {
	_poll.cancel();
	_watch.destroy();
	_currentMessage = FullMsgId();
	clearChunkFile();
	const auto transfer = find(_currentAlias);
	if (!transfer) {
		_busy = false;
		pump();
		return;
	}
	const auto alias = _currentAlias;
	if (!success) {
		_busy = false;
		_currentAlias = QString();
		if (++transfer->failures >= kMaxFailures) {
			toast(Text(
				"Sending in parts stopped: too many errors",
				"Отправка по частям остановлена: слишком много ошибок"));
			cancel(alias);
			return;
		}
		scheduleNext(kRetryDelayMs);
		return;
	}
	transfer->failures = 0;
	transfer->sent++;
	save();
	_changes.fire({});
	_busy = false;
	_currentAlias = QString();
	if (transfer->sent >= int(transfer->parts.size())) {
		finish(alias);
		return;
	}
	scheduleNext(NextDelayMs());
}

void Manager::finish(const QString &alias) {
	const auto i = ranges::find(_transfers, alias, &Transfer::alias);
	if (i == end(_transfers)) {
		return;
	}
	const auto name = i->fileName;
	_transfers.erase(i);
	save();
	_changes.fire({});
	toast(Text(
		"File sent in parts: %1",
		"Файл отправлен по частям: %1").arg(name));
	pump();
}

void Manager::scheduleNext(int delayMs) {
	_next.callOnce(delayMs);
}

void Manager::clearChunkFile() {
	if (!_currentChunkPath.isEmpty()) {
		QFile::remove(_currentChunkPath);
		_currentChunkPath = QString();
	}
}

} // namespace Prime::BigFile
