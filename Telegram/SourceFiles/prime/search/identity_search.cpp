/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/search/identity_search.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"

namespace Prime::IdentitySearch {
namespace {

constexpr auto kMaxDigits = 15;
constexpr auto kMinPhoneDigits = 7;

[[nodiscard]] bool AllDigits(const QString &text) {
	if (text.isEmpty()) {
		return false;
	}
	for (const auto ch : text) {
		if (ch < '0' || ch > '9') {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QString LinkUsername(const QString &query) {
	auto value = query.trimmed();
	const auto marker = value.indexOf(u"t.me/"_q);
	if (marker >= 0) {
		value = value.mid(marker + 5);
	} else if (value.startsWith(u"tg://resolve?domain="_q)) {
		value = value.mid(u"tg://resolve?domain="_q.size());
	} else {
		return QString();
	}
	auto cut = value.indexOf('?');
	if (cut >= 0) {
		value = value.left(cut);
	}
	cut = value.indexOf('/');
	if (cut >= 0) {
		value = value.left(cut);
	}
	// A "+" link is a private invite: a different request and answer.
	if (value.isEmpty() || value.startsWith('+')) {
		return QString();
	}
	return value;
}

[[nodiscard]] PeerData *Local(
		not_null<Main::Session*> session,
		int64 id) {
	auto &owner = session->data();
	if (id > 0) {
		if (const auto user = owner.userLoaded(UserId(id))) {
			return user;
		} else if (const auto chat = owner.chatLoaded(ChatId(id))) {
			return chat;
		}
		return owner.channelLoaded(ChannelId(id));
	}
	const auto positive = -id;
	// Bot API style: -100<channel id>.
	const auto text = QString::number(positive);
	if (text.startsWith(u"100"_q) && text.size() > 3) {
		return owner.channelLoaded(ChannelId(text.mid(3).toLongLong()));
	}
	if (const auto chat = owner.chatLoaded(ChatId(positive))) {
		return chat;
	}
	return owner.channelLoaded(ChannelId(positive));
}

} // namespace

Parsed Parse(const QString &query) {
	const auto text = query.trimmed();
	if (text.isEmpty()) {
		return {};
	}
	const auto negative = text.startsWith('-');
	const auto digits = negative ? text.mid(1) : text;
	if (AllDigits(digits) && digits.size() <= kMaxDigits) {
		const auto value = digits.toLongLong();
		if (value > 0) {
			return { Kind::Id, digits, negative ? -value : value };
		}
	}
	if (const auto username = LinkUsername(text); !username.isEmpty()) {
		return { Kind::Link, username, 0 };
	}
	if (text.startsWith('+') || text.contains(' ')) {
		auto phone = QString();
		for (const auto ch : text) {
			if (ch >= '0' && ch <= '9') {
				phone += ch;
			} else if (ch != '+' && ch != ' ' && ch != '-' && ch != '('
				&& ch != ')') {
				return {};
			}
		}
		if (phone.size() >= kMinPhoneDigits && phone.size() <= kMaxDigits) {
			return { Kind::Phone, phone, 0 };
		}
	}
	return {};
}

void Resolve(
		not_null<Main::Session*> session,
		const Parsed &parsed,
		Fn<void(PeerData*)> done) {
	auto &owner = session->data();
	const auto finish = [=](PeerData *peer) {
		if (done) {
			done(peer);
		}
	};
	switch (parsed.kind) {
	case Kind::Id: {
		if (const auto peer = Local(session, parsed.id)) {
			finish(peer);
			return;
		}
		if (parsed.id > 0) {
			// MTProto has no "get user by id": a zero access hash only
			// works for people the account has some relation with.
			session->api().request(MTPusers_GetUsers(
				MTP_vector<MTPInputUser>(1, MTP_inputUser(
					MTP_long(parsed.id),
					MTP_long(0)))
			)).done([=](const MTPVector<MTPUser> &result) {
				session->data().processUsers(result);
				finish(session->data().userLoaded(UserId(parsed.id)));
			}).fail([=] {
				finish(nullptr);
			}).send();
		} else {
			finish(nullptr);
		}
	} break;
	case Kind::Phone: {
		if (const auto user = owner.userByPhone(parsed.value)) {
			finish(user);
			return;
		}
		session->api().request(MTPcontacts_ResolvePhone(
			MTP_string(parsed.value)
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			result.match([&](const MTPDcontacts_resolvedPeer &data) {
				session->data().processUsers(data.vusers());
				session->data().processChats(data.vchats());
				if (const auto peerId = peerFromMTP(data.vpeer())) {
					finish(session->data().peer(peerId));
				} else {
					finish(nullptr);
				}
			});
		}).fail([=] {
			finish(nullptr);
		}).send();
	} break;
	case Kind::Link: {
		if (const auto peer = owner.peerByUsername(parsed.value)) {
			finish(peer);
			return;
		}
		session->api().request(MTPcontacts_ResolveUsername(
			MTP_flags(0),
			MTP_string(parsed.value),
			MTP_string()
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			result.match([&](const MTPDcontacts_resolvedPeer &data) {
				session->data().processUsers(data.vusers());
				session->data().processChats(data.vchats());
				if (const auto peerId = peerFromMTP(data.vpeer())) {
					finish(session->data().peer(peerId));
				} else {
					finish(nullptr);
				}
			});
		}).fail([=] {
			finish(nullptr);
		}).send();
	} break;
	case Kind::None:
		finish(nullptr);
		break;
	}
}

} // namespace Prime::IdentitySearch
