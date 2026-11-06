/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/nowplaying/nowplaying_share.h"

#include "apiwrap.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "main/main_session.h"
#include "prime/nowplaying/nowplaying_card.h"
#include "prime/prime_text.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "settings/settings_common.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Prime::NowPlaying {
namespace {

struct State {
	Track track;
	QImage card;
	rpl::event_stream<> changed;
	bool loading = true;
};

void SendCard(
		not_null<Window::SessionController*> controller,
		Api::SendAction action,
		const State &state) {
	auto image = state.card;
	auto list = Storage::PrepareMediaFromImage(
		std::move(image),
		QByteArray(),
		st::sendMediaPreviewSize);
	if (list.files.empty()) {
		return;
	}
	list.files.front().caption = TextWithTags{ state.track.line() };
	action.history->session().api().sendFiles(
		std::move(list),
		SendMediaType::Photo,
		nullptr,
		action);
}

void SendText(Api::SendAction action, const Track &track) {
	auto message = Api::MessageToSend(action);
	message.textWithTags = TextWithTags{
		QString::fromUtf8("🎵 ") + track.line(),
	};
	action.history->session().api().sendMessage(std::move(message));
}

} // namespace

void ShowShareBox(
		not_null<Window::SessionController*> controller,
		Api::SendAction action) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(TextValue("Now playing", "Сейчас играет"));
		const auto container = box->verticalLayout();
		const auto state = box->lifetime().make_state<State>();

		const auto label = container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				rpl::single(Text("Looking for a track…", "Ищем трек…")),
				st::boxLabel),
			st::boxRowPadding);

		const auto preview = container->add(
			object_ptr<Ui::RpWidget>(container),
			st::boxRowPadding);
		preview->widthValue(
		) | rpl::on_next([=](int width) {
			preview->resize(width, state->card.isNull()
				? 0
				: (width * state->card.height() / state->card.width()));
		}, preview->lifetime());
		preview->paintRequest(
		) | rpl::on_next([=] {
			if (state->card.isNull()) {
				return;
			}
			auto p = QPainter(preview);
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			p.drawImage(preview->rect(), state->card);
		}, preview->lifetime());

		const auto refresh = [=] {
			state->loading = true;
			FetchCurrentTrack(crl::guard(box, [=](Track track) {
				state->loading = false;
				state->track = std::move(track);
				state->card = state->track.active
					? RenderCard(state->track)
					: QImage();
				label->setText(state->track.active
					? (state->track.playing
						? state->track.line()
						: state->track.line() + Text(" (paused)", " (на паузе)"))
					: Text(
						"Nothing is playing. Start a track in Spotify, "
						"Yandex Music, a browser or any other player and "
						"press Refresh.",
						"Сейчас ничего не играет. Запустите трек в Spotify, "
						"Яндекс Музыке, браузере или другом плеере и "
						"нажмите «Обновить»."));
				preview->resize(preview->width(), state->card.isNull()
					? 0
					: (preview->width() * state->card.height()
						/ state->card.width()));
				preview->update();
			}));
		};

		const auto add = [&](
				const char *en,
				const char *ru,
				Fn<void()> handler,
				bool close) {
			Settings::AddButtonWithIcon(
				container,
				TextValue(en, ru),
				st::settingsButtonNoIcon
			)->addClickHandler([=] {
				handler();
				if (close) {
					box->closeBox();
				}
			});
		};
		add("Send as a picture", "Отправить картинкой", [=] {
			if (state->track.active && !state->card.isNull()) {
				SendCard(controller, action, *state);
			}
		}, true);
		add("Send as text", "Отправить текстом", [=] {
			if (state->track.active) {
				SendText(action, state->track);
			}
		}, true);
		add("Refresh", "Обновить", refresh, false);
		box->addButton(TextValue("Close", "Закрыть"), [=] {
			box->closeBox();
		});
		refresh();
	}));
}

} // namespace Prime::NowPlaying
