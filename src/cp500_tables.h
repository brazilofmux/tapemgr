// utf/tr_utf8_cp500.txt
//
// 256 code points.
// 3 states, 194 columns, 810 bytes
//
#define TR_CP500_START_STATE (0)
#define TR_CP500_ACCEPTING_STATES_START (3)
extern const unsigned char tr_cp500_itt[256];
extern const unsigned short tr_cp500_sot[3];
extern const unsigned short tr_cp500_sbt[274];

extern const uint8_t cp500_to_utf8_lengths[256];
extern const uint8_t cp500_to_utf8_bytes[256][4];
