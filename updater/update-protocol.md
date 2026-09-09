# MonOS — Protocole de mise à jour

Version 1.0 — Canal : `stable`

## 1. Principe

MonOS utilise **uniquement GitHub** comme serveur de distribution :

- **Repo manifeste** : `habarachida20-cell/Yoshi-OS-updates`
- **Releases** : `https://api.github.com/repos/habarachida20-cell/Yoshi-OS-updates/releases`
- **Assets** (URLs déterministes) :
  `https://github.com/habarachida20-cell/Yoshi-OS-updates/releases/download/<tag>/<asset>`
- **Manifeste de référence** à la racine (miroir de la dernière stable) :
  `https://raw.githubusercontent.com/habarachida20-cell/Yoshi-OS-updates/main/updates/version.json`

## 2. Règles du canal stable

Une version est **stable** et donc installable automatiquement si **toutes** les
conditions suivantes sont vraies :

| Règle | Test |
|---|---|
| Pas une draft | `release.draft == false` |
| Pas une prerelease | `release.prerelease == false` |
| Tag SemVer | `vX.Y.Z` strict |
| Sans suffixe | `version.isStable()` (pas de `-alpha`, `-beta`…) |
| Aucun mot interdit | ni dans le tag, ni dans le nom : `alpha`, `beta`, `rc`, `testing`, `unstable`, `development`, `snapshot`, `nightly`, `canary`, `preview` |
| Manifeste cohérent | `product == "MonOS"`, `channel == "stable"`, `release_tag == v<version>` |
| Image présente | asset `MonOS-<version>.img` de taille > 0 |
| SHA-256 valide | 64 hex, égal au hash réel de l'image |

Toute release en contradiction est **ignorée silencieusement** par le robot
(jamais d'échec, jamais de crash). Les versions sautées (ex : un `v1.3.0-beta`
qui devient `v1.4.0`) ne cassent rien : le robot ne voit que les stables.

## 3. Comparaison et chaîne de migration

- La comparaison est **SemVer numérique** : `1.9.0 < 1.10.0`, jamais un tri
  lexicographique.
- Si plusieurs stables ont été publiées, le robot installe **dans l'ordre** les
  versions dont `minimum_version <= version courante`, jusqu'à la plus haute
  atteignable.
- Si un saut exige une migration intermédiaire (ex : `minimum_version=1.2.0`
  alors que la machine est en `1.0.0`), la chaîne s'arrête et les étapes
  manquantes sont proposées dans l'ordre.

## 4. Chaîne de sécurité

```
Image téléchargée (HTTPS)
        │
        ▼
Taille == size  ───────────  sinon : suppression + annulation
        │
        ▼
SHA-256 == manifeste  ─────  sinon : suppression + annulation
        │
        ▼
Signature Ed25519 (option cadre)
        │
        ▼
Écriture dans le slot INACTIF (jamais le slot actif)
        │
        ▼
Relecture + SHA-256 du slot
        │
        ▼
Commit bootmeta (nextSlot + count) → reboot
```

Une simple URL GitHub n'est **jamais** une preuve d'authenticité : sans SHA-256
et (idéalement) signature Ed25519 valides, rien n'est installé.

## 5. États système

```
IDLE          → pas de pendance, boot du slot actif
BOOT_PENDING  → un slot de remplacement est armé (count > 0)
ROLLBACK_NEEDED → count épuisé : on revient à l'ancien slot
RECOVERY_REQUIRED → echec catastrophique : Recovery se présente
```

Tentative du slot de remplacement limitée à `kMaxBootAttempts = 3`. Au
troisième échec consécutif sans confirmation, le bootloader restaure la
dernière version stable et exige l'écran Recovery.

## 6. Journal

Chaque opération écrit des lignes `append-only` :

```
UPDATE START
Current: 1.0.0
Target: 1.1.0
Download: OK
SHA256: OK
Ed25519: OK
Install: OK
Boot: PENDING
UPDATE SUCCESS     (ou rollback/échec correspondant)
```

Le journal est conservé sur le disque (via `monos_updates.log` en mode hôte).

## 7. Hors ligne

Si GitHub est injoignable (`check` retourne un code 2), le système **garde sa
version courante** et affiche uniquement : `Impossible de rechercher les mises
à jour.` Ce n'est ni une erreur, ni une modification.

## 8. Fichiers impliqués

| Fichier | Rôle |
|---|---|
| `updater/version.h` | SemVer + identité produit |
| `updater/update-robot.cpp` | robot (check/chain/install) |
| `installer/bootmeta.h` | blob de démarrage A/B + journal |
| `installer/installer.cpp` | écriture sûre dans le slot inactif |
| `installer/rollback.cpp` | confirmation/rollback + compteur |
| `recovery/recovery.cpp` / `recovery-menu.cpp` | menu de secours |
| `tools/release_prepare.py` | génération du manifeste aux releases |
| `updates/version.json` | dernier manifeste stable |
| `.github/workflows/*.yml` | garde-fous CI |