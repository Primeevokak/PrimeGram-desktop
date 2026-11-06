// End-to-end check against the real Telegram datacenters through the local
// MTProxy: obfuscated2 handshake with our secret, then an unencrypted
// req_pq_multi; a resPQ coming back proves the whole path works.
//
//   tgws_proxy_test.exe [dc ...]     (default: 2 4 1)
#include "prime/tgws/tgws_engine.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpSocket>

#include <cstdio>

using namespace Prime::TgWs;

namespace {

Bytes ClientInit(const Secret &secret, int dc, std::array<uint8_t, 4> tag, AesCtr **encOut, AesCtr **decOut) {
	auto init = Handshake();
	while (true) {
		RandomBytes(init.data(), init.size());
		if (init[0] != 0xEF && !(init[4] == 0 && init[5] == 0 && init[6] == 0 && init[7] == 0)
			&& !(init[0] == 0xEE && init[1] == 0xEE) && !(init[0] == 0xDD && init[1] == 0xDD)) {
			break;
		}
	}
	auto keyInput = Bytes(init.begin() + kSkipLen, init.begin() + kSkipLen + kPrekeyLen);
	keyInput.insert(keyInput.end(), secret.begin(), secret.end());
	const auto key = Sha256(keyInput.data(), keyInput.size());
	auto enc = new AesCtr(key.data(), init.data() + kSkipLen + kPrekeyLen);

	auto plain = Handshake();
	auto probe = AesCtr(key.data(), init.data() + kSkipLen + kPrekeyLen);
	probe.process(init.data(), plain.data(), kHandshakeLen);
	auto tail = std::array<uint8_t, 8>{ tag[0], tag[1], tag[2], tag[3], uint8_t(dc & 0xFF), uint8_t(dc >> 8), 0, 0 };
	RandomBytes(tail.data() + 6, 2);
	auto result = init;
	for (auto i = 0; i != 8; ++i) {
		const auto keystream = plain[kProtoTagPos + i] ^ init[kProtoTagPos + i];
		result[kProtoTagPos + i] = tail[i] ^ uint8_t(keystream);
	}
	auto out = Bytes(result.begin(), result.end());

	enc->skip(kHandshakeLen);
	*encOut = enc;

	auto reversed = Bytes(init.begin() + kSkipLen, init.begin() + kSkipLen + kPrekeyLen + kIvLen);
	std::reverse(reversed.begin(), reversed.end());
	auto decKeyInput = Bytes(reversed.begin(), reversed.begin() + kPrekeyLen);
	decKeyInput.insert(decKeyInput.end(), secret.begin(), secret.end());
	const auto decKey = Sha256(decKeyInput.data(), decKeyInput.size());
	*decOut = new AesCtr(decKey.data(), reversed.data() + kPrekeyLen);
	return out;
}

bool TestDc(Engine &engine, int dc, int timeoutMs) {
	auto secretCopy = Secret();
	const auto hex = engine.secretHex().toLatin1();
	const auto raw = QByteArray::fromHex(hex);
	std::copy(raw.begin(), raw.end(), secretCopy.begin());

	AesCtr *enc = nullptr;
	AesCtr *dec = nullptr;
	const auto init = ClientInit(secretCopy, dc, kProtoTagIntermediate, &enc, &dec);

	auto payload = Bytes(40, 0);
	const auto msgId = (uint64_t(QDateTime::currentSecsSinceEpoch()) << 32) & ~uint64_t(3);
	for (auto i = 0; i != 8; ++i) {
		payload[8 + i] = uint8_t(msgId >> (8 * i));
	}
	payload[16] = 20;
	const auto ctor = std::array<uint8_t, 4>{ 0xf1, 0x8e, 0x7e, 0xbe };
	std::copy(ctor.begin(), ctor.end(), payload.begin() + 20);
	RandomBytes(payload.data() + 24, 16);
	auto packet = Bytes{ 40, 0, 0, 0 };
	packet.insert(packet.end(), payload.begin(), payload.end());
	const auto encrypted = enc->process(packet);

	QTcpSocket socket;
	QEventLoop loop;
	auto response = QByteArray();
	auto ok = false;
	QObject::connect(&socket, &QTcpSocket::connected, [&] {
		socket.write(reinterpret_cast<const char*>(init.data()), qint64(init.size()));
		socket.write(reinterpret_cast<const char*>(encrypted.data()), qint64(encrypted.size()));
	});
	QObject::connect(&socket, &QTcpSocket::readyRead, [&] {
		response.append(socket.readAll());
		if (response.size() >= 4) {
			const auto plain = dec->process(
				reinterpret_cast<const uint8_t*>(response.constData()),
				size_t(response.size()));
			if (plain.size() >= 28) {
				ok = plain[24] == 0x63 && plain[25] == 0x24
					&& plain[26] == 0x16 && plain[27] == 0x05;
				loop.quit();
			}
		}
	});
	QObject::connect(&socket, &QTcpSocket::disconnected, [&] { loop.quit(); });
	QTimer::singleShot(timeoutMs, &loop, [&] { loop.quit(); });
	socket.connectToHost("127.0.0.1", quint16(engine.boundPort()));
	loop.exec();
	std::printf("DC%d: %s (received %d bytes)\n", dc, ok ? "resPQ OK" : "NO RESPONSE", int(response.size()));
	delete enc;
	delete dec;
	return ok;
}

} // namespace

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	auto dcs = std::vector<int>();
	for (auto i = 1; i < argc; ++i) {
		dcs.push_back(std::atoi(argv[i]));
	}
	if (dcs.empty()) {
		dcs = { 2, 4, 1 };
	}

	Engine engine;
	engine.onLogLine = [](const QString &line) {
		std::printf("  | %s\n", line.toUtf8().constData());
		std::fflush(stdout);
	};
	auto config = Config();
	config.port = 14430;
	config.secret = RandomSecret();
	engine.start(config);

	QEventLoop wait;
	QTimer poll;
	QObject::connect(&poll, &QTimer::timeout, [&] {
		if (engine.state() == EngineState::Running || engine.state() == EngineState::Failed) {
			wait.quit();
		}
	});
	poll.start(50);
	QTimer::singleShot(5000, &wait, [&] { wait.quit(); });
	wait.exec();
	poll.stop();
	if (engine.state() != EngineState::Running) {
		std::printf("engine did not start: %s\n", engine.lastError().toUtf8().constData());
		return 3;
	}
	std::printf("proxy link: %s\n", engine.proxyLink().toUtf8().constData());

	auto failures = 0;
	for (const auto dc : dcs) {
		if (!TestDc(engine, dc, 25'000)) {
			++failures;
		}
	}
	const auto stats = engine.stats();
	std::printf("stats: total=%llu ws=%llu cf=%llu tcp=%llu bad=%llu errors=%llu\n",
		(unsigned long long)stats.connectionsTotal, (unsigned long long)stats.connectionsWs,
		(unsigned long long)stats.connectionsCfProxy, (unsigned long long)stats.connectionsTcpFallback,
		(unsigned long long)stats.connectionsBad, (unsigned long long)stats.wsErrors);
	engine.stop();
	std::printf(failures ? "FAILED: %d\n" : "ALL OK\n", failures);
	return failures ? 1 : 0;
}
