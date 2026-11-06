/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include <rpl/event_stream.h>

class QTime;
class PeerData;

namespace Prime::Tweaks {

// Small switches of how the interface looks and behaves. Each one is a plain
// yes/no kept in the PrimeGram settings file; reading is cheap, as it is done
// while painting.
enum class Flag {
	Seconds, // Seconds in the time of a message.
	EditedIcon, // A pencil instead of the word "edited".
	ExactNumbers, // 1234 instead of 1.2K.
	RelativeLastSeen, // "5 minutes ago" for a longer time.
	HideStories,
	HideReactionsChannels,
	HideReactionsGroups,
	HideReactionsPrivate,
	HideShareButton,
	HideSendAs,
	CommaAfterMention,
	StickerTime, // Time on stickers and round videos.
	KeepStickerSetOrder,
	HideAllChatsTab,
	HideArchiveRow,
	HideOwnEmojiStatus,
	HideOwnPhone,
	OnlineDotInGroups,
	HideNotificationText,
	AnonymizeDownloads,
	StripMetadata,
	SendWithoutCompression,

	kCount,
};

// 0 circle, 1 rounded square, 2 square with small corners.
inline constexpr auto kAvatarCircle = 0;
inline constexpr auto kAvatarRounded = 1;
inline constexpr auto kAvatarSquare = 2;

[[nodiscard]] bool On(Flag flag);
void Set(Flag flag, bool value);

[[nodiscard]] int AvatarShape();
void SetAvatarShape(int shape);

// The user who the app shows as "me" for phone number hiding.
[[nodiscard]] QString CustomPhone();
void SetCustomPhone(const QString &value);

// The time of a message, with seconds when that is switched on.
[[nodiscard]] QString FormatTime(const QTime &time);

// Whether reactions on messages of this chat are hidden.
[[nodiscard]] bool HideReactionsFor(not_null<PeerData*> peer);

[[nodiscard]] rpl::producer<> Changes();

} // namespace Prime::Tweaks
