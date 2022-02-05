// GPS.cpp 

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <time.h> 
#include <math.h>


// Motorola Oncore

// TODO: check bit, byte order of @@Cb data
// TODO: svHealth data to pg 25s

typedef unsigned char uchar;

HANDLE hCom = NULL;
DCB dcb;

void setResponseMs(DWORD ms) {
  COMMTIMEOUTS timeouts = { 0 };  // in ms
  timeouts.ReadIntervalTimeout = 100; // between charaters
  timeouts.ReadTotalTimeoutMultiplier = 20; // * num requested chars
  timeouts.ReadTotalTimeoutConstant = ms; // + this = total timeout
  if (!SetCommTimeouts(hCom, &timeouts))  printf("Can't SetCommTimeouts\n");
}

HANDLE openSerial(const char* portName, int baudRate = 4800) {
  char portDev[16] = "\\\\.\\";
  strcat_s(portDev, sizeof(portDev), portName);

  hCom = CreateFile(portDev, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL); // better OVERLAPPED
  if (hCom == INVALID_HANDLE_VALUE) return NULL;

  // configure far end bridge COM port - only for bridges - could check endpoint capabilites??
  dcb = { 0 };
  dcb.DCBlength = sizeof(DCB);
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

void setComm(int baudRate, bool dtrtsEnable = false) {
  dcb.BaudRate = baudRate;
  dcb.fDtrControl = dtrtsEnable ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
  dcb.fRtsControl =  dtrtsEnable ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
  SetCommState(hCom, &dcb);
}

#if 0  // 0 = GPS-500; 1 = Oncore

DWORD bytesRead;
uchar response[304];  // longest is Cj 294

bool motoCmd(const void* cmd, int len, void* pResponse = response, int responseLen = -1) {
  while (1) {
    if (cmd) {
      uchar sum = 0;
      for (int p = 0; p < len; ++p)
        sum ^= ((char*)cmd)[p]; // XOR of message bytes after @@ and before checksum

      uchar send[256] = "@@";   // longest message 171 bytes
      memcpy(send + 2, cmd, len);
      send[2 + len] = sum;  // checksum
      memcpy(send + 2 + len + 1, "\r\n", 2); // CRLF

      WriteFile(hCom, send, 2 + len + 3, NULL, NULL);
    }

    if (responseLen < 0) responseLen = 2 + len + 3;
    bool OK = ReadFile(hCom, pResponse, responseLen, &bytesRead, NULL);
    ((char*)(pResponse))[bytesRead] = 0;


    if (((char*)pResponse)[2] == 'Q') {  // QX after power cycle
      printf("Q");
      char restOfQ[128]; 
      ReadFile(hCom, restOfQ, sizeof(restOfQ), &bytesRead, NULL);  // flush, wait
      Sleep(5000);
      continue;
    }

    if (*(char*)pResponse == '@'
    && ((char*)pResponse)[bytesRead - 1] == '\n') 
      return true;

    printf("%s -> %s\n", (char*)cmd, (char*)pResponse); 
    Sleep(1000);
  } 
}

const int NUM_SATS = 8;  // or 6, 12  - could auto-set via lack of response to @@Ea with 6 channel recvrs; @@Ha with 6/8 ch

struct {
  char atat[2]; // @@
  char cmd[2];  // Ba 6 ch; Ea 8ch;  Ha 12 ch

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

  struct {
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
    char SNR;
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
  } sat[NUM_SATS];

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
} baMsg;



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


// TODO: check byte order of words, within words (Motorola typically Big endian in memory, same as satellite)
// left on IS-GPS-200M charts = LSB?
// MSB arrow?

struct {
  char cmd[2];     // Cb
  uchar subframe;  // 5: pg 1..25;  4: 2..5, 7..10. 25
  uchar page;

  // Big endian

  struct {
    uchar dataID : 2;   // TODO: see pg. 113
    uchar svID : 6;
  } w3msb;
  union {
    unsigned short eccentric;  // -21      Eccentricity
    struct {
      uchar toa;
      uchar week;
    } page25;
  };
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
    char af1lsb :  3;
    char af0lsb :  3;
    char t      :  2;
  };
} almMsg = { {'C', 'b'}, }; // s/b 28 bytes + 5 (@@ ... Chk CR LF) = 33 byte messages

// fields in .alm file order:
void* const pField[12] = { &almMsg.w3msb, &almMsg.svHealth, &almMsg.eccentric, &almMsg.toa,
                           &almMsg.deltaI, &almMsg.OmegaDot, &almMsg.rootA, &almMsg.Omega0,
                           &almMsg.omega, &almMsg.M0, &almMsg.af0msb, &almMsg.af1msb };
const int scale[12] = { 0,  0, -21, 12,  -19, -38, -11, -23,  -23, -23, -20, -38}; // scale LSB
const int width[12] = { 6,  8,  16,  8,   16,  16,  24,  24,   24,  24,  11,  11};


void setField(int field, char* line) {
  if (field == 1) {// svHealth
    almMsg.svHealth = atoi(line + 28); return;
  }

  switch (width[field]) {
    case  2: almMsg.w3msb.dataID = atoi(line + 28); return;  // never
    case  6: almMsg.w3msb.svID = atoi(line + 28);  return;
  }

  double val = atof(line + 28);
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

  if (val >= pow(2, width[field]) || val < -pow(2, width[field] - 1)) {  // check fit in width
    printf("In %s field %d of svID %d: %.0f won't fit\n", line, field, almMsg.w3msb.svID, val);
    exit(-6);
  }

  int set = (int)(val + 0.49999999); // most signed

  // set field:  Big endian, beware sign!!
  switch (width[field]) {
    case  8: *(uchar*)(pField[field]) = (uchar)set; break;
    case 11:  // split
      *(uchar*)(pField[field]) = (uchar)(set >> 3);
      switch (field) {
        case 10: almMsg.af0lsb = set & 7; break;
        case 11: almMsg.af1lsb = set & 7; break;
      }
      break;

     case 16: {unsigned short bigEnd = _byteswap_ushort((unsigned short)set); *(unsigned short*)pField[field] = bigEnd; } break;
     case 24: {unsigned long  bigEnd = _byteswap_ulong((unsigned long)set); memcpy(pField[field], (char*)&bigEnd + 1, 3); } break;
     default: printf("Width %d!\n", width[field]); break;
  }
}

char BdResponse[23];

void setSubframeAndPage(void) {
  int id = almMsg.w3msb.svID;
  printf("\r%2d", id);
  almMsg.subframe = id < 25 ? 5 : 4;  // 24 in subframe 5, 8 in subframe 4  pgs 2..5  7..10
  int page = id;
  if (page >= 25) {
    page += 2 - 25;
    if (page >= 6)  // page 6 skipped
      ++page;
  }
  almMsg.page = page;
}

void sendAlmanac() {
  printf("   almanac");
  FILE* alm = fopen("current.alm", "rb");
  if (!alm) exit(-5);
  almMsg.w3msb.dataID = 1;   // TODO: see pg. 113
  int week;
  for (int sat = 1; sat <= 32; ++sat) {
    char line[128];
    if (fgets(line, sizeof(line), alm)) {  // skip header line - exit on end of file
      week = atoi(line + 13);
    
      for (int i = 0; i < 12; ++i) {
        if (!fgets(line, sizeof(line), alm)) {
          printf("%s", line);
          exit(-7);
        }
        setField(i, line);
      }

      fgets(line, sizeof(line), alm);  // skip week
      fgets(line, sizeof(line), alm);  // skip blank line
    } 

    setSubframeAndPage();
    motoCmd(&almMsg, sizeof(almMsg), &response, 9);
  }

  // svID 28 currently missing
  almMsg.w3msb.svID = 28;
  setSubframeAndPage();
  motoCmd(&almMsg, sizeof(almMsg), &response, 9);

  // TODO: health from svHealth ( 0 = OK )
  // svConfig codes 0 = no info
  uchar toa = almMsg.toa;
  almMsg.page = 25;
  almMsg.subframe = 4; // p 87
  memset(&almMsg.eccentric, 0, sizeof(almMsg) - 5); 
  motoCmd(&almMsg, sizeof(almMsg), &response, 9);

  almMsg.subframe = 5; // p 83
  almMsg.page25.toa = toa; // ?? scale ??, offset TODO
  almMsg.page25.week = week & 0xFF; 
  motoCmd(&almMsg, sizeof(almMsg), &response, 9);

  // see also p 117, 120, 226

  fclose(alm);

  printf("\n for week %d\n", week);
}


uchar almanac[34*33];

int main() {

  if (sizeof(almMsg) != 33 - 5) exit(sizeof(almMsg));

#if 1
  if (!openSerial("COM3", 9600)) exit(-1);
#else
  if (!openSerial("COM3", 4800)) exit(-1);
  WriteFile(hCom, "$PMOTG,FOR,0*2A", 15, NULL, NULL);
  setComm(9600);
#endif

  WriteFile(hCom, NULL, 1, NULL, NULL); // to set baud rate?
  setResponseMs(1000);
  ReadFile(hCom, response, sizeof(response), &bytesRead, NULL);  // flush

  motoCmd("Cg\000", 3); // Position fix mode off

  setResponseMs(20000);  // ??

  motoCmd("Cj", 2, &response, 294);
  printf("%s\n", response + 4); // ID

  motoCmd(NUM_SATS == 6 ? "Ba\000" : "Ea\000", 3, response, NUM_SATS == 6 ? 68 : 76); // poll mode

  char almStatus[23];
  motoCmd("Bd\000", 3, almStatus, sizeof(almStatus));
  if (almStatus[4]) { // save alamanc
    motoCmd("Be\000", 3, almanac, sizeof(almanac)); // request almanac data
    FILE* alm = fopen("almanac.old.alm", "wb");
    fwrite(almanac, 1, bytesRead, alm);
    fclose(alm);
  } else {
    // motoCmd("Cf", 2); // factory defaults    returns Cg0 -- why?
    sendAlmanac();
    Sleep(1000);  // process almanac
    motoCmd("Bd\000", 3, almStatus, sizeof(almStatus));
    printf("Almanac %s\n", almStatus[4] ? "OK" : "bad!");
  }

  ReadFile(hCom, response, sizeof(response), &bytesRead, NULL);  // flush

  if (1) { // ignored if already fixing position
    // set date / time
    time_t rtime; time(&rtime);
    struct tm* pTime = gmtime(&rtime); pTime->tm_year += 1900;
    char cmd[16];
    sprintf(cmd, "Ac%c%c%c%c", pTime->tm_mon + 1, pTime->tm_mday, pTime->tm_year / 256, pTime->tm_year % 256);
    motoCmd(cmd, 6);

    sprintf(cmd, "Aa%c%c%c", pTime->tm_hour, pTime->tm_min, pTime->tm_sec);
    motoCmd(cmd, 5);

    // set approx position
    const int latitude = (int)(37.3392573 * 3600000);  // in milli arc seconds, Big endian
    const int longitude = (int)(-122.0496515 * 3600000);
    const int height = 80 * 100;  // in cm

    struct {    
      char cmdA;
      char cmd;
      int val;
      char MSL;      
    } posCmd = { 'A', 'd', 0, 1 };

    posCmd.val = _byteswap_ulong(latitude);
    motoCmd(&posCmd, 6);

    posCmd.cmd = 'f';
    posCmd.val = _byteswap_ulong(height);
    motoCmd(&posCmd, 7, &response, 15);    // slow reponse
  }

  motoCmd("Ac\377\377\377\377", 6);  // check date
  int year = response[6] * 256 + response[7];
  printf("GMT date: %X/%X/%d\n", response[4], response[5], year);

  motoCmd("Ag\000", 3); // set mask angle to 0 to use full sky

  motoCmd("Cg\001", 3); // position fix mode (for VP units)
  
  do {
    while (!_kbhit()) {
      if (motoCmd(NUM_SATS == 6 ? "Ba\000" : "Ea\000", 3, &baMsg, sizeof(baMsg)) && bytesRead == sizeof(baMsg)) { // unsolicited 1/sec  (or slower with longer serial timeout)
        for (int s = 0; s < NUM_SATS; ++s)
          printf("%3d:%X%+3d ", baMsg.sat[s].satID, baMsg.sat[s].trackMode, (char)(baMsg.sat[s].SNR + 127));

        printf("%X", *(uchar*)&baMsg.rcvrStatus);
      }
      else printf("Read %d", bytesRead);
      printf("\n");

      Sleep(5000);  // or more
    }
  } while (_getch() != 27);  // Escape

  CloseHandle(hCom);
}


#else //  GPS-500  Sirf III

// Sirf binary messages start with A0 A2, end with B0 B3

int main() {
  openSerial("COM4", 4800);

#if 1
  while (!_kbhit()) {
    char line[1024];
    DWORD bytesRead;
    ReadFile(hCom, line, sizeof(line), &bytesRead, NULL);
    if (bytesRead) {
      line[bytesRead] = 0;
      printf("%s\n", line);
    }
  }
  _getch();
#endif

  // ?? baud rate, format??

  setComm(4800, true);  // Pin 1a low

  while (!_kbhit()) {
    unsigned char line[512];
    DWORD bytesRead;
    ReadFile(hCom, line, sizeof(line), &bytesRead, NULL);

    for (DWORD i = 0; i < bytesRead; ++i)
      printf("%02X ", line[i]);
    printf("\n");    
  }
  CloseHandle(hCom);
}

#endif
