/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/e2e/e2e_manager.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_reply_markup.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "prime/e2e/e2e_crypto.h"
#include "prime/e2e/e2e_protocol.h"
#include "prime/prime_prefs.h"
#include "prime/prime_text.h"
#include "window/window_controller.h"

#include <string>

namespace Prime::E2E {
namespace {

constexpr auto kFlushMs = 2000;
constexpr auto kFlushSoonMs = 300;
// A request older than this is not offered as a prompt any more.
constexpr auto kFreshRequestSeconds = 15 * 60;
constexpr auto kMaxRequestAgeSeconds = 2 * 24 * 3600;

using Button = HistoryMessageMarkupButton;

[[nodiscard]] bool IsProtocol(const QString &text) {
	return IsHandshake(text)
		|| IsCiphertext(text)
		|| IsCancel(text);
}

[[nodiscard]] QString EstablishedLabel() {
	return Text(
		"🔒 Encrypted chat is on",
		"🔒 Защищённый чат включён");
}

[[nodiscard]] QString CancelLabel() {
	return Text("🔒 Request cancelled", "🔒 Запрос отменён");
}

[[nodiscard]] QString FailedLabel() {
	return Text(
		"🔒 Cannot decrypt this message",
		"🔒 Не удалось расшифровать сообщение");
}

[[nodiscard]] QString OtherDeviceLabel() {
	return Text(
		"🔒 Sent from another device",
		"🔒 Отправлено с другого устройства");
}

[[nodiscard]] QString DisabledLabel() {
	return Text(
		"🔒 Encryption was turned off",
		"🔒 Шифрование отключено");
}

[[nodiscard]] Button MakeButton(
		const QString &text,
		const QString &action,
		quint64 peer,
		bool primary = false) {
	return Button(
		Button::Type::Url,
		text,
		Button::Visual{
			.color = primary ? Button::Color::Primary : Button::Color::Normal,
		},
		QString::fromLatin1("tg://prime_e2e?a=%1&u=%2")
			.arg(action)
			.arg(peer)
			.toUtf8());
}

void SetCard(
		not_null<HistoryItem*> item,
		const QString &text,
		std::vector<Button> buttons) {
	item->replaceTextLocally({ text });
	auto markup = HistoryMessageMarkupData();
	if (!buttons.empty()) {
		markup.flags = ReplyMarkupFlag::Inline;
		markup.rows.push_back(std::move(buttons));
	}
	item->replaceReplyMarkupLocally(std::move(markup));
}

[[nodiscard]] std::string SeedKey(quint64 peer) {
	return "prime_e2e_seed_" + std::to_string(peer);
}

[[nodiscard]] std::string VerifiedKey(quint64 peer) {
	return "prime_e2e_verified_" + std::to_string(peer);
}

} // namespace

Manager::Manager()
: _flush([=] { _store.flush(); })
, _flushSoon([=] { _store.flush(); }) {
}

Manager::~Manager() {
	_store.flush();
}

void Manager::start() {
	if (_started) {
		return;
	}
	_started = true;
	_store.setDirtyCallback([=] {
		_flushSoon.callOnce(kFlushSoonMs);
	});
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
	_flush.callEach(kFlushMs);
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
		const auto i = _pending.find(
			FullMsgId(change.newId.peer, change.oldId));
		if (i == _pending.end()) {
			return;
		}
		if (const auto peer = session->data().peerLoaded(change.newId.peer)) {
			if (const auto user = peer->asUser()) {
				_store.rememberPlaintext(
					selfId(user),
					peerId(user),
					change.newId.msg.bare,
					i->second);
			}
		}
		_pending.erase(i);
	}, session->lifetime());
}

quint64 Manager::selfId(not_null<UserData*> user) const {
	return user->session().userId().bare;
}

quint64 Manager::peerId(not_null<UserData*> user) const {
	return peerToUser(user->id).bare;
}

std::pair<quint64, quint64> Manager::key(not_null<UserData*> user) const {
	return { selfId(user), peerId(user) };
}

bool Manager::eligible(not_null<PeerData*> peer) const {
	const auto user = peer->asUser();
	return user
		&& !user->isSelf()
		&& !user->isBot()
		&& !user->isInaccessible();
}

State Manager::state(not_null<UserData*> user) {
	const auto session = _store.session(selfId(user), peerId(user));
	if (!session) {
		return State::Off;
	}
	switch (session->mode) {
	case Session::Mode::Requested: return State::Requested;
	case Session::Mode::IncomingPending: return State::IncomingPending;
	case Session::Mode::Established: return State::Established;
	case Session::Mode::None: break;
	}
	return State::Off;
}

QString Manager::seedPhrase(not_null<UserData*> user) const {
	return Prefs::GetString(SeedKey(peerId(user)));
}

void Manager::setSeedPhrase(not_null<UserData*> user, const QString &seed) {
	Prefs::SetString(SeedKey(peerId(user)), seed);
}

bool Manager::verified(not_null<UserData*> user) const {
	return Prefs::GetBool(VerifiedKey(peerId(user)), false);
}

void Manager::setVerified(not_null<UserData*> user, bool value) {
	Prefs::SetBool(VerifiedKey(peerId(user)), value);
}

void Manager::toast(const QString &text) const {
	if (const auto window = Core::App().activeWindow()) {
		window->showToast(text);
	}
}

void Manager::notifyState(not_null<UserData*> user) {
	_store.save();
	refreshCards(user);
	_stateChanges.fire_copy(user);
}

void Manager::sendFramed(not_null<UserData*> user, const QString &text) {
	auto &session = user->session();
	const auto history = session.data().history(user);
	auto message = Api::MessageToSend(Api::SendAction(history));
	message.textWithTags = TextWithTags{ text };
	session.api().sendMessage(std::move(message));
}

void Manager::beginHandshake(not_null<UserData*> user) {
	if (!eligible(user) || state(user) != State::Off) {
		return;
	}
	const auto &identity = _store.identity();
	auto &session = _store.ensureSession(selfId(user), peerId(user));
	session = Session();
	session.selfUserId = selfId(user);
	session.peerUserId = peerId(user);
	session.mode = Session::Mode::Requested;
	session.pendingSpkPriv = identity.spk.priv;
	session.pendingSpkPub = identity.spk.pub;
	setVerified(user, false);
	sendFramed(user, EncodeHandshake(_store.LocalBundle(kSubtypeRequest)));
	notifyState(user);
	toast(Text(
		"Encryption request sent",
		"Запрос на шифрование отправлен"));
}

void Manager::cancelHandshake(not_null<UserData*> user) {
	reset(user);
	sendFramed(user, EncodeCancel());
}

void Manager::reset(not_null<UserData*> user) {
	_store.removeSession(selfId(user), peerId(user));
	setVerified(user, false);
	notifyState(user);
}

void Manager::resetSmart(not_null<UserData*> user) {
	switch (state(user)) {
	case State::Established: disable(user); break;
	case State::Requested:
	case State::IncomingPending: cancelHandshake(user); break;
	case State::Off: break;
	}
}

void Manager::disable(not_null<UserData*> user) {
	const auto session = _store.session(selfId(user), peerId(user));
	if (session
		&& session->mode == Session::Mode::Established
		&& !session->sendChainKey.isEmpty()) {
		// Authenticated by the ratchet key, unlike a bare marker that
		// anybody who can write into the chat could forge.
		const auto framed = EncodeCiphertext(Encrypt(
			*session,
			DisableSentinel().toUtf8()));
		_pendingByWire[framed] = DisabledLabel();
		sendFramed(user, framed);
	}
	reset(user);
}

void Manager::confirmIncoming(not_null<UserData*> user) {
	const auto pending = _store.session(selfId(user), peerId(user));
	if (!pending
		|| pending->mode != Session::Mode::IncomingPending
		|| pending->incomingBundleRaw.isEmpty()) {
		return;
	}
	auto request = Bundle();
	if (!DecodeHandshake(
			QString::fromLatin1(pending->incomingBundleRaw),
			request)) {
		reset(user);
		return;
	}
	const auto ek = GenerateX25519();
	const auto &identity = _store.identity();
	auto consumed = qint32(-1);
	auto opkPub = QByteArray();
	if (!request.opkIndices.empty()) {
		consumed = request.opkIndices.front();
		opkPub = request.opkPubs.front();
	}
	const auto dh1 = X25519(identity.ikDh.priv, request.spkPub);
	const auto dh2 = X25519(ek.priv, request.ikDhPub);
	const auto dh3 = X25519(ek.priv, request.spkPub);
	const auto dh4 = opkPub.isEmpty() ? QByteArray() : X25519(ek.priv, opkPub);
	if (dh1.isEmpty() || dh2.isEmpty() || dh3.isEmpty()) {
		reset(user);
		return;
	}
	const auto secret = DeriveX3dhSecret(dh1, dh2, dh3, dh4);

	auto session = Session();
	session.selfUserId = selfId(user);
	session.peerUserId = peerId(user);
	session.peerIkSignPub = request.ikSignPub;
	InitAsInitiator(session, secret, request.spkPub);
	_store.ensureSession(selfId(user), peerId(user)) = std::move(session);
	setVerified(user, false);

	auto response = _store.LocalBundle(kSubtypeResponse);
	response.consumedOpkIndex = consumed;
	response.ephemeralPub = ek.pub;
	sendFramed(user, EncodeHandshake(response));
	notifyState(user);
	toast(Text(
		"Encrypted chat is on",
		"Защищённый чат включён"));
}

QString Manager::verificationCode(
		not_null<UserData*> user,
		const QString &seed) {
	const auto session = _store.session(selfId(user), peerId(user));
	if (!session
		|| session->mode != Session::Mode::Established
		|| session->peerIkSignPub.isEmpty()) {
		return QString();
	}
	return VerificationCode(_store.ownIkSignPub(), session->peerIkSignPub, seed);
}

Manager::Outgoing Manager::encryptOutgoing(
		not_null<History*> history,
		const QString &plain,
		FullMsgId localId) {
	const auto user = history->peer->asUser();
	if (!user || !eligible(user) || plain.isEmpty() || IsProtocol(plain)) {
		return {};
	}
	const auto session = _store.session(selfId(user), peerId(user));
	if (!session || session->mode != Session::Mode::Established) {
		return {};
	}
	if (session->sendChainKey.isEmpty()) {
		// Established, but this side cannot send yet: sending the text
		// as it is would be a silent failure of confidentiality.
		return { Outgoing::Kind::Blocked };
	}
	const auto framed = EncodeCiphertext(Encrypt(*session, plain.toUtf8()));
	if (framed.isEmpty() || !IsCiphertext(framed)) {
		return { Outgoing::Kind::Blocked };
	}
	_store.save();
	_pending[localId] = plain;
	return { Outgoing::Kind::Encrypted, framed };
}

void Manager::trackCard(
		not_null<UserData*> user,
		not_null<HistoryItem*> item) {
	auto &cards = _cards[key(user)];
	const auto id = item->fullId();
	for (const auto &card : cards) {
		if (card.id == id) {
			return;
		}
	}
	cards.push_back({ id, item->out() });
}

void Manager::paintCard(
		not_null<UserData*> user,
		not_null<HistoryItem*> item,
		bool mine) {
	const auto name = user->name();
	const auto peer = peerId(user);
	switch (state(user)) {
	case State::Established:
		SetCard(
			item,
			Text(
				"🔒 Encrypted connection is established\n"
				"Check the security code to be sure that nobody is in the "
				"middle",
				"🔒 Зашифрованное соединение установлено\n"
				"Проверьте код безопасности, чтобы убедиться, что "
				"посредника нет"),
			{
				MakeButton(
					Text("Check the code", "Проверить код"),
					u"verify"_q,
					peer,
					true),
				MakeButton(
					Text("Reset", "Сбросить"),
					u"reset"_q,
					peer),
			});
		return;
	case State::Requested:
		if (mine) {
			SetCard(
				item,
				Text(
					"🔒 You asked %1 for an encrypted chat\nWaiting for "
					"the answer…",
					"🔒 Вы отправили %1 запрос на зашифрованное общение\n"
					"Ожидание ответа…").arg(name),
				{
					MakeButton(
						Text("Cancel", "Отменить"),
						u"cancel"_q,
						peer),
				});
			return;
		}
		break;
	case State::IncomingPending:
		if (!mine) {
			SetCard(
				item,
				Text(
					"🔒 %1 offers an encrypted connection\nAccept only if "
					"you trust the sender",
					"🔒 %1 предлагает установить зашифрованную связь\n"
					"Подтвердите, только если доверяете отправителю"
				).arg(name),
				{
					MakeButton(
						Text("Accept", "Подтвердить"),
						u"accept"_q,
						peer,
						true),
					MakeButton(
						Text("Decline", "Отклонить"),
						u"cancel"_q,
						peer),
				});
			return;
		}
		break;
	case State::Off:
		break;
	}
	SetCard(item, CancelLabel(), {});
}

void Manager::refreshCards(not_null<UserData*> user) {
	const auto i = _cards.find(key(user));
	if (i == _cards.end()) {
		return;
	}
	auto &list = i->second;
	for (auto j = list.begin(); j != list.end();) {
		const auto item = user->session().data().message(j->id);
		if (!item) {
			j = list.erase(j);
			continue;
		}
		paintCard(user, item, j->mine);
		++j;
	}
}

void Manager::showFailed(
		not_null<UserData*> user,
		not_null<HistoryItem*> item) {
	if (state(user) == State::Established) {
		SetCard(
			item,
			FailedLabel(),
			{
				MakeButton(
					Text("Reset encryption", "Сбросить шифрование"),
					u"reset"_q,
					peerId(user)),
			});
	} else {
		SetCard(item, FailedLabel(), {});
	}
}

void Manager::processLocalItem(not_null<HistoryItem*> item) {
	if (!item->out() || item->media()) {
		return;
	}
	const auto text = item->originalText().text;
	if (!IsProtocol(text)) {
		return;
	}
	const auto user = item->history()->peer->asUser();
	if (!user) {
		return;
	}
	if (IsHandshake(text)) {
		auto bundle = Bundle();
		if (DecodeHandshake(text, bundle)
			&& bundle.subtype == kSubtypeRequest) {
			trackCard(user, item);
			paintCard(user, item, true);
		} else {
			SetCard(item, EstablishedLabel(), {});
		}
	} else if (IsCancel(text)) {
		SetCard(item, CancelLabel(), {});
	} else if (IsCiphertext(text)) {
		const auto i = _pendingByWire.find(text);
		SetCard(
			item,
			(i != _pendingByWire.end()) ? i->second : OtherDeviceLabel(),
			{});
		if (i != _pendingByWire.end()) {
			_pendingByWire.erase(i);
		}
	}
}

void Manager::processItem(not_null<HistoryItem*> item) {
	if (item->isService() || item->media()) {
		return;
	}
	const auto text = item->originalText().text;
	if (!text.startsWith(u"[PGE2E1:"_q) || !IsProtocol(text)) {
		return;
	}
	const auto user = item->history()->peer->asUser();
	if (!user || !eligible(user)) {
		return;
	}
	if (item->out()) {
		// Our own message that came back from the server, for example
		// after a restart: the plaintext was remembered when it was sent.
		auto plain = QString();
		if (IsCiphertext(text)
			&& _store.plaintext(
				selfId(user),
				peerId(user),
				item->id.bare,
				plain)) {
			item->replaceTextLocally({ plain });
		} else if (IsHandshake(text)) {
			auto bundle = Bundle();
			if (DecodeHandshake(text, bundle)
				&& bundle.subtype == kSubtypeRequest) {
				trackCard(user, item);
				paintCard(user, item, true);
			} else {
				SetCard(item, EstablishedLabel(), {});
			}
		} else if (IsCancel(text)) {
			SetCard(item, CancelLabel(), {});
		} else {
			SetCard(item, OtherDeviceLabel(), {});
		}
		return;
	}
	if (IsHandshake(text)) {
		auto bundle = Bundle();
		if (!DecodeHandshake(text, bundle)) {
			SetCard(item, FailedLabel(), {});
			return;
		}
		// A protocol message is acted on once, however many times the
		// history that contains it is loaded.
		auto handled = QString();
		const auto seen = _store.plaintext(
			selfId(user),
			peerId(user),
			item->id.bare,
			handled);
		if (!seen) {
			_store.rememberPlaintext(
				selfId(user),
				peerId(user),
				item->id.bare,
				QString(QChar(1)));
		}
		if (bundle.subtype == kSubtypeRequest) {
			if (!seen) {
				handleRequest(user, text, bundle, item);
			}
			trackCard(user, item);
			paintCard(user, item, false);
		} else {
			if (!seen) {
				handleResponse(user, bundle);
			}
			SetCard(item, EstablishedLabel(), {});
		}
	} else if (IsCancel(text)) {
		auto handled = QString();
		if (!_store.plaintext(
				selfId(user),
				peerId(user),
				item->id.bare,
				handled)) {
			_store.rememberPlaintext(
				selfId(user),
				peerId(user),
				item->id.bare,
				QString(QChar(1)));
			handleCancel(user);
		}
		SetCard(item, CancelLabel(), {});
	} else {
		auto failed = false;
		const auto plain = decryptIncoming(user, item, failed);
		if (failed) {
			showFailed(user, item);
		} else {
			item->replaceTextLocally({ plain });
		}
	}
}

void Manager::handleRequest(
		not_null<UserData*> user,
		const QString &raw,
		const Bundle &bundle,
		not_null<HistoryItem*> item) {
	const auto age = int64(base::unixtime::now()) - int64(item->date());
	if (age > kMaxRequestAgeSeconds) {
		return;
	}
	// Forged or corrupt: never even show a prompt.
	if (!Ed25519Verify(bundle.ikSignPub, bundle.spkPub, bundle.spkSig)) {
		return;
	}
	// Only a request that was just made counts as a new one. An old
	// request that is loaded again with the history must not replace a
	// session that has been working since then.
	const auto current = state(user);
	if (current != State::Off && age > kFreshRequestSeconds) {
		return;
	}
	if (current == State::IncomingPending) {
		const auto session = _store.session(selfId(user), peerId(user));
		if (session && session->incomingBundleRaw == raw.toLatin1()) {
			return; // The very same request seen again.
		}
	}
	// A new request while a session exists means that the other side
	// started over: the old session is replaced by the new request.
	auto &session = _store.ensureSession(selfId(user), peerId(user));
	session = Session();
	session.selfUserId = selfId(user);
	session.peerUserId = peerId(user);
	session.mode = Session::Mode::IncomingPending;
	session.incomingBundleRaw = raw.toLatin1();
	setVerified(user, false);
	notifyState(user);
	if (age < kFreshRequestSeconds) {
		toast(Text(
			"%1 offers an encrypted chat",
			"%1 предлагает защищённый чат").arg(user->name()));
	}
}

void Manager::handleResponse(
		not_null<UserData*> user,
		const Bundle &response) {
	const auto session = _store.session(selfId(user), peerId(user));
	if (!session
		|| session->mode != Session::Mode::Requested
		|| session->pendingSpkPriv.isEmpty()) {
		return; // A stray, duplicate or replayed answer.
	}
	if (!Ed25519Verify(response.ikSignPub, response.spkPub, response.spkSig)
		|| response.ephemeralPub.isEmpty()) {
		return;
	}
	const auto &identity = _store.identity();
	const auto opk = (response.consumedOpkIndex >= 0)
		? _store.OpkIfUnconsumed(response.consumedOpkIndex)
		: KeyPair();
	const auto dh1 = X25519(session->pendingSpkPriv, response.ikDhPub);
	const auto dh2 = X25519(identity.ikDh.priv, response.ephemeralPub);
	const auto dh3 = X25519(session->pendingSpkPriv, response.ephemeralPub);
	const auto dh4 = opk.priv.isEmpty()
		? QByteArray()
		: X25519(opk.priv, response.ephemeralPub);
	if (dh1.isEmpty() || dh2.isEmpty() || dh3.isEmpty()) {
		return;
	}
	const auto secret = DeriveX3dhSecret(dh1, dh2, dh3, dh4);
	if (response.consumedOpkIndex >= 0) {
		_store.consumeOpk(response.consumedOpkIndex);
	}
	session->peerIkSignPub = response.ikSignPub;
	const auto spkPriv = session->pendingSpkPriv;
	const auto spkPub = session->pendingSpkPub;
	InitAsResponder(*session, secret, spkPriv, spkPub);
	session->pendingSpkPriv.clear();
	session->pendingSpkPub.clear();
	notifyState(user);
	toast(Text(
		"Encrypted chat is on",
		"Защищённый чат включён"));
}

void Manager::handleCancel(not_null<UserData*> user) {
	// An established session is never ended by a bare marker: that would
	// let anybody who can write into the chat kill it.
	const auto current = state(user);
	if (current == State::Requested || current == State::IncomingPending) {
		reset(user);
	}
}

QString Manager::decryptIncoming(
		not_null<UserData*> user,
		not_null<HistoryItem*> item,
		bool &failed) {
	failed = false;
	auto cached = QString();
	if (_store.plaintext(selfId(user), peerId(user), item->id.bare, cached)) {
		return cached;
	}
	const auto session = _store.session(selfId(user), peerId(user));
	if (!session || session->mode != Session::Mode::Established) {
		failed = true;
		return QString();
	}
	auto decoded = DecodedCiphertext();
	if (!DecodeCiphertext(item->originalText().text, decoded)) {
		failed = true;
		return QString();
	}
	const auto plain = Decrypt(
		*session,
		decoded.header,
		decoded.nonce,
		decoded.ciphertext);
	// The ratchet moved even if this message could not be read.
	_store.save();
	if (plain.isNull()) {
		failed = true;
		return QString();
	}
	const auto text = QString::fromUtf8(plain);
	if (IsDisableSentinel(text)) {
		reset(user);
		_store.rememberPlaintext(
			selfId(user),
			peerId(user),
			item->id.bare,
			DisabledLabel());
		return DisabledLabel();
	}
	_store.rememberPlaintext(
		selfId(user),
		peerId(user),
		item->id.bare,
		text);
	return text;
}

void Manager::wipeAll() {
	_pending.clear();
	_pendingByWire.clear();
	_cards.clear();
	_store.wipe();
}

} // namespace Prime::E2E
