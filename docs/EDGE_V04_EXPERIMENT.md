# EDGE v04 Experimental Integration

EDGE v04 is a controlled STEP-WENO insertion around the recovered EDGE v03 interface.
It is not a reconstruction of missing EDGE v03 code.

Compile-time modes:
- 0: EDGE v03 baseline
- 1: SW1
- 2: SW2
- 3: SW3

Promotion requires:
1. no image-quality regression versus EDGE v03;
2. actual removal/reduction of existing EDGE correction complexity;
3. acceptable GTX 960M GPU cost.

Until then, EDGE v03 remains locked.
