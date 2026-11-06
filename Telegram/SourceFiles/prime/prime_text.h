/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "lang/lang_instance.h"

#include <rpl/producer.h>
#include <rpl/then.h>

namespace Prime {

[[nodiscard]] inline QString Text(const char *en, const char *ru) {
	const auto russian = Lang::GetInstance().id().startsWith(u"ru"_q);
	return QString::fromUtf8(russian ? ru : en);
}

[[nodiscard]] inline QString Text(const QString &en, const QString &ru) {
	const auto russian = Lang::GetInstance().id().startsWith(u"ru"_q);
	return russian ? ru : en;
}

[[nodiscard]] inline rpl::producer<QString> TextValue(
		const char *en,
		const char *ru) {
	return rpl::single(Text(en, ru)) | rpl::then(
		Lang::GetInstance().idChanges() | rpl::map([=](QString) {
			return Text(en, ru);
		}));
}

} // namespace Prime
