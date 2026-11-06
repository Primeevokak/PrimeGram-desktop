/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"

namespace Prime {

// A logged in session of any account by the user id, or nullptr.
[[nodiscard]] inline Main::Session *SessionByUserId(uint64 userId) {
	for (const auto &entry : Core::App().domain().accounts()) {
		if (const auto session = entry.account->maybeSession()) {
			if (session->userId().bare == userId) {
				return session;
			}
		}
	}
	return nullptr;
}

} // namespace Prime
