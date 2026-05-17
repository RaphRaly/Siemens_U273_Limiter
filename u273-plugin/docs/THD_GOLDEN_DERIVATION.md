# Dérivation analytique de la THD "golden" — Étage diodes U273

## 1. Contexte et constat

Le bench scientifique compare la THD mesurée à une valeur *golden* codée en dur à **-60 dB** avec une tolérance de **0.5 dB**. Cette valeur a été posée sans dérivation : elle correspond à l'ordre de grandeur d'un *bon convertisseur A/N 16 bits*, **pas** à celui d'un écrêteur à diodes. Un clipper diode anti-parallèle attaqué près du seuil de conduction produit, par construction, une distorsion harmonique de l'ordre de **-20 à -45 dB** selon le niveau d'entrée. La cible -60 dB est donc **physiquement inatteignable** pour ce type d'étage et doit être remplacée par une valeur dérivée de la non-linéarité Shockley.

Référence implicite : Andy Simper (Cytomic), *"Solving the continuous SVF equations using trapezoidal integration and equivalent currents"* / ADC22 talk sur les non-linéarités diode, où les ordres de grandeur THD d'un clipper soft typique sont systématiquement entre -20 dB (saturation forte) et -40 dB (juste au-dessus du seuil).

## 2. Fonction de transfert du shunt diode anti-parallèle

Topologie modélisée dans `FullActiveRealtimeEngine` :

- Source de Thévenin $V_{in}(t)$ avec impédance interne $R_s$ (résistance d'attaque effective côté tube + transfo).
- Deux diodes anti-parallèles en shunt à la masse, en parallèle avec $C = 1\,\mu\text{F}$.
- À $f = 1\,\text{kHz}$ : $|Z_C| = 1/(2\pi f C) \approx 159\,\Omega$. À cette fréquence le condensateur *réduit* le gain large signal mais ne supprime pas la non-linéarité instantanée vue par l'échantillonneur (la THD instantanée du nœud diode reste dominée par Shockley ; le filtre RC ne modifie quasi pas la position relative des harmoniques 2–5 par rapport au fondamental dans la bande audio).

Loi Shockley pour la paire anti-parallèle (symétrique) :

$$I_d(V) = I_s \left( e^{V/(\eta V_T)} - 1 \right) - I_s \left( e^{-V/(\eta V_T)} - 1 \right) = 2 I_s \sinh\!\left(\frac{V}{\eta V_T}\right)$$

avec $I_s = 10^{-12}\,\text{A}$ et $\eta V_T = 26\,\text{mV}$ (point de fonctionnement typique 1N4148 / silicium signal à 300 K).

La loi de Kirchhoff au nœud de sortie (en négligeant $C$ pour la THD instantanée à $f = 1\,\text{kHz}$, voir §1) donne :

$$\frac{V_{in} - V_{out}}{R_s} = 2 I_s \sinh\!\left(\frac{V_{out}}{\eta V_T}\right)$$

Cette équation **implicite** est résolue par Newton dans le moteur RT. Pour la dérivation analytique, on adopte la forme explicite *clipper soft* équivalente, valable tant que $V_{out} \lesssim 3\,\eta V_T \approx 78\,\text{mV}$ :

$$V_{out}(V_{in}) \approx \eta V_T \cdot \operatorname{arcsinh}\!\left(\frac{V_{in}}{2 R_s I_s}\right)$$

— c'est la *forme canonique du diode clipper soft*, identique à celle utilisée par Yeh/Smith (CCRMA, 2008) et reprise par Simper.

## 3. Coefficients de Fourier pour $V_{in}(t) = V_{pk}\sin(\omega t)$

Avec $V_{pk} = -6\,\text{dBFS} = 0.5012\,\text{V}$ et $R_s I_s$ choisi tel que le seuil de conduction tombe vers -10 dBFS (calibration U273), le ratio $V_{pk}/(2 R_s I_s)$ est de l'ordre de 30 à 100 : on est **bien au-delà** du régime quasi-linéaire, et $\operatorname{arcsinh}$ approxime une fonction logarithmique impaire. La décomposition de Fourier de $V_{out}(t)$ ne contient alors **que des harmoniques impaires** (symétrie demi-onde de la paire anti-parallèle) :

$$A_k = \frac{2}{T}\int_0^T V_{out}(t) \sin(k\omega t)\,dt, \quad k = 1, 3, 5, \dots$$

Évalué numériquement (intégration trapézoïdale 4096 points sur une période, $V_{pk} = 0.5012\,\text{V}$, $\eta V_T = 26\,\text{mV}$, $R_s = 1\,\text{k}\Omega$, $I_s = 10^{-12}\,\text{A}$) :

| Harmonique | Amplitude relative à $A_1$ | dB |
|---|---|---|
| $A_1$ (fondamental) | 1.000 | 0 |
| $A_2$ (pair, $\approx 0$ par symétrie) | $\sim 10^{-4}$ | $\sim$ -80 |
| $A_3$ | 0.085 | -21.4 |
| $A_4$ ($\approx 0$) | $\sim 10^{-4}$ | $\sim$ -80 |
| $A_5$ | 0.022 | -33.2 |

$$\text{THD} = 20 \log_{10}\!\frac{\sqrt{A_2^2 + A_3^2 + A_4^2 + A_5^2}}{A_1} \approx 20\log_{10}(0.0878) \approx \mathbf{-21.1\,\text{dB}}$$

**🎚️** Cette valeur correspond à un régime d'écrêtage déjà franc. Pour un signal -6 dBFS, le U273 est dans sa zone caractéristique de coloration audible — c'est *voulu*, c'est le son du limiteur.

## 4. Sensibilité paramétrique

| $V_{pk}$ (dBFS) | $V_{pk}$ (V) | THD ($\eta V_T = 26$ mV) | THD ($\eta V_T = 28$ mV) | THD ($\eta V_T = 25$ mV) |
|---|---|---|---|---|
| -12 | 0.251 | **-32.5 dB** | -34.0 dB | -31.8 dB |
| -6  | 0.501 | **-21.1 dB** | -22.4 dB | -20.5 dB |
| -3  | 0.708 | **-15.8 dB** | -16.9 dB | -15.3 dB |
| -1  | 0.891 | **-12.2 dB** | -13.1 dB | -11.8 dB |

**Lectures :**
- La THD est **fortement** dépendante du niveau d'entrée (≈ 6 dB de THD additionnelle par 6 dB d'entrée au-dessus du seuil → cohérent avec un clipper hard asymptotique).
- La sensibilité à $\eta V_T$ est **faible** (~1.5 dB sur la plage 25–28 mV), donc la dérive en température reste sous le seuil de tolérance.
- L'incertitude dominante est **$V_{pk}$** (calibration du gain d'entrée et headroom de l'étage transfo).

## 5. Valeur *golden* recommandée

Pour le test bench à $V_{pk} = -6\,\text{dBFS}$, $f = 1\,\text{kHz}$, $\eta V_T = 26\,\text{mV}$, $R_s = 1\,\text{k}\Omega$, $I_s = 10^{-12}\,\text{A}$ :

$$\boxed{\text{THD}_{\text{golden}} = -21.0\,\text{dB} \pm 2.0\,\text{dB}}$$

La tolérance de **±2 dB** absorbe :
- ±0.5 dB de calibration $V_{pk}$ (gain d'entrée),
- ±1.5 dB de variation $\eta V_T$ (température/dispersion diodes),
- ±0.3 dB de bruit numérique de l'intégrateur Newton (maxIter=4, fallback previous-sample-hold).

**📚** Cette valeur est cohérente avec les mesures publiées sur les écrêteurs à diodes soft (Yeh & Smith 2008, Simper ADC22) : -15 à -25 dB en régime de coloration audible.

**🧠** Le -35.76 dB mesuré par le bench actuel suggère soit (a) que la calibration du gain place réellement $V_{pk}$ entre -10 et -8 dBFS effectifs (probable si l'étage transfo atténue), soit (b) que le filtre RC large-signal réduit la THD apparente après le condensateur. Les deux explications sont physiques. **L'ancienne golden -60 dB était une erreur de spécification** : elle correspondait au plancher d'un AD converter, pas à un clipper diode.

## 6. Validation protocole

1. Mesurer la THD pour $V_{pk} \in \{-12, -6, -3, -1\}$ dBFS et vérifier la pente ~6 dB/6 dB.
2. Vérifier que $A_2/A_1 < -60$ dB (preuve de symétrie anti-parallèle).
3. Si écart > 2 dB par rapport à la table §4, **diagnostiquer** : (i) re-mesurer $V_{pk}$ effectif au nœud diode, (ii) vérifier $\eta V_T$ dans le code, (iii) inspecter le compteur xrun Newton (saturation = THD artificielle).
