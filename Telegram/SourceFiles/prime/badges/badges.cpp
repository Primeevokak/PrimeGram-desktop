/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/badges/badges.h"

#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "prime/prime_hub.h"
#include "prime/prime_prefs.h"
#include "prime/prime_store.h"

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Prime::Badges {
namespace {

constexpr auto kUrl = "https://primeevokak.github.io/PrimeGram/badges.json";
constexpr auto kUpdatedKey = std::string_view("prime_badges_updated");
constexpr auto kRefreshMs = qint64(6) * 60 * 60 * 1000;
constexpr auto kCheckMs = 30 * 60 * 1000;
constexpr auto kTimeoutMs = 20 * 1000;

[[nodiscard]] QString CachePath() {
	return StorePath(u"badges"_q);
}

} // namespace

Manager::Manager()
: _timer([=] { refresh(); }) {
}

Manager::~Manager() = default;

void Manager::start() {
	auto file = QFile(CachePath());
	if (file.open(QIODevice::ReadOnly)) {
		apply(file.readAll(), false);
	}
	refresh();
	_timer.callEach(kCheckMs);
}

const Badge *Manager::find(not_null<const PeerData*> peer) const {
	if (_users.empty() && _chats.empty()) {
		return nullptr;
	}
	if (peer->isUser()) {
		const auto i = _users.find(peerToUser(peer->id).bare);
		return (i != _users.end()) ? &i->second : nullptr;
	} else if (peer->isChannel()) {
		const auto i = _chats.find(peerToChannel(peer->id).bare);
		return (i != _chats.end()) ? &i->second : nullptr;
	}
	return nullptr;
}

void Manager::refresh() {
	const auto last = Prefs::GetString(kUpdatedKey).toLongLong();
	const auto now = QDateTime::currentMSecsSinceEpoch();
	if (last > 0 && now - last < kRefreshMs && !_users.empty()) {
		return;
	}
	if (!_network) {
		_network = std::make_unique<QNetworkAccessManager>();
	}
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(kUrl)));
	request.setRawHeader("User-Agent", "PrimeGram");
	request.setTransferTimeout(kTimeoutMs);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	const auto reply = _network->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			return;
		}
		apply(reply->readAll(), true);
	});
}

void Manager::apply(const QByteArray &json, bool save) {
	const auto document = QJsonDocument::fromJson(json);
	if (!document.isArray()) {
		// A broken download never replaces a working list.
		return;
	}
	auto users = std::unordered_map<uint64, Badge>();
	auto chats = std::unordered_map<uint64, Badge>();
	for (const auto &value : document.array()) {
		const auto row = value.toObject();
		const auto id = row.value("id").toVariant().toULongLong();
		const auto emoji = row.value("custom_emoji_id")
			.toVariant().toULongLong();
		if (!id || !emoji) {
			continue;
		}
		const auto chat = (row.value("type").toString() == u"chat"_q);
		const auto badge = Badge{
			.id = id,
			.chat = chat,
			.customEmojiId = emoji,
			.text = row.value("text").toString(),
		};
		(chat ? chats : users).emplace(id, badge);
	}
	_users = std::move(users);
	_chats = std::move(chats);
	if (save) {
		auto file = QFile(CachePath());
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			file.write(json);
		}
		Prefs::SetString(
			kUpdatedKey,
			QString::number(QDateTime::currentMSecsSinceEpoch()));
	}
	notifyPeers();
	_changes.fire({});
}

void Manager::notifyPeers() {
	for (const auto &entry : Core::App().domain().accounts()) {
		const auto session = entry.account->maybeSession();
		if (!session) {
			continue;
		}
		const auto notify = [&](const auto &list, bool chats) {
			for (const auto &[id, badge] : list) {
				const auto peerId = chats
					? peerFromChannel(ChannelId(id))
					: peerFromUser(UserId(id));
				if (const auto peer = session->data().peerLoaded(peerId)) {
					session->changes().peerUpdated(
						peer,
						Data::PeerUpdate::Flag::EmojiStatus);
				}
			}
		};
		notify(_users, false);
		notify(_chats, true);
	}
}

bool HasBadge(not_null<const PeerData*> peer) {
	return Core::App().prime().badges().find(peer) != nullptr;
}

uint64 BadgeEmojiId(not_null<const PeerData*> peer) {
	const auto badge = Core::App().prime().badges().find(peer);
	return badge ? badge->customEmojiId : 0;
}

QString BadgeText(not_null<const PeerData*> peer) {
	const auto badge = Core::App().prime().badges().find(peer);
	return badge ? badge->text : QString();
}

} // namespace Prime::Badges
