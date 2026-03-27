# Build Script — merge-and-build.sh

Ce script construit le firmware dans un **clone temporaire** du repo local.  
Il ne touche jamais les branches locales ni le working tree.

## Modes

| Mode | Flag | Comportement |
|------|------|--------------|
| **localdev** *(défaut)* | `--localdev` | Clone la branche active avec les changements locaux (staged + unstaged) appliqués par-dessus |
| **localrelease** | `--localrelease` | Fetch `origin/main` avant le clone, puis démarre depuis `origin/main` |

Les deux modes peuvent être combinés pour produire les deux builds en une seule exécution.

Dans les deux cas, les branches listées dans `merge-branches.conf` sont fusionnées séquentiellement.

## Configuration — merge-branches.conf

Éditer `merge-branches.conf` pour définir les branches à fusionner :

```
# Branches à fusionner
feature/enphase-diagnostics
feature/mode-sequenceur
fix/serverHisto1an

# Autres branches disponibles (décommenter pour inclure) :
# feature/asset-optimization
```

Les lignes commençant par `#` sont ignorées. La section `# Autres branches disponibles` est
rafraîchie automatiquement à chaque exécution.

## Utilisation

```bash
# localdev (défaut) — branche active + changements locaux
./merge-and-build.sh

# localrelease — depuis origin/main
./merge-and-build.sh --localrelease

# Les deux en une fois
./merge-and-build.sh --localdev --localrelease

# Branches explicites (remplace le fichier de config)
./merge-and-build.sh --localrelease feature/enphase-diagnostics fix/serverHisto1an
```

## Résultat

Les binaires sont archivés dans `../releases/` avec un nom incluant le mode et le timestamp :

```
Solar_Router_V17_16_localdev-20260327-143000.ino.bin
Solar_Router_V17_16_localrelease-20260327-143000.ino.bin
```

La version affichée dans l'interface firmware reflète également le suffix :
`17.16-localdev-20260327-143000`

## Gestion des conflits

Si un merge échoue :
1. Le script s'arrête et indique la branche en conflit
2. Résoudre le conflit sur la branche source
3. Relancer le script

## Scripts

| Fichier | Rôle |
|---------|------|
| `merge-and-build.sh` | Script principal — clone, merge, build |
| `build-binary.sh` | Compilation firmware via `arduino-cli` |
| `merge-branches.conf` | Liste des branches à fusionner |
| `partitions.csv` | Table de partitions ESP32 (OTA) |
