#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ===========================================================================
# MonOS — préparateur de release.
#
# Usage :
#   python3 tools/release_prepare.py MonOS-1.4.0.img --version 1.4.0 \
#       [--tag v1.4.0] [--minimum-version 1.2.0] [--channel stable] \
#       [--release-notes "..." | --notes-file notes.md] \
#       [--sha256sums SHA256SUMS] [--sign-key cle_priv.pem]
#
# Effets :
#   . vérifie que l'image existe (obligatoire) ;
#   . calcule le SHA-256 et la taille ;
#   . génère updates/version.json (canal, version minimum, notes, tag vX.Y.Z) ;
#   . écrit le fichier SHA256SUMS ;
#   . signe (facultatif) : génère MonOS-X.Y.Z.sig en Ed25519 via openssl.
#
# Ne place JAMAIS la clé privée dans upsites/version.json ni dans le dépôt.
# ===========================================================================

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

PRODUCT = "MonOS"


def sha256_of(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_version(version: str, fname: str = "version") -> tuple:
    m = re.fullmatch(r"([0-9]+)\.([0-9]+)\.([0-9]+)", version)
    if not m:
        sys.exit(f"{fname} doit être `X.Y.Z`, reçu : {version!r}")
    if version != f"{int(m.group(1))}.{int(m.group(2))}.{int(m.group(3))}":
        sys.exit(f"{fname} ne doit pas contenir de zéros de tête : {version!r}")
    return m.group(1), m.group(2), m.group(3)


def semver_gt(a: tuple, b: tuple) -> bool:
    return list(map(int, a)) > list(map(int, b))


def main() -> int:
    ap = argparse.ArgumentParser(description="Préparateur de release MonOS")
    ap.add_argument("image", help="chemin de l'image (ex: MonOS-1.4.0.img)")
    ap.add_argument("--version", required=True, help="version X.Y.Z (stable uniquement)")
    ap.add_argument("--tag", default=None, help="tag GitHub (défaut: v<version>)")
    ap.add_argument("--minimum-version", default=None,
                    help="version minimale pour une MAJ directe (défaut: version)")
    ap.add_argument("--channel", default="stable", choices=["stable"],
                    help="canal ; seul 'stable' est accepté pour la MAJ auto")
    ap.add_argument("--release-notes", default=None, help="notes en ligne (séparées par |)")
    ap.add_argument("--notes-file", default=None, help="fichier markdown de notes")
    ap.add_argument("--sha256sums", default="SHA256SUMS", help="fichier checksums")
    ap.add_argument("--sign-key", default=None,
                    help="clé privée Ed25519 (openssl) pour générer le .sig")
    ap.add_argument("--verify-json", default=None,
                    help="vérifie un version.json existant et sort")
    args = ap.parse_args()

    # ----- mode vérification -------------------------------------------------
    if args.verify_json:
        with open(args.verify_json, encoding="utf-8") as f:
            m = json.load(f)
        parse_version(m["version"], "version")
        parse_version(m["minimum_version"], "minimum_version")
        parse_version(m["release_tag"].lstrip("v"), "release_tag")
        if not re.fullmatch(r"[0-9a-fA-F]{64}", m["sha256"]):
            sys.exit("sha256 n'est pas 64 hex")
        if not isinstance(m["size"], int) or m["size"] < 0:
            sys.exit("size doit être un entier >= 0")
        if m["product"] != PRODUCT:
            sys.exit(f"product doit être {PRODUCT}")
        if m["channel"] != "stable":
            sys.exit("channel doit être 'stable'")
        print(f"version.json vérifié : {m['version']} ({m['released']})")
        return 0

    # ----- préparation réelle ------------------------------------------------
    vparts = parse_version(args.version)
    version = ".".join(vparts)
    min_version = args.minimum_version or version
    parse_version(min_version, "minimum_version")
    tag = args.tag or "v" + version
    parse_version(tag.lstrip("v"), "tag")

    if not os.path.isfile(args.image):
        sys.exit(f"l'image n'existe pas : {args.image}")

    image_name = os.path.basename(args.image)
    if image_name != f"MonOS-{version}.img":
        sys.exit(f"nom d'image invalide (attendu MonOS-{version}.img) : {image_name}")

    sha = sha256_of(args.image)
    size = os.path.getsize(args.image)

    notes = []
    if args.release_notes:
        notes = [line.strip() for line in args.release_notes.split("|") if line.strip()]
    if args.notes_file:
        with open(args.notes_file, encoding="utf-8") as f:
            notes = [line.rstrip("\n") for line in f
                     if line.strip() and not line.lstrip().startswith("#")]

    manifest = {
        "product": PRODUCT,
        "channel": args.channel,          # stable uniquement
        "version": version,
        "release_tag": tag,
        "minimum_version": min_version,
        "image": image_name,
        "sha256": sha,
        "size": size,
        "release_notes": notes,
    }

    # JSON lisible et ordonné (diff propre sur GitHub).
    manifest_json = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
    with open("updates/version.json", "w", encoding="utf-8") as f:
        f.write(manifest_json)

    with open(args.sha256sums, "w", encoding="ascii") as f:
        f.write(f"{sha}  {image_name}\n")

    print(f"Manifeste écrit : updates/version.json ({version})")
    print(f"SHA-256 : {sha}")
    print(f"Taille  : {size} octets")

    # Signature Ed25519 optionnelle. La clé privée n'est JAMAIS intégrée.
    if args.sign_key:
        sig_path = f"{image_name}.sig"
        h = hashlib.sha256()
        with open(args.image, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        # openssl : signer le hash SHA-256 de l'image (Ed25519 = raw message).
        cmd = ["openssl", "pkeyutl", "-sign", "-inkey", args.sign_key,
               "-rawin", "-in", args.image, "-out", sig_path]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"échec de la signature Ed25519 : {r.stderr.strip()}")
        print(f"Signature Ed25519 : {sig_path}")
    else:
        print("Astuce : générer la signature Ed25519 avec --sign-key "
              "(openssl pkeyutl -sign -inkey cle_priv.pem -rawin -in <image> -out <image>.sig)")

    print(f"\nRelease GitHub à créer :")
    print(f"  - tag      : {tag}")
    print(f"  - assets   : {image_name}  +  version.json  +  SHA256SUMS"
          + (f"  +  {image_name}.sig" if args.sign_key else ""))
    print(f"  - prerelease : false (canal stable)   draft : false")
    print("  Les workflows check-release.yml et validate-update.yml valideront "
          "automatiquement la cohérence (tag, manifeste, présence des assets).")
    return 0


if __name__ == "__main__":
    sys.exit(main())