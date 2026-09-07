PTAR GW16H UNIFIEDREC3 SAFEPOINT11 / FUSEDDETAIL1
===================================================

Branch base: SAFEPOINT8/TDETAIL4_1. SAFEPOINT9/10 parameter experiments are intentionally not part of this lineage. Recorder/state/QSV fixes from SAFEPOINT2-5 remain retained.

WHY FUSEDDETAIL1
----------------
Field videos localized the remaining FG shimmer mainly to the selection circle at the character feet and repetitive floor grilles/vents. Laboratory sweeps showed that further global Guard/trust tuning is not a robust solution.

The retained approach is a local stabilizer fused directly into the existing FG compute shader. It reuses the four bilinear texels already loaded from each REAL endpoint to estimate local 2x2 green-channel range, then limits only excessive GENERATED deviation from the unwarped REAL blend.

COST-FIRST DESIGN
-----------------
- no new texture resource
- no new UAV
- no new resource binding
- no new Dispatch
- source-level .Load token count remains exactly 7, identical to TDETAIL4
- REAL frames are untouched
- only a small ALU/min/max/lerp tail is added to the existing GENERATED shader

The source-level structure therefore adds no texture-fetch site. Exact GPU time still requires the one real GTX 960M test; no unmeasured hardware timing is claimed by the package.

VISUAL POLICY
-------------
FUSEDDETAIL1 never sharpens. For profile 3 it computes a 0.30..1.00 scale and can only move the generated RGB result toward the unwarped REAL blend when local GENERATED deviation exceeds the endpoint 2x2 range budget. Stable/low-motion content is nearly untouched in the lab corpus.

PROFILE 2 QUALITY IS BYPASSED
-----------------------------
The stabilizer numerator contains a profile gate based on G. At QUALITY G=.35, its extra +10*(G-.25) term is at least +1.0 while normalized green deviation is <=1, forcing z=1 and therefore scale=1. QUALITY remains mathematically unchanged by the fused stabilizer.

LAB RESULT / TARGET
-------------------
On the TDETAIL4 field corpus used for development, the fused proxy materially reduced high-motion excursion on the selection circle and floor grilles while leaving low-motion pixel change near zero. The test is a proxy because the recorded video does not expose exact runtime motion vectors or pre-present surfaces. Hardware visual validation is still required.

INSTALLATION
------------
1. Fermer le jeu.
2. Lancer 01-INSTALL_GW16.bat.
3. Lancer 02-VERIFY_INSTALL.bat et exiger VERIFY=PASS.

UNIQUE GATE MATERIEL
--------------------
1. PTAR 1280x720 -> 1920x1080 + FG actif.
2. CTRL+F8: selectionner le profil 3 CONSERVATIVE.
3. Reproduire un deplacement/camera avec le cercle au pied du personnage et les grilles visibles.
4. Enregistrer 10-15 s avec CTRL+F9.
5. Envoyer la video et le ZIP de 04-COLLECT_RESULTS.bat.

Le meme run servira a juger simultanement le gain visuel et l'absence de regression de pacing/recorder.
