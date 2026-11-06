/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tempsub/tempsub_manager.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "prime/prime_session.h"
#include "prime/prime_store.h"

#include <QtCore/QJsonArray>

namespace Prime::TempSub {
namespace {

constexpr auto kSweepMs = 60 * 1000;
constexpr auto kStoreName = "tempsub";

} // namespace

Manager::Manager()
: _timer([=] { sweep(); }) {
}

void Manager::start() {
	if (_started) {
		return;
	}
	_started = true;
	load();
	_timer.callEach(kSweepMs);
	sweep();
}

void Manager::schedule(not_null<PeerData*> peer, int64 expiresAt) {
	cancel(peer);
	auto entry = Entry();
	entry.userId = peer->session().userId().bare;
	entry.peerId = peer->id.value;
	entry.expiresAt = expiresAt;
	entry.title = peer->name();
	_entries.push_back(std::move(entry));
	save();
	_changes.fire({});
}

void Manager::cancel(not_null<PeerData*> peer) {
	const auto userId = peer->session().userId().bare;
	const auto peerId = peer->id.value;
	const auto removed = std::erase_if(_entries, [&](const Entry &entry) {
		return entry.userId == userId && entry.peerId == peerId;
	});
	if (removed) {
		save();
		_changes.fire({});
	}
}

const Manager::Entry *Manager::entryFor(not_null<PeerData*> peer) const {
	const auto userId = peer->session().userId().bare;
	const auto peerId = peer->id.value;
	for (const auto &entry : _entries) {
		if (entry.userId == userId && entry.peerId == peerId) {
			return &entry;
		}
	}
	return nullptr;
}

void Manager::sweep() {
	if (_entries.empty()) {
		return;
	}
	const auto now = int64(base::unixtime::now());
	auto changed = false;
	for (auto i = _entries.begin(); i != _entries.end();) {
		if (i->expiresAt > now) {
			++i;
			continue;
		}
		const auto session = SessionByUserId(i->userId);
		if (!session) {
			++i;
			continue;
		}
		const auto peer = session->data().peerLoaded(PeerId(i->peerId));
		if (peer) {
			if (const auto channel = peer->asChannel()) {
				if (channel->amIn()) {
					session->api().leaveChannel(channel);
				}
			} else if (const auto chat = peer->asChat()) {
				if (chat->amIn()) {
					session->api().deleteConversation(chat, false);
				}
			}
		}
		i = _entries.erase(i);
		changed = true;
	}
	if (changed) {
		save();
		_changes.fire({});
	}
}

void Manager::load() {
	for (const auto &value : LoadStore(kStoreName).value("list").toArray()) {
		const auto object = value.toObject();
		auto entry = Entry();
		entry.userId = object.value("u").toString().toULongLong();
		entry.peerId = object.value("p").toString().toULongLong();
		entry.expiresAt = object.value("e").toString().toLongLong();
		entry.title = object.value("t").toString();
		if (entry.userId && entry.peerId) {
			_entries.push_back(std::move(entry));
		}
	}
}

void Manager::save() const {
	auto list = QJsonArray();
	for (const auto &entry : _entries) {
		list.append(QJsonObject{
			{ "u", QString::number(entry.userId) },
			{ "p", QString::number(entry.peerId) },
			{ "e", QString::number(entry.expiresAt) },
			{ "t", entry.title },
		});
	}
	auto root = QJsonObject();
	if (!list.isEmpty()) {
		root.insert("list", list);
	}
	SaveStore(kStoreName, root);
}

} // namespace Prime::TempSub
