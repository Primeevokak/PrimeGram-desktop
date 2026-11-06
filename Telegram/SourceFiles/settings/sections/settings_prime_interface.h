/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Settings {

// Switches of how chats and the interface look: the PrimeGram "tweaks".
class PrimeInterface : public Section<PrimeInterface> {
public:
	PrimeInterface(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings
