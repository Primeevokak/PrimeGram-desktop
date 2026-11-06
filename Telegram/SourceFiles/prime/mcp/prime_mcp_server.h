#pragma once

#include <QtCore/QObject>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <rpl/variable.h>
#include <rpl/producer.h>

namespace Prime::Mcp {

class Server final : public QObject {
public:
	static Server &Instance();

	void start();
	void stop();
	void restart();

	[[nodiscard]] bool isRunning() const;
	[[nodiscard]] uint16 port() const;
	[[nodiscard]] rpl::producer<bool> isRunningValue() const;

	[[nodiscard]] bool enabled() const;
	void setEnabled(bool enabled);

	[[nodiscard]] bool allowSend() const;
	void setAllowSend(bool allow);

	[[nodiscard]] bool allowDelete() const;
	void setAllowDelete(bool allow);

	[[nodiscard]] QString configJson() const;

private:
	Server();

	void handleNewConnection();
	void processHttpRequest(QTcpSocket *socket, const QByteArray &data);
	void sendHttpResponse(
		QTcpSocket *socket,
		int statusCode,
		const QString &contentType,
		const QByteArray &body);
	QByteArray handleJsonRpc(const QJsonObject &request);
	QByteArray executeTool(const QString &name, const QJsonObject &args);

	QTcpServer _tcpServer;
	rpl::variable<bool> _running = false;
	uint16 _port = 28888;

};

} // namespace Prime::Mcp
