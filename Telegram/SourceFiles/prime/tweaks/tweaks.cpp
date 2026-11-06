/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tweaks/tweaks.h"

#include "data/data_channel.h"
#include "data/data_peer.h"
#include "prime/prime_prefs.h"

#include <QtCore/QLocale>
#include <QtCore/QTime>

#include <array>

namespace Prime::Tweaks {
namespace {

struct Entry {
	const char *key;
	bool fallback;
};

constexpr auto kEntries = std::array<Entry, int(Flag::kCount)>{ {
	{ "prime_tw_seconds", false },
	{ "prime_tw_edited_icon", false },
	{ "prime_tw_exact_numbers", false },
	{ "prime_tw_relative_lastseen", false },
	{ "prime_tw_hide_stories", false },
	{ "prime_tw_hide_react_channels", false },
	{ "prime_tw_hide_react_groups", false },
	{ "prime_tw_hide_react_private", false },
	{ "prime_tw_hide_share", false },
	{ "prime_tw_hide_send_as", false },
	{ "prime_tw_comma_mention", false },
	{ "prime_tw_sticker_time", true },
	{ "prime_tw_keep_sticker_order", false },
	{ "prime_tw_hide_all_chats_tab", false },
	{ "prime_tw_hide_archive_row", false },
	{ "prime_tw_hide_own_status", false },
	{ "prime_tw_hide_own_phone", false },
	{ "prime_tw_online_dot_groups", false },
	{ "prime_tw_hide_notify_text", false },
	{ "prime_tw_anonymize_downloads", false },
	{ "prime_tw_strip_metadata", false },
	{ "prime_tw_send_uncompressed", false },
} };

constexpr auto kAvatarKey = std::string_view("prime_tw_avatar_shape");
constexpr auto kPhoneKey = std::string_view("prime_tw_custom_phone");

struct State {
	std::array<bool, int(Flag::kCount)> flags = {};
	int avatar = kAvatarCircle;
	bool loaded = false;
	rpl::event_stream<> changes;
};

[[nodiscard]] State &Data() {
	static auto result = State();
	if (!result.loaded) {
		result.loaded = true;
		for (auto i = 0; i != int(Flag::kCount); ++i) {
			result.flags[i] = Prefs::GetBool(
				kEntries[i].key,
				kEntries[i].fallback);
		}
		result.avatar = Prefs::GetInt(kAvatarKey, kAvatarCircle);
	}
	return result;
}

} // namespace

bool On(Flag flag) {
	return Data().flags[int(flag)];
}

void Set(Flag flag, bool value) {
	auto &data = Data();
	if (data.flags[int(flag)] == value) {
		return;
	}
	data.flags[int(flag)] = value;
	Prefs::SetBool(kEntries[int(flag)].key, value);
	data.changes.fire({});
}

int AvatarShape() {
	return Data().avatar;
}

void SetAvatarShape(int shape) {
	auto &data = Data();
	if (data.avatar == shape) {
		return;
	}
	data.avatar = shape;
	Prefs::SetInt(kAvatarKey, shape);
	data.changes.fire({});
}

QString CustomPhone() {
	return Prefs::GetString(kPhoneKey);
}

void SetCustomPhone(const QString &value) {
	Prefs::SetString(kPhoneKey, value.trimmed());
	Data().changes.fire({});
}

QString FormatTime(const QTime &time) {
	const auto locale = QLocale();
	if (!On(Flag::Seconds)) {
		return locale.toString(time, QLocale::ShortFormat);
	}
	// Put the seconds right after the minutes, whatever the format is:
	// "HH:mm" becomes "HH:mm:ss", "h:mm AP" becomes "h:mm:ss AP".
	auto format = locale.timeFormat(QLocale::ShortFormat);
	const auto minutes = format.indexOf(u"mm"_q);
	if (minutes >= 0) {
		format.insert(minutes + 2, u":ss"_q);
	} else {
		format += u":ss"_q;
	}
	return locale.toString(time, format);
}

bool HideReactionsFor(not_null<PeerData*> peer) {
	if (peer->isUser()) {
		return On(Flag::HideReactionsPrivate);
	} else if (const auto channel = peer->asChannel()) {
		return channel->isBroadcast()
			? On(Flag::HideReactionsChannels)
			: On(Flag::HideReactionsGroups);
	}
	return On(Flag::HideReactionsGroups);
}

rpl::producer<> Changes() {
	return Data().changes.events();
}

} // namespace Prime::Tweaks
