/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tgws/tgws_link.h"

#include <QtCore/QTimer>
#include <QtNetwork/QTcpSocket>

namespace Prime::TgWs {
namespace {

constexpr auto kSocketBuffer = 256 * 1024;
constexpr auto kDrainThreshold = qint64(256 * 1024);

} // namespace

WsLink::WsLink(WsPtr ws)
: _ws(std::move(ws)) {
	_ws->onMessage = [=](const QByteArray &data) {
		if (onData) {
			onData(data);
		}
	};
	_ws->onClosed = [=] {
		if (onClosed) {
			onClosed();
		}
	};
	_ws->onDrained = [=] {
		if (onDrained) {
			onDrained();
		}
	};
}

void WsLink::write(const Bytes &data) {
	_ws->send(data);
}

void WsLink::writeParts(const std::vector<Bytes> &parts) {
	_ws->sendBatch(parts);
}

qint64 WsLink::pending() const {
	return _ws->bytesToWrite();
}

void WsLink::setPaused(bool paused) {
	_ws->setPaused(paused);
}

void WsLink::close() {
	_ws.reset();
}

TcpLink::TcpLink(QTcpSocket *socket)
: _socket(socket) {
	_socket->setParent(nullptr);
	attach();
}

TcpLink::~TcpLink() {
	*_alive = false;
	if (_socket) {
		_socket->disconnect();
		_socket->abort();
		_socket->deleteLater();
	}
}

std::unique_ptr<TcpLink> TcpLink::Connect(
		const QString &host,
		int port,
		int timeoutMs,
		std::function<void(bool ok)> done) {
	auto socket = new QTcpSocket();
	auto link = std::make_unique<TcpLink>(socket);
	const auto finished = std::make_shared<bool>(false);
	const auto finish = [=](bool ok) {
		if (*finished) {
			return;
		}
		*finished = true;
		done(ok);
	};
	QObject::connect(socket, &QTcpSocket::connected, socket, [=] {
		finish(true);
	});
	QObject::connect(socket, &QTcpSocket::errorOccurred, socket, [=](
			QAbstractSocket::SocketError) {
		finish(false);
	});
	QTimer::singleShot(timeoutMs, socket, [=] {
		finish(false);
	});
	socket->connectToHost(host, quint16(port));
	return link;
}

void TcpLink::attach() {
	_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
	_socket->setReadBufferSize(kSocketBuffer);
	const auto alive = _alive;
	QTimer::singleShot(0, _socket, [=] {
		if (*alive && _socket && _socket->bytesAvailable() > 0) {
			readyRead();
		}
	});
	QObject::connect(_socket, &QTcpSocket::readyRead, _socket, [=] {
		readyRead();
	});
	QObject::connect(_socket, &QTcpSocket::bytesWritten, _socket, [=] {
		if (_socket && _socket->bytesToWrite() < kDrainThreshold && onDrained) {
			onDrained();
		}
	});
	const auto closed = [=] {
		if (_closedNotified) {
			return;
		}
		_closedNotified = true;
		if (onClosed) {
			onClosed();
		}
	};
	QObject::connect(_socket, &QTcpSocket::disconnected, _socket, closed);
	QObject::connect(_socket, &QTcpSocket::errorOccurred, _socket, [=](
			QAbstractSocket::SocketError error) {
		if (error != QAbstractSocket::RemoteHostClosedError) {
			closed();
		}
	});
}

void TcpLink::readyRead() {
	if (_paused || !_socket) {
		return;
	}
	const auto data = _socket->readAll();
	if (!data.isEmpty() && onData) {
		onData(data);
	}
}

void TcpLink::write(const Bytes &data) {
	if (_socket) {
		_socket->write(
			reinterpret_cast<const char*>(data.data()),
			qint64(data.size()));
	}
}

void TcpLink::writeParts(const std::vector<Bytes> &parts) {
	for (const auto &part : parts) {
		write(part);
	}
}

qint64 TcpLink::pending() const {
	return _socket ? _socket->bytesToWrite() : 0;
}

void TcpLink::setPaused(bool paused) {
	if (_paused == paused) {
		return;
	}
	_paused = paused;
	if (!_paused && _socket && _socket->bytesAvailable() > 0) {
		const auto alive = _alive;
		QTimer::singleShot(0, _socket, [=] {
			if (*alive) {
				readyRead();
			}
		});
	}
}

void TcpLink::close() {
	if (_socket) {
		_socket->disconnect();
		_socket->disconnectFromHost();
	}
}

} // namespace Prime::TgWs
