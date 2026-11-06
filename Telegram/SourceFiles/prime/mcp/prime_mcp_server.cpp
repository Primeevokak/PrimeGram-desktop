#include "prime/mcp/prime_mcp_server.h"

#include "prime/prime_prefs.h"
#include "prime/prime_text.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_peer.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_indexed_list.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "api/api_sending.h"
#include "apiwrap.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QUrl>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QHostAddress>

namespace Prime::Mcp {
namespace {

constexpr auto kDefaultPort = 28888;
constexpr auto kPrefMcpEnabled = "mcp_enabled";
constexpr auto kPrefMcpAllowSend = "mcp_allow_send";
constexpr auto kPrefMcpAllowDelete = "mcp_allow_delete";
constexpr auto kPrefMcpPort = "mcp_port";

Main::Session *GetActiveSession() {
	if (!Core::App().domain().started()) {
		return nullptr;
	}
	return Core::App().domain().activeAccount().maybeSession();
}

PeerData *ResolvePeer(Main::Session *session, const QString &target) {
	if (!session || target.isEmpty()) {
		return nullptr;
	}
	auto cleaned = target.trimmed();
	if (cleaned.startsWith('@')) {
		cleaned = cleaned.mid(1);
	}

	bool ok = false;
	const auto idVal = cleaned.toLongLong(&ok);
	if (ok) {
		if (const auto peer = session->data().peerLoaded(PeerId(idVal))) {
			return peer;
		}
	}

	if (const auto peer = session->data().peerByUsername(cleaned)) {
		return peer;
	}

	// Search in loaded dialogs by name
	for (const auto &row : session->data().chatsList()->indexed()->all()) {
		const auto p = row->history()->peer;
		if (p->name().compare(cleaned, Qt::CaseInsensitive) == 0) {
			return p;
		}
	}
	return nullptr;
}

} // namespace

Server &Server::Instance() {
	static Server instance;
	return instance;
}

Server::Server() {
	_port = Prefs::GetInt(kPrefMcpPort, kDefaultPort);
	connect(&_tcpServer, &QTcpServer::newConnection, this, &Server::handleNewConnection);
}

void Server::start() {
	if (!enabled() || _running.current()) {
		return;
	}

	if (_tcpServer.listen(QHostAddress::LocalHost, _port)) {
		_running = true;
	}
}

void Server::stop() {
	if (_tcpServer.isListening()) {
		_tcpServer.close();
	}
	_running = false;
}

void Server::restart() {
	stop();
	start();
}

bool Server::isRunning() const {
	return _running.current();
}

uint16 Server::port() const {
	return _port;
}

rpl::producer<bool> Server::isRunningValue() const {
	return _running.value();
}

bool Server::enabled() const {
	return Prefs::GetBool(kPrefMcpEnabled, false);
}

void Server::setEnabled(bool enabled) {
	Prefs::SetBool(kPrefMcpEnabled, enabled);
	if (enabled) {
		start();
	} else {
		stop();
	}
}

bool Server::allowSend() const {
	return Prefs::GetBool(kPrefMcpAllowSend, false);
}

void Server::setAllowSend(bool allow) {
	Prefs::SetBool(kPrefMcpAllowSend, allow);
}

bool Server::allowDelete() const {
	return Prefs::GetBool(kPrefMcpAllowDelete, false);
}

void Server::setAllowDelete(bool allow) {
	Prefs::SetBool(kPrefMcpAllowDelete, allow);
}

QString Server::configJson() const {
	return QString(
		"{\n"
		"  \"mcpServers\": {\n"
		"    \"primegram\": {\n"
		"      \"url\": \"http://127.0.0.1:%1/mcp\"\n"
		"    }\n"
		"  }\n"
		"}"
	).arg(_port);
}

void Server::handleNewConnection() {
	while (const auto socket = _tcpServer.nextPendingConnection()) {
		connect(socket, &QTcpSocket::readyRead, this, [=] {
			const auto data = socket->readAll();
			processHttpRequest(socket, data);
		});
		connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
	}
}

void Server::processHttpRequest(QTcpSocket *socket, const QByteArray &data) {
	if (data.isEmpty()) {
		return;
	}

	const auto headerEnd = data.indexOf("\r\n\r\n");
	const auto headerBytes = (headerEnd != -1) ? data.left(headerEnd) : data;
	const auto bodyBytes = (headerEnd != -1) ? data.mid(headerEnd + 4) : QByteArray();

	const auto lines = headerBytes.split('\n');
	if (lines.isEmpty()) {
		sendHttpResponse(socket, 400, "text/plain", "Bad Request");
		return;
	}

	const auto requestLine = lines[0].trimmed().split(' ');
	if (requestLine.size() < 2) {
		sendHttpResponse(socket, 400, "text/plain", "Bad Request");
		return;
	}

	const auto method = requestLine[0];
	const auto path = requestLine[1];

	if (method == "OPTIONS") {
		sendHttpResponse(socket, 200, "text/plain", "");
		return;
	}

	if (method == "GET") {
		if (path == "/" || path == "/health" || path == "/ping") {
			QJsonObject status;
			status["status"] = "ok";
			status["server"] = "PrimeGram Desktop MCP";
			status["version"] = AppVersionStr;
			status["port"] = _port;
			status["connected"] = (GetActiveSession() != nullptr);
			sendHttpResponse(socket, 200, "application/json", QJsonDocument(status).toJson(QJsonDocument::Compact));
			return;
		}

		if (path == "/sse") {
			// SSE initialization stream for MCP clients
			const auto response =
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: text/event-stream\r\n"
				"Cache-Control: no-cache\r\n"
				"Connection: keep-alive\r\n"
				"Access-Control-Allow-Origin: *\r\n\r\n"
				"event: endpoint\r\n"
				"data: /mcp\r\n\r\n";
			socket->write(response);
			socket->flush();
			return;
		}
	}

	if (method == "POST") {
		const auto doc = QJsonDocument::fromJson(bodyBytes);
		if (!doc.isObject()) {
			sendHttpResponse(socket, 400, "application/json", "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}");
			return;
		}

		const auto responseJson = handleJsonRpc(doc.object());
		sendHttpResponse(socket, 200, "application/json", responseJson);
		return;
	}

	sendHttpResponse(socket, 404, "text/plain", "Not Found");
}

void Server::sendHttpResponse(
		QTcpSocket *socket,
		int statusCode,
		const QString &contentType,
		const QByteArray &body) {
	const auto statusText = (statusCode == 200) ? "OK" : (statusCode == 400) ? "Bad Request" : "Not Found";
	const auto header = QString(
		"HTTP/1.1 %1 %2\r\n"
		"Content-Type: %3; charset=utf-8\r\n"
		"Content-Length: %4\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
		"Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
		"Connection: close\r\n\r\n"
	).arg(statusCode).arg(statusText).arg(contentType).arg(body.size());

	socket->write(header.toUtf8());
	socket->write(body);
	socket->flush();
	socket->disconnectFromHost();
}

QByteArray Server::handleJsonRpc(const QJsonObject &request) {
	const auto id = request.value("id");
	const auto method = request.value("method").toString();

	QJsonObject response;
	response["jsonrpc"] = "2.0";
	response["id"] = id;

	if (method == "initialize") {
		QJsonObject result;
		result["protocolVersion"] = "2024-11-05";

		QJsonObject capabilities;
		capabilities["tools"] = QJsonObject();
		result["capabilities"] = capabilities;

		QJsonObject serverInfo;
		serverInfo["name"] = "PrimeGram Desktop MCP";
		serverInfo["version"] = AppVersionStr;
		result["serverInfo"] = serverInfo;

		response["result"] = result;
		return QJsonDocument(response).toJson(QJsonDocument::Compact);
	}

	if (method == "notifications/initialized") {
		return QJsonDocument(response).toJson(QJsonDocument::Compact);
	}

	if (method == "tools/list") {
		QJsonArray tools;

		// 1. telegram_get_me
		{
			QJsonObject tool;
			tool["name"] = "telegram_get_me";
			tool["description"] = "Get current logged-in Telegram user information (ID, name, username, phone).";
			tool["inputSchema"] = QJsonObject{ { "type", "object" }, { "properties", QJsonObject() } };
			tools.append(tool);
		}

		// 2. telegram_list_chats
		{
			QJsonObject tool;
			tool["name"] = "telegram_list_chats";
			tool["description"] = "List recent dialogs, groups, and channels from Telegram.";
			QJsonObject props;
			props["limit"] = QJsonObject{ { "type", "number" }, { "description", "Maximum number of chats (default 20)" } };
			tool["inputSchema"] = QJsonObject{ { "type", "object" }, { "properties", props } };
			tools.append(tool);
		}

		// 3. telegram_read_history
		{
			QJsonObject tool;
			tool["name"] = "telegram_read_history";
			tool["description"] = "Read recent messages from a specific chat by chat username, title or ID.";
			QJsonObject props;
			props["chat"] = QJsonObject{ { "type", "string" }, { "description", "Chat username (@name), title or numeric PeerId" } };
			props["limit"] = QJsonObject{ { "type", "number" }, { "description", "Number of messages to read (default 20)" } };
			tool["inputSchema"] = QJsonObject{
				{ "type", "object" },
				{ "properties", props },
				{ "required", QJsonArray{ "chat" } }
			};
			tools.append(tool);
		}

		// 4. telegram_search_messages
		{
			QJsonObject tool;
			tool["name"] = "telegram_search_messages";
			tool["description"] = "Search loaded Telegram messages by keyword or text query.";
			QJsonObject props;
			props["query"] = QJsonObject{ { "type", "string" }, { "description", "Text to search for" } };
			props["limit"] = QJsonObject{ { "type", "number" }, { "description", "Max results (default 20)" } };
			tool["inputSchema"] = QJsonObject{
				{ "type", "object" },
				{ "properties", props },
				{ "required", QJsonArray{ "query" } }
			};
			tools.append(tool);
		}

		// 5. telegram_send_message
		{
			QJsonObject tool;
			tool["name"] = "telegram_send_message";
			tool["description"] = "Send a text message to a specific Telegram chat (requires send permission).";
			QJsonObject props;
			props["chat"] = QJsonObject{ { "type", "string" }, { "description", "Chat username, title or numeric PeerId" } };
			props["text"] = QJsonObject{ { "type", "string" }, { "description", "Text of the message to send" } };
			tool["inputSchema"] = QJsonObject{
				{ "type", "object" },
				{ "properties", props },
				{ "required", QJsonArray{ "chat", "text" } }
			};
			tools.append(tool);
		}

		QJsonObject result;
		result["tools"] = tools;
		response["result"] = result;
		return QJsonDocument(response).toJson(QJsonDocument::Compact);
	}

	if (method == "tools/call") {
		const auto params = request.value("params").toObject();
		const auto name = params.value("name").toString();
		const auto args = params.value("arguments").toObject();

		return executeTool(name, args);
	}

	QJsonObject err;
	err["code"] = -32601;
	err["message"] = "Method not found";
	response["error"] = err;
	return QJsonDocument(response).toJson(QJsonDocument::Compact);
}

QByteArray Server::executeTool(const QString &name, const QJsonObject &args) {
	const auto session = GetActiveSession();
	if (!session) {
		QJsonObject res;
		res["isError"] = true;
		res["content"] = QJsonArray{ QJsonObject{ { "type", "text" }, { "text", "Error: No active Telegram session. Please log in first." } } };
		return QJsonDocument(res).toJson(QJsonDocument::Compact);
	}

	if (name == "telegram_get_me") {
		const auto me = session->user();
		QJsonObject data;
		data["id"] = QString::number(me->id.value);
		data["name"] = me->name();
		data["username"] = me->username();
		data["phone"] = me->phone();

		QJsonObject res;
		res["content"] = QJsonArray{ QJsonObject{ { "type", "text" }, { "text", QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Indented)) } } };
		return QJsonDocument(res).toJson(QJsonDocument::Compact);
	}

	if (name == "telegram_list_chats") {
		const auto limit = args.value("limit").toInt(20);
		QJsonArray chats;

		for (const auto &row : session->data().chatsList()->indexed()->all()) {
			const auto p = row->history()->peer;
			QJsonObject item;
			item["id"] = QString::number(p->id.value);
			item["title"] = p->name();
			if (!p->username().isEmpty()) {
				item["username"] = p->username();
			}
			item["is_channel"] = p->isChannel();
			item["is_group"] = p->isChat() || p->isMegagroup();
			item["is_user"] = p->isUser();
			item["unread_count"] = row->history()->unreadCount();
			chats.append(item);

			if (chats.size() >= limit) {
				break;
			}
		}

		QJsonObject res;
		res["content"] = QJsonArray{ QJsonObject{ { "type", "text" }, { "text", QString::fromUtf8(QJsonDocument(chats).toJson(QJsonDocument::Indented)) } } };
		return QJsonDocument(res).toJson(QJsonDocument::Compact);
	}

	if (name == "telegram_read_history") {
		const auto chat = args.value("chat").toString();
		const auto limit = args.value("limit").toInt(20);
		const auto peer = ResolvePeer(session, chat);

		if (!peer) {
			QJsonObject res;
			res["isError"] = true;
			res["content"] = QJsonArray{ QJsonObject{ { "type", "text" }, { "text", QString("Chat not found: %1").arg(chat) } } };
			return QJsonDocument(res).toJson(QJsonDocument::Compact);
		}

		const auto history = session->data().history(peer);
		QJsonArray messages;
		auto count = 0;

		for (auto b = history->blocks.rbegin(); b != history->blocks.rend(); ++b) {
			for (auto m = (*b)->messages.rbegin(); m != (*b)->messages.rend(); ++m) {
				const auto item = (*m)->data();
				if (!item) {
					continue;
				}
				QJsonObject msg;
				msg["id"] = int(item->id.bare);
				msg["date"] = int(item->date());
				msg["from"] = item->from() ? item->from()->name() : "";
				msg["text"] = item->originalText().text;
				msg["out"] = item->out();
				messages.append(msg);

				if (++count >= limit) {
					break;
				}
			}
			if (count >= limit) {
				break;
			}
		}

		QJsonObject res;
		res["content"] = QJsonArray{ QJsonObject{ { "type", "text" }, { "text", QString::fromUtf8(QJsonDocument(messages).toJson(QJsonDocument::Indented)) } } };
		return QJsonDocument(res).toJson(QJsonDocument::Compact);
	}

	if (name == "telegram_search_messages") {
		const auto query = args.value("query").toString();
		const auto limit = args.value("limit").toInt(20);
		QJsonArray results;
		auto count = 0;

		for (const auto &row : session->data().chatsList()->indexed()->all()) {
			const auto history = row->history();
			for (auto b = history->blocks.rbegin(); b != history->blocks.rend(); ++b) {
				for (auto m = (*b)->messages.rbegin(); m != (*b)->messages.rend(); ++m) {
					const auto item = (*m)->data();
					if (!item) {
						continue;
					}
					const auto text = item->originalText().text;
					if (text.contains(query, Qt::CaseInsensitive)) {
						QJsonObject hit;
						hit["chat_id"] = QString::number(history->peer->id.value);
						hit["chat_title"] = history->peer->name();
						hit["message_id"] = int(item->id.bare);
						hit["from"] = item->from() ? item->from()->name() : "";
						hit["date"] = int(item->date());
						hit["text"] = text;
						results.append(hit);

						if (++count >= limit) {
							break;
						}
					}
				}
				if (count >= limit) {
					break;
				}
			}
			if (count >= limit) {
				break;
			}
		}

		QJsonObject res;
		res["content"] = QJsonArray{ QJsonObject{ { "type", "text" }, { "text", QString::fromUtf8(QJsonDocument(results).toJson(QJsonDocument::Indented)) } } };
		return QJsonDocument(res).toJson(QJsonDocument::Compact);
	}

	if (name == "telegram_send_message") {
		if (!allowSend()) {
			QJsonObject res;
			res["isError"] = true;
			res["content"] = QJsonArray{ QJsonObject{ { "type", "text" }, { "text", "Permission denied: Sending messages via MCP is disabled in PrimeGram settings." } } };
			return QJsonDocument(res).toJson(QJsonDocument::Compact);
		}

		const auto chat = args.value("chat").toString();
		const auto text = args.value("text").toString();
		const auto peer = ResolvePeer(session, chat);

		if (!peer) {
			QJsonObject res;
			res["isError"] = true;
			res["content"] = QJsonArray{ QJsonObject{ { "type", "text" }, { "text", QString("Chat not found: %1").arg(chat) } } };
			return QJsonDocument(res).toJson(QJsonDocument::Compact);
		}

		const auto history = session->data().history(peer);
		auto message = Api::MessageToSend(Api::SendAction(history));
		message.textWithTags = { text };
		session->api().sendMessage(std::move(message));

		QJsonObject res;
		res["content"] = QJsonArray{ QJsonObject{ { "type", "text" }, { "text", QString("Message sent to %1").arg(peer->name()) } } };
		return QJsonDocument(res).toJson(QJsonDocument::Compact);
	}

	QJsonObject res;
	res["isError"] = true;
	res["content"] = QJsonArray{ QJsonObject{ { "type", "text" }, { "text", QString("Unknown tool: %1").arg(name) } } };
	return QJsonDocument(res).toJson(QJsonDocument::Compact);
}

} // namespace Prime::Mcp
