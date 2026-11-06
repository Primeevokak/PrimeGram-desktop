/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tgws/tgws_context.h"

#include <QtCore/QDateTime>
#include <QtCore/QTimer>

#include <algorithm>
#include <chrono>

namespace Prime::TgWs {
namespace {

struct ConnectState {
	ConnectPlan plan;
	WsReady done;
	int index = 0;
};

void TryNext(const std::shared_ptr<ConnectState> &state) {
	if (state->index >= state->plan.hosts.size()) {
		state->done(nullptr);
		return;
	}
	const auto host = state->plan.hosts[state->index++];
	auto holder = std::make_shared<WsPtr>(WsPtr(new WsClient()));
	const auto raw = holder->get();
	auto target = state->plan.base;
	target.host = host;
	if (state->plan.ipIsHost) {
		target.ip = host;
	}
	raw->onOpened = [state, holder] {
		auto ready = std::move(*holder);
		ready->onFailed = nullptr;
		state->done(std::move(ready));
	};
	raw->onFailed = [state, holder](const WsFailure &failure) {
		// Closing the client destroys this callback: do it after it returns.
		QTimer::singleShot(0, [holder] {
			holder->reset();
		});
		if (failure.isRedirect() || !state->plan.stopOnError) {
			TryNext(state);
		} else {
			state->done(nullptr);
		}
	};
	raw->connectTo(target);
}

} // namespace

void WsDeleter::operator()(WsClient *client) const {
	if (client) {
		client->close();
		client->deleteLater();
	}
}

void Log::add(const QString &line) {
	{
		auto lock = std::lock_guard(_mutex);
		_lines.push_back(line);
		while (_lines.size() > kMaxLines) {
			_lines.pop_front();
		}
	}
	if (onLine) {
		onLine(line);
	}
}

QStringList Log::lines() const {
	auto lock = std::lock_guard(_mutex);
	return QStringList(_lines.begin(), _lines.end());
}

void Log::clear() {
	auto lock = std::lock_guard(_mutex);
	_lines.clear();
}

qint64 NowMs() {
	using namespace std::chrono;
	return duration_cast<milliseconds>(
		steady_clock::now().time_since_epoch()).count();
}

WsPool::WsPool()
: _alive(std::make_shared<bool>(true)) {
}

WsPool::~WsPool() {
	*_alive = false;
}

WsPtr WsPool::take(const QString &key, int maxAgeMs) {
	auto &bucket = _idle[key];
	while (!bucket.empty()) {
		auto ws = std::move(bucket.front());
		bucket.pop_front();
		if (ws->isOpen() && ws->ageMs() <= maxAgeMs) {
			ws->onClosed = nullptr;
			return ws;
		}
	}
	return nullptr;
}

void WsPool::refill(const QString &key, int target, const WsMaker &maker) {
	if (_refilling.count(key) != 0) {
		return;
	}
	const auto needed = target - int(_idle[key].size());
	if (needed <= 0) {
		return;
	}
	_refilling.insert(key);
	const auto remaining = std::make_shared<int>(needed);
	const auto alive = _alive;
	for (auto i = 0; i != needed; ++i) {
		maker([=](WsPtr ws) {
			if (!*alive) {
				return;
			}
			if (ws) {
				const auto raw = ws.get();
				ws->onClosed = [=] {
					if (*alive) {
						remove(key, raw);
					}
				};
				_idle[key].push_back(std::move(ws));
			}
			if (--*remaining == 0) {
				_refilling.erase(key);
			}
		});
	}
}

void WsPool::remove(const QString &key, const WsClient *client) {
	const auto i = _idle.find(key);
	if (i == _idle.end()) {
		return;
	}
	auto &bucket = i->second;
	bucket.erase(
		std::remove_if(bucket.begin(), bucket.end(), [&](const WsPtr &ws) {
			return ws.get() == client;
		}),
		bucket.end());
}

void WsPool::reset() {
	_idle.clear();
	_refilling.clear();
}

void Context::reset() {
	wsPool.reset();
	workerPool.reset();
	wsBlacklist.clear();
	dcFailUntil.clear();
	ipFailUntil.clear();
	frontingUntil = 0;
}

void ConnectFirst(ConnectPlan plan, WsReady done) {
	auto state = std::make_shared<ConnectState>();
	state->plan = std::move(plan);
	state->done = std::move(done);
	TryNext(state);
}

WsMaker MakeDirectMaker(
		Context *context,
		const QString &target,
		const QStringList &hosts) {
	return [=](WsReady ready) {
		auto plan = ConnectPlan();
		plan.base.ip = target;
		plan.base.timeoutMs = 8'000;
		if (NowMs() < context->frontingUntil) {
			plan.base.sni = QString::fromLatin1(kFrontingSni);
		}
		plan.hosts = hosts;
		ConnectFirst(std::move(plan), std::move(ready));
	};
}

} // namespace Prime::TgWs
