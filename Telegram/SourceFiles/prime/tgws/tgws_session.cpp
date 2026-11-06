/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tgws/tgws_session.h"

#include <QtCore/QRandomGenerator>
#include <QtCore/QTimer>

#include <algorithm>

namespace Prime::TgWs {
namespace {

constexpr auto kBackpressureBytes = qint64(1024 * 1024);
constexpr auto kRejectedLingerMs = 30'000;

enum FallbackStep {
	kStepWorker,
	kStepCfProxy,
	kStepTcp,
};

[[nodiscard]] QString PoolKey(int dcId, bool isMedia) {
	return QString::number(dcId) + (isMedia ? QString::fromLatin1("m") : QString());
}

[[nodiscard]] QStringList ToList(const std::vector<std::string> &values) {
	auto result = QStringList();
	for (const auto &value : values) {
		result.push_back(QString::fromStdString(value));
	}
	return result;
}

[[nodiscard]] QString WorkerPath(const QString &dst, int dcId) {
	return QString::fromLatin1("/apiws?dst=%1&dc=%2").arg(dst).arg(dcId);
}

} // namespace

Session::Session(Context &context, std::unique_ptr<Link> client, QString label)
: _context(context)
, _client(std::move(client))
, _label(std::move(label)) {
	++_context.stats.connectionsTotal;
	++_context.stats.connectionsActive;
	_timer.start();
}

Session::~Session() {
	*_alive = false;
}

void Session::start() {
	const auto alive = _alive;
	_client->onData = [=](const QByteArray &data) {
		if (*alive) {
			clientData(data);
		}
	};
	_client->onClosed = [=] {
		if (*alive) {
			clientClosed();
		}
	};
}

void Session::abort() {
	finish(QString::fromLatin1("engine stopped"));
}

void Session::clientData(const QByteArray &data) {
	const auto bytes = reinterpret_cast<const uint8_t*>(data.constData());
	switch (_phase) {
	case Phase::ReadingInit: {
		_initBuffer.insert(_initBuffer.end(), bytes, bytes + data.size());
		if (_initBuffer.size() < kHandshakeLen) {
			return;
		}
		auto handshake = Handshake();
		std::copy(
			_initBuffer.begin(),
			_initBuffer.begin() + kHandshakeLen,
			handshake.begin());
		_pending.assign(_initBuffer.begin() + kHandshakeLen, _initBuffer.end());
		_initBuffer.clear();
		acceptInit(handshake);
	} break;
	case Phase::Connecting:
		_pending.insert(_pending.end(), bytes, bytes + data.size());
		break;
	case Phase::Bridging:
		uplink(data);
		break;
	case Phase::Rejected:
	case Phase::Finished:
		break;
	}
}

void Session::clientClosed() {
	if (_phase == Phase::Bridging && _upstream && _splitter) {
		const auto tail = _splitter->flush();
		if (!tail.empty()) {
			_upstream->write(tail.front());
		}
	}
	finish(QString::fromLatin1("client closed"));
}

void Session::acceptInit(const Handshake &handshake) {
	_init = ParseClientInit(handshake, _context.config.secret);
	if (!_init) {
		++_context.stats.connectionsBad;
		log(QString::fromLatin1("bad handshake (wrong secret or protocol)"));
		_phase = Phase::Rejected;
		const auto alive = _alive;
		QTimer::singleShot(kRejectedLingerMs, [=] {
			if (*alive) {
				finish(QString::fromLatin1("rejected"));
			}
		});
		return;
	}
	const auto dcIdx = _init->isMedia ? -_init->dcId : _init->dcId;
	_relayInit = GenerateRelayInit(_init->protoTag, dcIdx);
	_crypto = std::make_unique<CryptoCtx>(
		BuildCryptoCtx(*_init, _context.config.secret, _relayInit));
	_splitter = std::make_unique<MsgSplitter>(_relayInit, _init->protoInt());
	_phase = Phase::Connecting;
	log(QString::fromLatin1("handshake ok: %1 proto=0x%2")
		.arg(dcTag())
		.arg(_init->protoInt(), 8, 16));
	route();
}

void Session::route() {
	const auto dc = _init->dcId;
	const auto &config = _context.config;
	const auto dcKey = std::to_string(dc) + (_init->isMedia ? "m" : "");
	const auto target = RedirectIpForDc(dc);
	const auto now = NowMs();
	const auto anyCf = config.fallbackCfProxy || !config.cfWorkerDomains.isEmpty();
	const auto failed = _context.ipFailUntil.find(target);
	const auto ipFailed = (failed != _context.ipFailUntil.end())
		&& (now < failed->second);

	if (target.empty()) {
		log(QString::fromLatin1("%1 has no direct route -> fallback").arg(dcTag()));
		startFallback();
	} else if (_context.wsBlacklist.count(dcKey)) {
		log(QString::fromLatin1("%1 WS blacklisted -> fallback").arg(dcTag()));
		startFallback();
	} else if (ipFailed && anyCf) {
		log(QString::fromLatin1("%1 WS connect to %2 timed out recently -> fallback")
			.arg(dcTag())
			.arg(QString::fromStdString(target)));
		startFallback();
	} else {
		tryDirect(QString::fromStdString(target), dcKey);
	}
}

void Session::tryDirect(const QString &target, const std::string &dcKey) {
	const auto now = NowMs();
	const auto dc = _init->dcId;
	const auto isMedia = _init->isMedia;
	const auto failing = _context.dcFailUntil.find(dcKey);
	const auto timeout = (failing != _context.dcFailUntil.end()
		&& now < failing->second)
		? kWsFailTimeoutMs
		: kWsConnectTimeoutMs;
	const auto frontingActive = (now < _context.frontingUntil);
	const auto domains = ToList(WsDomainsForDc(dc, isMedia));
	const auto key = PoolKey(dc, isMedia);
	const auto context = &_context;
	const auto alive = _alive;

	auto pooled = _context.wsPool.take(key, kWsPoolMaxAgeMs);
	_context.wsPool.refill(
		key,
		_context.config.poolSize,
		MakeDirectMaker(context, target, domains));

	const auto opened = [=](WsPtr ws) {
		_context.dcFailUntil.erase(dcKey);
		_context.ipFailUntil.erase(target.toStdString());
		++_context.stats.connectionsWs;
		_route = QString::fromLatin1("ws");
		bridge(std::make_unique<WsLink>(std::move(ws)), true);
	};

	if (pooled) {
		++_context.stats.poolHits;
		log(QString::fromLatin1("%1 -> pool hit via %2").arg(dcTag()).arg(target));
		opened(std::move(pooled));
		return;
	}
	++_context.stats.poolMisses;

	if (frontingActive) {
		log(QString::fromLatin1("%1 -> fronting / Host %2")
			.arg(dcTag())
			.arg(domains.front()));
		tryFronting(target, [=](WsPtr ws) {
			if (!*alive) {
				return;
			}
			if (ws) {
				opened(std::move(ws));
			} else {
				directFailed(target, dcKey, false, false, false);
			}
		});
		return;
	}

	struct State {
		int index = 0;
		bool timedOut = false;
		bool failedRedirect = false;
		bool allRedirects = true;
		std::function<void()> step;
	};
	const auto state = std::make_shared<State>();
	state->step = [=] {
		if (!*alive) {
			state->step = nullptr;
			return;
		}
		if (state->index >= domains.size()) {
			const auto timedOut = state->timedOut;
			const auto failedRedirect = state->failedRedirect;
			const auto allRedirects = state->allRedirects;
			state->step = nullptr;
			if (timedOut) {
				log(QString::fromLatin1("%1 -> fronting fallback").arg(dcTag()));
				tryFronting(target, [=](WsPtr ws) {
					if (!*alive) {
						return;
					}
					if (ws) {
						opened(std::move(ws));
					} else {
						directFailed(target, dcKey, true, failedRedirect, allRedirects);
					}
				});
			} else {
				directFailed(target, dcKey, false, failedRedirect, allRedirects);
			}
			return;
		}
		const auto host = domains[state->index++];
		log(QString::fromLatin1("%1 -> wss://%2/apiws via %3")
			.arg(dcTag())
			.arg(host)
			.arg(target));
		const auto holder = std::make_shared<WsPtr>(WsPtr(new WsClient()));
		auto wsTarget = WsTarget();
		wsTarget.ip = target;
		wsTarget.host = host;
		wsTarget.timeoutMs = timeout;
		(*holder)->onOpened = [=] {
			state->allRedirects = false;
			state->step = nullptr;
			auto ws = std::move(*holder);
			ws->onFailed = nullptr;
			if (*alive) {
				opened(std::move(ws));
			}
		};
		(*holder)->onFailed = [=](const WsFailure &failure) {
			// The client is closed later: closing it here would destroy this
			// very callback while it is still running.
			QTimer::singleShot(0, [holder] {
				holder->reset();
			});
			++_context.stats.wsErrors;
			if (failure.httpStatus) {
				if (failure.isRedirect()) {
					state->failedRedirect = true;
					log(QString::fromLatin1("%1 got %2 from %3 -> %4")
						.arg(dcTag())
						.arg(failure.httpStatus)
						.arg(host)
						.arg(failure.location));
				} else {
					state->allRedirects = false;
					log(QString::fromLatin1("%1 WS handshake: %2")
						.arg(dcTag())
						.arg(failure.reason));
				}
			} else if (failure.timedOut) {
				state->timedOut = true;
				log(QString::fromLatin1("%1 WS connect timed out via %2")
					.arg(dcTag())
					.arg(host));
				state->index = int(domains.size());
			} else {
				state->allRedirects = false;
				log(QString::fromLatin1("%1 WS connect failed: %2")
					.arg(dcTag())
					.arg(failure.reason));
			}
			if (const auto step = state->step) {
				step();
			}
		};
		(*holder)->connectTo(wsTarget);
	};
	state->step();
}

void Session::tryFronting(
		const QString &target,
		std::function<void(WsPtr)> done) {
	const auto domains = ToList(WsDomainsForDc(_init->dcId, _init->isMedia));
	auto plan = ConnectPlan();
	plan.base.ip = target;
	plan.base.sni = QString::fromLatin1(kFrontingSni);
	plan.base.timeoutMs = 5'000;
	plan.hosts = QStringList{ domains.front() };
	const auto context = &_context;
	ConnectFirst(std::move(plan), [=](WsPtr ws) {
		if (ws) {
			++context->stats.connectionsFronting;
			context->frontingUntil = NowMs() + kFrontingCooldownMs;
		} else {
			++context->stats.wsErrors;
			context->frontingUntil = 0;
		}
		done(std::move(ws));
	});
}

void Session::directFailed(
		const QString &target,
		const std::string &dcKey,
		bool timedOut,
		bool failedRedirect,
		bool allRedirects) {
	const auto now = NowMs();
	if (timedOut) {
		_context.ipFailUntil[target.toStdString()] = now + kIpFailCooldownMs;
	}
	if (failedRedirect && allRedirects) {
		_context.wsBlacklist.insert(dcKey);
		log(QString::fromLatin1("%1 blacklisted for WS (all redirects)").arg(dcTag()));
	} else {
		_context.dcFailUntil[dcKey] = now + kDcFailCooldownMs;
		log(QString::fromLatin1("%1 WS cooldown for %2s")
			.arg(dcTag())
			.arg(kDcFailCooldownMs / 1000));
	}
	startFallback();
}

void Session::startFallback() {
	const auto &config = _context.config;
	const auto hasDst = !DefaultIpForDc(_init->dcId).empty();
	_fallbackSteps.clear();
	_fallbackIndex = 0;
	if (!config.cfWorkerDomains.isEmpty() && hasDst) {
		_fallbackSteps.push_back(kStepWorker);
	}
	if (config.fallbackCfProxy) {
		_fallbackSteps.push_back(kStepCfProxy);
	}
	if (hasDst) {
		_fallbackSteps.push_back(kStepTcp);
	}
	nextFallback();
}

void Session::nextFallback() {
	if (_phase == Phase::Finished) {
		return;
	}
	if (_fallbackIndex >= _fallbackSteps.size()) {
		log(QString::fromLatin1("%1 no fallback available").arg(dcTag()));
		finish(QString::fromLatin1("no route"));
		return;
	}
	const auto alive = _alive;
	const auto done = [=](bool ok) {
		if (*alive && !ok) {
			nextFallback();
		}
	};
	switch (_fallbackSteps[_fallbackIndex++]) {
	case kStepWorker: tryWorkers(done); break;
	case kStepCfProxy: tryCfProxy(done); break;
	case kStepTcp: tryRawTcp(done); break;
	}
}

void Session::tryWorkers(std::function<void(bool)> done) {
	auto domains = _context.config.cfWorkerDomains;
	std::shuffle(
		domains.begin(),
		domains.end(),
		*QRandomGenerator::global());
	tryWorker(std::move(domains), 0, std::move(done));
}

void Session::tryWorker(
		QStringList domains,
		int index,
		std::function<void(bool)> done) {
	if (index >= domains.size()) {
		done(false);
		return;
	}
	const auto dc = _init->dcId;
	const auto worker = domains[index];
	const auto dst = QString::fromStdString(DefaultIpForDc(dc));
	const auto path = WorkerPath(dst, dc);
	const auto key = QString::fromLatin1("w:%1:%2").arg(dc).arg(worker);
	const auto alive = _alive;

	auto pooled = _context.workerPool.take(key, kCfWorkerPoolMaxAgeMs);
	_context.workerPool.refill(
		key,
		_context.config.poolSize,
		[=](WsReady ready) {
			auto plan = ConnectPlan();
			plan.base.path = path;
			plan.base.timeoutMs = 8'000;
			plan.hosts = QStringList{ worker };
			plan.ipIsHost = true;
			ConnectFirst(std::move(plan), std::move(ready));
		});

	const auto opened = [=](WsPtr ws) {
		++_context.stats.connectionsCfProxy;
		_route = QString::fromLatin1("cf worker");
		bridge(std::make_unique<WsLink>(std::move(ws)), true);
		done(true);
	};
	if (pooled) {
		log(QString::fromLatin1("%1 -> CF worker pool hit for %2")
			.arg(dcTag())
			.arg(dst));
		opened(std::move(pooled));
		return;
	}
	log(QString::fromLatin1("%1 -> trying CF worker %2 for %3")
		.arg(dcTag())
		.arg(worker)
		.arg(dst));
	auto plan = ConnectPlan();
	plan.base.path = path;
	plan.base.timeoutMs = 10'000;
	plan.hosts = QStringList{ worker };
	plan.ipIsHost = true;
	ConnectFirst(std::move(plan), [=](WsPtr ws) {
		if (!*alive) {
			return;
		}
		if (ws) {
			opened(std::move(ws));
		} else {
			log(QString::fromLatin1("%1 CF worker %2 failed")
				.arg(dcTag())
				.arg(worker));
			tryWorker(domains, index + 1, done);
		}
	});
}

void Session::tryCfProxy(std::function<void(bool)> done) {
	const auto dc = _init->dcId;
	const auto prefix = QString::fromLatin1("kws%1.").arg(dc == 203 ? 2 : dc);
	auto hosts = QStringList();
	for (const auto &base : _context.balancer.domainsForDc(dc)) {
		hosts.push_back(prefix + base);
	}
	if (hosts.isEmpty()) {
		done(false);
		return;
	}
	log(QString::fromLatin1("%1 -> trying CF proxy").arg(dcTag()));
	auto plan = ConnectPlan();
	plan.base.timeoutMs = 10'000;
	plan.hosts = hosts;
	plan.ipIsHost = true;
	plan.stopOnError = false;
	const auto alive = _alive;
	ConnectFirst(std::move(plan), [=](WsPtr ws) {
		if (!*alive) {
			return;
		}
		if (!ws) {
			log(QString::fromLatin1("%1 CF proxy failed").arg(dcTag()));
			done(false);
			return;
		}
		auto base = ws->host();
		if (base.startsWith(prefix)) {
			base.remove(0, prefix.size());
		}
		if (_context.balancer.setActiveForDc(dc, base)) {
			log(QString::fromLatin1("switched active CF domain to %1").arg(base));
		}
		++_context.stats.connectionsCfProxy;
		_route = QString::fromLatin1("cf proxy");
		bridge(std::make_unique<WsLink>(std::move(ws)), true);
		done(true);
	});
}

void Session::tryRawTcp(std::function<void(bool)> done) {
	const auto dst = QString::fromStdString(DefaultIpForDc(_init->dcId));
	log(QString::fromLatin1("%1 -> TCP fallback to %2:443").arg(dcTag()).arg(dst));
	const auto alive = _alive;
	const auto holder = std::make_shared<std::unique_ptr<TcpLink>>();
	*holder = TcpLink::Connect(dst, 443, 10'000, [=](bool ok) {
		if (!*alive) {
			return;
		}
		if (!ok) {
			log(QString::fromLatin1("%1 TCP fallback to %2 failed")
				.arg(dcTag())
				.arg(dst));
			QTimer::singleShot(0, [holder] {
				holder->reset();
			});
			done(false);
			return;
		}
		++_context.stats.connectionsTcpFallback;
		_route = QString::fromLatin1("tcp");
		bridge(std::move(*holder), false);
		done(true);
	});
}

void Session::bridge(std::unique_ptr<Link> upstream, bool useSplitter) {
	if (_phase == Phase::Finished) {
		return;
	}
	_phase = Phase::Bridging;
	_upstream = std::move(upstream);
	if (!useSplitter) {
		_splitter.reset();
	}
	const auto alive = _alive;
	_upstream->onData = [=](const QByteArray &data) {
		if (*alive) {
			downlink(data);
		}
	};
	_upstream->onClosed = [=] {
		if (*alive) {
			finish(QString::fromLatin1("upstream closed"));
		}
	};
	_upstream->onDrained = [=] {
		if (*alive) {
			_client->setPaused(false);
		}
	};
	_client->onDrained = [=] {
		if (*alive) {
			_upstream->setPaused(false);
		}
	};
	_upstream->write(Bytes(_relayInit.begin(), _relayInit.end()));
	if (!_pending.empty()) {
		auto pending = std::move(_pending);
		_pending.clear();
		uplink(QByteArray(
			reinterpret_cast<const char*>(pending.data()),
			int(pending.size())));
	}
}

void Session::uplink(const QByteArray &data) {
	if (!_upstream || data.isEmpty()) {
		return;
	}
	_bytesUp += data.size();
	_context.stats.bytesUp += data.size();
	const auto plain = _crypto->clientDecrypt->process(
		reinterpret_cast<const uint8_t*>(data.constData()),
		size_t(data.size()));
	auto encrypted = _crypto->relayEncrypt->process(plain);
	if (_splitter) {
		auto parts = _splitter->split(encrypted);
		if (parts.empty()) {
			return;
		} else if (parts.size() > 1) {
			_upstream->writeParts(parts);
		} else {
			_upstream->write(parts.front());
		}
	} else {
		_upstream->write(encrypted);
	}
	if (_upstream->pending() > kBackpressureBytes) {
		_client->setPaused(true);
	}
}

void Session::downlink(const QByteArray &data) {
	if (data.isEmpty()) {
		return;
	}
	_bytesDown += data.size();
	_context.stats.bytesDown += data.size();
	const auto plain = _crypto->relayDecrypt->process(
		reinterpret_cast<const uint8_t*>(data.constData()),
		size_t(data.size()));
	_client->write(_crypto->clientEncrypt->process(plain));
	if (_client->pending() > kBackpressureBytes) {
		_upstream->setPaused(true);
	}
}

void Session::finish(const QString &reason) {
	if (_phase == Phase::Finished) {
		return;
	}
	const auto wasBridging = (_phase == Phase::Bridging);
	_phase = Phase::Finished;
	*_alive = false;
	--_context.stats.connectionsActive;
	if (wasBridging) {
		log(QString::fromLatin1("%1 session closed (%2, %3): up %4 B down %5 B in %6 ms")
			.arg(dcTag())
			.arg(_route)
			.arg(reason)
			.arg(_bytesUp)
			.arg(_bytesDown)
			.arg(_timer.elapsed()));
	}
	auto links = std::make_shared<std::pair<
		std::unique_ptr<Link>,
		std::unique_ptr<Link>>>(std::move(_client), std::move(_upstream));
	// WHY: a link is destroyed from inside its own callback otherwise,
	// freeing the std::function that is still running.
	QTimer::singleShot(0, [links] {
	});
	if (const auto callback = std::move(onFinished)) {
		onFinished = nullptr;
		callback();
	}
}

QString Session::dcTag() const {
	if (!_init) {
		return QString::fromLatin1("DC?");
	}
	return QString::fromLatin1("DC%1%2")
		.arg(_init->dcId)
		.arg(_init->isMedia ? QString::fromLatin1(" media") : QString());
}

void Session::log(const QString &line) {
	_context.log.add(QString::fromLatin1("[%1] %2").arg(_label).arg(line));
}

} // namespace Prime::TgWs
