#!/usr/bin/env python3
"""Создаёт воспроизводимые тестовые пакеты CRX3."""
import argparse
import base64
import hashlib
import io
import struct
import sys
import zipfile
from pathlib import Path

try:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec, padding
except ImportError as exc:
    raise SystemExit(
        "Для фикстур нужен пакет cryptography (pip install 'cryptography>=43')"
    ) from exc

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "tests" / "data"
CONTEXT = b"CRX3 SignedData"
KNOWN_EXTENSION_ID = "jifpbeccnghkjeaalbbjmodiffmgedin"

# Эти ключи принадлежат только тестовым фикстурам, не использовать в приложении.
RSA_PRIVATE_KEY_PEM = b"""-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDrxBMTk2a/5yFB
Rm6QUdjhD06HRHEQCW0y3Fx5Z/f5sxfnL+QngectfJhACQKQUV6nTdHLgDoiuhV4
9JGLejiXtGt2cpLiG3pvIDfwCAbN8KV7CPdU1NJeea/lpyiO3o92egBFMPCUCuPQ
mfWWiyVxoxSI3uU1F0/Uv5eczoNN4pTXpChn/QbxGvMMhGI0CWAARQ4L9thNj1PV
WQJ4coCBBq7E8vB6MMcwkGL/Fi3sq4kwSIcVP0gL2J6GHdc9KVHoOaNIPmL9L473
H7bpCURVWwORNuwPqe/wxD8OPQQTZUSaXw0B0oMKv+UCXHLCE/p8iQiF7o57xzRW
DBasV+Q/AgMBAAECggEAAoOruaRV3ZKtS/X+BbYmDYGhDYksPpWJ69KhL2ELFpYs
D4yA5QL3pUN2v0gUZOt2xZJNz9sqElMzQPY0mQCpJJ2m4jNr1ecDlFQe3TbFAfYq
lQIX7rOEfeb68Hqq0IZR3zQ0V018T6QteXzPxTk9IXNs3Mqp52syiME6UwHwMSjj
b5e1ccP1Lzu6ZzT/FvlF6u8VfIe/J6CI3a0LyDEG8MSXzrYO1H2CGwL256w4rNkM
TKbDp8EQNiJoyia5ZgLlmHeuboleAGgVIZTz6vLElH+SW8436MiwFhrDkel2yr+w
VyBFpDyjwd+TZGsfkm+GsqrSFjIY2jKI4SwVRZ3UQQKBgQD41GLKnMOX3JEGMWmU
y62O/QWlFB4Etl5IHicQ9KM+XyIU1W7zKnlJ/6tCrA51y8KZHgDnxNTRZ9IlG6e2
o+8XfqAAbPTrPumWUuOlp5mM1cXAECdcWtqIawW7eX+HV7GItfVOY81j9M2M7h8y
7adBFXZgvegourNhdu7OSHjTjwKBgQDyj1FUAyyFEkngD1YH2p37w1+2wqa70I6w
S19kL36oM6UqVbpsSj1UFxbNMw+0zoOmeaGdQ8FsvDKYp3ENOfGC1IlNoZn25XZg
ZJWQ7PD9BZXLiWoqIj/Xw5ZXt9y6snQcHz530uLrOXdf6fTmHMVXu5sV2x3n/CG+
BDwc4onMUQKBgHfTj1+S7n4LMauR0A5fp0zcfANrFKmgSPbhzl5T8OjwVzFBNxTS
t2XzQT3yLWR8o1GpKTZziyjsnj5QewtxFopx6AtMbQV8XnCNqiLz+vXfXEOJg+rt
tZ9MKo6Fi+xZRp1XQrJlTVt9YB8l7NBRseGdvEwKJ+q1Pf0JkSa1r7nxAoGABuFf
2jU41xF8vPlSZGikIJcbKPD+j8dLtN5+dzYNrrHAVqIV5qp4cOEyyWFQab2N6HP8
zJw2aWIxQTlGsKxbGG308xnJZ0J9CCDySSkfcN3usNZWB5nvG6y1oAlcHoOfXYjZ
WcbGnwuDQlo9kyVvx5xPut/7SjBRuLUgC2W1qPECgYEA6up/H+sTd5W27KrKj1hc
GsBOSNC4BuvV/3sPMEHboR3zF+oAhPOP5FPguoSaP6VKMJ0JaLwaTD2QISeaUZ//
zMoD4ldNkO86frZVxgc6Iy0PVc2MeEWtEXu7NRZdZknABbM7gvvLUw9PoPq2KB8I
vPP0sjW46ukaQCclIrWs038=
-----END PRIVATE KEY-----
"""
EC_PRIVATE_KEY_PEM = b"""-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQg6f53a4gJv6SfM/iE
1AfKM25JAeRfJYdHld7/o4/h2m+hRANCAATdOg6WdDCcWLiw5jg53c9pAkrrJQsm
kQbzyr/k4YqRhNk8zBryo+UYgEYYsIOmXPHhZRRs61+yx5jcxQwJ1JYI
-----END PRIVATE KEY-----
"""

# Ключ из опубликованного CRX3-пакета CRX Viewer 1.6.10. Он нужен только для
# регрессии ID; это старый 1024-битный ключ и им не подписываются фикстуры.
KNOWN_EXTENSION_PUBLIC_KEY_DER = base64.b64decode(
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCNxbeYbxnMB9L788ALfgWazpSq"
    "fHsylFfXpFmeh0eBprnJKzfrqDDpWrrvAoU+nKAJyYPDoEhhE+tZKdXF8auOYJPW"
    "CHQ4COsqzjQim44Hv+e8EmSsEB4K3tZGdhaYfd8IRJzZ/vVU1IZzWufV+BGi9NHL"
    "gXE+daK2eij1Zjvj4wIDAQAB"
)


def varint(value: int) -> bytes:
    out = bytearray()
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def bytes_field(number: int, value: bytes) -> bytes:
    return varint((number << 3) | 2) + varint(len(value)) + value


def extension_id(public_key_der: bytes) -> str:
    digest = hashlib.sha256(public_key_der).digest()[:16]
    return "".join(chr(ord("a") + nibble) for byte in digest
                   for nibble in (byte >> 4, byte & 0x0F))


def zip_archive() -> bytes:
    manifest = (
        b'{"manifest_version":3,"name":"SHELTER CRX3 fixture",'
        b'"version":"1.0","permissions":["storage"],'
        b'"host_permissions":["https://example.com/*"]}'
    )
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_STORED) as archive:
        info = zipfile.ZipInfo("manifest.json", date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_STORED
        info.create_system = 3
        info.external_attr = 0o100644 << 16
        archive.writestr(info, manifest)
        readme = zipfile.ZipInfo("fixture.txt", date_time=(1980, 1, 1, 0, 0, 0))
        readme.compress_type = zipfile.ZIP_STORED
        readme.create_system = 3
        readme.external_attr = 0o100644 << 16
        archive.writestr(readme, b"CRX3 signature test fixture\n")
    return stream.getvalue()


def signed_header(crx_id: bytes, marker: bytes = b"") -> bytes:
    value = bytes_field(1, crx_id)
    if marker:
        value += bytes_field(2, marker)
    return value


def signed_message(signed_header_data: bytes, archive: bytes,
                   include_context_nul: bool = True) -> bytes:
    context = CONTEXT + (b"\x00" if include_context_nul else b"")
    return context + struct.pack("<I", len(signed_header_data)) + \
        signed_header_data + archive


def rsa_signature(key, signed_header_data: bytes, archive: bytes,
                  include_context_nul: bool = True) -> bytes:
    return key.sign(
        signed_message(signed_header_data, archive, include_context_nul),
        padding.PKCS1v15(),
        hashes.SHA256(),
    )


def ecdsa_signature(key, signed_header_data: bytes, archive: bytes) -> bytes:
    try:
        algorithm = ec.ECDSA(hashes.SHA256(), deterministic_signing=True)
    except TypeError as exc:
        raise SystemExit(
            "Для воспроизводимой ECDSA-подписи нужен cryptography>=43"
        ) from exc
    return key.sign(signed_message(signed_header_data, archive), algorithm)


def proof_field(ecdsa: bool, public_key_der: bytes, signature: bytes) -> bytes:
    proof = bytes_field(1, public_key_der) + bytes_field(2, signature)
    return bytes_field(3 if ecdsa else 2, proof)


def package(proofs, signed_header_data: bytes, archive: bytes) -> bytes:
    header = b"".join(proofs) + bytes_field(10000, signed_header_data)
    return b"Cr24" + struct.pack("<II", 3, len(header)) + header + archive


def generate() -> dict[str, bytes]:
    rsa_key = serialization.load_pem_private_key(RSA_PRIVATE_KEY_PEM, password=None)
    ec_key = serialization.load_pem_private_key(EC_PRIVATE_KEY_PEM, password=None)
    rsa_public = rsa_key.public_key().public_bytes(
        serialization.Encoding.DER,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    ec_public = ec_key.public_key().public_bytes(
        serialization.Encoding.DER,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    rsa_id = bytes.fromhex(hashlib.sha256(rsa_public).hexdigest()[:32])
    ec_id = bytes.fromhex(hashlib.sha256(ec_public).hexdigest()[:32])
    archive = zip_archive()

    rsa_header = signed_header(rsa_id)
    rsa_proof = proof_field(
        False, rsa_public, rsa_signature(rsa_key, rsa_header, archive)
    )
    ec_header = signed_header(ec_id)
    ec_proof = proof_field(
        True, ec_public, ecdsa_signature(ec_key, ec_header, archive)
    )

    foreign_first = proof_field(
        True, ec_public, ecdsa_signature(ec_key, rsa_header, archive)
    )
    files = {
        "rsa_valid.crx": package([rsa_proof], rsa_header, archive),
        "ecdsa_valid.crx": package([ec_proof], ec_header, archive),
        "multi_proof_foreign_key.crx": package(
            [foreign_first, rsa_proof], rsa_header, archive
        ),
        "bad_zip_byte.crx": package(
            [rsa_proof], rsa_header, archive[:-1] + bytes([archive[-1] ^ 1])
        ),
    }

    original_header = signed_header(rsa_id, marker=b"A")
    original_signature = rsa_signature(rsa_key, original_header, archive)
    changed_header = original_header[:-1] + b"B"
    files["bad_signed_header_data.crx"] = package(
        [proof_field(False, rsa_public, original_signature)], changed_header,
        archive,
    )

    wrong_id_header = signed_header(ec_id)
    wrong_id_signature = rsa_signature(rsa_key, wrong_id_header, archive)
    files["bad_crx_id.crx"] = package(
        [proof_field(False, rsa_public, wrong_id_signature)], wrong_id_header,
        archive,
    )

    no_nul_signature = rsa_signature(
        rsa_key, rsa_header, archive, include_context_nul=False
    )
    files["bad_context_without_nul.crx"] = package(
        [proof_field(False, rsa_public, no_nul_signature)], rsa_header, archive
    )
    files["crxviewer_1.6.10_public_key.der"] = KNOWN_EXTENSION_PUBLIC_KEY_DER

    if extension_id(KNOWN_EXTENSION_PUBLIC_KEY_DER) != KNOWN_EXTENSION_ID:
        raise RuntimeError("Встроенный тестовый ключ CRX Viewer не совпал с известным ID")
    return files


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--check",
        action="store_true",
        help="проверить, что сохранённые фикстуры совпадают с генератором",
    )
    args = parser.parse_args()
    generated = generate()

    if args.check:
        existing = {
            path.name: path.read_bytes()
            for path in args.output.iterdir()
            if path.is_file() and path.suffix in {".crx", ".der"}
        } if args.output.is_dir() else {}
        if existing != generated:
            missing = sorted(set(generated) - set(existing))
            extra = sorted(set(existing) - set(generated))
            changed = sorted(
                name for name in set(generated) & set(existing)
                if generated[name] != existing[name]
            )
            print(
                f"CRX3 fixtures differ: missing={missing}, extra={extra}, "
                f"changed={changed}", file=sys.stderr,
            )
            return 1
        print("CRX3 fixtures are deterministic and current")
        return 0

    args.output.mkdir(parents=True, exist_ok=True)
    for name, data in generated.items():
        (args.output / name).write_bytes(data)
    print(f"Wrote {len(generated)} CRX3 fixtures to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
