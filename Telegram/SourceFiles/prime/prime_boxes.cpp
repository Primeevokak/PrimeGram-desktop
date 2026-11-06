/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/prime_boxes.h"

#include "apiwrap.h"
#include "main/main_session.h"
#include "base/event_filter.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "ui/abstract_button.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_widgets.h"
#include "styles/style_menu_icons.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "prime/e2e/e2e_manager.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/fields/input_field.h"
#include "prime/autodelete/autodelete_manager.h"
#include "prime/prime_hub.h"
#include "prime/prime_text.h"
#include "prime/tempsub/tempsub_manager.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Prime {
namespace {

struct Preset {
	int64 seconds = 0;
	const char *en = nullptr;
	const char *ru = nullptr;
};

constexpr auto kHoldMs = 450;

constexpr Preset kPresets[] = {
	{ 3600, "1 hour", "1 час" },
	{ 6 * 3600, "6 hours", "6 часов" },
	{ 24 * 3600, "1 day", "1 день" },
	{ 3 * 24 * 3600, "3 days", "3 дня" },
	{ 7 * 24 * 3600, "7 days", "7 дней" },
	{ 30 * 24 * 3600, "30 days", "30 дней" },
};

[[nodiscard]] QString LeftText(int64 seconds) {
	if (seconds < 3600) {
		return Text("%1 min", "%1 мин").arg(std::max<int64>(1, seconds / 60));
	} else if (seconds < 24 * 3600) {
		return Text("%1 h", "%1 ч").arg(seconds / 3600);
	}
	return Text("%1 d", "%1 д").arg(seconds / (24 * 3600));
}

} // namespace

void ShowTempSubBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(TextValue(
			"Temporary subscription",
			"Временная подписка"));
		const auto container = box->verticalLayout();
		auto &manager = Core::App().prime().tempSub();
		const auto existing = manager.entryFor(peer);
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				TextValue(
					"Leave this chat automatically after:",
					"Автоматически выйти из чата через:"),
				st::boxLabel),
			st::boxRowPadding);
		if (existing) {
			const auto left = existing->expiresAt
				- int64(base::unixtime::now());
			container->add(
				object_ptr<Ui::FlatLabel>(
					container,
					rpl::single(Text("Left: %1", "Осталось: %1").arg(
						LeftText(std::max<int64>(0, left)))),
					st::boxLabel),
				st::boxRowPadding);
		}
		for (const auto &preset : kPresets) {
			const auto seconds = preset.seconds;
			Settings::AddButtonWithIcon(
				container,
				TextValue(preset.en, preset.ru),
				st::settingsButtonNoIcon
			)->addClickHandler([=] {
				Core::App().prime().tempSub().schedule(
					peer,
					int64(base::unixtime::now()) + seconds);
				if (const auto channel = peer->asChannel();
					channel && !channel->amIn()) {
					peer->session().api().joinChannel(channel);
				}
				controller->window().showToast(Text(
					"Will leave in %1",
					"Выйдем через %1").arg(LeftText(seconds)));
				box->closeBox();
			});
		}
		if (existing) {
			box->addLeftButton(
				TextValue("Cancel auto-leave", "Отменить автовыход"),
				[=] {
					Core::App().prime().tempSub().cancel(peer);
					box->closeBox();
				});
		}
		box->addButton(TextValue("Close", "Закрыть"), [=] {
			box->closeBox();
		});
	}));
}

void SetupTempJoinButton(
		not_null<Ui::AbstractButton*> button,
		not_null<Window::SessionController*> controller,
		Fn<ChannelData*()> channel,
		Fn<void()> join) {
	struct State {
		base::Timer timer;
		base::unique_qptr<Ui::PopupMenu> menu;
		bool held = false;
	};
	const auto state = button->lifetime().make_state<State>();
	button->setAcceptBoth(true);

	const auto showMenu = [=] {
		const auto target = channel();
		if (!target) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		state->menu->addAction(
			Text("Subscribe", "Подписаться"),
			join,
			&st::menuIconAddToFolder);
		state->menu->addSeparator();
		for (const auto &preset : kPresets) {
			const auto seconds = preset.seconds;
			state->menu->addAction(
				Text("Subscribe for ", "Подписаться на ")
					+ Text(preset.en, preset.ru),
				[=] {
					Core::App().prime().tempSub().schedule(
						target,
						int64(base::unixtime::now()) + seconds);
					join();
					controller->window().showToast(Text(
						"Will leave in %1",
						"Выйдем через %1").arg(LeftText(seconds)));
				},
				&st::menuIconTimer);
		}
		state->menu->popup(QCursor::pos());
	};

	state->timer.setCallback([=] {
		state->held = true;
		showMenu();
	});
	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::MouseButtonPress: {
			const auto event = static_cast<QMouseEvent*>(e.get());
			if (event->button() == Qt::LeftButton) {
				state->held = false;
				state->timer.callOnce(kHoldMs);
			}
		} break;
		case QEvent::MouseButtonRelease:
		case QEvent::Leave:
		case QEvent::Hide:
			state->timer.cancel();
			break;
		default: break;
		}
		return base::EventFilterResult::Continue;
	});
	button->clicks(
	) | rpl::on_next([=](Qt::MouseButton which) {
		if (which == Qt::RightButton) {
			showMenu();
		} else if (state->held) {
			state->held = false;
		} else {
			join();
		}
	}, button->lifetime());
}

void ShowAutoDeleteChatBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(TextValue(
			"Auto-delete my messages",
			"Автоудаление моих сообщений"));
		const auto container = box->verticalLayout();
		auto &manager = Core::App().prime().autoDelete();
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				TextValue(
					"Your own messages in this chat disappear some time "
					"after sending. The time is set in PrimeGram settings.",
					"Ваши сообщения в этом чате исчезают через некоторое "
					"время после отправки. Время задаётся в настройках "
					"PrimeGram."),
				st::boxLabel),
			st::boxRowPadding);
		const auto add = [=](
				const char *en,
				const char *ru,
				int value) {
			Settings::AddButtonWithIcon(
				container,
				TextValue(en, ru),
				st::settingsButtonNoIcon
			)->addClickHandler([=] {
				Core::App().prime().autoDelete().setChatOverride(peer, value);
				box->closeBox();
			});
		};
		add("Follow the global setting", "Как в общих настройках", -1);
		add("Always on in this chat", "Всегда включено в этом чате", 1);
		add("Always off in this chat", "Всегда выключено в этом чате", 0);
		box->addButton(TextValue("Close", "Закрыть"), [=] {
			box->closeBox();
		});
	}));
}

void ShowE2EVerifyBox(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(TextValue(
			"E2E encryption check",
			"Проверка E2E-шифрования"));
		const auto container = box->verticalLayout();
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				TextValue(
					"Type a phrase that you have agreed on with the other "
					"person outside Telegram: by voice or in person. "
					"Compare the code you get with theirs. If it is the "
					"same, the chat is protected from a substitution when "
					"the connection was set up. The app does not send this "
					"phrase anywhere.",
					"Введите фразу, о которой вы заранее договорились с "
					"собеседником вне Telegram (голосом, лично). Сравните "
					"получившийся код с собеседником. Если он совпадает, "
					"переписка защищена от подмены при установке "
					"соединения. Само приложение эту фразу никуда не "
					"передаёт."),
				st::boxLabel),
			st::boxRowPadding);
		const auto field = container->add(
			object_ptr<Ui::InputField>(
				container,
				st::defaultInputField,
				TextValue("Secret phrase", "Секретная фраза"),
				Core::App().prime().e2e().seedPhrase(user)),
			st::boxRowPadding);
		const auto code = container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				QString(),
				st::boxTitle),
			st::boxRowPadding);
		const auto refresh = [=] {
			const auto seed = field->getLastText();
			Core::App().prime().e2e().setSeedPhrase(user, seed);
			const auto value = Core::App().prime().e2e().verificationCode(
				user,
				seed);
			code->setText(value.isEmpty()
				? QString::fromUtf8("——————")
				: value);
		};
		field->changes() | rpl::on_next(refresh, field->lifetime());
		refresh();
		const auto verified = Core::App().prime().e2e().verified(user);
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				verified
					? TextValue(
						"Verified: the codes matched.",
						"Проверено: коды совпали.")
					: TextValue(
						"Not verified yet.",
						"Пока не проверено."),
				st::boxLabel),
			st::boxRowPadding);
		box->addButton(
			TextValue(
				"The codes match",
				"Коды совпадают — подтвердить"),
			[=] {
				Core::App().prime().e2e().setVerified(user, true);
				box->closeBox();
			});
		box->addButton(TextValue("Close", "Закрыть"), [=] {
			box->closeBox();
		});
		box->addLeftButton(
			TextValue("Reset the session", "Сбросить сессию"),
			[=] {
				controller->show(Ui::MakeConfirmBox({
					.text = TextValue(
						"The chat will be set up again. Encrypted messages "
						"that were received before will stop being "
						"readable after a restart of the app.",
						"Переписка будет установлена заново. Уже "
						"полученные зашифрованные сообщения перестанут "
						"расшифровываться."),
					.confirmed = [=](Fn<void()> close) {
						Core::App().prime().e2e().resetSmart(user);
						close();
						box->closeBox();
					},
					.confirmText = TextValue("Reset", "Сбросить"),
					.confirmStyle = &st::attentionBoxButton,
				}));
			},
			st::attentionBoxButton);
	}));
}

void ShowE2EBox(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		auto &manager = Core::App().prime().e2e();
		box->setTitle(TextValue("Encrypted chat", "Защищённый чат"));
		const auto container = box->verticalLayout();
		const auto label = [&](const QString &text) {
			container->add(
				object_ptr<Ui::FlatLabel>(
					container,
					rpl::single(text),
					st::boxLabel),
				st::boxRowPadding);
		};
		const auto action = [&](
				const char *en,
				const char *ru,
				Fn<void()> handler) {
			Settings::AddButtonWithIcon(
				container,
				TextValue(en, ru),
				st::settingsButtonNoIcon
			)->addClickHandler([=] {
				handler();
				box->closeBox();
			});
		};
		const auto state = manager.state(user);
		switch (state) {
		case E2E::State::Off:
			label(Text(
				"With end-to-end encryption only you and %1 can read the "
				"messages. It works between PrimeGram apps: the other side "
				"has to accept the request.",
				"При сквозном шифровании сообщения могут прочитать только "
				"вы и %1. Работает между приложениями PrimeGram: собеседник "
				"должен принять запрос.").arg(user->name()));
			action("Enable encryption", "Включить шифрование", [=] {
				Core::App().prime().e2e().beginHandshake(user);
			});
			break;
		case E2E::State::Requested:
			label(Text(
				"Waiting for %1 to accept the request.",
				"Ждём, пока %1 примет запрос.").arg(user->name()));
			action("Cancel the request", "Отменить запрос", [=] {
				Core::App().prime().e2e().cancelHandshake(user);
			});
			break;
		case E2E::State::IncomingPending:
			label(Text(
				"%1 wants to start an encrypted chat.",
				"%1 хочет начать защищённый чат.").arg(user->name()));
			action("Accept", "Принять", [=] {
				Core::App().prime().e2e().confirmIncoming(user);
			});
			action("Decline", "Отклонить", [=] {
				Core::App().prime().e2e().cancelHandshake(user);
			});
			break;
		case E2E::State::Established:
			label(Text(
				"Encryption is on. Check the code with %1 to be sure that "
				"nobody is in the middle.",
				"Шифрование включено. Сверьте код с %1, чтобы убедиться, "
				"что посредника нет.").arg(user->name()));
			label(manager.verified(user)
				? Text("Verified ✓", "Проверено ✓")
				: Text("Not verified yet", "Пока не проверено"));
			action("Check the code", "Проверить код", [=] {
				ShowE2EVerifyBox(controller, user);
			});
			action("Turn off encryption", "Выключить шифрование", [=] {
				Core::App().prime().e2e().disable(user);
			});
			break;
		}
		if (state != E2E::State::Off) {
			label(Text(
				"If the chat does not work after the other side reset the "
				"encryption, reset it here too.",
				"Если чат не работает после того, как собеседник сбросил "
				"шифрование, сбросьте его и здесь."));
			action(
				"Reset the session on this device",
				"Сбросить сессию на этом устройстве",
				[=] {
					Core::App().prime().e2e().reset(user);
				});
		}
		box->addButton(TextValue("Close", "Закрыть"), [=] {
			box->closeBox();
		});
	}));
}

bool HandleE2ELink(
		not_null<Window::SessionController*> controller,
		const QString &action,
		quint64 userId) {
	const auto user = controller->session().data().userLoaded(
		UserId(userId));
	if (!user) {
		return false;
	}
	auto &manager = Core::App().prime().e2e();
	if (action == u"accept"_q) {
		manager.confirmIncoming(user);
	} else if (action == u"cancel"_q) {
		manager.cancelHandshake(user);
	} else if (action == u"verify"_q) {
		ShowE2EVerifyBox(controller, user);
	} else if (action == u"reset"_q) {
		controller->show(Ui::MakeConfirmBox({
			.text = TextValue(
				"Reset the encryption in this chat?",
				"Сбросить шифрование в этом чате?"),
			.confirmed = [=](Fn<void()> close) {
				Core::App().prime().e2e().resetSmart(user);
				close();
			},
			.confirmText = TextValue("Reset", "Сбросить"),
			.confirmStyle = &st::attentionBoxButton,
		}));
	} else {
		return false;
	}
	return true;
}

} // namespace Prime
