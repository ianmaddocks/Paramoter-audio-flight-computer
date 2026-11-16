#!/usr/bin/env python3
"""Reference implementation of the ParaVario sensor API's message authentication (protocol 1).

The owner opens pairing on the vario, which shows a random 64-bit pairing secret (16 lowercase hex characters). The owner gives it to the
client out of band (typing it in); it is never sent over the air. Both sides use it directly as the key of an HMAC-SHA256 that is attached to
every request, in both directions. This file lets a sensor project check its implementation against known results without access to the vario source:
`python pv_crypto.py` prints the test vectors given in ../README.md section 6. Python standard library only.
"""
import hashlib
import hmac

NO_SESSION = "00000000"  # the session field of requests made before a session exists (pair, register)


def _hmac(key: str, msg: bytes) -> bytes:
    return hmac.new(key.encode("ascii"), msg, hashlib.sha256).digest()


def request_mac(secret: str, method: str, path: str, session: str, counter: int, body: bytes) -> str:
    """First 8 bytes of HMAC-SHA256(secret, METHOD LF path LF session LF counter LF body) as 16 lowercase hex characters.
    `path` includes the query string exactly as sent; for a request with no body, body is empty."""
    return _hmac(secret, f"{method}\n{path}\n{session}\n{counter}\n".encode("ascii") + body)[:8].hex()


def auth_header(device_id: str, secret: str, method: str, path: str, session: str, counter: int, body: bytes) -> str:
    """Value of the X-PV-Auth header: <device_id>:<session>:<counter>:<mac>."""
    return f"{device_id}:{session}:{counter}:{request_mac(secret, method, path, session, counter, body)}"


def pair_reply_mac(secret: str, nonce_s: str, device_id: str, vario_id: str) -> str:
    """The vario's proof in the pair response (16 hex): a vario that was not shown the secret cannot produce it."""
    return _hmac(secret, f"PVP1-pair|{nonce_s}|{device_id}|{vario_id}".encode("ascii"))[:8].hex()


def register_reply_mac(secret: str, nonce_s: str, session: str) -> str:
    """The vario's proof in the register response (16 hex)."""
    return _hmac(secret, f"PVP1-reg|{nonce_s}|{session}".encode("ascii"))[:8].hex()


# ---- test vectors (the values in README.md section 6)
VECTOR = dict(
    secret="7f3a9c2e5b1d4860",
    vario_id="ac276e8112b8",
    device_id="5ccf7f0a1b2c",
    nonce_s="00112233445566778899aabbccddeeff",
    session="c0ffee11",
)


def vectors():
    v = VECTOR
    pair_body = b'{"device_id":"5ccf7f0a1b2c","type":"egt","proto":{"min":1,"max":1},"nonce_s":"00112233445566778899aabbccddeeff"}'
    body = b'{"boot":"0a1b2c3d","rec":[[1041,5000,0,6153,0]]}'
    return {
        "pair_body": pair_body.decode(),
        "pair_header": auth_header(v["device_id"], v["secret"], "POST", "/api/v1/pair", NO_SESSION, 0, pair_body),
        "pair_reply_mac": pair_reply_mac(v["secret"], v["nonce_s"], v["device_id"], v["vario_id"]),
        "register_reply_mac": register_reply_mac(v["secret"], v["nonce_s"], v["session"]),
        "telemetry_body": body.decode(),
        "telemetry_header": auth_header(v["device_id"], v["secret"], "POST", "/api/v1/sensors/telemetry", v["session"], 1, body),
        # vario -> sensor: the sender id is the vario's id, the counter is the vario's own (starts at 1 after register)
        "history_header": auth_header(v["vario_id"], v["secret"], "GET", "/api/v1/records?epoch=1a2b3c4d&after=1040&max=32",
                                      v["session"], 1, b""),
    }


if __name__ == "__main__":
    for name, value in vectors().items():
        print(f"{name}: {value}")
