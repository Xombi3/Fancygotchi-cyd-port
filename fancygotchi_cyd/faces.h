#pragma once
// ================================================================
//  faces.h — Switchable face packs for FancyGotchi CYD v2
//
//  HOW TO SWITCH PACKS AT RUNTIME:
//    Tap TOP-RIGHT on screen. (Top-LEFT cycles themes.)
//
//  HOW TO ADD YOUR OWN PACK:
//    1. Copy any block below and give it a new name.
//    2. Replace the strings with whatever you like.
//    3. Max 47 characters per face. Any UTF-8 TFT font 2 can render.
//    4. FACE_SET_COUNT updates automatically — nothing else to change.
//
//  MOOD SLOTS (3 variants each, cycled every 8s):
//    idle / scan / excited / happy / intense / bored / sleep
// ================================================================

struct FaceSet {
  const char* name;
  const char* idle[3];
  const char* scan[3];
  const char* excited[3];
  const char* happy[3];
  const char* intense[3];
  const char* bored[3];
  const char* sleep[3];
};

static const FaceSet FACE_SETS[] = {

  // ── classic ── original pwnagotchi-style ─────────────────────────
  { "classic",
    { "(^‿‿^)",   "( -_-)",    "(= =)"      },  // idle
    { "(°▃▃°)",   "( ⊙ ‿ ⊙)", "(^‿‿^)"     },  // scan
    { "(ᵔ◡◡ᵔ)",  "(✜‿‿✜)",   "(ᵔ◡ᵔ)"      },  // excited
    { "(◕‿‿◕)",  "(^‿‿^)",    "( ◕◡◕)"     },  // happy
    { "(°▃▃°)",   "(⚆_⚆)",    "( ☉_☉)"     },  // intense
    { "(-__-)",   "(ب__ب)",   "(-_-)"       },  // bored
    { "(⇀‿‿↼)",  "( -.- )",   "(-zzz-)"    },  // sleep
  },

  // ── kawaii ── soft Japanese-style ────────────────────────────────
  { "kawaii",
    { "(◠‿◠)",    "(｡-_-｡)",  "(－_－)"    },
    { "(｀・ω・´)","(°▽°)",    "(＾▽＾)"    },
    { "(≧◡≦)",   "(✧ω✧)",    "(★^O^★)"   },
    { "(´▽｀)",   "(⌒‿⌒)",   "(◍•ᴗ•◍)"   },
    { "(ಠ_ಠ)",   "(ó_ò)",    "(╬Ò﹏Ó)"    },
    { "(-ω-)",    "(´-ω-`)",  "(＿＿;)"    },
    { "(；￣Д￣)","(ᴗ_ᴗ)",   "( -zzz-)"   },
  },

  // ── ghost ── spooky vibes ─────────────────────────────────────────
  { "ghost",
    { "( ˘▾˘)",   "( ._. )",   "( °□°)"    },
    { "( ◎_◎)",   "( o.o )",   "( -.-)~"   },
    { "( ^v^ )",  "( *_* )",   "( ^u^ )"   },
    { "( uvu )",  "( ◠‿◠)",   "( ^-^ )"   },
    { "( o_O )",  "( !_! )",   "( >_< )"   },
    { "( .___. )","( ¬_¬)",    "( meh )"   },
    { "( zzz )",  "( -_- )",   "( ...z )"  },
  },

  // ── robot ── mechanical / glitchy ────────────────────────────────
  { "robot",
    { "[- _ -]",  "[ . _ . ]", "[-_-]"      },
    { "[o_O]",    "[? _ ?]",   "[=_=]"       },
    { "[^o^]",    "[!!!]",     "[*_*]"       },
    { "[^_^]",    "[ :) ]",    "[>v<]"       },
    { "[>_<]",    "[X_X]",     "[!_!]"       },
    { "[._.]",    "[-_-]",     "[zzz]"       },
    { "[Zzz]",    "[- -]",     "[...z]"      },
  },

  // ── demon ── edgy dark faces ──────────────────────────────────────
  { "demon",
    { "(¬‿¬)",    "( ._. )",   "( o_o)"     },
    { "(ò_ó)",    "(ಠ_ಠ)",    "(¬_¬)"       },
    { "(>:D)",    "(ò‿ó)",    "(*•w•*)"     },
    { "(¬‿¬)",    "(>:)",      "( ^_- )"    },
    { "(ಥ_ಥ)",   "( >_< )",   "(>o<)"       },
    { "(¬_¬)",    "( -.- )",   "(-_-)"       },
    { "( -_- )",  "( .zZ )",   "(-zzz-)"    },
  },

  // ── pixel ── simple ASCII faces ───────────────────────────────────
  { "pixel",
    { ":‑)",      ":‑|",       ":|"          },
    { ":‑o",      ":‑/",       "=‑="         },
    { ":‑D",      "=D",        ":-)))))"     },
    { ":)",       "=)",        ":‑]"         },
    { ">:‑(",     ">:O",       ":‑@"         },
    { ":‑/",      "-_-",       ":|"          },
    { "-.‑",     ":|zzz",      "=.="         },
  },

};

#define FACE_SET_COUNT  ((int)(sizeof(FACE_SETS)/sizeof(FACE_SETS[0])))

static uint8_t _faceSetIdx = 0;

inline const FaceSet* activeFaceSet()     { return &FACE_SETS[_faceSetIdx]; }
inline const char*    activeFaceSetName() { return FACE_SETS[_faceSetIdx].name; }
inline uint8_t        activeFaceSetIdx()  { return _faceSetIdx; }
inline void           faceSetCycleNext()  { _faceSetIdx = (_faceSetIdx + 1) % FACE_SET_COUNT; }
inline void           faceSetSet(uint8_t idx) { if (idx < FACE_SET_COUNT) _faceSetIdx = idx; }
