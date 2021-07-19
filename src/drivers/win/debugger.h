#ifndef DEBUGGER_H
#define DEBUGGER_H

//#define GetMem(x) (((x < 0x2000) || (x >= 0x4020))?ARead[x](x):0xFF)
#include <windows.h>
//#include "debug.h"

// TODO: Maybe change breakpoint array to std::vector
// Maximum number of breakpoints supported
#define MAXIMUM_NUMBER_OF_BREAKPOINTS 64

// Return values for AddBreak
#define TOO_MANY_BREAKPOINTS 1
#define INVALID_BREAKPOINT_CONDITION 3

extern HWND hDebug;

extern int childwnd,numWPs;
extern bool debuggerAutoload;
extern bool debuggerSaveLoadDEBFiles;
extern bool debuggerDisplayROMoffsets;
extern bool debuggerIDAFont;

extern unsigned int debuggerPageSize;
extern unsigned int debuggerFontSize;
extern unsigned int hexeditorFontWidth;
extern unsigned int hexeditorFontHeight;
extern char* hexeditorFontName;

void CenterWindow(HWND hwndDlg);
void DoPatcher(int address,HWND hParent);
int GetEditHex(HWND hwndDlg, int id);

extern void AddBreakList();
extern char* BreakToText(unsigned int num);

void UpdateDebugger(bool jump_to_pc = true);
void DoDebug(uint8 halt);
void DebuggerExit();
void DisassembleToWindow(HWND hWnd, int id, int scrollid, unsigned int addr, int skiplines = 0);
void DisassembleToWindow(HWND hWnd, int id, int scrollid);
void PrintOffsetToSeekAndBookmarkFields(int offset);

void LoadGameDebuggerData(HWND hwndDlg);
void updateGameDependentMenusDebugger();

extern bool inDebugger;

extern class DebugSystem {
public:
	DebugSystem();
	~DebugSystem();
	
	void init();

	HFONT hFixedFont;
	int fixedFontWidth;
	int fixedFontHeight;

	HFONT hIDAFont;

	HFONT hDisasmFont;
	int disasmFontHeight;

	HFONT hHexeditorFont;
	int HexeditorFontWidth;
	int HexeditorFontHeight;

} *debugSystem;

// Partial List of Color Definitions
// owomomo: I'm tired to write those repeated words.
#define OPHEXRGB(_OP, _SEP)                              \
/* normal hex text color */                              \
_OP(HexFore,         0,   0,   0) _SEP /* Black */       \
/* normal hex background color */                        \
_OP(HexBack,       255, 255, 255) _SEP /* White */       \
/* highlight hex text color */                           \
_OP(HexHlFore,     255, 255, 255) _SEP /* White */       \
/* highlight hex background color */                     \
_OP(HexHlBack,       0,   0,   0) _SEP /* Black */       \
/* unfocused highlight hex text color */                 \
_OP(HexHlShdFore,    0,   0,   0) _SEP /* Black */       \
/* unfocused highlight hex background color */           \
_OP(HexHlShdBack,  224, 224, 224) _SEP /* Grey */        \
/* freezed color */                                      \
_OP(HexFreeze,       0,   0, 255) _SEP /* Blue */        \
/* freezed color in ROM */                               \
_OP(RomFreeze,     255,   0,   0) _SEP /* Red */         \
/* hex color out of bounds */                            \
_OP(HexBound,      220, 220, 220) _SEP /* Grey */        \
/* bookmark color */                                     \
_OP(HexBookmark,     0, 204,   0) _SEP /* Light green */ \
/* address header color*/                                \
_OP(HexAddr,       128, 128, 128)      /* Dark grey */

#define OPCDLRGB(_OP, _SEP)                              \
/* Logged as code */                                     \
_OP(CdlCode,       160, 140,   0) _SEP /* Dark yellow */ \
/* Logged as data */                                     \
_OP(CdlData,         0,   0, 210) _SEP /* Blue */        \
/* Logged as PCM */                                      \
_OP(CdlPcm,          0, 130, 160) _SEP /* Cyan */        \
/* Logged as code and data */                            \
_OP(CdlCodeData,     0, 190,   0) _SEP /* Green */       \
/* Rendered */                                           \
_OP(CdlRender,     210, 190,   0) _SEP /* Yellow */      \
/* Read */                                               \
_OP(CdlRead,        15,  15, 255) _SEP /* Light blue */  \
/* Rendered and read */                                  \
_OP(CdlRenderRead,   5, 255,   5)      /* Light green */

// Debugger color list
// owomomo: the configurable colors
#define OPDBGRGB(_OP, _SEP)                        \
/* PC */                                           \
_OP(DbgPC,       0,   0, 255) _SEP /* Blue */      \
/* Mnemonic */                                     \
_OP(DbgMnem,     0,   0, 128) _SEP /* Dark blue */ \
/* Operand */                                      \
_OP(DbgOper,     0, 128,   0) _SEP /* Green */     \
/* Comment */                                      \
_OP(DbgComm,   128, 128, 128) _SEP /* Grey */      \
/* Operand note */                                 \
_OP(DbgOpNt,   128, 128, 255) _SEP /* Purple */    \
/* Symbolic name */                                \
_OP(DbgSym,     34,  75, 143) _SEP /* Rurikon */   \
/* Effective address */                            \
_OP(DbgEff,    106, 109, 169) _SEP /* Fujinando */ \
/* RTS */                                          \
_OP(DbgRts,    187,  80,  93)      /* Imayou */

#define _COMMA ,
#define SBT(name, suf) name##suf

#define SBCLR(name, suf) SBT(name, Color)##suf
#define SPCLR(pf, name, suf) pf SBCLR(name, suf)
#define CSCLR(pf, name, suf, op, val) SPCLR(pf, name, suf) op val
#define CNRGB(pf, name, op, r, g, b, sep) CSCLR(pf, name, R, op, r) sep CSCLR(pf, name, G, op, g) sep CSCLR(pf, name, B, op, b)
#define PPRGB(name) NULL, CNRGB(&, name, , , , , _COMMA)
#define MKRGB(name) (RGB(SBCLR(name, R), SBCLR(name, G), SBCLR(name, B)))
#define DEFRGB(name, r, g, b) CNRGB( , name, =, r, g, b, _COMMA)
#define DCLRGB(name, r, g, b) CNRGB( , name, , , , , _COMMA)
#define CMPRGB(name, r, g, b) (CNRGB( , name, !=, r, g, b, ||))

#define SBCF(name) SBT(name, ChFmt)
#define PPCF(name) &SBCF(name)
#define PPCCF(name) PPRGB(name), PPCF(name)
#define DEFCF(name, r, g, b) (DEFRGB(name, r, g, b), SBCF(name).crTextColor = RGB(r, g, b))
#define CMPCF(name, r, g, b) (CNRGB( , name, !=, r, g, b, ||) || SBCF(name).crTextColor != RGB(r, g, b))
#define INITCF(name)                        \
(memset(PPCF(name), 0, sizeof(SBCF(name))), \
SBCF(name).cbSize = sizeof(SBCF(name)),     \
SBCF(name).dwMask = CFM_COLOR,              \
SBCF(name).crTextColor = MKRGB(name))

#define RGBOP DCLRGB

#define DefHexRGB OPHEXRGB(RGBOP, _COMMA)
#define DefCdlRGB OPCDLRGB(RGBOP, _COMMA)
#define DefDbgRGB OPDBGRGB(RGBOP, _COMMA)
#define DefDbgChFmt OPDBGRGB(SBCF, _COMMA)

extern int DefHexRGB, DefCdlRGB, DefDbgRGB;

#undef RGBOP
#define RGBOP DEFRGB

#define RestoreDefaultHexColor() (DefHexRGB)
#define IsHexColorDefault() (!(OPHEXRGB(CMPRGB, ||)))

#define RestoreDefaultCdlColor() (DefCdlRGB)
#define IsCdlColorDefault() (!(OPCDLRGB(CMPRGB, ||)))

#define InitDbgCharFormat() (OPDBGRGB(INITCF, _COMMA))
#define RestoreDefaultDebugColor() (OPDBGRGB(DEFCF, _COMMA))
#define IsDebugColorDefault() (!(OPDBGRGB(CMPCF, ||)))

#endif
