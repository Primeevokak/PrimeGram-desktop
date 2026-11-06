#include "prime/tgws/tgws_balancer.h"

#include <QtCore/QCoreApplication>

#include <cstdio>

using namespace Prime::TgWs;

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	const auto expected = QStringList{
		"pclead.co.uk", "offshor.co.uk", "cakeisalie.co.uk",
		"noskomnadzor.co.uk", "lovetrue.co.uk", "sorokdva.co.uk",
		"pyatdesyatdva.co.uk", "kartoshka.co.uk", "sorokodin.co.uk",
		"pyatdesyatodin.co.uk", "notelega.co.uk", "ebally.co.uk",
		"nebally.co.uk", "havegreatday.co.uk", "pomogite.co.uk",
		"fixtelega.co.uk", "sadnews.co.uk", "onedaychamp.co.uk",
		"stopblocking.co.uk", "nothingthere.co.uk",
	};
	auto failures = 0;
	const auto got = DefaultCfDomains();
	if (got != expected) {
		++failures;
		std::printf("FAIL default domains\n got: %s\n", got.join(',').toUtf8().constData());
	}
	if (DecodeCfDomain("plain.example") != "plain.example") {
		++failures;
		std::printf("FAIL passthrough\n");
	}
	const auto list = ParseCfDomainList("# comment\n\nvirkgj.com\r\nvmmzovy.com\n");
	if (list != QStringList{ "pclead.co.uk", "offshor.co.uk" }) {
		++failures;
		std::printf("FAIL list parse: %s\n", list.join(',').toUtf8().constData());
	}
	if (CoerceDomainList("a.com, B.com;a.com  c.com") != QStringList{ "a.com", "B.com", "c.com" }) {
		++failures;
		std::printf("FAIL coerce\n");
	}
	Balancer b;
	b.setDomains(expected);
	auto order = b.domainsForDc(2);
	const auto total = order.size();
	order.removeDuplicates();
	if (total != expected.size() || order.size() != expected.size()) {
		++failures;
		std::printf("FAIL balancer order\n");
	}
	if (!b.setActiveForDc(2, "x.co.uk") || b.setActiveForDc(2, "x.co.uk")) {
		++failures;
		std::printf("FAIL setActive\n");
	}
	std::printf(failures ? "FAILED: %d\n" : "ALL OK\n", failures);
	return failures ? 1 : 0;
}
