#pragma once

/** GitHub Pages firmware installer (issue #15). */
#ifndef FW_UPDATE_URL
#if defined(MM1_BOARD_P4)
#define FW_UPDATE_URL "https://verlab.github.io/mm1-black/firmware/?board=p4"
#else
#define FW_UPDATE_URL "https://verlab.github.io/mm1-black/firmware/"
#endif
#endif
