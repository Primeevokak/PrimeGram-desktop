"""Generates cross-check vectors for the C++ TgWs core from the ORIGINAL
Flowseal/tg-ws-proxy Python code.

    python gen_vectors.py <path to tg-ws-proxy checkout> <out file>

The "client" and "Telegram" ends are simulated with an independent AES
implementation (pycryptodome), so a mistake mirrored in both the original
and the port cannot go unnoticed.
"""
import hashlib
import os
import struct
import sys

from Crypto.Cipher import AES

repo, out_path = sys.argv[1], sys.argv[2]
sys.path.insert(0, repo)
from proxy.tg_ws_proxy import _try_handshake, _generate_relay_init, _build_crypto_ctx  # noqa: E402
from proxy.bridge import MsgSplitter  # noqa: E402


def ctr(key, iv, skip=0):
    cipher = AES.new(key, AES.MODE_CTR, nonce=b'', initial_value=int.from_bytes(iv, 'big'))
    if skip:
        cipher.encrypt(b'\x00' * skip)
    return cipher


def reserved(rnd):
    return (rnd[0] == 0xEF or bytes(rnd[:4]) in (
        b'HEAD', b'POST', b'GET ', b'\xee\xee\xee\xee', b'\xdd\xdd\xdd\xdd',
        b'\x16\x03\x01\x02') or bytes(rnd[4:8]) == b'\x00' * 4)


def client_init(secret, proto_tag, dc_idx):
    while True:
        rnd = bytearray(os.urandom(64))
        if not reserved(rnd):
            break
    key = hashlib.sha256(bytes(rnd[8:40]) + secret).digest()
    iv = bytes(rnd[40:56])
    keystream = ctr(key, iv).encrypt(bytes(rnd))
    tail = proto_tag + struct.pack('<h', dc_idx) + os.urandom(2)
    for i in range(8):
        rnd[56 + i] = tail[i] ^ (keystream[56 + i] ^ rnd[56 + i])
    return bytes(rnd)


def packets(proto, count):
    out = []
    for n in range(count):
        payload = os.urandom(8 * (n + 3))
        if proto == 0xEFEFEFEF:
            out.append(bytes([len(payload) // 4]) + payload)
        else:
            out.append(struct.pack('<I', len(payload)) + payload)
    return out


secret = os.urandom(16)
lines = ['secret ' + secret.hex()]

cases = [
    (b'\xdd\xdd\xdd\xdd', 0xDDDDDDDD, 2, False),
    (b'\xee\xee\xee\xee', 0xEEEEEEEE, 4, True),
    (b'\xef\xef\xef\xef', 0xEFEFEFEF, 5, False),
]
for index, (tag, proto, dc, media) in enumerate(cases):
    dc_idx = -dc if media else dc
    hs = client_init(secret, tag, dc_idx)
    parsed = _try_handshake(hs, secret)
    assert parsed and parsed[0] == dc and parsed[1] == media, parsed
    _, _, _, prekey_iv = parsed
    relay_init = _generate_relay_init(tag, dc_idx)
    ctx = _build_crypto_ctx(prekey_iv, secret, relay_init)

    # Uplink: independent client cipher, handshake already consumed.
    client_key = hashlib.sha256(hs[8:40] + secret).digest()
    client_cipher = ctr(client_key, hs[40:56], skip=64)
    up_plain = packets(proto, 3)
    up_client = [client_cipher.encrypt(p) for p in up_plain]
    up_tg = [ctx.tg_enc.update(ctx.clt_dec.update(c)) for c in up_client]
    splitter = MsgSplitter(relay_init, proto)
    joined = b''.join(up_tg)
    # Split in awkward chunks to exercise partial packets.
    cut = [0, 7, 7 + 13, len(joined)]
    parts = []
    for a, b in zip(cut, cut[1:]):
        parts += splitter.split(joined[a:b])
    parts += splitter.flush()
    assert b''.join(parts) == joined

    # Independent check that the relay accepts what we send.
    relay_check = ctr(relay_init[8:40], relay_init[40:56], skip=64)
    assert [relay_check.encrypt(p) for p in up_tg] == up_plain

    # Downlink.
    down_plain = packets(proto, 2)
    rev = relay_init[8:56][::-1]
    tg_cipher = ctr(rev[:32], rev[32:])
    down_tg = [tg_cipher.encrypt(p) for p in down_plain]
    down_client = [ctx.clt_enc.update(ctx.tg_dec.update(c)) for c in down_tg]
    crev = hs[8:56][::-1]
    ckey = hashlib.sha256(crev[:32] + secret).digest()
    client_down = ctr(ckey, crev[32:])
    assert [client_down.encrypt(c) for c in down_client] == down_plain

    p = 'c%d_' % index
    lines += [
        p + 'handshake ' + hs.hex(),
        p + 'dc %d' % dc,
        p + 'media %d' % int(media),
        p + 'proto %d' % proto,
        p + 'relay_init ' + relay_init.hex(),
        p + 'up_client ' + ','.join(c.hex() for c in up_client),
        p + 'up_tg ' + ','.join(c.hex() for c in up_tg),
        p + 'up_chunk_cuts 7,20',
        p + 'up_parts ' + ','.join(x.hex() for x in parts),
        p + 'down_tg ' + ','.join(c.hex() for c in down_tg),
        p + 'down_client ' + ','.join(c.hex() for c in down_client),
    ]

with open(out_path, 'w') as f:
    f.write('\n'.join(lines) + '\n')
print('wrote', out_path, len(lines), 'lines')
