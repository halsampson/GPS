// GPS.cpp 

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <time.h> 
#include <math.h>

// Motorola Oncore and Sirf GPS experiments

// TODO: Ubuntu build for small laptop
 
// TODO: auto set 0xFF health for missing SVs

// message structs to .h file

// #define Sirf  // comment out for Moto Oncore



// Common routines

#pragma warning(disable : 6031)

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong; // also 4 bytes

int bigEnd(int le) {
  return _byteswap_ulong((uint)le);
}

uint bigEnd(uint le) {
  return _byteswap_ulong(le);
}

ulong bigEnd(ulong le) {
  return _byteswap_ulong(le);
}

short bigEnd(short le) {
  return (short)_byteswap_ushort((ushort)le);
}

ushort bigEnd(ushort le) {
  return _byteswap_ushort(le);
}

HANDLE hCom = NULL;
DCB dcb;

void setResponseMs(DWORD ms) {
  COMMTIMEOUTS timeouts = { 0 };  // in ms
  timeouts.ReadIntervalTimeout = 100; // between charaters
  timeouts.ReadTotalTimeoutMultiplier = 10; // * num requested chars
  timeouts.ReadTotalTimeoutConstant = ms; // + this = total timeout
  if (!SetCommTimeouts(hCom, &timeouts))  printf("Can't SetCommTimeouts\n");
}

HANDLE openSerial(const char* portName, int baudRate = 4800) {
  char portDev[16] = "\\\\.\\";
  strcat_s(portDev, sizeof portDev, portName);

  hCom = CreateFile(portDev, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL); // better OVERLAPPED
  if (hCom == INVALID_HANDLE_VALUE) return NULL;

  // configure far end bridge COM port - only for bridges - could check endpoint capabilites??
  dcb = { 0 };
  dcb.DCBlength = sizeof DCB;
  GetCommState(hCom, &dcb);

  dcb.BaudRate = baudRate;
  dcb.ByteSize = DATABITS_8;
  dcb.StopBits = 0; // STOPBITS_10;   // BUG in SetCommState or VCP??
  dcb.fBinary = TRUE; // no EOF check

  dcb.fDtrControl = DTR_CONTROL_DISABLE; // pin 1a high

  if (!SetCommState(hCom, &dcb)) { printf("Can't set baud\n"); }
  if (!SetupComm(hCom, 16384, 16)) printf("Can't SetupComm\n"); // Set size of I/O buffers (max 16384 on Win7)

  setResponseMs(1000);

  return hCom;
}

int rxRdy(void) {
  COMSTAT cs;
  DWORD commErrors;
  if (!ClearCommError(hCom, &commErrors, &cs)) return -1;
  if (commErrors)
    printf("\n\rCommErr %X\n", commErrors); // 8 = framing (wrong baud rate); 2 = overrurn; 1 = overflow
  return cs.cbInQue;
}



void setComm(int baudRate, bool dtrEnable = true, bool rtsEnable = true) {
  dcb.BaudRate = baudRate;
  dcb.fDtrControl = dtrEnable ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
  dcb.fRtsControl =  rtsEnable ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
  SetCommState(hCom, &dcb);
}

void nmeaCmd(const char* cmd) { // w/o $ and *
  int chksum = 0;
  const char* p = cmd;
  while (*p) chksum ^= *p++;

  char send[256];
  int len = sprintf(send, "$%s*%02X\r\n", cmd, chksum);
  WriteFile(hCom, send, len, NULL, NULL);
}

// Almanac: See IS-GPS- 200M pg 82, 116
// all signed except eccentric, toa
// 32! PRNs, 33 bytes each - 9 ovhd = 24 * 8 bits each

// Almanac file from https://www.navcen.uscg.gov/?pageName=gpsAlmanacs
/*
******** Week 147 almanac for PRN-01 ********
ID:                         01
Health:                     000
Eccentricity:               0.1132822037E-001
Time of Applicability(s):  233472.0000            0..602112 secs    2^20 >> 12

Orbital Inclination(rad):   0.9865078384
Rate of Right Ascen(r/s):  -0.8114623721E-008
SQRT(A)  (m 1/2):           5153.621094           2530 to 8192
Right Ascen at Week(rad):  -0.1660719209E+001

Argument of Perigee(rad):   0.882766995
Mean Anom(rad):             0.3052278345E+001
Af0(s):                     0.4425048828E-003
Af1(s/s):                  -0.1091393642E-010
week:                        147

******** Week 147 almanac for PRN-02 ********
...

*/

#pragma pack(1)

typedef struct {
  char b[3];
} int24;


typedef struct {
  // Big endian
  uchar svID : 6;     // see pg 224 ??
  uchar dataID : 2;   // 1  see pg. 113
  ushort eccentric;  // -21      Eccentricity

  // scale (2^N LSB)
  uchar toa;        //  12      Time of Applicability (seconds)
  short deltaI;     // -19      - i0 = 0.3 semi-circles; Inclination Angle at Reference Time

  short OmegaDot;   // -38      Rate of Right Ascen: semi-circles/sec
  uchar svHealth;

  int24 rootA;      // -11      Square Root of the Semi-Major Axis
  int24 Omega0;     // -23      Longitude of Ascending Node of Orbit Plane at Weekly Epoch 
  int24 omega;      // -23      Argument of Perigee
  int24 M0;         // -23      Mean Anom      

  struct {
    char af0msb;    // -20      SV Clock Bias Correction Coefficient
    char af1msb;    // -38      SV Clock Bias Correction Coefficient

    char t : 2;  //  parity
    char af0lsb : 3;
    char af1lsb : 3;
  };
} almData;


typedef  struct {
  uchar svh1ms : 2;
  uchar svh0 : 6;

  uchar svh2ms : 4;   // 28
  uchar svh1ls : 4;

  uchar svh3 : 6;
  uchar svh2ls : 2;   // 28
} svHealth6w;


struct {
  uchar svID : 6;
  uchar dataID : 2;   // 1  see pg. 113
  
  uchar toa;
  uchar week;

  svHealth6w svHealth6[6]; // for SV1..24  - copy svHealth or 0

  int24 rsvrd; // LS t : 2;  // parity
} alm5p25 = { 115, 1};


typedef struct {
  uchar svHealth1 : 4;  // 9
  uchar svHealth0 : 4;  // 9
} svHealth4b;


struct {
  uchar svID : 6;
  uchar dataID : 2;   // 1 : see pg. 113
 
  svHealth4b svHealth4[16];
  uchar svHealth25 : 6;
  uchar resrvd     : 2;

  svHealth6w svHealth26[2];  // last field reserved 
} alm4p25 = { 127, 1 };


static union {
  almData  alm;
  uchar    almWord[8][3];
  ushort   almShort[14];
};


// fields in .alm file order:
void* const pField[12] = { &alm, &alm.svHealth, &alm.eccentric, &alm.toa,
                           &alm.deltaI, &alm.OmegaDot, &alm.rootA, &alm.Omega0,
                           &alm.omega, &alm.M0, &alm.af0msb, &alm.af1msb };
const int scale[12] = { 0,  0, -21, 12,  -19, -38, -11, -23,  -23, -23, -20, -38 }; // scale LSB
const int width[12] = { 6,  8,  16,  8,   16,  16,  24,  24,   24,  24,  11,  11 };


void setField(int field, char* line) {
  if (field == 1) {// svHealth
    alm.svHealth = atoi(line + 27);
    if (alm.svHealth >= 63) alm.svHealth |= 0xC0;  // summary bits
    return;
  }

  switch (width[field]) {
    case  6: alm.svID = atoi(line + 27); 
      // fall thru to set dataID
    case  2: alm.dataID = 1;
      return;
  }

  double val = atof(line + 27);
  const double PI = 3.141592653589793238462643383279502884L;
  switch (field) {
  case 4:
  case 5:
  case 7:
  case 8:
  case 9:
    val /= PI; // rads -> semi-circles
    break;
  }
  if (field == 4) val -= 0.3; // subtract 0.3 for deltaI

  val /= pow(2, scale[field]);  // scale  LSB 

  if (val >= pow(2, width[field]) || val < -pow(2, width[field])/2) {  // check fit in width
    printf("In %s field %d of svID %d: %.0f won't fit\n", line, field, alm.svID, val);
    exit(-6);
  }

  int set = (int)round(val); // most signed

  // set field:  Big endian, beware sign!!
  switch (width[field]) {
  case  8: *(uchar*)(pField[field]) = (uchar)set; break;
  case 11:  // split
    *(char*)(pField[field]) = (char)(set >> 3);
    switch (field) {
    case 10: alm.af0lsb = set & 7; break;
    case 11: alm.af1lsb = set & 7; break;
    }
    break;

  case 16: {ushort be = bigEnd((ushort)set); *(ushort*)pField[field] = be; } break;
  case 24: {ulong  be = bigEnd((ulong)set); memcpy(pField[field], (char*)&be + 1, 3); } break;
  default: printf("Width %d!\n", width[field]); break;
  }
}

// Almanac data from
// https://celestrak.com/GPS/almanac/Yuma/2022/
// https://gps.afspc.af.mil/gps/archive/2022/almanacs/yuma/

void almanacPage(almData& almd);

int convertAlmanac(const char* almPath = "../../../Desktop/Tools/modules/GPS/almanac.yuma.week0149.147456.txt") {  // returns GPS week 
  FILE* almf = fopen(almPath, "rt");
  if (!almf) {
    almPath = "almanac.yuma.week0149.147456.txt";
    almf = fopen(almPath, "rt");
  }
  printf("Almanac %s\n   Sat\r", almPath);
  if (!almf) exit(-5);

  alm.dataID = 1;   // TODO: see pg. 113
  int week;
  while (1) {
    char line[128];
    if (!fgets(line, sizeof line, almf)) break;  // skip header line - exit on end of file
    week = atoi(line + 13);

    for (int i = 0; i < 12; ++i) {
      if (!fgets(line, sizeof line, almf)) {
        printf("%s", line);
        exit(-7);
      }
      setField(i, line);
    }

    fgets(line, sizeof line, almf);  // skip week
    fgets(line, sizeof line, almf);  // skip blank line

    almanacPage(alm);

#if 1 // svID 28 data currently missing
    if (alm.svID == 27) {
      alm.svID = 28;
      // alm.toa = 0x90; // old
      alm.svHealth = 0xFF;  // bad? -- not in Sirf data -- check 1996 Moto
      almanacPage(alm);
    }
#endif
  }
  fclose(almf);
  return week;
}


#ifndef Sirf  // Moto Oncore

// Moto Oncore

DWORD bytesRead;
uchar response[304];  // longest is Cj 294

bool motoCmd(const void* cmd, int len, int responseLen = -1, void* pResponse = response) {
  while (1) {
    uchar send[256] = "@@";   // longest message 171 bytes

    if (cmd) {
      uchar sum = 0;
      for (int p = 0; p < len; ++p)
        sum ^= ((char*)cmd)[p]; // XOR of message bytes after @@ and before checksum

      memcpy(send + 2, cmd, len);
      send[2 + len] = sum;  // checksum
      memcpy(send + 2 + len + 1, "\r\n", 2); // CRLF

      WriteFile(hCom, send, 2 + len + 3, NULL, NULL);
    }

    if (!responseLen) return true; // no response

    if (responseLen < 0) responseLen = 2 + len + 3;
    bool OK = ReadFile(hCom, pResponse, responseLen, &bytesRead, NULL);
    
    if (((char*)pResponse)[2] == 'Q') {  // QX after power cycle
      printf("Q");
      char restOfQ[128]; 
      ReadFile(hCom, restOfQ, sizeof restOfQ, &bytesRead, NULL);  // flush, wait
      Sleep(5000);
      continue;
    }

#if 0  // print almanac hex
    if (send[2] == 'C' && send[3] == 'b') {  // almanac input data
      int wordPos = 0;
      for (int i = 0; i < 2 + len + 3; ++i) {
        printf("%02X", send[i]);
        if (!(++wordPos % 3) && wordPos != 3 || wordPos == 4) 
          printf(" ");
      }
      printf("\n");

      FILE* bin = fopen("../../../Desktop/Tools/Modules/GPS/almanac.test.bin", "ab");
      fwrite(send, 1, 2 + len + 3, bin);
      fclose(bin);
    }
#endif

    if (bytesRead == responseLen
    && *(char*)pResponse == '@'
    && ((char*)pResponse)[bytesRead - 1] == '\n') 
      return true;

    printf("%s -> ", (char*)cmd); 
    fwrite(pResponse, 1, bytesRead, stdout);
    printf("\n");
    Sleep(1000);
  } 
}

typedef struct {
  char month;
  char day;
  uchar year[2]; // Big endian
  char hour;
  char minutes;
  char seconds;
  uchar ns[4];  // Big endian

  char position[23]; //  aaaaoooohhhhmmmmvvhhddt

  char visible;
  char tracked;
} EaBa;

typedef struct {
  char satID;
  uchar trackMode;
  // 0 - Code Search 
  // 1 - Code Acquire 
  // 2 - AGC Set 
  // 3 - Freq Acquire
  // 4 - Bit Sync Detect
  // 5 - Message Sync Detect
  // 6 - Satellite Time Available
  // 7 - Ephemeris Acquire
  // 8 - Avail for Position
  uchar SNR;
  struct {
    uchar used : 1;
    uchar momAlert : 1;
    uchar spoof : 1;
    uchar unhealthy : 1;
    uchar inaccurate : 1;
    uchar spare2 : 1;
    uchar spare1 : 1;
    uchar parityErr : 1;
  } chStatus;
} Sat;

static union {
  struct {
    char atat[2]; // @@
    char cmd[2];  // Ba 6 ch; Ea 8ch;  Ha 12 ch

    EaBa ba;

    Sat sat[6];

    struct {
      uchar posProp : 1;
      uchar poorGeo : 1;
      uchar fix3D : 1;
      uchar fix2D : 1;
      uchar acquire : 1;
      uchar differential : 1;
      uchar insuffVisible : 1;
      uchar badAlmanac : 1;
    } rcvrStatus;

    uchar check;
    char CR;
    char LF;
  } BaResponse;

  struct {
    char atat[2]; // @@
    char cmd[2];  // Ba 6 ch; Ea 8ch;  Ha 12 ch

    EaBa ea;

    Sat sat[8];

    struct {
      uchar posProp : 1;
      uchar poorGeo : 1;
      uchar fix3D : 1;
      uchar fix2D : 1;
      uchar acquire : 1;
      uchar differential : 1;
      uchar insuffVisible : 1;
      uchar badAlmanac : 1;
    } rcvrStatus;

    uchar check;
    char CR;
    char LF;
  } EaResponse;
};

struct {
  char cmd[2];     // Cb
  uchar subframe;  // 5: pg 1..25;  4: 2..5, 7..10. 25
  uchar page;
  almData data;
} almMsg = { {'C', 'b'}, }; // s/b 28 bytes + 5 (@@ ... Chk CR LF) = 33 byte messages


void setSubframeAndPage(int id) {
  switch (64 + id) {
    case 115: almMsg.subframe = 5; almMsg.page = 25; return;
    case 127: almMsg.subframe = 4; almMsg.page = 25; return;
  }

  almMsg.subframe = id < 25 ? 5 : 4;  // 24 in subframe 5, 8 in subframe 4  pgs 2..5  7..10
  int page = id;
  if (page >= 25) {
    page += 2 - 25;
    if (page >= 6)  // page 6 skipped
      ++page;
  }
  almMsg.page = page;

  printf("\r%2d\r", id);
}

void almanacPage(almData& almd) {
  almMsg.data = almd;
  int id = almd.svID;
  setSubframeAndPage(id);

  // TODO: handle missing SVs *****

  uchar health6b = almd.svHealth & 0x3F;

  // simpler using general bit position and width

  if (id < 25) {
    int index = (id - 1) / 4;
    switch (id % 4) {
    case 1 : alm5p25.svHealth6[index].svh0 = health6b; break;
    case 2 : 
      alm5p25.svHealth6[index].svh1ms = health6b >> 4;
      alm5p25.svHealth6[index].svh1ls = health6b & 0xF; break;
    case 3 :
      alm5p25.svHealth6[index].svh2ms = health6b >> 2;
      alm5p25.svHealth6[index].svh2ls = health6b & 3; break;
    case 0 : alm5p25.svHealth6[index].svh3 = health6b; break;
    }    
  } else if (id == 25) {
    alm4p25.svHealth25 = health6b;
  } else if (id <= 32)  {
    int index = (id - 26) / 4;
    switch (id % 4) {
    case 2: alm4p25.svHealth26[index].svh0 = health6b; break;
    case 3:
      alm4p25.svHealth26[index].svh1ms = health6b >> 4;
      alm4p25.svHealth26[index].svh1ls = health6b & 0xF; break;
    case 0:
      alm4p25.svHealth26[index].svh2ms = health6b >> 2;
      alm4p25.svHealth26[index].svh2ls = health6b & 3; break;
    case 1: alm4p25.svHealth26[index].svh3 = health6b; break;
    }
  }

  if (id <= 32) {
    uchar health4b = almd.svHealth ? 0 : 9; // TODO 1, ...
    int index = (almd.svID - 1) / 2;
    if (almd.svID & 1)
      alm4p25.svHealth4[index].svHealth0 = health4b;
    else alm4p25.svHealth4[index].svHealth1 = health4b;
  }

  motoCmd(&almMsg, sizeof almMsg, 9);
}

#if 1

void sendAlmanac() {
  int week = convertAlmanac();
  // int week = convertAlmanac("../../../Desktop/Tools/Modules/GPS/almanac.yuma.week0860.1996.txt");  // testing
  uchar toa = almMsg.data.toa;  // same as others

  // svConfig codes 0 = no info

  alm5p25.toa = toa; // same as all other pages
  alm5p25.week = week & 0xFF;
  almanacPage((almData&)alm5p25);

  almanacPage((almData&)alm4p25);

  printf("\r  for week %d\n", week);

  // see also p 117, 120, 226
}

#elif 1  // works

void sendAlmanac() {
  // FILE* almf = fopen("../../../Desktop/Tools/Modules/GPS/Moto.6ch.0860.1996.alm.bin", "rb");
  FILE* almf = fopen("../../../Desktop/Tools/Modules/GPS/almanac.testing.bin", "rb");
  while (!feof(almf)) {
    char cbMsg[2 + sizeof almMsg + 3];
    if (fread(&cbMsg, 1, sizeof cbMsg, almf) < sizeof cbMsg) break;
    ((almData*)(cbMsg + 2 + 2 + 2))->t = 0;  //parity doesn't matter
    motoCmd(cbMsg + 2, sizeof almMsg, 9);
  }
  fclose(almf);
}

#else

void sendAlmanac() {
  FILE* bps = fopen("../../../Desktop/Tools/Modules/GPS/sirf.bps.45.bin", "rb");
  while (!feof(bps)) {
    uint r50bps[10];
    if (fread(r50bps, 1, sizeof r50bps, bps) < sizeof r50bps) break;
    for (int w = 0; w < 8; ++w) {
      uint le = (bigEnd((ulong)r50bps[w + 2]) >> 6) & 0xFFFFFF; // remove parity
      uint be = bigEnd(le);
      memcpy(almWord[w], (uchar*)&be + 1, 3);
    }
    bool missing = alm.svID == 27;
    almanacPage(alm);

#if 1
    if (missing) {
      alm.svID = 28;
      alm.svHealth = 0xFF;
      almanacPage(alm);
    }
#endif

  }
  fclose(bps);
}

#endif

void selectSatellites() {
  motoCmd("Ah\001", 3);  // manual satellite selection  

  // initial Chs: 1 6 9 14 20 22 24 25
  int onDeck[] = { 2, 3, 4, 5,  7, 8,  10, 11, 12, 13,  15, 16, 17, 18, 19,  21,  23,  26, 27, 28, 29, 30, 31, 32 };
  int oldPtr = 0;

  int bestSVs[] = { 29, 18, 20, 26,  23, 15, 13, 2,  0 };  // get from Sirf
  for (int ch = 0; bestSVs[ch]; ++ch) {
    char satSelect[16];
    sprintf(satSelect, "Ai%c%c", ch + 1, bestSVs[ch]);
    motoCmd(satSelect, 4);
  }
}


void watchBroadcast() {  // no response
  motoCmd("Bl/001", 3, 0);
  while (1) {
    char broadcast[41];
    DWORD bytesRead;
    ReadFile(hCom, broadcast, sizeof broadcast, &bytesRead, NULL);
    if (bytesRead) {
      for (int i = 0; i < sizeof broadcast; ++i)
        printf("%02X", broadcast[i]);
      printf("\n");
    }
  }
}


// TODO: struct for En

uchar cmd_En[] = { 'E', 'n', 0, 1, 0, 10, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // PPS on when lock to any satellite; TRAIM if possible; // Bn for 6 ch

struct {   // otaapnnnmdyyhmspysreen sffffsffffsffffsffffsffffsffffsffffsffff
  char cmd[4];  // @@En or Bn
  char rate;
  char RAIM_on;
  ushort alarm;  // Big endian !!
  char PPS;
  char PPSrate[3]; // ???
  char nextFire[7];
  char pulseStatus;
  char pulseSync;
  char solnStatus;
  char RAIM_Status;
  ushort sigma_ns;
  char negSawtooth;

  struct {
    char svID;
    int ns;  // Big endian !!
  } satTime[6];
  char chksum;
  char CRLF[2];
} Bn_status;

struct {   // otaapnnnmdyyhmspysreen sffffsffffsffffsffffsffffsffffsffffsffff
  char cmd[4];  // @@En or Bn
  char rate;
  char RAIM_on;
  ushort alarm;  // Big endian !!
  char PPS;
  char PPSrate[3]; // ???
  char nextFire[7];
  char pulseStatus;
  char pulseSync;
  char solnStatus;
  char RAIM_Status;
  ushort sigma_ns;
  char negSawtooth;

  struct {
    char svID;
    int ns;  // Big endian !!
  } satTime[8];
  char chksum;
  char CRLF[2];
} En_status;


uchar almanac[34*33];

#if 0
void weekRollovers() {  // no need
  const int extraSecs = 1;
  char cmd[16];

#if 0
  sprintf(cmd, "Ac%c%c%c%c", 8, 21, 1999 / 256, 1999 % 256);
  motoCmd(cmd, 6);

  sprintf(cmd, "Aa%c%c%c", 23, 59, 59 - extraSecs);
  motoCmd(cmd, 5);

  motoCmd("Cg\001", 3); // out of idle: required for clock to advance

  Sleep(extraSecs * 1000);

  motoCmd("Ac\377\377\377\377", 6);  // check date
  printf("GMT date: %d/%d/%d\n", response[4], response[5], response[6] * 256 + response[7]);
#endif

  sprintf(cmd, "Ac%c%c%c%c", 4, 6, 2019 / 256, 2019 % 256);
  motoCmd(cmd, 6);

  sprintf(cmd, "Aa%c%c%c", 23, 59, 59 - extraSecs);
  motoCmd(cmd, 5);

  motoCmd("Cg\001", 3); // out of idle: required for clock to advance

  Sleep(extraSecs * 1000);

  motoCmd("Ac\377\377\377\377", 6);  // check date
  printf("GMT date: %d/%d/%d\n", response[4], response[5], response[6] * 256 + response[7]);

  motoCmd("Cg\000", 3); // idle
}
#endif


bool forceAlmanacUpdate;

void updateAlmanac() {  
  char almStatus[23];
  motoCmd("Bd\000", 3, sizeof almStatus, almStatus);
  if (almStatus[4]) {  // TODO: update if have later almanac -- check week
    printf("Almanac in EEPROM or RAM\n");  // check week!
#if 0
    // save alamanc
    printf("Saving almanac ...");
    motoCmd("Be\000", 3, sizeof almanac, almanac); // request almanac data    
    FILE* alm = fopen("Moto.1.alm", "wb");
    fwrite(almanac, 1, bytesRead, alm);
    fclose(alm);
    printf("\n");
#endif
    if (!forceAlmanacUpdate) return;
  } 
  sendAlmanac();

  Sleep(1000);
  if (!rxRdy()) {
    printf("No Bd response to almanac data!\n");
    motoCmd("Bd\000", 3, sizeof almStatus, almStatus);
    printf("Almanac %s\n", almStatus[4] == 1 ? "OK" : "bad!");
  }
  else {
    ReadFile(hCom, almStatus, sizeof almStatus, &bytesRead, NULL);  // received almanac
    printf("Almanac %s\n", almStatus[4] == 1 ? "OK" : "bad!");

    printf("Saving to EEPROM ");
    while (!rxRdy()) {
      Sleep(200);
      printf(".");
    }
    ReadFile(hCom, almStatus, sizeof almStatus, &bytesRead, NULL);  // stored to EEPROM
    printf("\n");
  }
}


void setPosition() { // ignored if already fixing position
  // set approx position
  const int latitude =    (int)(37.3392573 * 60 * 60 * 1000);  // in milli arc seconds
  const int longitude = (int)(-122.0496515 * 60 * 60 * 1000);
  const int height = 80 * 100;  // in cm

  motoCmd("At\000", 3); //position hold off to set position
  motoCmd("Av\000", 3); // altitude hold off

  struct {
    char cmdA;
    char cmd;
    int  lati;
    int  longi;
    int  height;
    char MSL;
  } posHoldCmd = { 'A', 's'};

  posHoldCmd.lati = bigEnd(latitude); // Big endian
  posHoldCmd.longi = bigEnd(longitude);
  posHoldCmd.height = bigEnd(height);
  posHoldCmd.MSL = 1;
  motoCmd(&posHoldCmd, sizeof posHoldCmd);

  struct {
    char cmdA;
    char cmd;
    int val;
    char MSL;
  } posCmd = { 'A', 'd', 0, 1 };

  posCmd.cmd = 'u';  // altitude hold
  posCmd.val = bigEnd(height);
  motoCmd(&posCmd, sizeof posCmd);

  posCmd.cmd = 'f';
  motoCmd(&posCmd, sizeof posCmd, 15);    // slow response

  posCmd.cmd = 'd';
  posCmd.val = bigEnd(latitude);
  motoCmd(&posCmd, sizeof posCmd - 1); // no MSL

  posCmd.cmd = 'e';
  posCmd.val = bigEnd(longitude);
  motoCmd(&posCmd, sizeof posCmd - 1);

  motoCmd("At\001", 3); //position hold on
  motoCmd("Av\001", 3); // altitude hold on
}

void setTime() {  // set date / time
  motoCmd("Ab\000\000\000", 5);  // set GMT offset
  motoCmd("Aw\001", 3);  // UTC time

  // weekRollovers();

  time_t rtime; time(&rtime);
  struct tm* pTime = gmtime(&rtime); pTime->tm_year += 1900;
  char cmd[16];
  sprintf(cmd, "Ac%c%c%c%c", pTime->tm_mon + 1, pTime->tm_mday, pTime->tm_year / 256, pTime->tm_year % 256);
  motoCmd(cmd, 6);

  time(&rtime);
  pTime = gmtime(&rtime);
  sprintf(cmd, "Aa%c%c%c", pTime->tm_hour, pTime->tm_min, pTime->tm_sec);
  motoCmd(cmd, 5);

  motoCmd("Cg\001", 3); // start clock, idle off position fix mode (for VP units)
  Sleep(500);

  motoCmd("Ac\377\377\377\377", 6);  // check date
  printf("GMT date set: %d/%d/%d\n", response[4], response[5], response[6] * 256 + response[7]);
}

int numChannels = 8;  // or 6, 12  

void initOncore() {
  motoCmd("Cg\000", 3); // position fix off
  motoCmd("Bb\000", 3, 92); // don't send satellites

  motoCmd(numChannels < 8 ? "Ba\000" : "Ea\000", 3, numChannels < 8 ? sizeof BaResponse : sizeof EaResponse); // poll mode
  
#if 0
  motoCmd("Cf", 2, 7); // factory defaults    returns Cg0 -- why?
  motoCmd("Ac\377\377\377\377", 6);  // check date
  printf("GMT date was: %d/%d/%d\n", response[4], response[5], response[6] * 256 + response[7]);
#endif

  motoCmd(numChannels < 8 ? "Ca" : "Fa", 2, 9);  //self test
  if (response[4] || response[5])
    printf("Self test fail\n");

  updateAlmanac();
  setPosition();
  motoCmd("AB\004", 3); // set Application type static 

  setTime();

  if (numChannels < 8) {
    cmd_En[0] = 'B';
    motoCmd(cmd_En, sizeof cmd_En, sizeof Bn_status);
  } else motoCmd(cmd_En, sizeof cmd_En, sizeof En_status);  // enable 1 PPS
  // printf(" %d %d %d %d", En_status.pulseStatus, En_status.solnStatus, En_status.RAIM_Status, En_status.negSawtooth);
}


int main(int argc, char** argv) {
  forceAlmanacUpdate = argc > 1 && argv[1][0] == 'f';

#if 1
  if (!openSerial("COM34", 9600))  // default on power-cycle
  if (!openSerial("COM3", 9600)) exit(-1); // default on power-cycle
#else  // switch from NMEA mode to binary
  if (!openSerial("COM3", 4800)) exit(-1);
  WriteFile(hCom, "$PMOTG,FOR,0*2A", 15, NULL, NULL);
  setComm(9600);
#endif

  WriteFile(hCom, NULL, 1, NULL, NULL); // to set baud rate?
  setResponseMs(200);
  ReadFile(hCom, response, sizeof response, &bytesRead, NULL);  // flush

  setResponseMs(10000); 

  motoCmd("Cj", 2, 294);
  response[bytesRead] = 0;
  printf("%s\n", response + 4); // ID
  numChannels = strstr((char*)response, "8_CH") ? 8 : 6;

#if 1
  initOncore();
#else
  motoCmd("Cg\001", 3); // idle off
  Sleep(500);
#endif

  motoCmd("Aa\377\377\377", 5);  // check time
  printf("%02d:%02d:%02d GMT\n", response[4], response[5], response[6]);
  printf("\n");

  // watchBroadcast();

  do {
    while (!_kbhit()) {
      if (motoCmd(numChannels < 8 ? "Ba\000" : "Ea\000", 3, numChannels < 8 ? sizeof BaResponse : sizeof EaResponse, &EaResponse)) { // unsolicited 1/sec  (or slower with longer serial timeout)              
        int worstSignal = 256;
        int worstCh, worstSV;
        int totalBadSNR = 0;
        int notLocked = 0;
        for (int s = 0; s < numChannels; ++s) {
          if (EaResponse.sat[s].trackMode) {
            printf("%2d: %d+%3d  ", EaResponse.sat[s].satID, EaResponse.sat[s].trackMode, EaResponse.sat[s].SNR);
          } else {
            ++notLocked;
            totalBadSNR += EaResponse.sat[s].SNR;
          }
          if (EaResponse.sat[s].SNR < worstSignal) {  // no signal -> 100 ??
            worstSignal = EaResponse.sat[s].SNR;
            worstSV = EaResponse.sat[s].satID;
            worstCh = s;
          }
        }
        printf("%d %X", totalBadSNR / notLocked, numChannels < 8 ? *(uchar*)&BaResponse.rcvrStatus : *(uchar*)&EaResponse.rcvrStatus);
        printf(notLocked < numChannels ? "\n" : "\r");
#if 0
        // "The signal strength value is meaningless when the channel tracking mode is zero."  *****
        // kick out worst signal, replace with channel thrown out longest ago
        char satSelect[16];
        sprintf(satSelect, "Ai%c%c", worstCh + 1, onDeck[oldPtr]);  
        motoCmd(satSelect, 4);
        onDeck[oldPtr] = worstSV;
        if (++oldPtr >= 24)
          oldPtr = 0;
#endif
      } else printf("Read %d  ", bytesRead);
      Sleep(5000); // * (32 - 8) = 24 satellites to check
    }
  } while (_getch() != 27);  // Escape

  motoCmd("Cg\000", 3);  // restart in idle mode

  CloseHandle(hCom);
}



#else // Sirf  GPS-500, VK16E


// Initialize Data Source – Message ID 128

/*
If Nav Lib data are enabled, the resulting messages are enabled: 
  Clock Status(Message ID 7),  
  50BPS(Message ID 8) *****,
  Raw DGPS(Message ID 17), 
  NL Measurement Data(Message ID 28), 
  DGPS Data(Message ID 29), 
  SV State Data (Message ID 30), and 
  NL Initialized Data(Message ID 31). 
  
All messages sent at 1 Hz.

If SiRFDemo is used to enable NavLib data, the bit rate is automatically set to 57600 by SiRFDemo.
*/

#pragma pack(1)

struct {
  uchar  msgID;
  int    x, y, z; // Big endian
  int    drift;
  uint   tow;
  ushort week;
  uchar  channels;
  uchar  reset;
} initNav = {128,-2694294,-4303469,3847444,91477,0,2196,12,0x51};   // RTC imprecise (1 sec)


void sirfCmd(void* cmd, int len) {
  uchar pre[4] = {0xA0, 0xA2, (uchar)(len >> 8), (uchar)(len & 0xFF)};
  int chkSum = 0;
  for (int i = 0; i < len; ++i)
    chkSum += (((uchar*)cmd)[i]);
  uchar post[4] = { (uchar)(chkSum >> 8), (uchar)(chkSum & 0x7F), 0xB0, 0xB3 };

  WriteFile(hCom, pre, 4, NULL, NULL);
  WriteFile(hCom, cmd, len, NULL, NULL);
  WriteFile(hCom, post, 4, NULL, NULL);
}

bool checkAck() {
  uchar response[16];
  DWORD bytesRead;
  ReadFile(hCom, response, 10, &bytesRead, NULL);
  bool OK = bytesRead == 10 && response[4] == 11;
  printf(OK ? "OK\n" : "NAK\n");
  return OK;
}

struct {
  uchar  msgID;
  short sirfAlm[32][14];
} setAlmanac = { 130 };

void almanacPage(almData& almd) {
  int id = almd.svID - 1;
  setAlmanac.sirfAlm[id][0] = almd.svHealth == 0 ? 1 : 0;
  memcpy(setAlmanac.sirfAlm[id] + 1, &almd, sizeof almd);
}

const int MaxSeen = 1024;  

int numSeen; 
uchar navData[MaxSeen][40];
int seen[MaxSeen];

uchar line[4096];
DWORD bytesRead;

int main() {
  openSerial("COM5", 57600);

  COMMTIMEOUTS timeouts = { 0 };  // in ms
  timeouts.ReadTotalTimeoutConstant = 500; // added to below
  timeouts.ReadTotalTimeoutMultiplier = 0; // * num requested chars
  timeouts.ReadIntervalTimeout = 200; // between characters

  ReadFile(hCom, line, sizeof line, &bytesRead, NULL);  // flush

  if (sizeof almData != 24) exit(sizeof almData);
  int almWeek = convertAlmanac();
  for (int id = 0; id < 32; ++id) {
    setAlmanac.sirfAlm[id][0] |= almWeek << 6;
    ushort checksum = 0;
    for (int i = 0; i < 13; ++i) {
      if (i) setAlmanac.sirfAlm[id][i] = bigEnd(setAlmanac.sirfAlm[id][i]);
      checksum += setAlmanac.sirfAlm[id][i];
    }
    setAlmanac.sirfAlm[id][13] = checksum;
  }

  sirfCmd(&setAlmanac, sizeof(setAlmanac));
  checkAck();  // NAK, also rejects SirfDemo almanac files

  time_t rtime; time(&rtime);
  time_t gpsSecs = rtime - 315964800; //  Jan 6, 1980 timestamp Epoch
  const int weekSecs = 7 * 24 * 60 * 60;

  int week = (int)(gpsSecs / weekSecs);
  int tow = gpsSecs % weekSecs;  // from Saturday midnite GMT
  tow += 18; // leap seconds

  // send InitNav with NavLab requested
  initNav.x = bigEnd(initNav.x);
  initNav.y = bigEnd(initNav.y);
  initNav.z = bigEnd(initNav.z);
  initNav.drift = bigEnd(initNav.drift);
  initNav.tow = bigEnd(tow);
  initNav.week = bigEnd(week);
  sirfCmd(&initNav, sizeof initNav);
  checkAck();

  exit(1);

  if (!SetCommTimeouts(hCom, &timeouts))  printf("Can't SetCommTimeouts\n");

  while (1) {
    ReadFile(hCom, line, sizeof line, &bytesRead, NULL);
    if (bytesRead) {
      for (int i = 0; i < (int)bytesRead; ++i) {
        if (line[i] == 0xA0 && line[i + 1] == 0xA2) { // start sequence
          int len = line[i + 2] * 256 + line[i + 3];  // payload
          if (line[i + 4] == 8) {  // 50 PBS (every 6 sec) 
            int preamble = line[i + 7] << 2 | line[i + 8] >> 6;  // word 1 MSB
            if (preamble == 0x8B || preamble == 0x74) {  // preamble can be inverted
              if (preamble == 0x74) // invert rest of page
                for (int j = i + 4 + 3; j < i + 4 + len; ++j)
                  line[j] = ~line[j];

              // check if data seen before
              int j;
              for (j = 0; j < numSeen; ++j)
                if (!memcmp(line + i + 4 + 3 + 12, navData[j] + 12, 40 - 12 - 4)) {  // ignore TLM, HOW, toa, parity
                  ++seen[j];
                  break;
                }
              if (j < numSeen) continue;
              memcpy(navData[numSeen++], line + i + 4 + 3, 40);
            }
          }
          i += 4 + len + 4 - 1; // skip packet
        }
      }
    }

    if (numSeen >= MaxSeen || _kbhit()) { // dump data
      FILE* bps = fopen("../../../Desktop/Tools/Modules/GPS/sirf.bps.45.bin", "wb");

      // find highest seen count for each svID
      for (int almPage = 1; almPage <= 34; ++almPage) {
        int svID = 64 + almPage;  // DATA ID | SV ID  -- spec is confusing
        switch (almPage) {
          case 33: svID = 115; break; // svHealth
          case 34: svID = 127; break;
        }

        int maxSeen = 0;
        int bestSeen = -1;
        for (int i = 0; i < numSeen; ++i) {
          int ID = navData[i][8] << 2 | navData[i][9] >> 6;  // word 3 MSB
          if (ID == svID && seen[i] >= maxSeen) {  // word 3 MSB
            bestSeen = i;
            maxSeen = seen[i];
          }
        }
        if (bestSeen < 0) continue;

        fwrite(navData[bestSeen], 1, 40, bps);

        printf("%3dx%3d: ", seen[bestSeen], almPage);
        for (int p = 2; p < 10; ++p) {
          uint word = _byteswap_ulong(*(ulong*)&navData[bestSeen][4 * p]) >> 6 & 0xFFFFFF; // remove parity
          printf("%06X ", word);
        }
        printf("\n");
      }
      fclose(bps);
      printf("%d\n", numSeen);

      if (numSeen >= MaxSeen) break;
      if (_getch() == 27) break;
    }
  }
}



void nmea() {
  openSerial("COM6", 4800);

  time_t rtime; time(&rtime);
  time_t gpsSecs = rtime - 315964800; //  Jan 6, 1980 timestamp Epoch
  const int weekSecs = 7 * 24 * 60 * 60;

  int week = (int)(gpsSecs / weekSecs);
  int tow = gpsSecs % weekSecs;  // from Saturday midnite GMT
  tow += 18; // leap seconds

  char initNav[64];
  // TODO: update coords, oscillator offset
  sprintf(initNav, "PSRF101,-2694294,-4303469,3847444,91446,%d,%d,12,17", tow, week);
  nmeaCmd(initNav);


  // TODO: set static navigation Msg 143 (freeze on low velocity)?

  exit(0);


  while (1) {
    nmeaCmd("PSRF100,0,4800,8,1,0"); // switch from NMEA to Sirf binary; send on split pin 1a

    while (!_kbhit()) {
      ReadFile(hCom, line, 200, &bytesRead, NULL);
      if (bytesRead) {
        line[bytesRead] = 0;
        printf("%s\n", line);
      }
    }
    _getch();
  }


  // ReadFile(hCom, line, sizeof line, &bytesRead, NULL);  // flush

  //  try all combinations using DTR, RTS - no help
  for (int dtr = 0; dtr <= 1; ++dtr)
    for (int rts = 0; rts <= 1; ++rts) {
      setComm(4800, dtr, rts);
      Sleep(1000);

      // set message rates
      nmeaCmd("PSRF103,00,00,05,01");
      nmeaCmd("PSRF103,01,00,05,01");
      nmeaCmd("PSRF103,02,00,05,01");
      nmeaCmd("PSRF103,03,00,05,01");
      nmeaCmd("PSRF103,04,00,05,01");
      nmeaCmd("PSRF103,05,00,05,01");

      while (!_kbhit()) {
        ReadFile(hCom, line, sizeof line, &bytesRead, NULL);
        if (bytesRead) {
          line[bytesRead] = 0;
          printf("%s\n", line);
        }
      }
      _getch();

    
      while (!_kbhit()) {
        ReadFile(hCom, line, sizeof line, &bytesRead, NULL);
        if (bytesRead) {
          line[bytesRead] = 0;
          printf("%s\n", line);
        }
      }
      _getch();
    }


  while (!_kbhit()) {
    ReadFile(hCom, line, sizeof line, &bytesRead, NULL);
    if (bytesRead) {
      line[bytesRead] = 0;
      printf("%s\n", line);
    }
  }
  _getch();

  // chk SW version
  // nmeaCmd("PSRF103,06,00,00,01");
  // nmeaCmd("PSRF103,08,00,00,01");

  while (!_kbhit()) {
    ReadFile(hCom, line, sizeof line, &bytesRead, NULL);
    if (bytesRead) {
      line[bytesRead] = 0;
      printf("%s\n", line);
    }
  }
  _getch();

  nmeaCmd("PSRF100,0,4800,8,1,0"); // switch from NMEA to Sirf binary; send on split pin 1a

  // 2 Pharos don't respond on RxD or floating pins should be tied ??

  // Sirf binary messages start with A0 A2, end with B0 B3

  FILE* sirf = fopen("sirf.test.bin", "wb");

  while (!_kbhit()) {
    ReadFile(hCom, line, sizeof line, &bytesRead, NULL);

    for (DWORD i = 0; i < bytesRead; ++i)
      printf("%02X ", line[i]);
    printf("\n\n");    

    fwrite(line, 1, bytesRead, sirf);
  }
  fclose(sirf);
  CloseHandle(hCom);
}

/*
Raw Sirf Almanac data -- week 147:

A0 A2 00 1E 0E 01 25 01 41 5D 02 24 1C CA FD 61 00 A1 0D 07 B7 42 DF 23 EC 52 87 6F 27 39 FF AC C1 60 0B 4D B0 B3 
A0 A2 00 1E 0E 02 25 01 42 A9 5A 24 0E AC FD 54 00 A1 0D 24 B3 A1 36 C5 15 53 8D 87 EC AA 00 1A 55 97 0A E3 B0 B3
A0 A2 00 1E 0E 03 25 01 43 1F C6 24 13 97 FD 50 00 A1 0D 11 E1 73 9C 27 39 84 58 40 0F F1 FF 71 6B 9D 0B 1D B0 B3
A0 A2 00 1E 0E 04 25 01 44 0D E3 24 0C 58 FD 51 00 A1 0C D1 0D 7E 95 82 E3 7B D0 5D 02 E7 00 20 BD 2C 0A DF B0 B3
A0 A2 00 1E 0E 05 25 01 45 30 21 24 0B 18 FD 43 00 A1 0C DC DF CF 4B 29 25 07 F3 7C FD F6 00 1B E2 B9 0B 45 B0 B3
A0 A2 00 1E 0E 06 25 01 46 15 E9 0F 1C 60 FD 63 00 A1 0C FB B6 F3 9A D8 82 48 96 12 0A 19 00 6C F0 2E 0B 4B B0 B3
A0 A2 00 1E 0E 07 25 01 47 7F 0A 0F 05 59 FD 4B 00 A1 0D 28 37 0A 24 A2 92 FE 6D 12 FB 28 00 2D DE 0D 08 E7 B0 B3
A0 A2 00 1E 0E 08 25 01 48 3B 6D 0F 0E BF FD 49 00 A1 0D 58 8B 4D ED 03 F2 51 E4 EE 20 F8 00 19 64 EC 0B B2 B0 B3
A0 A2 00 1E 0E 09 25 01 49 12 AB 0F 07 CA FD 4A 00 A1 0C 97 0B 5E 53 4C 43 DC F3 25 3B D1 00 0D FC F7 0A F9 B0 B3
A0 A2 00 1E 0E 0A 25 01 4A 3D 61 0F 13 77 FD 47 00 A1 0D 02 E1 5D BF 99 55 EA 35 AF A9 D7 FF A1 C4 B5 0D 05 B0 B3
A0 A2 00 1E 0E 0B 25 01 4B 02 4C 0F 0C 8C FD 4C FF A1 0D C3 B9 14 B5 74 18 77 DF AD FB F3 00 85 36 72 0C 64 B0 B3
A0 A2 00 1E 0E 0C 25 01 4C 46 F4 0F 12 35 FD 43 00 A1 0C 6E 64 62 21 32 B0 C6 52 52 07 EA FF C6 12 39 0A AB B0 B3
A0 A2 00 1E 0E 0D 25 01 4D 2F 80 24 11 2E FD 57 00 A1 0C FD 11 8F 7E 27 36 26 CC 50 07 21 00 5B A8 1F 08 A5 B0 B3
A0 A2 00 1E 0E 0E 25 01 4E 0B 93 24 07 60 FD 3C 00 A1 0C C0 63 15 B6 7B 2F EF 53 F0 51 F5 FF EC 07 7D 0C 1E B0 B3
A0 A2 00 1E 0E 0F 25 01 4F 72 12 24 F8 0E FD 30 00 A1 0D 63 07 07 75 2B C6 3C BB 44 1D F4 00 39 A5 B8 09 D4 B0 B3
A0 A2 00 1E 0E 10 25 01 50 68 4D 0F 12 2F FD 43 00 A1 0C 60 65 24 58 1C BC 3D 06 C5 54 C2 FF FC B3 EB 0B 56 B0 B3
A0 A2 00 1E 0E 11 25 01 51 6F C9 0F 18 95 FD 59 00 A1 0D AD 8E A2 5A C2 D8 77 E1 1F 3C 4A 00 3D 43 3C 0B 18 B0 B3
A0 A2 00 1E 0E 12 25 01 52 11 2A 0F 12 36 FD 54 00 A1 0C 9E B7 9C D0 7E 8E 93 90 55 69 20 FF C8 CD D4 0C 5E B0 B3
A0 A2 00 1E 0E 13 25 01 53 49 52 0F 17 C3 FD 58 00 A1 0D 1C 90 76 63 51 AD E9 43 3B F2 0F 00 26 C4 51 0A 47 B0 B3
A0 A2 00 1E 0E 14 25 01 54 2A 9A 0F FF 73 FD 2B 00 A1 0D 74 DB 32 E8 7F 4F 6C B2 26 77 43 00 19 5A 8C 0A EB B0 B3
A0 A2 00 1E 0E 15 25 01 55 C9 52 0F 0A EA FD 51 00 A1 0C 50 B3 97 FF D6 D2 50 E8 64 DB 14 00 17 2B 51 0C 16 B0 B3
A0 A2 00 1E 0E 16 25 01 56 69 4C 0F 0C 70 FD 53 00 A1 0C D5 0F 7D 81 B3 9C AC DB CB 67 1B 00 53 4F C7 0B 4B B0 B3
A0 A2 00 1E 0E 17 25 01 57 10 2B 0F 10 3C FD 43 00 A1 0D 2C E0 5A F4 78 45 DB 6B A9 3E 00 FF F3 86 B5 0B 97 B0 B3
A0 A2 00 1E 0E 18 25 01 58 64 F8 0F FA 9B FD 3F 00 A1 0D 23 33 AC 4A 20 DC 20 94 65 C6 24 00 05 2F 8C 0A 99 B0 B3
A0 A2 00 1E 0E 19 25 01 59 55 CD 24 09 F9 FD 3F 00 A1 0D 4B 61 3B 97 27 E4 AB 47 5C 10 25 00 40 95 6C 0A 25 B0 B3
A0 A2 00 1E 0E 1A 25 01 5A 38 26 24 FD 78 FD 2F 00 A1 0D 1E 5F 52 ED 0E E2 C6 2C D1 3A 17 00 38 44 09 09 BE B0 B3
A0 A2 00 1E 0E 1B 25 01 5B 53 64 24 14 68 FD 4C 00 A1 0C D3 8C 10 84 19 E5 6D E2 02 5F 0E 00 A9 3A EF 0A 78 B0 B3
A0 A2 00 1E 0E 1C 06 C1 5C 9C AD 90 18 87 FD 53 00 A1 0D 50 CE 95 09 C4 E8 B0 78 67 CD 63 00 0E 3B 99 0C CC B0 B3
A0 A2 00 1E 0E 1D 25 01 5D 0E CB 0F 19 E5 FD 5B 00 A1 0D B1 8F 29 D4 5C 54 B9 E7 B0 87 C1 FF F1 9A 50 0D F9 B0 B3
A0 A2 00 1E 0E 1E 25 01 5E 2C EF 0F FB AD FD 3E 00 A1 0C 39 37 94 90 90 04 96 6E 5D CB BC FF FF 7E D3 0D C9 B0 B3
A0 A2 00 1E 0E 1F 25 01 5F 55 AA 0F 08 1A FD 51 00 A1 0D 14 37 C6 8C 0F 3C 7C 5A 9B 20 EA FF F7 BD 52 0B 46 B0 B3
A0 A2 00 1E 0E 20 25 01 60 2D 5F 0F 0A 4E FD 4E 00 A1 0D C2 0B D2 BC 9F D0 A4 FD 90 B8 F7 FF D9 49 B1 0E 1C B0 B3
*/

#endif
