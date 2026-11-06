/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/autodelete/autodelete_manager.h"

#include "api/api_common.h"
#include "api/api_editing.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "prime/prime_prefs.h"
#include "prime/prime_session.h"
#include "prime/prime_store.h"

#include <QtCore/QJsonArray>

namespace Prime::AutoDelete {
namespace {

constexpr auto kEnabledKey = std::string_view("prime_autodelete_enabled");
constexpr auto kPeriodKey = std::string_view("prime_autodelete_hours");
constexpr auto kDefaultHours = 168;
constexpr auto kSweepMs = 30 * 1000;
constexpr auto kEditGraceSeconds = 60;
constexpr auto kStoreName = "autodelete";

void DeleteById(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId msgId) {
	const auto id = FullMsgId(peerId, msgId);
	if (session->data().message(id)) {
		session->data().histories().deleteMessages({ id }, true);
		return;
	}
	const auto peer = session->data().peer(peerId);
	auto ids = QVector<MTPint>(1, MTP_int(msgId.bare));
	if (const auto channel = peer->asChannel()) {
		session->api().request(MTPchannels_DeleteMessages(
			channel->inputChannel(),
			MTP_vector<MTPint>(ids)
		)).send();
	} else {
		using Flag = MTPmessages_DeleteMessages::Flag;
		session->api().request(MTPmessages_DeleteMessages(
			MTP_flags(Flag::f_revoke),
			MTP_vector<MTPint>(ids)
		)).send();
	}
}

} // namespace

Manager::Manager()
: _timer([=] { sweep(); }) {
}

Manager::~Manager() = default;

void Manager::start() {
	if (_started) {
		return;
	}
	_started = true;
	load();
	const auto hookAll = [=] {
		for (const auto &entry : Core::App().domain().accounts()) {
			entry.account->sessionValue(
			) | rpl::on_next([=](Main::Session *session) {
				if (session) {
					hook(session);
				}
			}, _lifetime);
		}
	};
	hookAll();
	Core::App().domain().accountsChanges(
	) | rpl::on_next(hookAll, _lifetime);
	_timer.callEach(kSweepMs);
}

bool Manager::enabled() const {
	return Prefs::GetBool(kEnabledKey, false);
}

void Manager::setEnabled(bool value) {
	Prefs::SetBool(kEnabledKey, value);
	_changes.fire({});
}

int Manager::periodHours() const {
	return Prefs::GetInt(kPeriodKey, kDefaultHours);
}

void Manager::setPeriodHours(int hours) {
	Prefs::SetInt(kPeriodKey, hours);
	_changes.fire({});
}

QString Manager::chatKey(not_null<PeerData*> peer) const {
	return QString::number(peer->session().userId().bare)
		+ ':'
		+ QString::number(peer->id.value);
}

int Manager::chatOverride(not_null<PeerData*> peer) const {
	return _overrides.value(chatKey(peer), -1);
}

void Manager::setChatOverride(not_null<PeerData*> peer, int value) {
	if (value < 0) {
		_overrides.remove(chatKey(peer));
	} else {
		_overrides[chatKey(peer)] = value;
	}
	save();
	_changes.fire({});
}

bool Manager::enabledFor(not_null<PeerData*> peer) const {
	const auto value = chatOverride(peer);
	return (value >= 0) ? (value == 1) : enabled();
}

void Manager::hook(not_null<Main::Session*> session) {
	if (!_hooked.emplace(session).second) {
		return;
	}
	session->lifetime().add([=] {
		_hooked.remove(session);
	});
	session->data().itemIdChanged(
	) | rpl::on_next([=](const Data::Session::IdChange &change) {
		if (const auto item = session->data().message(change.newId)) {
			onSent(item);
		}
	}, session->lifetime());
}

void Manager::onSent(not_null<HistoryItem*> item) {
	if (!item->out()
		|| item->isService()
		|| item->isScheduled()
		|| item->isSending()
		|| !IsServerMsgId(item->id)
		|| !enabledFor(item->history()->peer)) {
		return;
	}
	const auto userId = item->history()->session().userId().bare;
	const auto peerId = item->history()->peer->id.value;
	for (const auto &entry : _entries) {
		if (entry.userId == userId
			&& entry.peerId == peerId
			&& entry.msgId == item->id.bare) {
			return;
		}
	}
	auto entry = Entry();
	entry.userId = userId;
	entry.peerId = peerId;
	entry.msgId = item->id.bare;
	entry.deleteAt = int64(item->date()) + int64(periodHours()) * 3600;
	entry.editAt = entry.deleteAt - kEditGraceSeconds;
	if (entry.editAt <= item->date()) {
		entry.edited = true;
	}
	_entries.push_back(entry);
	save();
}

void Manager::sweep() {
	if (_entries.empty()) {
		return;
	}
	const auto now = int64(base::unixtime::now());
	auto changed = false;
	for (auto i = _entries.begin(); i != _entries.end();) {
		const auto session = SessionByUserId(i->userId);
		if (!session) {
			++i;
			continue;
		}
		const auto peerId = PeerId(i->peerId);
		const auto msgId = MsgId(i->msgId);
		if (!i->edited && now >= i->editAt) {
			i->edited = true;
			changed = true;
			const auto item = session->data().message(
				FullMsgId(peerId, msgId));
			if (item
				&& !item->media()
				&& !item->originalText().text.isEmpty()
				&& item->originalText().text != u"."_q) {
				Api::EditTextMessage(
					item,
					TextWithEntities{ u"."_q },
					Data::WebPageDraft(),
					Api::SendOptions(),
					[](mtpRequestId) {},
					[](const QString &, mtpRequestId) {},
					false);
			}
		}
		if (now >= i->deleteAt) {
			DeleteById(session, peerId, msgId);
			i = _entries.erase(i);
			changed = true;
			continue;
		}
		++i;
	}
	if (changed) {
		save();
	}
}

void Manager::load() {
	const auto root = LoadStore(kStoreName);
	const auto overrides = root.value("overrides").toObject();
	for (auto i = overrides.begin(); i != overrides.end(); ++i) {
		_overrides.insert(i.key(), i.value().toInt(-1));
	}
	for (const auto &value : root.value("pending").toArray()) {
		const auto object = value.toObject();
		auto entry = Entry();
		entry.userId = object.value("u").toString().toULongLong();
		entry.peerId = object.value("p").toString().toULongLong();
		entry.msgId = object.value("m").toString().toLongLong();
		entry.editAt = object.value("e").toString().toLongLong();
		entry.deleteAt = object.value("d").toString().toLongLong();
		entry.edited = object.value("x").toBool();
		if (entry.userId && entry.peerId && entry.msgId) {
			_entries.push_back(entry);
		}
	}
}

void Manager::save() const {
	auto overrides = QJsonObject();
	for (auto i = _overrides.begin(); i != _overrides.end(); ++i) {
		overrides.insert(i.key(), i.value());
	}
	auto pending = QJsonArray();
	for (const auto &entry : _entries) {
		pending.append(QJsonObject{
			{ "u", QString::number(entry.userId) },
			{ "p", QString::number(entry.peerId) },
			{ "m", QString::number(entry.msgId) },
			{ "e", QString::number(entry.editAt) },
			{ "d", QString::number(entry.deleteAt) },
			{ "x", entry.edited },
		});
	}
	auto root = QJsonObject();
	if (!overrides.isEmpty()) {
		root.insert("overrides", overrides);
	}
	if (!pending.isEmpty()) {
		root.insert("pending", pending);
	}
	SaveStore(kStoreName, root);
}

} // namespace Prime::AutoDelete
