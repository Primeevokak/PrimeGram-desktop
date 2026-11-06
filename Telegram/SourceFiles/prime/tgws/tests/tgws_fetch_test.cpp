#include "prime/tgws/tgws_balancer.h"
#include "prime/tgws/tgws_fetch.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>

#include <cstdio>

using namespace Prime::TgWs;

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	HttpsGetPinned(
		"185.199.109.133",
		"raw.githubusercontent.com",
		"/Flowseal/tg-ws-proxy/main/.github/cfproxy-domains.txt",
		10000,
		[&](QByteArray body) {
			std::printf("body bytes: %d\n", int(body.size()));
			const auto domains = ParseCfDomainList(body);
			std::printf("domains: %d\n", int(domains.size()));
			for (const auto &d : domains) {
				std::printf("  %s\n", d.toUtf8().constData());
			}
			app.quit();
		});
	return app.exec();
}
