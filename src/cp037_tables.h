// utf/tr_utf8_cp037.txt
//
// 256 code points.
// 3 states, 194 columns, 810 bytes
//
#define TR_CP037_START_STATE (0)
#define TR_CP037_ACCEPTING_STATES_START (3)
extern const unsigned char tr_cp037_itt[256];
extern const unsigned short tr_cp037_sot[3];
extern const unsigned short tr_cp037_sbt[274];

extern const uint8_t cp037_to_utf8_lengths[256];
extern const uint8_t cp037_to_utf8_bytes[256][4];
