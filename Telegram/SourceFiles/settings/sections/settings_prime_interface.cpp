/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "settings/sections/settings_prime_interface.h"

#include "prime/prime_text.h"
#include "prime/tweaks/tweaks.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using Prime::Text;
using Prime::TextValue;
namespace T = Prime::Tweaks;

void AddToggle(
		not_null<Ui::VerticalLayout*> container,
		const char *en,
		const char *ru,
		T::Flag flag) {
	const auto button = container->add(object_ptr<Button>(
		container,
		TextValue(en, ru),
		st::settingsButtonNoIcon));
	button->toggleOn(rpl::single(T::On(flag)));
	button->toggledValue(
	) | rpl::filter([=](bool toggled) {
		return toggled != T::On(flag);
	}) | rpl::on_next([=](bool toggled) {
		T::Set(flag, toggled);
	}, button->lifetime());
}

[[nodiscard]] QString AvatarShapeName() {
	switch (T::AvatarShape()) {
	case T::kAvatarRounded: return Text("Rounded", "Скруглённая");
	case T::kAvatarSquare: return Text("Square", "Квадратная");
	}
	return Text("Circle", "Круглая");
}

} // namespace

PrimeInterface::PrimeInterface(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> PrimeInterface::title() {
	return TextValue("Interface and chats", "Интерфейс и чаты");
}

void PrimeInterface::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, TextValue("Time and numbers", "Время и числа"));
	AddToggle(content, "Seconds in message time", "Секунды во времени сообщений", T::Flag::Seconds);
	AddToggle(content, "A pencil instead of “edited”", "Значок вместо «изменено»", T::Flag::EditedIcon);
	AddToggle(content, "Exact numbers (no 1.2K)", "Точные числа без округления", T::Flag::ExactNumbers);
	AddToggle(content, "“Last seen 5 hours ago” for longer", "«Был(а) 5 часов назад» вместо времени", T::Flag::RelativeLastSeen);
	Ui::AddSkip(content);

	Ui::AddDivider(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, TextValue("Reactions and stories", "Реакции и истории"));
	AddToggle(content, "Hide reactions in channels", "Скрыть реакции в каналах", T::Flag::HideReactionsChannels);
	AddToggle(content, "Hide reactions in groups", "Скрыть реакции в группах", T::Flag::HideReactionsGroups);
	AddToggle(content, "Hide reactions in private chats", "Скрыть реакции в личных чатах", T::Flag::HideReactionsPrivate);
	AddToggle(content, "Hide stories", "Скрыть истории", T::Flag::HideStories);
	Ui::AddSkip(content);

	Ui::AddDivider(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, TextValue("Avatars", "Аватарки"));
	AddButtonWithLabel(
		content,
		TextValue("Avatar shape", "Форма аватарок"),
		rpl::single(rpl::empty) | rpl::then(
			T::Changes()
		) | rpl::map([] {
			return AvatarShapeName();
		}),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(TextValue("Avatar shape", "Форма аватарок"));
			const auto add = [=](
					const char *en,
					const char *ru,
					int shape) {
				AddButtonWithIcon(
					box->verticalLayout(),
					TextValue(en, ru),
					st::settingsButtonNoIcon
				)->addClickHandler([=] {
					T::SetAvatarShape(shape);
					controller->window().showToast(Text(
						"Restart the app to apply the new shape everywhere",
						"Перезапустите приложение, чтобы форма применилась везде"));
					box->closeBox();
				});
			};
			add("Circle", "Круглая", T::kAvatarCircle);
			add("Rounded square", "Скруглённый квадрат", T::kAvatarRounded);
			add("Square", "Квадратная", T::kAvatarSquare);
			box->addButton(TextValue("Close", "Закрыть"), [=] {
				box->closeBox();
			});
		}));
	});
	Ui::AddSkip(content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
