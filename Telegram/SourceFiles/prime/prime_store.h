/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "settings.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Prime {

// A small JSON file in tdata for the lists that PrimeGram features keep.
[[nodiscard]] inline QString StorePath(const QString &name) {
	return cWorkingDir() + u"tdata/prime_"_q + name + u".json"_q;
}

[[nodiscard]] inline QJsonObject LoadStore(const QString &name) {
	auto file = QFile(StorePath(name));
	if (!file.open(QIODevice::ReadOnly)) {
		return QJsonObject();
	}
	return QJsonDocument::fromJson(file.readAll()).object();
}

inline void SaveStore(const QString &name, const QJsonObject &object) {
	auto file = QFile(StorePath(name));
	if (object.isEmpty()) {
		file.remove();
		return;
	}
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
	}
}

} // namespace Prime
