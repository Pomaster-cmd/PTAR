PTAR RC55 - BOUND PHYSICAL IDEMPOTENCE - TEST TERRAIN
=====================================================

Base fonctionnelle conservee:
- RC51 presenter / Windowed-Borderless;
- RC52 raster-state quarantine;
- aucun changement NVENC/FG/shader/runtime.

Constat RC54 terrain reproduit en labo:
- target physique: 1280x720;
- appel viewport deja physique: 1280x720;
- RC52 le remappait a tort en 853.333x480 quand le primary target etait lie;
- le scissor 1280x720 devenait 854x480.

Correction RC55:
- UNIQUEMENT le viewport plein ecran exactement physique 1280x720 et le scissor 0,0,1280,720 sont rendus idempotents lorsqu'ils arrivent avec le primary target lie;
- 1920x1080 logique continue d'etre converti en 1280x720;
- les petits viewports gardent le comportement RC52 existant;
- la quarantaine RC52 des etats ambigus hors target est conservee.

Validation labo:
- signature RC52 1280x720 -> 853.333x480 reproduite: PASS;
- RC55 1280x720 -> 1280x720: PASS;
- repetition 32 fois sans derive: PASS;
- 1920x1080 -> 1280x720: PASS;
- replay bound/unbound/bound: PASS;
- hote de regression RC52: PASS;
- hote production RC41: PASS;
- stress: 64 cycles RC55 + 64 cycles RC52 PASS.

RC55 DLL SHA-256:
271187ab9fd82b6829c52a667223f241ec79e98fa340b13c9a72270142156949

Procedure terrain:
1. Fermer Warhammer.
2. Extraire le dossier du pack dans le dossier du jeu OU dans un sous-dossier direct.
3. Lancer 00-A-INSTALL_RC55_BOUND_PHYSICAL_FIX.bat et exiger PASS.
4. Lancer le jeu normalement et verifier l'echelle de la GUI interne sur le meme ecran que RC54.
5. Fermer le jeu normalement.
6. Lancer 00-C-COLLECT_RC55_RESULTS.bat.
7. Fournir PTAR_RC55_RESULTS_*.zip et, si possible, une capture du meme ecran.

Rollback:
- 00-D-ROLLBACK_RC55_TO_PREVIOUS.bat restaure exactement la sidecar active avant RC55, uniquement si la sauvegarde et son hash sont coherents.

Registre AffectGuiResolution:
- RC55 tente de retirer proprement l'essai RC53 uniquement si un manifeste d'ownership RC53 valide est retrouve et si la valeur courante correspond encore a la valeur installee par RC53;
- sinon RC55 ne modifie pas cette valeur arbitrairement.

Securite:
- l'installateur refuse une base runtime/presenter inconnue;
- il refuse une sidecar inconnue;
- il sauvegarde la sidecar precedente avant remplacement;
- aucun effacement recursif du dossier du jeu;
- le collecteur ne supprime que son propre dossier temporaire.
