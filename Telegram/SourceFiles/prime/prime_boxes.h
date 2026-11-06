/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

class PeerData;
class ChannelData;
class UserData;

namespace Ui {
class AbstractButton;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Prime {

void ShowTempSubBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

// Makes the "Join channel" button open a menu on a right click or a long
// press, to join for a limited time.
void SetupTempJoinButton(
	not_null<Ui::AbstractButton*> button,
	not_null<Window::SessionController*> controller,
	Fn<ChannelData*()> channel,
	Fn<void()> join);

void ShowAutoDeleteChatBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

void ShowE2EVerifyBox(
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user);

// Buttons in the encryption cards of a chat come here as tg:// links.
[[nodiscard]] bool HandleE2ELink(
	not_null<Window::SessionController*> controller,
	const QString &action,
	quint64 userId);

void ShowE2EBox(
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user);

} // namespace Prime
