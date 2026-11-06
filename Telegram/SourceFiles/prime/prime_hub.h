/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "prime/autodelete/autodelete_manager.h"
#include "prime/badges/badges.h"
#include "prime/e2e/e2e_manager.h"
#include "prime/tempsub/tempsub_manager.h"
#include "prime/whisper/whisper_manager.h"
#include "prime/mcp/prime_mcp_server.h"

namespace Prime {

// Owns the PrimeGram features that live for the whole app lifetime.
class Hub final {
public:
	void start() {
		_autoDelete.start();
		_tempSub.start();
		_badges.start();
		_e2e.start();
		Mcp::Server::Instance().start();
	}

	[[nodiscard]] AutoDelete::Manager &autoDelete() {
		return _autoDelete;
	}
	[[nodiscard]] TempSub::Manager &tempSub() {
		return _tempSub;
	}
	[[nodiscard]] Badges::Manager &badges() {
		return _badges;
	}
	[[nodiscard]] E2E::Manager &e2e() {
		return _e2e;
	}
	[[nodiscard]] Whisper::Manager &whisper() {
		return _whisper;
	}
	[[nodiscard]] Mcp::Server &mcp() {
		return Mcp::Server::Instance();
	}

private:
	AutoDelete::Manager _autoDelete;
	TempSub::Manager _tempSub;
	Whisper::Manager _whisper;
	Badges::Manager _badges;
	E2E::Manager _e2e;

};

} // namespace Prime
