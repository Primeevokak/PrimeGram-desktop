/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Prime::IdentitySearch {

enum class Kind {
	None,
	Id,
	Phone,
	Link,
};

struct Parsed {
	Kind kind = Kind::None;
	QString value; // Digits for an id or a phone, a username for a link.
	int64 id = 0; // Only for Kind::Id; negative for chats and channels.
};

// Recognizes what the query is, strictly: a bare number is an id,
// a number with a plus or spacing is a phone, a t.me link has a username.
[[nodiscard]] Parsed Parse(const QString &query);

// Finds the peer, locally when possible. `done` gets nullptr when nothing
// is known: Telegram only answers for peers the account has met.
void Resolve(
	not_null<Main::Session*> session,
	const Parsed &parsed,
	Fn<void(PeerData*)> done);

} // namespace Prime::IdentitySearch
