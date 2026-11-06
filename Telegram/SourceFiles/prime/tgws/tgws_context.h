/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "prime/tgws/tgws_balancer.h"
#include "prime/tgws/tgws_crypto.h"
#include "prime/tgws/tgws_ws_client.h"

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace Prime::TgWs {

struct WsDeleter {
	void operator()(WsClient *client) const;
};
using WsPtr = std::unique_ptr<WsClient, WsDeleter>;

struct Config {
	QString host = QString::fromLatin1("127.0.0.1");
	int port = 1443;
	Secret secret = {};
	int poolSize = 4;
	bool fallbackCfProxy = true;
	QStringList cfUserDomains;
	QStringList cfWorkerDomains;
};

struct Stats {
	uint64_t connectionsTotal = 0;
	uint64_t connectionsActive = 0;
	uint64_t connectionsWs = 0;
	uint64_t connectionsFronting = 0;
	uint64_t connectionsCfProxy = 0;
	uint64_t connectionsTcpFallback = 0;
	uint64_t connectionsBad = 0;
	uint64_t wsErrors = 0;
	uint64_t poolHits = 0;
	uint64_t poolMisses = 0;
	uint64_t bytesUp = 0;
	uint64_t bytesDown = 0;
};

class Log final {
public:
	static constexpr auto kMaxLines = 200;

	void add(const QString &line);
	[[nodiscard]] QStringList lines() const;
	void clear();

	std::function<void(const QString &)> onLine;

private:
	mutable std::mutex _mutex;
	std::deque<QString> _lines;

};

[[nodiscard]] qint64 NowMs();

using WsReady = std::function<void(WsPtr)>;
using WsMaker = std::function<void(WsReady)>;

class WsPool final {
public:
	WsPool();
	~WsPool();

	[[nodiscard]] WsPtr take(const QString &key, int maxAgeMs);
	void refill(const QString &key, int target, const WsMaker &maker);
	void reset();

private:
	void remove(const QString &key, const WsClient *client);

	std::map<QString, std::deque<WsPtr>> _idle;
	std::set<QString> _refilling;
	std::shared_ptr<bool> _alive;

};

struct Context {
	Config config;
	Balancer balancer;
	Stats stats;
	Log log;
	WsPool wsPool;
	WsPool workerPool;
	std::set<std::string> wsBlacklist;
	std::map<std::string, qint64> dcFailUntil;
	std::map<std::string, qint64> ipFailUntil;
	qint64 frontingUntil = 0;

	void reset();
};

struct ConnectPlan {
	WsTarget base;
	QStringList hosts;
	bool ipIsHost = false;
	bool stopOnError = true;
};

void ConnectFirst(ConnectPlan plan, WsReady done);

[[nodiscard]] WsMaker MakeDirectMaker(
	Context *context,
	const QString &target,
	const QStringList &hosts);

} // namespace Prime::TgWs
