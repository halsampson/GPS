// GPS-500.cpp 

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <time.h> 

typedef unsigned char uchar;

HANDLE hCom = NULL;
DCB dcb;

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

  // USB bulk packets arrive at 1 kHz rate
  COMMTIMEOUTS timeouts = { 0 };  // in ms
  timeouts.ReadIntervalTimeout = 1000; // between charaters
  timeouts.ReadTotalTimeoutMultiplier = 20; // * num requested chars
  timeouts.ReadTotalTimeoutConstant = 1000; // + this = total timeout
  if (!SetCommTimeouts(hCom, &timeouts))  printf("Can't SetCommTimeouts\n");

  return hCom;
}

void setComm(int baudRate, bool dtrEnable = false) {
  dcb.BaudRate = baudRate;
  dcb.fDtrControl = dtrEnable ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
  SetCommState(hCom, &dcb);
}


DWORD bytesRead;
uchar response[256];

bool motoCmd(const void* cmd, int len, void* pResponse = response, int responseLen = sizeof(response)) {
  uchar sum = 0;
  for (int p = 0; p < len; ++p) 
    sum ^= ((char*)cmd)[p]; // XOR of message bytes after @@ and before checksum

  uchar send[16] = "@@";
  memcpy(send + 2, cmd, len);
  send[2 + len] = sum;  // checksum
  memcpy(send + 3 + len, "\r\n", 2); // CRLF

  WriteFile(hCom, send, len + 5, NULL, NULL);
  bool OK = ReadFile(hCom, pResponse, responseLen, &bytesRead, NULL);
  ((char*)(pResponse))[bytesRead] = 0;
  return OK;
}


#if 1  // Oncore black boxes

const int NUM_SATS = 8;  // or 8  - could auto-set via lack of response to @@Ea with 6 channel recvrs

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
      char used : 1;
      char momAlert : 1;
      char spoof : 1;
      char unhealthy : 1;
      char inaccurate : 1;
      char spare2 : 1;
      char spare1 : 1;
      char parityErr : 1;
    } chStatus;
  } sat[NUM_SATS];

  struct {
    char posProp : 1;
    char poorGeo : 1;
    char fix3D : 1;
    char fix2D : 1;
    char acquire : 1;
    char differential : 1;
    char insuffVisible : 1;
    char badAlmanac : 1;
  } recvrStatus;

  uchar check;
  char CR;
  char LF;
} baMsg;


#pragma pack(1)

typedef struct {
  char b[3];
} int24;

struct {
  char subframe;  // 5: pg 1..25;  4: 2..5, 7..10. 25
  char page;

  // Big endian
  struct {
    char dataID : 2;
    char svID   : 6;
  };
  short eccentricity; // 2^-16

  char  toa;          //  12
  short deltaI;       // -14

  short Ohmega;       // -15
  char  svHealth; 
  
  int24 rootA;        // -4
  int24 ohm0;         // -15
  int24 ohmeg;        // -15
  int24 M0;           // -15
  struct {
    char af0msb;      // -20
    char af1msb;      // -37
    char af1lsb :  3;
    char af0lsb :  3;
    char t      :  2;
  };
} almPageMsg; // s/b 26 bytes + 7 (@@Cb    Chk CR LF) = 33 byte messages


/*

@@Cb

Subframe 5, pg 1 first

32! PRNs,  33 bytes each - 9 ovhd = 24 * 8 bits each

ids   8
e    16
toa   8
dt   16
Ohm  16
svH   8
sqrA 24
Ohm0 24
ohm  24
M0   24
af0  11
af1  11
t     2
________
    192

See IS-GPS-200M pg 82, 179

// parse:
ID                                01
Health : 000
Eccentricity : 0.1132822037E-001
Time of Applicability(s) : 233472.0000
Orbital Inclination(rad) : 0.9865078384
Rate of Right Ascen(r / s) : -0.8114623721E-008
SQRT(A)  (m 1 / 2) : 5153.621094
Right Ascen at Week(rad) : -0.1660719209E+001
Argument of Perigee(rad) : 0.882766995
Mean Anom(rad) : 0.3052278345E+001
Af0(s) : 0.4425048828E-003
Af1(s / s) : -0.1091393642E-010
week : 147
*/


char almanac[1122];

int main() {

  if (sizeof(almPageMsg) != 26) exit(sizeof(almPageMsg));

#if 1
  if (!openSerial("COM3", 9600)) exit(-1);
#else
  if (!openSerial("COM3", 4800)) exit(-1);

  WriteFile(hCom, "$PMOTG,FOR,0*2A", 15, NULL, NULL);
  setComm(9600);
#endif

  motoCmd(NUM_SATS == 6 ? "Ba\000" : "Ea\000", 3); // poll

  motoCmd("Ac\377\377\377\377", 6);  // check date
  int year = response[6] * 256 + response[7];
  if (year != 2022) {
    motoCmd("Cf", 2); // factory defaults;
    // TODO: load almanac
  } else {
    motoCmd("Be\000", 3, almanac, sizeof(almanac)); // request alamanac data
    if (bytesRead > 1000) {
      FILE* alm = fopen("almanac.alm", "wb");
      fwrite(almanac, 1, bytesRead, alm);
      fclose(alm);
    }
  }

  if (1) { // ignored if already fixing position
    // set date / time
    time_t rtime; time(&rtime);
    struct tm* pTime = gmtime(&rtime);
    char cmd[16];
    sprintf(cmd, "Ac%c%c%c%c", pTime->tm_mon + 1, pTime->tm_mday, pTime->tm_year / 256, pTime->tm_year % 256);
    motoCmd(cmd, 6);

    sprintf(cmd, "Aa%c%c%c", pTime->tm_hour, pTime->tm_min, pTime->tm_sec);
    motoCmd(cmd, 5);

    // set approx position
    const int latitude = (int)(37.3392573 * 3600000);  // in milli arc seconds, Big endian
    const int longitude = (int)(- 122.0496515 * 3600000);
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
    motoCmd(&posCmd, 7);    
  }

  motoCmd("Ac\377\377\377\377", 6);  // check date
  year = response[6] * 256 + response[7];
  printf("Date is %X/%X/%d\n", response[4], response[5], year); // wrong *******************8

  motoCmd("Cj", 2);
  printf("%s\n", response); // ID

  motoCmd("Ag\000", 3); // set mask angle to 0 to use full sky
  motoCmd("Cg\001", 3); // position fix mode (for VP units)
    
  while (!_kbhit()) {
     if (motoCmd(NUM_SATS == 6 ? "Ba\000" : "Ea\000", 3, &baMsg, sizeof(baMsg)) && bytesRead == sizeof(baMsg)) { // unsolicited 1/sec  (or slower with longer serial timeout)
      for (int s = 0; s < NUM_SATS; ++s)
        printf("%3d:%X%+3d ", baMsg.sat[s].satID, baMsg.sat[s].trackMode, (char)(baMsg.sat[s].SNR + 127));
    } else printf("Read %d", bytesRead);
    printf("\n");

    Sleep(60000);  // or more
  }
  CloseHandle(hCom);
}

#else //  GPS-500  Sirf

int main() {
  openSerial("COM4", 4800);

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


// What is this data?   Telescope mode?

/*  4800 baud:
34 9B 63 AC D2 6B 36 1A 8B B1
1C 93 63 AC D2 6B 36 1A 8B B1
34 9B 8F C7 23 AC 6C 1A 8B B1
1C 93 AC D6 69 2D 23 AC D6 B6 1B 8D 9B B2 63 1A 8B B1
64 6D 36 8D 23 AC 6C 1A 8B B1
64 3D 1E 8D 23 AC 6C 1A 8B B1
B4 F2 D6 6B 1A 2D 23 B4 6C F6
B4 F2 76 6B 1A 2D 23 B4 6C F6
B4 D9 D6 6B 1A 2D 23 AC D6 B6 1B 8D 9B B2 63 1A 8B B1
64 6D 36 8D 23 AC 6C 1A 8B B1
B4 F2 76 3B 1A 2D 23 B4 6C F6
1C 93 AC D6 69 2D 23 B4 6C F6
A4 F4 79 6B 8D 63 B1 D2 6D B6 FE
1C 93 AC D6 69 2D 23 AC D6 B6 1B 8D 9B B2 63 1A 8B B1
34 9B 8F C7 23 AC 6C 1A 8B B1
8C 93 AC D6 69 2D 23 B4 6C F6
B4 F2 76 6B 1A 2D 23 B4 6C F6
B4 F2 76 6B 1A 2D 23 B4 6C F6
B4 99 D6 76 69 2D 23 AC D6 B6 1B 8D 9B B2 63 1A 8B B1
B4 F2 76 6B 1A 2D 23 B4 6C F6
34 9B 8F C7 23 AC 6C 1A 8B B1
A3 B2 D6 6B 39 2D 23 B4 6C F6
A3 32 AC D6 69 2D 23 B4 6C F6
E4 CD 36 8D 23 AC 6C 1A 8D 93 B7 D2 6B 36 5B 8C A3 B1 EC
A4 F4 79 6B 8D 63 B1 D2 6D B6 FE
46 26 63 AC D2 6B 36 1A 8B B1
B4 F2 76 6B 1A 2D 23 B4 6C F6
64 6D 36 8D 23 AC 6C 1A 8B B1
E4 6D 36 8D 23 AC 6C 1A 8D 93 B7 D2 6B 36 5B 8C A3 B1 EC
A3 B2 D6 6B 1A 2D 23 B4 6C F6
34 9B 8F C7 23 AC 6C 1A 8B B1
A3 B2 D6 6B 1A 2D 23 B4 6C F6
1C 93 AC D6 69 2D 23 B4 6C F6
B4 D9 D6 6B 1A 2D 23 AC D6 B6 1B 8D 9B B2 63 1A 8B B1
34 9B 63 AC D2 6B 36 1A 8B B1
B4 F2 76 3B 1A 2D 23 B4 6C F6
B4 F2 76 3B 1A 2D 23 B4 6C F6
B4 F2 76 3B 1A 2D 23 B4 6C F6
B4 D9 D6 6B 1A 2D 23 AC D6 B6 1B 8D 63 B5 1B 8D 8B B1
34 9B 63 AC D2 6B 36 1A 8B B1
A4 F4 79 3B 8D 63 B1 D2 6D B6 FB
B4 F2 76 3B 1A 2D 23 B4 6C F6
A3 B2 D6 6B 1A 2D 23 B4 6C F6
B4 D9 D6 6B 1A 2D 23 AC D6 B6 1B 8D 63 B5 1B 8D 8B B1
A3 B2 D6 6B 1A 2D 23 B4 6C F6
64 3D 36 8D 23 AC 6C 1A 8B B1
B4 F2 76 3B 1A 2D 23 B4 6C F6
34 9B 8F C7 23 AC 6C 1A 8B B1
B4 D9 D6 6B 1A 2D 23 AC D6 B6 1B 8D 63 B5 1B 8D 8B B1
8C 93 AC D6 69 2D 23 B4 6C F6
46 93 AC D6 69 2D 23 B4 6C F6
B4 F2 76 3B 1A 2D 23 B4 6C F6
A3 B2 D6 6B 1A 2D 23 B4 6C F6
*/

// 9600 baud: all paired bits -> baud too fast
/*
06 0F F6 30 0F CF 60 F3 E0 0F F6
06 0F F6 30 0F CF 60 F3 E0 0F F6
06 0F F6 30 0F CF 60 F6 E0 0F F6
60 3E E0 0F F6 30 0F CF 60 F6 78 0F CF 86 CF 0F 60 F6 78 0F C3 06 0F CF
06 0F F6 30 0F CF 60 F6 E0 0F F6
60 CF 78 F8 F6 18 0F CF 60 F3 E0 0F F6
60 CF 78 F8 F6 18 0F CF 60 F6 E0 0F F6
3C E0 0F F6 30 0F CF 60 F6 E0 0F F6
06 0F F6 30 0F CF 60 F6 78 0F CF 86 CF 0F 60 F3 78 0F C3 06 0F CF
60 CF 78 F8 F3 18 0F CF 60 F6 E0 0F F6
3C E0 0F F6 30 0F CF 60 F6 E0 0F F6
60 3E E0 0F F6 30 0F CF 60 F6 E0 0F F6
F0 03 0F F6 30 0F CF 60 F6 E0 0F F6
06 0F F6 30 0F CF 60 F6 78 0F CF 86 CF 0F 60 F6 78 0F C3 06 0F CF
60 3E E0 0F F6 30 0F CF 60 F6 E0 0F F6
CF F0 F8 F6 18 0F CF 60 F6 E0 0F F6
60 CF 78 F8 F3 18 0F CF 60 F6 E0 0F F6
60 3F E0 0F F6 30 0F CF 60 F3 E0 0F F6
60 CF 78 F8 F6 18 0F CF 60 F6 78 0F CF 86 CF 0F 60 F6 78 0F C3 06 0F CF
06 0F F6 30 0F CF 60 F6 E0 0F F6
F0 03 0F F6 30 0F CF 60 F6 E0 0F F6
60 CF 78 F8 F6 18 0F CF 60 F6 E0 0F F6
60 06 0F F6 30 0F CF 60 F6 E0 0F F6
CF F0 F8 F6 18 0F CF 60 F6 78 0F CF 86 CF 0F 60 F6 78 0F C3 06 0F CF
06 0F F6 30 0F CF 60 F6 E0 0F F6
60 CF 78 F8 F6 18 0F CF 60 F6 E0 0F F6
00 0F F6 30 0F CF 60 F6 80 0F CF
00 0F F6 30 0F CF 60 F6 80 0F CF
00 0F F6 30 0F CF 60 F6 78 0F CF 86 CF 0F 60 F6 78 0F C3 00 0F CF
3C C0 0F F6 30 0F CF 60 F6 80 0F CF
00 0F F6 30 3C 9E 0F C3 00 0F CF
0F F0 F8 F2 18 0F CF 60 F6 80 0F CF
00 0F F6 30 0F CF 60 F6 80 0F CF
60 00 0F F6 30 0F CF 60 F6 78 0F CF 86 CF 0F 60 F6 78 0F C3 00 0F CF
00 0F F6 30 0F CF 60 F6 80 0F CF
00 0F F6 30 0F CF 60 F6 80 0F CF
E0 00 0F F6 30 0F CF 60 F6 80 0F CF
*/