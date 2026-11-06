/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "prime/prime_store.h"

#include <QtCore/QJsonObject>

#include <string_view>

namespace Prime::Prefs {
namespace details {

// Kept apart from Core::Settings, which forgets its key-value prefs when
// the last account logs out (and on every start while logged out).
[[nodiscard]] inline QJsonObject &Data() {
	static auto result = LoadStore(u"prefs"_q);
	return result;
}

[[nodiscard]] inline QString Key(std::string_view key) {
	return QString::fromUtf8(key.data(), int(key.size()));
}

inline void Write(std::string_view key, const QJsonValue &value) {
	Data().insert(Key(key), value);
	SaveStore(u"prefs"_q, Data());
}

} // namespace details

[[nodiscard]] inline bool GetBool(std::string_view key, bool fallback) {
	return details::Data().value(details::Key(key)).toBool(fallback);
}

inline void SetBool(std::string_view key, bool value) {
	details::Write(key, value);
}

[[nodiscard]] inline int GetInt(std::string_view key, int fallback) {
	return details::Data().value(details::Key(key)).toInt(fallback);
}

inline void SetInt(std::string_view key, int value) {
	details::Write(key, value);
}

[[nodiscard]] inline QString GetString(std::string_view key) {
	return details::Data().value(details::Key(key)).toString();
}

inline void SetString(std::string_view key, const QString &value) {
	details::Write(key, value);
}

[[nodiscard]] inline QByteArray GetBytes(std::string_view key) {
	return QByteArray::fromBase64(GetString(key).toLatin1());
}

inline void SetBytes(std::string_view key, const QByteArray &value) {
	SetString(key, QString::fromLatin1(value.toBase64()));
}

} // namespace Prime::Prefs
