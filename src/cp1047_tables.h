// utf/tr_utf8_cp1047.txt
//
// 256 code points.
// 3 states, 194 columns, 810 bytes
//
#define TR_CP1047_START_STATE (0)
#define TR_CP1047_ACCEPTING_STATES_START (3)
extern const unsigned char tr_cp1047_itt[256];
extern const unsigned short tr_cp1047_sot[3];
extern const unsigned short tr_cp1047_sbt[274];

extern const uint8_t cp1047_to_utf8_lengths[256];
extern const uint8_t cp1047_to_utf8_bytes[256][4];
