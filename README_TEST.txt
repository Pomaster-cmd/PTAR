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
On the TDETAIL4 field corpus used for development, the fused proxy materially reduced high-motion excursion on the selection circle and floor grilles while leaving low-motion pixel change near zero. The test is a proxy because the recorded video does not expose exact runtime motion vectors or pre-present surfaces. SAFEPOINT11/FUSEDDETAIL1 is already the promoted hardware-validated baseline. HUDREC1 does not reopen that visual-quality gate.

INSTALLATION
------------
1. Fermer le jeu.
2. Lancer 01-INSTALL_GW16.bat.
3. Lancer 02-VERIFY_INSTALL.bat et exiger VERIFY=PASS.

BASELINE HARDWARE STATUS
------------------------
SAFEPOINT11/FUSEDDETAIL1 is already hardware validated. No new broad visual/pacing regression run is required for HUDREC1.



HUDREC1 - VIDEO AVEC HUD / FPS
--------------------------------
Cette variante conserve SAFEPOINT11/FUSEDDETAIL1 et modifie uniquement la source de capture du recorder natif FG OFF : le recorder lit maintenant le BackBuffer0 du presenter PTAR visible, le meme domaine visuel que la capture F9, au lieu du backbuffer jeu pre-HUD.

Resultat attendu : quand Overlay=1, les videos CTRL+F9 contiennent le HUD PTAR et notamment la valeur FPS. Le chemin recorder FG ON et l'algorithme FG sont inchanges.

MARQUEUR FG (PETITS CARRES)
----------------------------
Lancer diag\FG_MARKER_VISIBILITY.bat pour lire, activer ou desactiver uniquement les petits carres clignotants de cadence FG. Ce reglage pilote VBlankDiagnostics et ne coupe pas le HUD/FPS.


GATE MATERIEL HUDREC1
---------------------
Une seule session materielle tres courte est necessaire apres les gates labo :
1. laisser Overlay=1 ;
2. CLIP A - FG OFF : enregistrer environ 5 s avec CTRL+F9, puis verifier dans le MP4 que le HUD PTAR et la valeur FPS sont visibles ;
3. CLIP B - FG ON : activer FG avec CTRL+F6, enregistrer environ 5 s avec CTRL+F9, puis verifier que le HUD PTAR et la valeur FPS restent visibles ;
4. optionnel : utiliser diag\\FG_MARKER_VISIBILITY.bat pour masquer/afficher uniquement les petits carres FG et verifier que le HUD/FPS reste visible.

Le clip A est le gate fonctionnel principal de HUDREC1, car le patch binaire modifie uniquement la source du recorder FG OFF. Le clip B confirme que le chemin FG ON, laisse inchange, reste bien post-HUD. Aucune suite de regression materielle large n'est demandee.


GATE UNIVERSAL1 - CIBLAGE MULTI-JEU
====================================
PTAR n'est pas lie a Warhammer. Le runtime HUDREC1 est conserve byte-identique.

Test recommande :
1. Extraire le pack a cote de l executable x64 du jeu, ou lancer :
   01-INSTALL_GW16.bat "C:\chemin\vers\Game.exe"
2. 02-VERIFY_INSTALL.bat doit afficher VERIFY=PASS.
3. Verifier que win81_nis.ini installe contient TargetExe=<nom reel du jeu>.
4. Lancer le jeu D3D11 et confirmer affichage HUD / reconstruction.
5. CTRL+F9 : confirmer video avec HUD/FPS.

Warhammer/Inquisitor est uniquement un titre de validation historique.
