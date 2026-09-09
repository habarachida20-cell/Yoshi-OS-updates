# MonOS — Système de mises à jour automatiques

Ce document décrit l'infrastructure complète, serveur **GitHub uniquement**,
pour distribuer des mises à jour STABLE à MonOS avec vérification d'intégrité,
installation sécurisée (slots A/B) et rollback automatique.

---

## 1. Architecture générale

```
                    ┌──────────────────────────────────────────┐
                    │  GitHub (seul serveur de distribution)   │
                    │  - Releases (images + manifestes)        │
                    │  - branch main (updates/version.json)    │
                    │  - GitHub Actions (gardes-fous)          │
                    └───────────────┬──────────────────────────┘
                                    │ HTTPS
                    ┌───────────────▼──────────────────────────┐
                    │  MonOS Update Robot                      │
                    │  (updater/update-robot.cpp)              │
                    │  1. lit la version installée             │
                    │  2. liste les releases                   │
                    │  3. filtre STABLE (jamais alpha/beta/rc) │
                    │  4. SemVer + chaîne de migration         │
                    │  5. télécharge → SHA-256 → Ed25519       │
                    └───────────────┬──────────────────────────┘
                                    │
                    ┌───────────────▼──────────────────────────┐
                    │  Installateur  (slot INACTIF)            │
                    │  installer/installer.cpp                 │
                    │  write + relecture + SHA-256 du slot     │
                    │  commit bootmeta (nextSlot, attempts)    │
                    └───────────────┬──────────────────────────┘
                                    │ reboot
                    ┌───────────────▼──────────────────────────┐
                    │  Bootloader + Rollback                   │
                    │  installer/rollback.cpp                  │
                    │  si OK  → confirm (ancien = sauvegarde)  │
                    │  sinon  → rollback → Recovery            │
                    └──────────────────────────────────────────┘
```

Schéma des slots sur le disque de MonOS :

```
┌──────────┬──────────┬───────────────────┬──────────────────┐
│ SLOT A   │ SLOT B   │ RESCUE            │ BLOB METADATA    │
│ (stable) │ (update) │ (bootloader mini) │ bootmeta.h       │
└──────────┴──────────┴───────────────────┴──────────────────┘
```

- On ne parcourt **jamais** le système en cours (slot actif) : l'écriture se
  fait toujours sur le slot inactif, complétée par une **relecture** vérifiant
  le SHA-256 avant tout commit.
- `BootMeta` stocke le slot actif, le slot suivant, le compteur de tentatives
  et le numéro de série (anti double-comptage).

## 2. GitHub Releases

Exemple de release stable `v1.4.0` :

```
v1.4.0
├── MonOS-1.4.0.img       ← image complète (kernel + fs)
├── version.json          ← manifeste (sha256, size, version min)
├── SHA256SUMS            ← sha256 de l'image (double vérif)
└── MonOS-1.4.0.sig       ← signature Ed25519 (option recommandée)
```

Une release sans ces éléments, ou marquée *prerelease*, est ignorée par le
robot (`check-release.yml` la bloque aussi côté GitHub Actions).

## 3. version.json

`updates/version.json` (racine du dépôt) est maintenu par
`tools/release_prepare.py` et sert de manifeste de référence. Chaque release
embarque sa propre copie. Champs :

| Champ | Rôle |
|---|---|
| `product` | doit être `MonOS` |
| `channel` | doit être `stable` |
| `version` | SemVer X.Y.Z |
| `release_tag` | tag `vX.Y.Z` cohérent |
| `minimum_version` | version minimale pour une MAJ directe |
| `image` | `MonOS-X.Y.Z.img` |
| `sha256` | 64 hex, SHA-256 réel de l'image |
| `size` | taille en octets |
| `release_notes` | liste de notes |

## 4. Le robot (`updater/update-robot.cpp`)

Commandes : `update-robot check`, `chain`, `install`, `settings`.

- `check` : lit `kInstalledVersion`, interroge les releases, **refuse tout
  ce qui n'est pas stable**, compare avec SemVer numérique.
  - à jour → `MonOS est à jour`
  - disponible → `Une mise à jour est disponible` + versions
  - hors ligne → `Impossible de rechercher les mises à jour.` (code 2, pas une
    erreur système)
- `chain` : affiche la liste ordonnée des stables à installer, en respectant
  `minimum_version` (migrations intermédiaires).
- `install` : suite complète journalisée > voir §6.
- `settings` : écran « Mise à jour de MonOS » (Version actuelle / Canal /
  État + boutons). Les boutons graphiques dépendent du shell MonOS (TODO).

Points d'architecture kernel (non inventés ici, explicitement marqués TODO
dans le code) :

- pile HTTPS du kernel (`monos_net_https_get`) ;
- écriture disque (`slot_write_block` / couche disque MonOS) ;
- reboot (`monos_reboot`) ;
- console du shell (`monos_getch`, boutons).

## 5. Vérifications de sécurité

```
Image ──► Taille == size ──► SHA-256 == manifeste ──► Ed25519 ──► Installation
```

- **SHA-256 obligatoire** : implémenté en pur C++ (`updater/sha256.h`), vérifié
  une fois sur le fichier téléchargé et une seconde fois sur la relecture du
  slot.
- **Ed25519 optionnelle mais recommandée** : la clé publique est compilée dans
  le produit (`#if MONOS_UPD_HAS_ED25519`), la clé privée n'est **jamais**
  stockée dans l'image (usage : `openssl pkeyutl -sign` hors ligne, cf.
  `tools/release_prepare.py --sign-key`).
- Une URL GitHub ne prouve rien : sans hash valide, rien n'est installé.

## 6. Installation sécurisée (A/B)

Étapes de `install` :

1. téléchargement avec reprise (fichier partiel conservé, `Range:` en hôte) ;
2. vérification `size` ;
3. SHA-256 du fichier **vs manifeste** ; en cas de désaccord : suppression du
   fichier, annulation, message d'erreur ;
4. écriture dans le **slot inactif** par blocs de 128 Ko ;
5. relecture + SHA-256 du slot écrit ;
6. commit `bootmeta` (`nextSlot`, `bootAttempts=3`, `bootSerial++`) ;
7. reboot sur le nouveau slot.

Interruption pendant l'installation → le slot inactif est « incomplet » (pas
d'en-tête valide) ; il n'est jamais démarré.

## 7. Après redémarrage — rollback (`installer/rollback.cpp`)

`rollback_on_boot()` (appelé tôt par le bootloader) :

- pas de pendance → démarre le slot actif ;
- pendance + compteur > 0 → démarre le slot pending, `bootAttempts--` (l'OS
  doit `rollback_confirm_slot()` une fois opérationnel) ;
- compteur épuisé → restaure la dernière version stable, met `recoveryRequired`,
  journalise.

Antiboucle : `kMaxBootAttempts = 3` et `bootSerial` incrémenté à chaque commit
permettent de ne jamais rester coincé en boucle infinie de reboots.

`rollback_confirm_slot()` valide la nouvelle version (`activeSlot = nextSlot`,
`previousVersion` conservée en sauvegarde temporaire).

## 8. Recovery (`recovery/recovery.cpp`, `recovery-menu.cpp`)

Menu : 1 Restaurer la dernière stable · 2 Réinstaller la mise à jour stable ·
3 Rechercher des mises à jour stables · 4 Réparer le démarrage · 5 Annuler la
dernière mise à jour · 6 Redémarrer · 7 Contacter le support · 0 Continuer.

Indépendant du système de fichiers complet (fonctionne même si MonOS ne boote
plus) : entrées clavier `readKey()` (TODO kernel pour la console).

## 9. GitHub Actions

- **validate-update.yml** : sur push/PR touchant `updates/`, `tools/` — valide
  le JSON, les champs (product/channel/semver/tag/sha256/size), la cohérence
  image↔version, la compilation des outils, et la présence des mots interdits
  dans le robot.
- **check-release.yml** : sur `release: [published, edited]` — refuse les
  drafts et **toute prerelease**, vérifie tag SemVer, télécharge les assets,
  compare `version.json` ↔ tag ↔ taille ↔ SHA-256 réel ↔ SHA256SUMS, exige
  l'image `MonOS-X.Y.Z.img`.

## 10. Créer une nouvelle release

```bash
# 1. préparer l'image de la release
python3 tools/release_prepare.py MonOS-1.4.0.img --version 1.4.0 \
       --minimum-version 1.2.0 --release-notes "Corrige le noyau|Ajoute x" \
       --sign-key chemin/cle_priv_ed25519.pem
#    → écrit updates/version.json, SHA256SUMS et MonOS-1.4.0.img.sig

# 2. versionner + tag + push
git add updates/version.json SHA256SUMS MonOS-1.4.0.sig
git commit -m "release 1.4.0"
git tag -a v1.4.0 -m "MonOS 1.4.0 stable"
git push --follow-tags

# 3. créer la Release (UI ou gh) avec les assets :
#    MonOS-1.4.0.img, version.json, SHA256SUMS, MonOS-1.4.0.sig
#    prerelease = OFF, draft = OFF
gh release create v1.4.0 MonOS-1.4.0.img version.json SHA256SUMS MonOS-1.4.0.sig \
   --title "MonOS 1.4.0" --notes "stable"
```

`check-release.yml` valide automatiquement ; les robots MonOS téléchargent la
nouvelle version au prochain `check`.

## 11. Limites connues / à brancher sur le vrai kernel

Toutes les fonctions dépendant de l'architecture MonOS réelle sont marquées
`TODO(noyau MonOS)` dans le code et ne sont pas simulées :

- pile HTTPS du kernel ;
- couche disque (slots physiques) ;
- `monos_reboot` / `monos_getch` / shell graphique ;
- backend Ed25519 du noyau.

En attendant, le robot s'utilise sur PC en mode hôte
(`-DMONOS_UPD_HOST -lwinhttp` pour le réseau, slots simulés par fichiers).