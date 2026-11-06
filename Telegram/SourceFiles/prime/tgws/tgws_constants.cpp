/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tgws/tgws_constants.h"

namespace Prime::TgWs {

std::string DefaultIpForDc(int dcId) {
	switch (dcId) {
	case 1: return "149.154.175.50";
	case 2: return "149.154.167.51";
	case 3: return "149.154.175.100";
	case 4: return "149.154.167.91";
	case 5: return "149.154.171.5";
	case 203: return "91.105.192.100";
	}
	return {};
}

std::vector<std::string> WsDomainsForDc(int dcId, bool isMedia) {
	if (dcId == 203) {
		dcId = 2;
	}
	const auto base = "kws" + std::to_string(dcId) + ".web.telegram.org";
	const auto alt = "kws" + std::to_string(dcId) + "-1.web.telegram.org";
	return isMedia
		? std::vector<std::string>{ alt, base }
		: std::vector<std::string>{ base, alt };
}

std::string RedirectIpForDc(int dcId) {
	return (dcId == 2 || dcId == 4) ? std::string(kDcRedirectIp) : std::string();
}

} // namespace Prime::TgWs
