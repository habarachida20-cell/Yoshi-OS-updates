# Branchement des hooks kernel (MonOS Update)

Ce dossier `kernel-bindings/` fixe le **contrat unique** entre le robot /
l'installeur / Recovery (domaine utilisateur) et le noyau MonOS.

## Fichiers

| Fichier                  | Role                                                        |
|--------------------------|-------------------------------------------------------------|
| `monos_kernel_api.h`     | Contrat ABI des 9 hooks (C/C++). Aucune implementation.     |
| `monos_kernel_impl.c`    | Implementation pour le kernel MonOS (base xv6-rev11).       |

## Primitives kernel reelles utilisees (verifiees dans le source du noyau)

| Hook MonOS        | Symbole kernel existant                                      |
|-------------------|--------------------------------------------------------------|
| disque (slot)     | `bread(uint,uint)` / `bwrite` / `brelse` / `iderw` (bio+ide) |
| metadonnees       | idem (bloc logique reserve 1..15)                            |
| journal           | idem (blocs journal reserves)                                |
| clavier           | `uartgetc()` (uart.c, -1 si vide)                            |
| reboot            | `outb(0x64, 0xFE)`  (x86.h, reset 8042)                      |
| HTTPS             | **absent dans le noyau** -> `MONOS_ERR_NONET` (refus propre) |
| Ed25519           | **absent dans le noyau** -> `MONOS_ERR_NOKEY` (SHA-256 seul) |

## Integration dans le noyau MonOS

1. Copier `kernel-bindings/` dans l'arborescence du kernel (ex. `kernel/updapi/`).
2. Ajouter `USERCFLAGS += -I<chemin>/updapi` et compiler `monos_kernel_impl.c`
   dans le kernel (`OBJS += updapi/monos_kernel_impl.o`) ainsi que le domaine
   utilisateur (`update-robot`, `installer`, `recovery`) avec `-DMONOS_UPD_KERNEL`.
3. Le build kernel qui ne veut pas encore du robot peut verifier le contrat seul :
   `gcc -c monos_kernel_impl.c -I<kernel>` (teste dans ce depot par stubs).

## Geometrie de l'image exigee (a valider lors de la prochaine fabrication)

```
bloc  0        : bootblock
bloc  1..15    : metadonnees + journal + reserve     (META=1, JOURNAL=2)
bloc  16..4111 : SLOT A  (4096 blocs = 2 Mo)
bloc 4112..8207: SLOT B
fin de disque   : Recovery (image-ecran de secours)
```

**Pre-requis :** l'image `yoshi.img` actuelle est un disque xv6 "une seule
partition FS". L'ecriture A/B par `monos_disk_write` n'a de sens qu'avec une
image **mono-disque decoupee en slots reserves** (voir
`docs/update-system.md`). Tant que ce layout n'existe pas, l'API retourne des
erreurs explicites et n'ecrit jamais le slot actif — aucun risque de boot casse.

## Ce qui reste a ecrire (hors perimetre de ce branchement)

- Pile HTTPS kernel : TLS + TCP/IP + pilote carte reseau (le noyau actuel n'a
  aucun de ces trois morceaux). La fonction a remplir est `monos_net_https_get`.
- Backend Ed25519 (arithmetique courbe) : `monos_ed25519_verify`.
- Confirmation post-boot : le systeme confirme sa propre stabilite
  (`rollback_confirm_slot`), a raccorder a l'init kernel une fois demarre.

## Testes

- `update-robot check/chain/simulate` (hote, WinINet) : OK dans la sandbox.
- Compilation/syntaxe de `monos_kernel_impl.c` : verifiee contre des stubs
  reproduisant exactement `types.h/defs.h/buf.h/x86.h/fs.h` du kernel (rendu
  dans `docs/` ? non : les stubs sont jetables, la commande est documentee dans
  cet historique de session).