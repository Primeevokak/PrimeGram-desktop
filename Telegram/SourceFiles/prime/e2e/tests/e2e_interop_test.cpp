// Checks the desktop E2E port against the real PrimeGram Android classes.
// The Java side lives in tmp/e2e_java (Interop.java) and writes the vectors.
//
//   e2e_interop_test <mode> <dir>
//     s1a: C++ is Alice (requester), reads vec1.txt, writes cpp1.txt + cpp1_state.txt
//     s1b: reads java1.txt (messages after the ratchet turn)
//     s2a: C++ is Bob (replier), reads vec2.txt, writes cpp2.txt + cpp2_state.txt
//     s2b: reads java2.txt
#include <cstdint>
using uint64 = std::uint64_t;

#include "prime/e2e/e2e_crypto.h"
#include "prime/e2e/e2e_protocol.h"

#include <QtCore/QFile>
#include <QtCore/QMap>
#include <QtCore/QTextStream>

#include <cstdio>

using namespace Prime::E2E;

namespace {
QString operator""_q(const char16_t *text, size_t size) {
	return QString::fromUtf16(text, int(size));
}
} // namespace

namespace {

int failures = 0;

void Check(bool ok, const char *what) {
	std::printf("%s %s\n", ok ? "OK  " : "FAIL", what);
	if (!ok) {
		++failures;
	}
}

QByteArray H(const QString &text) {
	if (text == u"-"_q) {
		return QByteArray();
	} else if (text == u"."_q) {
		return QByteArray("");
	}
	return QByteArray::fromHex(text.toLatin1());
}

QString Hex(const QByteArray &data) {
	return data.isNull() ? u"-"_q : data.isEmpty() ? u"."_q : QString::fromLatin1(data.toHex());
}

using Lines = QMap<QString, QString>;

Lines Read(const QString &path) {
	auto result = Lines();
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		std::printf("cannot open %s\n", path.toUtf8().constData());
		std::exit(2);
	}
	const auto text = QString::fromUtf8(file.readAll());
	for (const auto &line : text.split('\n')) {
		const auto space = line.indexOf(' ');
		if (space > 0) {
			result.insert(line.left(space), line.mid(space + 1));
		}
	}
	return result;
}

void Write(const QString &path, const Lines &lines) {
	auto file = QFile(path);
	file.open(QIODevice::WriteOnly | QIODevice::Truncate);
	for (auto i = lines.begin(); i != lines.end(); ++i) {
		file.write((i.key() + ' ' + i.value() + '\n').toUtf8());
	}
}

void Dump(Lines &out, const QString &p, const Session &s) {
	out[p + "self"] = QString::number(s.selfUserId);
	out[p + "peer"] = QString::number(s.peerUserId);
	out[p + "peerIkSign"] = Hex(s.peerIkSignPub);
	out[p + "root"] = Hex(s.rootKey);
	out[p + "sendCk"] = Hex(s.sendChainKey);
	out[p + "sendN"] = QString::number(s.sendMessageNumber);
	out[p + "recvCk"] = Hex(s.recvChainKey);
	out[p + "recvN"] = QString::number(s.recvMessageNumber);
	out[p + "pn"] = QString::number(s.previousSendChainLength);
	out[p + "myPriv"] = Hex(s.myRatchetPriv);
	out[p + "myPub"] = Hex(s.myRatchetPub);
	out[p + "theirPub"] = Hex(s.theirRatchetPub);
	auto skipped = QString();
	for (const auto &[id, key] : s.skipped) {
		skipped += QString::fromLatin1(id) + '=' + Hex(key) + ',';
	}
	out[p + "skipped"] = skipped.isEmpty() ? u"."_q : skipped;
}

Session Load(const Lines &m, const QString &p) {
	auto s = Session();
	s.selfUserId = m[p + "self"].toULongLong();
	s.peerUserId = m[p + "peer"].toULongLong();
	s.mode = Session::Mode::Established;
	s.peerIkSignPub = H(m[p + "peerIkSign"]);
	s.rootKey = H(m[p + "root"]);
	s.sendChainKey = H(m[p + "sendCk"]);
	s.sendMessageNumber = m[p + "sendN"].toInt();
	s.recvChainKey = H(m[p + "recvCk"]);
	s.recvMessageNumber = m[p + "recvN"].toInt();
	s.previousSendChainLength = m[p + "pn"].toInt();
	s.myRatchetPriv = H(m[p + "myPriv"]);
	s.myRatchetPub = H(m[p + "myPub"]);
	s.theirRatchetPub = H(m[p + "theirPub"]);
	const auto skipped = m[p + "skipped"];
	if (skipped != u"."_q) {
		for (const auto &part : skipped.split(',', Qt::SkipEmptyParts)) {
			const auto eq = part.indexOf('=');
			s.skipped.emplace_back(part.left(eq).toLatin1(), H(part.mid(eq + 1)));
		}
	}
	return s;
}

QString Enc(Session &s, const QString &text) {
	return EncodeCiphertext(Encrypt(s, text.toUtf8()));
}

QString Dec(Session &s, const QString &framed) {
	auto decoded = DecodedCiphertext();
	if (!DecodeCiphertext(framed, decoded)) {
		return u"<bad frame>"_q;
	}
	const auto plain = Decrypt(
		s,
		decoded.header,
		decoded.nonce,
		decoded.ciphertext);
	return plain.isNull() ? u"<failed>"_q : QString::fromUtf8(plain);
}

struct Identity {
	KeyPair ikDh;
	KeyPair ikSign;
	KeyPair spk;
	QByteArray spkSig;
	std::vector<KeyPair> opks;
};

} // namespace

int main(int argc, char **argv) {
	if (argc < 3) {
		std::printf("usage: e2e_interop_test <mode> <dir>\n");
		return 2;
	}
	const auto mode = QString::fromLatin1(argv[1]);
	const auto dir = QString::fromLatin1(argv[2]);

	// Self test of the primitives against RFC vectors.
	{
		// RFC 5869 test case 1.
		const auto ikm = QByteArray::fromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
		const auto salt = QByteArray::fromHex("000102030405060708090a0b0c");
		const auto info = QByteArray::fromHex("f0f1f2f3f4f5f6f7f8f9");
		const auto okm = HkdfSha256(salt, ikm, info, 42);
		Check(okm.toHex() == "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865", "hkdf rfc5869 case 1");
		// RFC 7748 X25519 test vector.
		const auto a = QByteArray::fromHex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
		const auto bPub = QByteArray::fromHex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
		const auto shared = X25519(a, bPub);
		Check(shared.toHex() == "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", "x25519 rfc7748");
		// RFC 8032 test 1: empty message.
		const auto seed = QByteArray::fromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
		const auto sig = Ed25519Sign(seed, QByteArray());
		Check(sig.toHex() == "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", "ed25519 rfc8032 sign");
		const auto pub = QByteArray::fromHex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
		Check(Ed25519Verify(pub, QByteArray(), sig), "ed25519 verify");
		// AES-GCM round trip with a tampered tag.
		const auto key = RandomBytes(32);
		const auto nonce = RandomBytes(12);
		const auto sealed = AesGcmEncrypt(key, nonce, "aad", "hello");
		Check(AesGcmDecrypt(key, nonce, "aad", sealed) == "hello", "gcm round trip");
		auto bad = sealed;
		bad[0] = char(bad[0] ^ 1);
		Check(AesGcmDecrypt(key, nonce, "aad", bad).isNull(), "gcm rejects tamper");
		Check(!AesGcmDecrypt(key, nonce, "aad", AesGcmEncrypt(key, nonce, "aad", QByteArray())).isNull(), "gcm empty plaintext ok");
	}

	if (mode == u"s1a"_q) {
		auto v = Read(dir + "/vec1.txt");
		auto alice = Identity();
		alice.ikDh = { H(v["alice.ikDhPriv"]), H(v["alice.ikDhPub"]) };
		alice.ikSign = { H(v["alice.ikSignPriv"]), H(v["alice.ikSignPub"]) };
		alice.spk = { H(v["alice.spkPriv"]), H(v["alice.spkPub"]) };
		for (auto i = 0; v.contains("alice.opkPriv" + QString::number(i)); ++i) {
			alice.opks.push_back({
				H(v["alice.opkPriv" + QString::number(i)]),
				H(v["alice.opkPub" + QString::number(i)]),
			});
		}
		auto request = Bundle();
		Check(DecodeHandshake(v["request"], request), "decode java request bundle");
		Check(EncodeHandshake(request) == v["request"], "re-encode request is identical");
		Check(Ed25519Verify(request.ikSignPub, request.spkPub, request.spkSig), "java spk signature verifies");

		auto response = Bundle();
		Check(DecodeHandshake(v["response"], response), "decode java response bundle");
		Check(Ed25519Verify(response.ikSignPub, response.spkPub, response.spkSig), "java bob spk signature verifies");
		Check(EncodeHandshake(response) == v["response"], "re-encode response is identical");

		const auto opkPriv = alice.opks[response.consumedOpkIndex].priv;
		const auto dh1 = X25519(alice.spk.priv, response.ikDhPub);
		const auto dh2 = X25519(alice.ikDh.priv, response.ephemeralPub);
		const auto dh3 = X25519(alice.spk.priv, response.ephemeralPub);
		const auto dh4 = X25519(opkPriv, response.ephemeralPub);
		const auto sk = DeriveX3dhSecret(dh1, dh2, dh3, dh4);
		auto session = Session();
		session.selfUserId = v["aliceId"].toULongLong();
		session.peerUserId = v["bobId"].toULongLong();
		session.peerIkSignPub = response.ikSignPub;
		InitAsResponder(session, sk, alice.spk.priv, alice.spk.pub);

		Check(Dec(session, v["m0"]) == u"hello alice"_q, "cpp decrypts java message 0");
		Check(Dec(session, v["m1"]) == QString::fromUtf8("second: ünïcode ✓ второе"), "cpp decrypts java message 1 (unicode)");
		Check(Dec(session, v["m2"]).isEmpty(), "cpp decrypts java empty message");
		Check(VerificationCode(alice.ikSign.pub, response.ikSignPub, u"seed phrase"_q) == v["code"], "verification code matches java");

		auto out = Lines();
		out["a0"] = Enc(session, u"reply one"_q);
		out["a1"] = Enc(session, QString::fromUtf8("reply two ✓"));
		Write(dir + "/cpp1.txt", out);
		auto state = Lines();
		Dump(state, u"alice."_q, session);
		Write(dir + "/cpp1_state.txt", state);
	} else if (mode == u"s1b"_q) {
		const auto state = Read(dir + "/cpp1_state.txt");
		const auto j = Read(dir + "/java1.txt");
		auto session = Load(state, u"alice."_q);
		Check(Dec(session, j["b3"]) == u"after the ratchet turned"_q, "cpp decrypts java message after ratchet turn");
		Check(Dec(session, j["b4"]) == u"and one more"_q, "cpp decrypts the next one");
	} else if (mode == u"s2a"_q) {
		auto v = Read(dir + "/vec2.txt");
		// C++ is Bob: its own identity.
		auto bob = Identity();
		bob.ikDh = GenerateX25519();
		bob.ikSign = GenerateEd25519();
		bob.spk = GenerateX25519();
		bob.spkSig = Ed25519Sign(bob.ikSign.priv, bob.spk.pub);

		auto request = Bundle();
		Check(DecodeHandshake(v["request"], request), "decode java request");
		Check(Ed25519Verify(request.ikSignPub, request.spkPub, request.spkSig), "java request signature verifies");
		const auto ek = GenerateX25519();
		const auto consumed = request.opkIndices.front();
		const auto dh1 = X25519(bob.ikDh.priv, request.spkPub);
		const auto dh2 = X25519(ek.priv, request.ikDhPub);
		const auto dh3 = X25519(ek.priv, request.spkPub);
		const auto dh4 = X25519(ek.priv, request.opkPubs.front());
		const auto sk = DeriveX3dhSecret(dh1, dh2, dh3, dh4);
		auto session = Session();
		session.selfUserId = v["bobId"].toULongLong();
		session.peerUserId = v["aliceId"].toULongLong();
		session.peerIkSignPub = request.ikSignPub;
		InitAsInitiator(session, sk, request.spkPub);

		auto response = Bundle();
		response.subtype = kSubtypeResponse;
		response.ikDhPub = bob.ikDh.pub;
		response.ikSignPub = bob.ikSign.pub;
		response.spkPub = bob.spk.pub;
		response.spkSig = bob.spkSig;
		response.spkId = 3;
		response.consumedOpkIndex = consumed;
		response.ephemeralPub = ek.pub;

		auto out = Lines();
		out["response"] = EncodeHandshake(response);
		out["c0"] = Enc(session, u"hi from desktop"_q);
		out["c1"] = Enc(session, QString::fromUtf8("ünï ✓ два"));
		out["c2"] = Enc(session, u"third"_q);
		out["c3"] = Enc(session, u"fourth"_q);
		out["c4"] = Enc(session, u"fifth"_q);
		Write(dir + "/cpp2.txt", out);
		auto state = Lines();
		Dump(state, u"bob."_q, session);
		Write(dir + "/cpp2_state.txt", state);
	} else if (mode == u"s2b"_q) {
		const auto state = Read(dir + "/cpp2_state.txt");
		const auto j = Read(dir + "/java2.txt");
		auto session = Load(state, u"bob."_q);
		Check(Dec(session, j["a0"]) == u"java says one"_q, "cpp decrypts java alice message 0");
		Check(Dec(session, j["a1"]) == u"java says two"_q, "cpp decrypts java alice message 1");
	}
	std::printf(failures ? "FAILED: %d\n" : "ALL OK\n", failures);
	return failures ? 1 : 0;
}
