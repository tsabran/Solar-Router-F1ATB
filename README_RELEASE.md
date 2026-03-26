# Custom Release Builder

Ce worktree est dédié à la création de releases personnalisées en fusionnant plusieurs branches locales.

## Configuration

Le worktree `custom-release` est basé sur la branche `main` et permet de créer une release en combinant les changements de plusieurs branches locales.

### Fichier de configuration

Le fichier `release.config` contient la liste des branches à fusionner par défaut:

```
feature/enphase-diagnostics
feature/asset-optimization
fix/serverHisto1an
```

Modifiez ce fichier pour définir les branches que vous voulez inclure dans votre release. Les lignes commençant par `#` sont des commentaires et seront ignorées.

## Utilisation

### 1. Éditer le fichier de config (optionnel)

```bash
# Éditer release.config pour spécifier les branches à fusionner
nano release.config
```

### 2. Exécuter le script de build

**Option A: Avec branches en arguments**
```bash
cd /Users/tsabran/dev/Solar-Router-F1ATB-workspace/custom-release
./build-release.sh branch1 branch2 branch3
```

**Option B: À partir du fichier de config**
```bash
cd /Users/tsabran/dev/Solar-Router-F1ATB-workspace/custom-release
./build-release.sh
```

### Exemples

**Exemple 1: Fusionner une seule branche**
```bash
./build-release.sh feature/enphase-diagnostics
```

**Exemple 2: Fusionner plusieurs branches**
```bash
./build-release.sh feature/enphase-diagnostics feature/asset-optimization fix/serverHisto1an
```

**Exemple 3: Utiliser le fichier de config**
```bash
./build-release.sh
```

## Ce que le script fait

1. **Fetch**: Récupère les derniers changements de `main` depuis le remote `origin`
2. **Reset Hard**: Réinitialise le worktree à `origin/main` (tous les changements locaux sont perdus)
3. **Merge**: Fusionne séquentiellement les branches spécifiées en paramètre (ou depuis le config)

## How Worktrees Share Branches (Git's Branch Syncing)

Excellente question! Voici comment ça fonctionne:

### Architecture Git avec Worktrees

```
.git/                  (dossier partagé)
  ├── refs/heads/      (BRANCHES PARTAGÉES)
  ├── objects/         (objets Git partagés)
  └── ...

Solar-Router-F1ATB/.git    (fichier, pas dossier!)
  → pointe vers le gitdir principal

custom-release/.git        (fichier, pas dossier!)
  → pointe vers le gitdir principal
```

### Points clés

✅ **Les branches sont PARTAGÉES entre worktrees**
- Les deux worktrees utilisent le même dossier `.git`
- Une branche locale existe une seule fois dans `.git/refs/heads/`
- Donc vous **NE DEVEZ PAS** spécifier le worktree quand vous mergez

✅ **Un fetch/pull affecte TOUS les worktrees**
- Si vous faites `git fetch origin` dans le worktree A, tous les worktrees verront les nouveaux commits
- Les remotes (`origin/main`, etc.) sont aussi partagées

✅ **Chaque worktree a son propre répertoire de travail et index**
- Le `.git` est un simple fichier qui pointe vers le gitdir partagé:
  ```
  $ cat custom-release/.git
  gitdir: /Users/tsabran/dev/Solar-Router-F1ATB-workspace/Solar-Router-F1ATB/.git/worktrees/custom-release
  ```

### Exemple: Workflow multi-worktree

```bash
# Dans Solar-Router-F1ATB
git fetch origin
git checkout feature/enphase-diagnostics
# Les modifications sont visibles dans custom-release aussi!

# Dans custom-release
./build-release.sh
# Le script voit feature/enphase-diagnostics mise à jour
```

### ⚠️ Limitations importantes

- **Une branche NE PEUT PAS être "checked out" dans 2 worktrees en même temps**
  - Si elle est checked out dans `Solar-Router-F1ATB`, vous ne pouvez pas la checker dans `custom-release`
  - Vous pouvez les fusionner, mais pas les avoir actives simultanément

- **Le script suppose que les branches existent localement**
  - Faites d'abord `git fetch origin` dans le worktree principal si nécessaire

## Résultat

Après exécution réussie, vous avez un worktree avec:
- La dernière version de `main`
- Tous les changements des branches spécifiées fusionnés

## Prochaines étapes

```bash
# 1. Vérifier les changements
git log --oneline --decorate

# 2. Compiler pour tester
# (suivre votre procédure de build)

# 3. Créer une tag pour la release
git tag -a v1.X.X -m "Release notes"

# 4. Pousser vers remote
git push origin main --tags
```

## Gestion des conflits

Si un merge échoue à cause de conflits:
1. Le script s'arrête et affiche le message d'erreur
2. Résolvez les conflits manuellement
3. Exécutez `git merge --continue` ou relancez le script après correction

## Branches disponibles dans le repo

Consultez les branches disponibles avec:
```bash
cd /Users/tsabran/dev/Solar-Router-F1ATB-workspace/Solar-Router-F1ATB
git branch -v
git branch -a  # Montrer aussi les branches remote
```

Branches actuelles:
- `feature/asset-optimization`
- `feature/enphase-diagnostics`
- `feature/mode-sequenceur`
- `fix/serverHisto1an`
- `main`
