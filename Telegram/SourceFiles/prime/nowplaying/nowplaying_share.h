/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "api/api_common.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Prime::NowPlaying {

// A box that shows what is playing now and sends it into the chat as a
// picture with the cover or as a line of text.
void ShowShareBox(
	not_null<Window::SessionController*> controller,
	Api::SendAction action);

} // namespace Prime::NowPlaying
