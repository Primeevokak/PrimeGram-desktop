/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "prime/tgws/tgws_ws_frame.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>

#include <chrono>
#include <functional>
#include <memory>

class QSslSocket;
class QTimer;

namespace Prime::TgWs {

struct WsTarget {
	QString ip;
	QString sni;
	QString host;
	QString path;
	int timeoutMs = 10'000;
};

struct WsFailure {
	QString reason;
	int httpStatus = 0;
	QString location;
	bool timedOut = false;

	[[nodiscard]] bool isRedirect() const {
		return httpStatus == 301
			|| httpStatus == 302
			|| httpStatus == 303
			|| httpStatus == 307
			|| httpStatus == 308;
	}
};

class WsClient final : public QObject {
public:
	explicit WsClient(QObject *parent = nullptr);
	~WsClient();

	void connectTo(const WsTarget &target);
	void send(const Bytes &data);
	void sendBatch(const std::vector<Bytes> &parts);
	void close();
	void setPaused(bool paused);

	[[nodiscard]] bool isOpen() const;
	[[nodiscard]] qint64 bytesToWrite() const;
	[[nodiscard]] qint64 ageMs() const;
	[[nodiscard]] const QString &host() const;

	std::function<void()> onOpened;
	std::function<void(const WsFailure &)> onFailed;
	std::function<void(const QByteArray &)> onMessage;
	std::function<void()> onClosed;
	std::function<void()> onDrained;

private:
	enum class State {
		Idle,
		Connecting,
		Upgrading,
		Open,
		Closed,
	};

	void fail(const QString &reason, int status = 0, const QString &location = {});
	void readyRead();
	void processUpgrade();
	void processFrames();
	void finishClosed();
	void writeFrame(uint8_t opcode, const Bytes &payload);

	QPointer<QSslSocket> _socket;
	QTimer *_timer = nullptr;
	State _state = State::Idle;
	Bytes _head;
	Ws::FrameParser _parser;
	bool _timedOut = false;
	bool _paused = false;
	QString _host;
	std::chrono::steady_clock::time_point _created = std::chrono::steady_clock::now();

};

} // namespace Prime::TgWs
