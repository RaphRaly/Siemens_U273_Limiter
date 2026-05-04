# Guide Complet de Modélisation Analogique pour l'Audio
## Basé sur la conférence d'Andy Simper (Cytomic) — ADC 2020

---

## Table des Matières

1. [Vue d'ensemble : Qu'est-ce que la modélisation analogique ?](#1-vue-densemble)
2. [White Box vs Black Box](#2-white-box-vs-black-box)
3. [Ce qu'on cherche à capturer (et ce qu'on ne peut pas)](#3-objectifs-et-limites)
4. [Les bases mathématiques indispensables](#4-les-bases-mathématiques)
5. [Les lois de Kirchhoff appliquées aux circuits](#5-les-lois-de-kirchhoff)
6. [Exemple 1 : Le Diode Clipper (non-linéarité sans mémoire)](#6-le-diode-clipper)
7. [Exemple 2 : Le Condensateur et les Filtres (mémoire)](#7-les-condensateurs-et-filtres)
8. [Les méthodes d'intégration numérique](#8-intégration-numérique)
9. [Assemblage : Circuit complet (non-linéaire + mémoire)](#9-circuit-complet)
10. [Formulations alternatives (State Space, DK, Wave Digital)](#10-formulations-alternatives)
11. [Efficacité et optimisation CPU](#11-efficacité-et-optimisation)
12. [L'aliasing et comment le réduire](#12-aliasing)
13. [Automatiser le processus](#13-automatisation)
14. [Terminologie récapitulative](#14-terminologie)
15. [De la théorie au projet concret : checklist](#15-checklist-projet)

---

## 1. Vue d'ensemble

### Le problème fondamental

On a un **circuit analogique désirable** (une pédale, un synthé, un filtre...) dont on possède le schéma électronique. L'objectif est de **transformer ce schéma en code** qui génère de l'audio en temps réel, en conservant toutes les propriétés sonores qui font le caractère du circuit original.

Le processus se décompose en trois étapes :

```
Circuit physique  →  Schéma électronique  →  Code (DSP temps réel)
     (hardware)        (on part d'ici)         (objectif final)
```

On ne s'occupe **pas** de la capture du circuit physique vers le schéma — on suppose qu'on a déjà le schéma (datasheet, reverse engineering, etc.). Tout le travail consiste à passer du schéma au code.

### Pourquoi modéliser la structure plutôt que le comportement ?

Quand on modélise **la structure** (white box), on obtient des propriétés émergentes qu'on n'aurait jamais programmées directement :

- **Dynamique naturelle** : quand le signal est faible, le circuit se comporte quasi-linéairement (son doux) ; quand il est fort, les non-linéarités s'activent (harmoniques, saturation). Cette transition est progressive et musicale.
- **Interaction entre composants** : le filtrage et la distorsion ne sont pas séparés — ils s'influencent mutuellement. C'est ce qui donne ce son "organique" qu'on ne peut pas reproduire en enchaînant simplement un filtre et un clipper.
- **Variation de composants** : en changeant les valeurs des composants (simulation Monte Carlo), on peut simuler le fait que chaque unité matérielle sonne légèrement différemment.

### Le résultat concret qu'on vise

Andy Simper montre dans sa conférence la différence entre :
- **Le modèle complet** (circuit modélisé dans sa globalité) — courbes douces, transitions progressives, arrondi naturel des crêtes
- **L'approche DSP naïve** (filtre HP → filtre LP → clipper en série) — transitions abruptes, arêtes vives

Les deux traitent le même signal, mais le modèle complet sonne significativement mieux parce que les composants interagissent entre eux au sein de chaque sample.

---

## 2. White Box vs Black Box

### White Box (ce qu'on fait ici)

On modélise **l'intérieur** du circuit : chaque résistance, condensateur, transistor, diode est représenté par ses équations physiques. On assemble ces équations et on les résout à chaque sample.

**Avantages :**
- Paramétrage naturel (on tourne les potentiomètres comme sur le vrai circuit)
- Variation de composants possible
- Comportement émergent fidèle
- Compréhension de ce qui se passe

**Inconvénients :**
- Coûteux en CPU (résolution d'équations à chaque sample)
- Complexité qui grandit avec la taille du circuit
- Nécessite de connaître le schéma

### Black Box

On ne regarde pas l'intérieur. On enregistre l'entrée/sortie du circuit réel et on entraîne un modèle (typiquement un réseau de neurones) pour reproduire ce comportement.

**Avantages :**
- Pas besoin du schéma
- Potentiellement moins coûteux en CPU une fois entraîné

**Inconvénients :**
- Difficulté à varier les paramètres (il faut ré-entraîner)
- Pas de compréhension du mécanisme
- Snapshot d'un seul circuit (pas de variation de composants)

### Approche hybride

Andy mentionne que les réseaux de neurones peuvent intervenir **à l'intérieur** d'un white box — par exemple pour approximer les tables de lookup multidimensionnelles (tables MIMO) qui deviennent trop grandes. On garde la structure du circuit mais on utilise le ML pour les parties les plus coûteuses.

---

## 3. Objectifs et Limites

### Ce qu'on veut capturer

| Propriété | Description | Difficulté |
|-----------|-------------|------------|
| **Circuits hautement paramétriques** | Potentiomètres avec contrôle lisse (pas de zipper noise) | Moyenne |
| **Non-linéarités** | Diodes, transistors, tubes — ce qui donne le "son" | Élevée |
| **Structure du circuit** | Où les composants sont connectés et comment ils interagissent | Moyenne |
| **Variation de composants** | Chaque circuit physique est légèrement différent (tolérance des composants) | Faible |
| **Composants à mémoire** | Condensateurs et inductances (filtrage) | Moyenne |
| **Faible CPU** | Temps réel à 44.1kHz+ avec latence minimale | Élevée |
| **Faible aliasing** | Pas d'artefacts numériques audibles | Moyenne |

Le défi central : **toutes ces propriétés sont en conflit**. Un circuit très non-linéaire avec beaucoup de paramètres coûte beaucoup de CPU. L'art de la modélisation analogique est de trouver le bon compromis.

### Ce qu'on NE gère PAS (trop complexe)

- **Modélisation thermique** : deux composants proches se chauffent mutuellement, ce qui change leurs caractéristiques. Utilisé en pratique pour la stabilisation thermique dans les circuits réels. Même les simulateurs commerciaux (SPICE) ne le font pas bien.
- **Interférences électromagnétiques** entre composants
- **Modélisation électrochimique / électromécanique**
- **Modélisation du bruit** : partiellement traitable mais pas couvert ici

---

## 4. Les Bases Mathématiques

### Le principe fondamental : on ne sait résoudre que des lignes

C'est **le** message central de la conférence d'Andy. Toute la sophistication de la modélisation analogique repose sur un fait simple et un peu déprimant : la seule chose qu'on sait résoudre analytiquement, c'est une équation linéaire.

```
y = m·x + b

Résolution : x = (y - b) / m    (si m ≠ 0)
```

Quand `m` est proche de zéro, la solution devient instable — c'est un point à surveiller dans tous les solveurs numériques.

### La linéarisation : transformer une courbe en ligne

Puisqu'on ne sait résoudre que des lignes, on doit **transformer toute fonction non-linéaire en approximation linéaire locale**. C'est le cœur de la méthode.

Pour une fonction non-linéaire `f(x)` évaluée au point `x₀` :

```
Pente locale :    m = f'(x₀)         (la dérivée, le "slope")
Offset :          b = f(x₀) - m·x₀   (on "recale" la ligne)

Approximation :   f(x) ≈ m·x + b     (notre ligne)
```

Visuellement : on trace la tangente à la courbe au point x₀. Cette tangente est notre approximation linéaire.

**Notation dans le contexte des circuits :**
- `gd` = pente (conductance dynamique) → c'est le `m`
- `ideq` = courant équivalent → c'est le `b`
- `id = gd·vd + ideq` → c'est le `y = m·x + b`

---

## 5. Les Lois de Kirchhoff

### Kirchhoff's Current Law (KCL) — Loi des courants

**Principe : la somme des courants entrant dans un nœud est égale à la somme des courants sortant.**

L'analogie de l'eau est la plus intuitive : l'eau arrive dans un tuyau qui se sépare en deux. Toute l'eau qui entre doit sortir — on ne peut pas en créer ou en détruire.

```
Courant entrant = Courant sortant
ou de manière équivalente :
Σ courants au nœud = 0
```

### Les deux grandeurs fondamentales d'un circuit

En DSP classique, on travaille avec **le signal** (la tension). En modélisation de circuits, on travaille avec **deux** grandeurs indissociables :

| Grandeur | Analogie eau | Unité | Symbole |
|----------|-------------|-------|---------|
| **Tension** (voltage) | Pression | Volts (V) | `v` |
| **Courant** (current) | Débit | Ampères (A) | `i` |

C'est cette dualité courant/tension qui donne la "douceur" et les compromis naturels du modeling analogique. En DSP classique, on sépare ces deux aspects ; en circuit modeling, ils restent couplés, et c'est ce couplage qui produit le son caractéristique.

### Comment appliquer KCL concrètement

Pour chaque **nœud** du circuit (point de connexion entre composants), on écrit une équation qui dit "la somme des courants = 0".

Exemple au nœud v2 d'un diode clipper :
```
0 = ir1 - id1 + id2

En développant :
0 = gr1·(v2 - v1) - is1·(exp((0-v2)/vt1) - 1) + is2·(exp((v2-0)/vt2) - 1)
```

### Modified Nodal Analysis (MNA)

C'est la variante la plus utilisée. "Modified" signifie qu'on résout pour **certains courants en plus des tensions** aux nœuds. C'est nécessaire quand on a des sources de tension — elles imposent une tension mais le courant qu'elles délivrent est inconnu.

L'avantage de MNA : c'est systématique et automatisable. On peut écrire un programme qui prend une netlist et génère automatiquement les équations.

---

## 6. Le Diode Clipper (Premier exemple)

### Pourquoi cet exemple

Le diode clipper est le "Hello World" de la modélisation analogique :
- Assez simple pour être approchable
- Assez complexe pour exhiber les problèmes fondamentaux (non-linéarité implicite)
- Contient le pattern de résolution qu'on retrouve dans tous les circuits

### Le circuit

```
v1 (entrée) ── R1 ── v2 (sortie)
                       │
                      d1 (vers masse, pointe vers le haut)
                      d2 (vers masse, pointe vers le bas)
                       │
                      GND
```

C'est un **waveshaper sans mémoire** (memoryless nonlinear waveshaper) : pas de condensateur, donc pas d'état à mémoriser entre les samples.

### Les équations des composants

**Résistance :**
```
ir = vr / r = gr · (v2 - v1)     où gr = 1/r
```

**Diode (modèle exponentiel de Shockley) :**
```
id = is · (exp(vd/vt) - 1)
```
où `is` ≈ 1e-15 A (courant de saturation), `vt` ≈ 26 mV (tension thermique).

Pour les deux diodes en antiparallèle :
```
id1 = is1 · (exp((0 - v2)/vt1) - 1)     [diode 1 : anode à la masse]
id2 = is2 · (exp((v2 - 0)/vt2) - 1)     [diode 2 : anode à v2]
```

### L'équation nodale (KCL au nœud v2)

```
0 = gr1·(v2 - v1) - is1·(exp((0-v2)/vt1) - 1) + is2·(exp((v2-0)/vt2) - 1)
```

**Problème** : v2 apparaît à la fois dans les termes linéaires ET à l'intérieur des exponentielles. On ne peut pas isoler v2 algébriquement. C'est une **équation implicite en v2** — et c'est LE problème central de la modélisation analogique.

### La solution : Newton-Raphson

Puisqu'on ne peut pas résoudre analytiquement, on **itère** :

1. On fait un **guess initial** pour v2 (par exemple v2 = 0)
2. On **linéarise** les diodes autour de ce guess :
   - On calcule `gd = is/vt · exp(vd/vt)` (la pente locale)
   - On calcule `ideq = id - gd·vd` (l'offset)
   - Maintenant `id ≈ gd·vd + ideq` — c'est une ligne !
3. On **résout** l'équation linéarisée (on sait faire ça)
4. On **met à jour** v2 avec la nouvelle solution
5. On recommence à l'étape 2 jusqu'à convergence

C'est la **boucle Newton-Raphson (NR)**.

### La formule de résolution linéarisée

Après substitution des diodes linéarisées dans KCL :
```
v2 = (id1eq - id2eq + gr1·v1) / (gd1 + gd2 + gr1)
```

C'est une fraction simple — on peut la calculer en une opération.

### Le pseudo-code complet

```
init():
    is1 = is2 = 1e-15
    vt1 = vt2 = 26e-3
    gr1 = 1/2.2e3         // résistance de 2.2kΩ

tick(input):
    v1 = input
    v2 = 0                // guess initial

    NR loop (répéter N fois):
        // Diode 1
        vd1 = (0 - v2)
        ed1 = exp(vd1/vt1)
        id1 = is1·ed1 - is1
        gd1 = is1·ed1/vt1
        id1eq = id1 - gd1·vd1

        // Diode 2
        vd2 = (v2 - 0)
        ed2 = exp(vd2/vt2)
        id2 = is2·ed2 - is2
        gd2 = is2·ed2/vt2
        id2eq = id2 - gd2·vd2

        // Résoudre
        v2 = (id1eq - id2eq + gr1·v1) / (gd1 + gd2 + gr1)

    return v2  // sortie
```

### Le problème du guess initial

Avec v2 = 0 comme guess, la convergence est lente : **50 itérations** ! C'est 50 × 2 évaluations d'exponentielles — inacceptable pour du temps réel.

**Solution** : utiliser la valeur de v2 du sample précédent comme guess. Comme le signal audio change peu entre deux samples (surtout à 44.1 kHz), on est généralement très proche de la solution et on converge en **2-4 itérations**.

### Le résultat

Le diode clipper produit une courbe de transfert en S (sigmoïde) — il arrondit les crêtes positives et négatives du signal. Appliqué à une dent de scie, on obtient une forme d'onde adoucie aux extrêmes.

---

## 7. Les Condensateurs et Filtres (Mémoire)

### Le condensateur : un intégrateur de courant

Le condensateur est le composant fondamental pour le filtrage. Il **intègre le courant** qu'il reçoit :

```
vc' = (1/C) · ic        [la dérivée de la tension = courant / capacité]

En discret : vc += (1/C) · ic    [on accumule le courant]
```

C'est un composant **avec mémoire** : sa tension à l'instant t dépend de tout ce qui s'est passé avant. C'est ce qui crée le filtrage (passe-bas, passe-haut, etc.).

### Forme mathématique générale

On écrit ça sous la forme d'une **équation différentielle ordinaire (ODE)** :

```
y' = f(t, y)

où :
  y  = tension sur le condensateur (ce qu'on cherche)
  y' = dérivée de y (taux de changement)
  f  = fonction qui dépend du circuit (les courants)
```

`y` est **l'aire totale sous la courbe** de f — c'est l'intégrale.

### L'intégration pas à pas

On ne connaît pas l'intégrale exacte, alors on l'approxime en **petits pas** :

```
y₁ = y₀ + Δy₁     [on part de la condition initiale, on ajoute un petit morceau]
y₂ = y₁ + Δy₂     [on continue]
y₃ = y₂ + Δy₃     [etc.]
```

Chaque Δy est calculé comme : `Δy ≈ h · (moyenne pondérée des valeurs de f)`
où `h` est le pas de temps (1/sampleRate).

---

## 8. Les Méthodes d'Intégration Numérique

### Vue d'ensemble

C'est ici qu'on décide **comment** calculer ces Δy. Le choix de la méthode a un impact direct sur :
- La précision du son
- La stabilité numérique
- Le coût CPU
- Le warping fréquentiel (décalage des fréquences par rapport au circuit analogique)

### Forward Euler (m = 0)

```
Δy₄ = h · f(t₃, y₃)
```

On utilise **uniquement la valeur au point précédent**. C'est **explicite** : pas besoin de résoudre quoi que ce soit, on calcule directement.

**Problème** : instable pour les systèmes raides (stiff systems), ce qui inclut la plupart des circuits audio intéressants. Quasiment jamais utilisé en pratique.

### Backward Euler (m = 1)

```
Δy₄ = h · f(t₄, y₄)
```

On utilise **la valeur au point qu'on est en train de calculer**. C'est **implicite** : y₄ apparaît des deux côtés de l'équation, il faut résoudre.

**Avantages** : très stable, amortit les oscillations parasites.
**Inconvénients** : trop amorti, "mange" les hautes fréquences.

### Trapezoidal (m = 1/2) — LA méthode de référence

```
Δy₄ = h · (1/2 · f(t₃, y₃) + 1/2 · f(t₄, y₄))
```

On prend **la moyenne** des valeurs aux deux extrémités de l'intervalle. C'est implicite aussi (y₄ est des deux côtés).

**Pourquoi c'est la méthode standard en audio :**
- Pour les filtres linéaires, elle donne une correspondance fréquentielle exacte (pas de gain à Nyquist pour un passe-bas)
- Bonne stabilité
- Précision d'ordre 2

**Inconvénient** : elle peut produire des oscillations parasites (ringing) sur les discontinuités.

### La méthode "Mix" — L'innovation d'Andy

Andy introduit un paramètre `m` qui interpole continuellement entre Forward Euler et Backward Euler :

```
Δy₄ = h · ((1-m) · f(t₃, y₃) + m · f(t₄, y₄))
```

| m | Méthode |
|---|---------|
| 0 | Forward Euler |
| 1/2 | Trapezoidal |
| 1 | Backward Euler |

**L'intérêt** : on peut ajuster m entre 1/2 et 1 pour se rapprocher de la réponse continue idéale. Le graphe de la conférence montre qu'en augmentant m au-delà de 1/2, la courbe de réponse en fréquence se rapproche de la courbe analogique continue (la courbe jaune idéale).

**On peut même utiliser des valeurs de m différentes pour chaque composant** du circuit ! Par exemple, trapezoidal pour un filtre (pour matcher la phase), et un m plus élevé pour un autre composant (pour matcher le gain à Nyquist).

### Implicit Midpoint

```
Δy₄ = h · f(1/2·(t₃+t₄), 1/2·(y₃+y₄))
```

On évalue f au **point milieu** en temps ET en valeur. Pour des fonctions linéaires, ça donne le même résultat que trapezoidal. Pour des non-linéarités, c'est différent et potentiellement meilleur car on fait une **moyenne puis on applique la non-linéarité**, ce qui a un effet de lissage (low-pass sur l'entrée de la non-linéarité).

Notation importante pour comprendre la différence :
```
Trapezoidal :   1/2·tanh(a) + 1/2·tanh(b)
Midpoint :      tanh(1/2·(a + b))

Ces deux expressions ne sont PAS égales en général !
```

### Runge-Kutta 4 (RK4)

Méthode **explicite** d'ordre 4. Utilisée dans certains produits audio.

```
k₁ = f(t₀, y₀)
k₂ = f(t₁/₂, y₀ + k₁·h/2)
k₃ = f(t₁/₂, y₀ + k₂·h/2)
k₄ = f(t₁, y₀ + k₃)

y₁ = y₀ + h·(1/6·k₁ + 1/3·k₂ + 1/3·k₃ + 1/6·k₄)
```

**4 évaluations** de la non-linéarité par pas, mais pas besoin de résolution implicite. Donne de bons résultats mais Andy recommande plutôt les méthodes implicites d'ordre 2 + suréchantillonnage.

### Tableaux de Butcher

Les méthodes sont souvent résumées sous forme de tableaux de Butcher. Voici ceux présentés :

```
Implicit Midpoint :     Trapezoidal :         Mix TR/BE :
  1/2 | 1/2              0 |  0   0           0 |  0    0
  ----|----               1 | 1/2 1/2          1 | 1-m   m
      |  1                  | 1/2 1/2            | 1-m   m
```

Le point clé : si la diagonale du tableau contient des **zéros**, la méthode est explicite. Si elle contient des **valeurs non-nulles**, elle est implicite.

### Recommandation pratique d'Andy

Pour l'audio :
- Utiliser des méthodes **d'ordre 2 maximum** (trapezoidal, midpoint, mix)
- Ne pas aller au-delà — il vaut mieux **suréchantillonner** car ça aide aussi pour l'aliasing et le warping
- Mention de **TRBDF2** (Trapezoidal Rule followed by BDF2) développée par Gil Strang au MIT — une méthode prometteuse pour l'audio

---

## 9. Circuit Complet (Non-linéaire + Mémoire)

### Le circuit du diode clipper complet

On combine maintenant les filtres (condensateurs) avec les diodes non-linéaires :

```
v1 ── R1 ── v2 ── C1 ── v3
              │           │
              C2          R2 ── d1 ── d2
              │           │
             GND         GND
```

C'est un filtre passe-haut (C1/R1) + passe-bas (C2) + clipper (d1/d2) — le tout interconnecté.

### Les équations nodales

Au nœud v2 :
```
0 = gr1·(v2 - v1) - gc1·(v3 - v2) + ic1eq
```

Au nœud v3 :
```
0 = gc1·(v3 - v2) - ic1eq + gc2·(v3 - 0) - ic2eq + gr2·(v3 - 0) - id1 + id2
```

Où `gc` et `iceq` sont les termes linéarisés des condensateurs (voir section intégration).

### La discrétisation du condensateur

Le condensateur se transforme en un **équivalent linéaire** : une résistance + une source de courant.

```
ic_t = gc · vc_t - iceq_t
```

Où :
- `gc = C / (h·m)` — la conductance équivalente (le "m" de la résistance)
- `iceq_t` — le courant équivalent qui encode la mémoire (le "b" de la source)
- `h = 1/sampleRate` — le pas de temps

**Mise à jour de l'état** (après chaque sample) :
```
iceq_{t+1} = iceq_t + (1/m)·(gc·vc_t - iceq_t)
```

Visuellement, le condensateur discrétisé ressemble à une **petite résistance gc en parallèle avec une source de courant iceq**. C'est l'équivalent de Thévenin/Norton du condensateur.

### Le pseudo-code du circuit complet

```
init(sr):
    gr1 = 1/(2.2e3)
    gr2 = 1/(6.8e3)
    m = 1/2                    // trapezoidal
    minv = 1/m
    gc1 = 0.47e-6 · sr · minv  // condensateur 1
    gc2 = 0.01e-6 · sr · minv  // condensateur 2
    ic1eq = 0
    ic2eq = 0

tick(input):
    v1 = input
    v2 = 0; v3 = 0            // guess initial

    NR loop:
        // Linéariser les diodes (comme avant)
        // ...
        // Résoudre pour v2 et v3 (système 2×2 linéaire)
        v2 = solve(...)
        v3 = solve(...)

    // Mettre à jour les états des condensateurs
    ic1eq += minv · (gc1·(v3 - v2) - ic1eq)
    ic2eq += minv · (gc2·(v3 - 0) - ic2eq)

    return v3  // sortie
```

### Le résultat : pourquoi c'est supérieur

Le graphe comparatif de la conférence montre clairement :

| Full Model | DSP naïf (HP→LP→clip) |
|------------|----------------------|
| Crêtes arrondies naturellement | Arêtes vives |
| Transitions douces | Transitions abruptes |
| Dynamique (doux quand faible, brillant quand fort) | Comportement statique |
| Filtrage et clipping interagissent | Filtrage et clipping séparés |

**C'est ça le "graal" du modeling analogique** : le signal faible passe à travers le circuit quasi-linéairement (son doux et sombre), tandis que le signal fort active les non-linéarités (son brillant et saturé). Cette dynamique naturelle est ce qui fait sonner les bons circuits analogiques.

---

## 10. Formulations Alternatives

### 10.1 Analyse Nodale (MNA) — Ce qu'on a fait jusqu'ici

On résout pour les **tensions aux nœuds** (v2, v3, etc.). C'est la méthode la plus intuitive et celle qu'Andy enseigne en premier.

```
Variables : v2, v3 (tensions nodales)
Équations : KCL à chaque nœud
```

### 10.2 State Space

Au lieu de résoudre pour les tensions nodales, on résout pour les **tensions aux bornes des condensateurs** (vc1, vc2). C'est une formulation différente qui donne le **même résultat** mais avec des propriétés numériques différentes.

```
Variables : dvc1, dvc2 (changements de tension aux bornes des condensateurs)
Ensuite : vc1 = (dvc1 + icleq) / gc1
           vc2 = (dvc2 + ic2eq) / gc2
           v2 = k·vc2 + vc1     (reconstruction des tensions nodales)
           v3 = vc2
```

**Mise à jour d'état :**
```
icleq += (1/m) · dvc1
ic2eq += (1/m) · dvc2
```

Andy montre le filtre Sallen-Key (le filtre du Korg MS-20) résolu des deux manières. Les courbes se superposent parfaitement — même résultat, chemin différent.

**Quand utiliser state space :** utile quand on veut exprimer le circuit en termes de ses variables d'état naturelles (charges sur les condensateurs).

### 10.3 Méthode DK (Discrete Kirchhoff)

C'est une technique **d'optimisation** qui sépare les contributions linéaires et non-linéaires du circuit. L'idée clé :

1. **Résoudre les contributions linéaires** (en un seul pas, sans itération)
2. **Calculer un offset de tension** `vdk1 = v3 - 0` à partir des contributions linéaires
3. **Pré-calculer** les contributions non-linéaires dans une **table de lookup** (dk_table) en fonction de vdk1
4. **Ajouter** les contributions non-linéaires au résultat linéaire

**Le point crucial** (souligné en jaune dans les slides) :

> La contribution non-linéaire ne dépend QUE de vdk1, PAS de v1, icleq, ou ic2eq !

Cela signifie que la table non-linéaire est **indépendante des paramètres du circuit** (valeurs des condensateurs, résistances). On la pré-calcule une fois, et elle reste valide quelle que soit la position des potentiomètres.

**Pseudo-code DK :**
```
tick_dk(input):
    v1 = input
    // Étape 1 : résoudre linéaire (pas d'itération)
    v2 = solve for v2 using linear contributions
    v3 = solve for v3 using linear contributions
    vdk1 = v3 - 0

    // Étape 2 : lookup non-linéaire (pré-calculé)
    {id1, id2} = dk_table(vdk1)

    // Étape 3 : résoudre avec non-linéarités
    v2 = solve for v2 using original equations
    v3 = solve for v3 using original equations

    // Mise à jour états
    ic1eq += minv·(gc1·(v3 - v2) - ic1eq)
    ic2eq += minv·(gc2·(v3 - 0) - ic2eq)
```

### 10.4 Le problème des tables MIMO

Quand un circuit a **plusieurs non-linéarités couplées**, la table DK devient multidimensionnelle :

| Paramètres | Points par dim | Taille table | Viable ? |
|-----------|---------------|-------------|----------|
| 1 non-linéarité | 128 | ~128 points | Oui |
| 2 non-linéarités | 128 | ~16k points | Oui |
| 5 non-lin + 7 gains | 128 | ~5 pétaoctets | Non ! |
| 5 non-lin + 7 gains | 16 | ~5 gigaoctets | Non |

**La taille explose exponentiellement** avec le nombre de dimensions.

### Solutions au problème des tables MIMO

1. **Itérer** (Newton-Raphson classique) — toujours possible mais coûteux
2. **Réseaux de neurones** — approximer la table par un réseau entraîné
3. **Méthode PDK** (Parallel DK) — ce sur quoi Andy travaille : séparer les non-linéarités pour avoir `taille × n` au lieu de `taille^n`

### 10.5 Wave Digital Filters (WDF)

Mentionnés mais non détaillés. L'idée : représenter le circuit comme des **ondes voyageant** dans les connexions, avec une onde transmise (v + i) et une onde réfléchie (v - i). C'est analogue aux méthodes de différences finies.

---

## 11. Efficacité et Optimisation CPU

### Le problème

À chaque sample (22.7 µs à 44.1 kHz), on doit :
1. Linéariser toutes les non-linéarités
2. Résoudre un système d'équations linéaires
3. Itérer (Newton-Raphson) jusqu'à convergence
4. Mettre à jour les états
5. Calculer la sortie

Et si on itère en moyenne 4 fois, avec 5 non-linéarités, c'est 20 évaluations d'exponentielles par sample.

### Les stratégies d'optimisation

**1. Bon guess initial** : utiliser la sortie du sample précédent réduit drastiquement le nombre d'itérations (de 50 à 2-4).

**2. Pré-computation (tables DK)** : pour les circuits où c'est applicable, la partie non-linéaire est pré-calculée.

**3. Diviser le circuit en sous-blocs** : au lieu d'itérer sur tout le circuit, on peut séparer les parties non-linéaires et itérer sur des sous-systèmes plus petits.

**4. Niveaux de détail** : on peut retirer certaines non-linéarités pour avoir une version "basse CPU". Plus de non-linéarités = plus de CPU.

**5. Formulation en matrice** : pour les gros circuits, on formule le système comme une matrice et on utilise des solveurs optimisés (LU decomposition, etc.).

---

## 12. L'Aliasing et Comment le Réduire

### Pourquoi les circuits analogiques aliasent moins naturellement

Les circuits analogiques qui "sonnent bien" ont des non-linéarités qui **courbent** le signal de manière douce. Ces courbes douces produisent moins d'harmoniques hautes que des non-linéarités brutales. C'est un avantage naturel du modeling par rapport au hard clipping numérique.

### Stratégies anti-aliasing

**1. Suréchantillonnage (oversampling)** : la stratégie principale. On calcule à 2× ou 4× le sample rate, puis on filtre et décime. Ça aide aussi pour la réponse en phase et le warping fréquentiel.

**2. Contrôle de la douceur des fonctions** : les tables de lookup et les approximations de non-linéarités peuvent être rendues plus lisses. Plus c'est lisse, moins ça aliase. Il faut **matcher les dérivées aux points de jonction** pour maintenir la continuité.

**3. Anti-Derivative Anti-Aliasing (ADAA)** : technique qui utilise l'anti-dérivée de la fonction non-linéaire pour calculer une version anti-aliasée. Ajoute un **demi-sample de délai**.

Julian Parker et des chercheurs de Native Instruments ont montré qu'on peut **intégrer ce demi-sample de délai dans l'intégrateur trapezoidal** (qui donne déjà un demi-sample de délai), ce qui annule le problème. Mais c'est applicable seulement dans certaines situations.

**4. Rendu hors-ligne** : si on n'est pas en temps réel, on peut suréchantillonner massivement pour un aliasing quasi-nul.

---

## 13. Automatiser le Processus

### Pourquoi automatiser

Andy est un fervent défenseur de la **génération de code** automatique. L'idée :

1. On décrit le circuit à haut niveau (composants, connexions, valeurs)
2. Un programme génère automatiquement les équations
3. Il génère le code optimisé (C++, Python, Mathematica...)

### Les avantages

- **Itération rapide** : on peut tester de nouvelles idées et voir le résultat immédiatement
- **Multi-cible** : même spécification → C++, Python, Mathematica
- **Réduction des erreurs** : les équations dérivées à la main sont sujettes aux erreurs ; l'automatisation est systématique
- **Amélioration du solveur** : si on améliore la méthode de résolution, on peut "re-cruncher" toutes les définitions de circuits existantes et obtenir un meilleur résultat partout
- **Niveaux de détail** : on peut automatiquement générer des versions avec ou sans certaines non-linéarités (version HQ vs version low-CPU)

### Le workflow d'Andy chez Cytomic

```
Définition du circuit (haut niveau)
         │
         ▼
Générateur d'équations
         │
         ▼
Classe C++ templatée (auto-générée)
         │
         ▼
Compilation → Plugin audio
```

---

## 14. Terminologie Récapitulative

| Terme | Signification |
|-------|--------------|
| **Intégration numérique d'EDO non-linéaires** | Le nom formel de ce qu'on fait : résoudre numériquement des équations différentielles ordinaires qui contiennent des termes non-linéaires |
| **Discrétisation** | Transformer un circuit continu en équations qu'on résout à un sample rate fixe |
| **Équivalent de Thévenin/Norton** | Transformer un condensateur discrétisé en résistance + source de courant |
| **Équation implicite** | Équation où la variable qu'on cherche apparaît des deux côtés (nécessite itération si non-linéaire) |
| **Équation explicite** | La solution ne dépend que de valeurs connues (pas d'itération nécessaire) |
| **Newton-Raphson (NR)** | Méthode itérative pour trouver le zéro d'une fonction non-linéaire en linéarisant successivement |
| **Linéarisation** | Approximer une courbe par sa tangente locale (y = mx + b) |
| **MNA** | Modified Nodal Analysis — résoudre pour les tensions aux nœuds |
| **State Space** | Résoudre pour les tensions aux bornes des condensateurs |
| **DK Method** | Discrete Kirchhoff — séparer contributions linéaires et non-linéaires |
| **PDK** | Parallel DK — méthode en développement par Andy pour séparer les non-linéarités couplées |
| **MIMO Table** | Multiple Input Multiple Output — table de lookup multidimensionnelle |
| **Stiff System** | Système où certaines constantes de temps sont très différentes, rendant la résolution numérique difficile |
| **Warping** | Décalage des fréquences entre le modèle discret et le circuit analogique continu |
| **ADAA** | Anti-Derivative Anti-Aliasing |

---

## 15. Checklist pour un Projet de Modélisation Concret

### Phase 0 : Prérequis

- [ ] Avoir le **schéma complet** du circuit avec les valeurs de tous les composants
- [ ] Identifier la **netlist** (quoi est connecté à quoi)
- [ ] Numéroter les **nœuds** du circuit
- [ ] Identifier les **composants non-linéaires** (diodes, transistors, tubes)
- [ ] Identifier les **composants à mémoire** (condensateurs, inductances)
- [ ] Identifier les **paramètres** (potentiomètres)
- [ ] Choisir un langage (Python pour le prototypage, C++ pour le temps réel)

### Phase 1 : Validation sur des sous-circuits simples

- [ ] Implémenter un **diode clipper** seul (2 diodes + 1 résistance)
- [ ] Vérifier le résultat avec SPICE
- [ ] Implémenter un **filtre RC simple** (1 résistance + 1 condensateur)
- [ ] Vérifier la réponse en fréquence
- [ ] Combiner les deux (clipper + filtre)
- [ ] Comparer avec la version "filtre puis clip" pour voir la différence

### Phase 2 : Modélisation du circuit cible

- [ ] Écrire les **équations KCL** à chaque nœud
- [ ] Identifier les termes linéaires et non-linéaires
- [ ] Choisir la méthode d'intégration (trapezoidal recommandé pour commencer)
- [ ] Discrétiser les condensateurs (calculer gc et iceq)
- [ ] Linéariser les non-linéarités (calculer gd et ideq pour chaque composant non-linéaire)
- [ ] Formuler le **système d'équations linéaires** résultant
- [ ] Implémenter la boucle Newton-Raphson
- [ ] Implémenter la mise à jour des états

### Phase 3 : Validation et optimisation

- [ ] Comparer avec SPICE (réponse en fréquence, réponse transitoire)
- [ ] Mesurer le **nombre d'itérations** NR en moyenne
- [ ] Optimiser le guess initial
- [ ] Profiler le CPU
- [ ] Tester le suréchantillonnage (2×, 4×)
- [ ] Écouter et comparer avec le circuit réel si possible

### Phase 4 : Vers le produit final

- [ ] Porter en C++ si le prototype est en Python
- [ ] Implémenter le contrôle des paramètres (potentiomètres) avec smoothing
- [ ] Optimiser (DK si applicable, tables de lookup, SIMD)
- [ ] Tester la stabilité sur tout le range des paramètres
- [ ] Mesurer l'aliasing et ajouter de l'oversampling si nécessaire
- [ ] Intégrer dans un framework audio (JUCE, iPlug2, etc.)

---

## Références et Ressources

*(Mentionnées par Andy Simper dans sa conférence)*

- **Cytomic** : site de l'auteur avec ses plugins (The Drop, The Glue, etc.) et des technical papers
- **ADC (Audio Developer Conference)** : conférences annuelles, archives en ligne
- **SPICE** : simulateur de circuits de référence pour valider les modèles
- **Gil Strang (MIT)** : travaux sur TRBDF2 et méthodes numériques
- **Julian Parker (Native Instruments)** : travaux sur ADAA
- **Tableau de Butcher** : référence sur Wikipedia pour les méthodes Runge-Kutta
- **Wave Digital Filters** : couvert dans d'autres talks ADC

---

*Document basé sur la conférence "Analog Modeling" d'Andy Simper (Cytomic) à l'ADC 2020, incluant la transcription complète et les slides de la présentation.*
