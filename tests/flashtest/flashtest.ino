// Program intended to be used for testing dw-link

#define LED SCK

#define SEQ {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255}

#define AR(num) const byte a ## num[] PROGMEM = SEQ


AR(00);
AR(01);
#if (FLASHEND+1 <= 1024)
#define FILLED 2
const byte* allar[]  = {a00, a01};
#else
AR(02); AR(03);
#if (FLASHEND+1 <= 2048)
#define FILLED 4
const byte* allar[]  = {a00, a01, a02, a03};
#else
AR(04); AR(05); AR(06); AR(07); AR(08); AR(09); AR(10);
#if (FLASHEND+1 <= 4096)
#define FILLED 11
const byte* allar[]  = {a00, a01, a02, a03, a04, a05, a06, a07, a08, a09, a10};
#else
AR(11); AR(12); AR(13); AR(14); AR(15); AR(16); AR(17); AR(18); AR(19); AR(20); 
AR(21); AR(22); AR(23); AR(24); AR(25); AR(26);
#if (FLASHEND+1 <= 8192)
#define FILLED 27
const byte* allar[]  = {a00, a01, a02, a03, a04, a05, a06, a07, a08, a09, 
    a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, 
    a20, a21, a22, a23, a24, a25, a26};
#else
AR(27); AR(28); AR(29); AR(30); AR(31); AR(32); AR(33); AR(34); AR(35); AR(36); 
AR(37); AR(38); AR(39); AR(40); AR(41); AR(42); AR(43); AR(44); AR(45); AR(46); 
AR(47); AR(48); AR(49); AR(50); AR(51); AR(52); AR(53); AR(54); AR(55); AR(56); 
AR(57); AR(58); AR(59); 
#if (FLASHEND+1 <= 16384)
#define FILLED 58
const byte* allar[]  = {a00, a01, a02, a03, a04, a05, a06, a07, a08, a09, 
    a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, 
    a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, 
    a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, 
    a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, 
    a50, a51, a52, a53, a54, a55, a56, a57};
#else
AR(60); AR(61); AR(62); AR(63); AR(64); AR(65); AR(66); AR(67); AR(68); AR(69); 
AR(70); AR(71); AR(72); AR(73); AR(74); AR(75); AR(76); AR(77); AR(78); AR(79); 
AR(80); AR(81); AR(82); AR(83); AR(84); AR(85); AR(86); AR(87); AR(88); AR(89); 
AR(90); AR(91); AR(92); AR(93); AR(94); AR(95); AR(96); AR(97); AR(98); AR(99); 
AR(100); AR(101); AR(102); AR(103); AR(104); AR(105); AR(106); AR(107); AR(108); AR(109); 
AR(110); AR(111); AR(112); AR(113); AR(114); AR(115); AR(116); AR(117); AR(118); AR(119);
AR(120); 
#if (FLASHEND+1 <= 32768)
#define FILLED 121
const byte* allar[]  = {a00, a01, a02, a03, a04, a05, a06, a07, a08, a09, 
    a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, 
    a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, 
    a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, 
    a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, 
    a50, a51, a52, a53, a54, a55, a56, a57, a58, a59,
    a60, a61, a62, a63, a64, a65, a66, a67, a68, a69,
    a70, a71, a72, a73, a74, a75, a76, a77, a78, a79,
    a80, a81, a82, a83, a84, a85, a86, a87, a88, a89,
    a90, a91, a92, a93, a94, a95, a96, a97, a98, a99,
    a100, a101, a102, a103, a104, a105, a106, a107, a108, a109,
    a110, a111, a112, a113, a114, a115, a116, a117, a118, a119,
    a120};
#endif
#endif
#endif
#endif
#endif
#endif

byte val = 0;
byte *flashptr;


void setup()
{
  pinMode(LED, OUTPUT);
#ifdef LED_BUILTIN  && FLASHEND+1 > 2048
  pinMode(LED_BUILTIN, OUTPUT);
#endif
}

void loop()
{
  unsigned int cnt = 0;
  byte mem, i;

  for (i=0; i < FILLED; i++) {
    flashptr = (byte*)allar[i];
    cnt = 0;
    while (cnt++ < 256) {
      mem = pgm_read_byte(flashptr);
      flashptr++;
      if (mem != val) 
	flasherror();
      val++;
    }
  }
  flashOK();
}

void flasherror()
{
  while (1) {
    digitalWrite(LED, HIGH);
#ifdef LED_BUILTIN && FLASHEND+1 > 2048
  digitalWrite(LED_BUILTIN, HIGH);
#endif

    delay(200);

    digitalWrite(LED, LOW);
#ifdef LED_BUILTIN  && FLASHEND+1 > 2048
    digitalWrite(LED_BUILTIN, LOW);
#endif

    delay(200);
  }
}

void flashOK()
{
#ifdef LED_BUILTIN  && FLASHEND+1 > 2048
  digitalWrite(LED_BUILTIN, HIGH);
#endif
  while (1);
}
