/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tgws/tgws_ws_client.h"

#include <QtCore/QTimer>
#include <QtNetwork/QSslSocket>

#include <stdexcept>

namespace Prime::TgWs {
namespace {

constexpr auto kDrainThreshold = qint64(256 * 1024);

} // namespace

WsClient::WsClient(QObject *parent)
: QObject(parent) {
}

WsClient::~WsClient() {
	if (_socket) {
		_socket->disconnect(this);
		_socket->abort();
	}
}

void WsClient::connectTo(const WsTarget &target) {
	if (_state != State::Idle) {
		return;
	}
	_state = State::Connecting;
	_host = target.host;
	_socket = new QSslSocket(this);
	_socket->setPeerVerifyName(target.host);
	_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
	_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);

	_timer = new QTimer(this);
	_timer->setSingleShot(true);
	connect(_timer, &QTimer::timeout, this, [=] {
		_timedOut = true;
		fail(QString::fromLatin1("timed out"));
	});
	_timer->start(target.timeoutMs);

	const auto path = target.path.isEmpty()
		? QString::fromLatin1("/apiws")
		: target.path;
	const auto host = target.host.toStdString();
	connect(_socket, &QSslSocket::encrypted, this, [=] {
		if (_state != State::Connecting) {
			return;
		}
		_state = State::Upgrading;
		const auto request = Ws::BuildUpgradeRequest(host, path.toStdString());
		_socket->write(request.text.data(), qint64(request.text.size()));
	});
	connect(_socket, &QSslSocket::readyRead, this, [=] {
		readyRead();
	});
	connect(_socket, &QSslSocket::bytesWritten, this, [=] {
		if (_state == State::Open
			&& _socket
			&& _socket->bytesToWrite() < kDrainThreshold
			&& onDrained) {
			onDrained();
		}
	});
	connect(_socket, &QSslSocket::errorOccurred, this, [=](
			QAbstractSocket::SocketError) {
		if (_state == State::Open) {
			finishClosed();
		} else {
			fail(_socket ? _socket->errorString() : QString());
		}
	});
	connect(_socket, &QSslSocket::disconnected, this, [=] {
		if (_state == State::Open) {
			finishClosed();
		} else if (_state == State::Connecting || _state == State::Upgrading) {
			fail(QString::fromLatin1("disconnected"));
		}
	});

	const auto sni = target.sni.isEmpty() ? target.host : target.sni;
	_socket->connectToHostEncrypted(target.ip, 443, sni);
}

void WsClient::send(const Bytes &data) {
	if (_state == State::Open) {
		writeFrame(Ws::kOpBinary, data);
	}
}

void WsClient::sendBatch(const std::vector<Bytes> &parts) {
	if (_state != State::Open) {
		return;
	}
	for (const auto &part : parts) {
		const auto frame = Ws::BuildFrame(Ws::kOpBinary, part);
		_socket->write(
			reinterpret_cast<const char*>(frame.data()),
			qint64(frame.size()));
	}
}

void WsClient::close() {
	const auto wasOpen = (_state == State::Open);
	_state = State::Closed;
	onOpened = nullptr;
	onFailed = nullptr;
	onMessage = nullptr;
	onClosed = nullptr;
	onDrained = nullptr;
	if (!_socket) {
		return;
	}
	if (wasOpen) {
		const auto frame = Ws::BuildFrame(Ws::kOpClose, nullptr, 0);
		_socket->write(
			reinterpret_cast<const char*>(frame.data()),
			qint64(frame.size()));
		_socket->flush();
	}
	_socket->abort();
}

void WsClient::setPaused(bool paused) {
	if (_paused == paused) {
		return;
	}
	_paused = paused;
	if (!_paused && _socket && _socket->bytesAvailable() > 0) {
		QTimer::singleShot(0, this, [=] {
			readyRead();
		});
	}
}

bool WsClient::isOpen() const {
	return (_state == State::Open);
}

qint64 WsClient::bytesToWrite() const {
	return _socket ? _socket->bytesToWrite() : 0;
}

const QString &WsClient::host() const {
	return _host;
}

qint64 WsClient::ageMs() const {
	using namespace std::chrono;
	return duration_cast<milliseconds>(steady_clock::now() - _created).count();
}

void WsClient::fail(
		const QString &reason,
		int status,
		const QString &location) {
	if (_state == State::Closed || _state == State::Open) {
		return;
	}
	_state = State::Closed;
	if (_timer) {
		_timer->stop();
	}
	if (_socket) {
		_socket->abort();
	}
	auto failure = WsFailure();
	failure.reason = reason;
	failure.httpStatus = status;
	failure.location = location;
	failure.timedOut = _timedOut;
	if (const auto callback = std::move(onFailed)) {
		onFailed = nullptr;
		callback(failure);
	}
}

void WsClient::readyRead() {
	if (!_socket || _paused) {
		return;
	}
	const auto data = _socket->readAll();
	if (data.isEmpty()) {
		return;
	}
	if (_state == State::Upgrading) {
		_head.insert(
			_head.end(),
			reinterpret_cast<const uint8_t*>(data.constData()),
			reinterpret_cast<const uint8_t*>(data.constData()) + data.size());
		processUpgrade();
	} else if (_state == State::Open) {
		_parser.append(
			reinterpret_cast<const uint8_t*>(data.constData()),
			size_t(data.size()));
		processFrames();
	}
}

void WsClient::processUpgrade() {
	const auto response = Ws::ParseUpgradeResponse(_head.data(), _head.size());
	if (!response) {
		return;
	}
	if (response->status != 101) {
		fail(
			QString::fromStdString(response->statusLine),
			response->status,
			QString::fromStdString(response->location));
		return;
	}
	if (_timer) {
		_timer->stop();
	}
	_state = State::Open;
	if (response->consumed < _head.size()) {
		_parser.append(
			_head.data() + response->consumed,
			_head.size() - response->consumed);
	}
	_head.clear();
	if (const auto callback = std::move(onOpened)) {
		onOpened = nullptr;
		callback();
	}
	if (_state == State::Open) {
		processFrames();
	}
}

void WsClient::processFrames() {
	while (_state == State::Open) {
		auto frame = std::optional<Ws::Frame>();
		try {
			frame = _parser.next();
		} catch (const std::exception &) {
			finishClosed();
			return;
		}
		if (!frame) {
			return;
		}
		switch (frame->opcode) {
		case Ws::kOpBinary:
		case Ws::kOpText:
			if (onMessage) {
				onMessage(QByteArray(
					reinterpret_cast<const char*>(frame->payload.data()),
					int(frame->payload.size())));
			}
			break;
		case Ws::kOpPing:
			writeFrame(Ws::kOpPong, frame->payload);
			break;
		case Ws::kOpClose: {
			auto echo = Bytes(
				frame->payload.begin(),
				frame->payload.begin()
					+ std::min<size_t>(2, frame->payload.size()));
			writeFrame(Ws::kOpClose, echo);
			finishClosed();
			return;
		}
		default:
			break;
		}
	}
}

void WsClient::finishClosed() {
	if (_state != State::Open) {
		return;
	}
	_state = State::Closed;
	if (_socket) {
		_socket->disconnectFromHost();
	}
	if (const auto callback = std::move(onClosed)) {
		onClosed = nullptr;
		callback();
	}
}

void WsClient::writeFrame(uint8_t opcode, const Bytes &payload) {
	if (!_socket) {
		return;
	}
	const auto frame = Ws::BuildFrame(opcode, payload);
	_socket->write(
		reinterpret_cast<const char*>(frame.data()),
		qint64(frame.size()));
}

} // namespace Prime::TgWs
