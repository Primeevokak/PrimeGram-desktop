/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <functional>

namespace Prime::TgWs {

void HttpsGetPinned(
	const QString &ip,
	const QString &host,
	const QString &path,
	int timeoutMs,
	std::function<void(QByteArray)> done);

[[nodiscard]] QByteArray DecodeHttpBody(const QByteArray &response);

} // namespace Prime::TgWs
