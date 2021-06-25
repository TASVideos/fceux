/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Ben Parnell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "common.h"
#include "../../utils/xstring.h"
#include "../../types.h"
#include "debugger.h"
#include "../../x6502.h"
#include "../../fceu.h"
#include "../../debug.h"
#include "../../nsf.h"
#include "../../ppu.h"
#include "../../cart.h"
#include "../../ines.h"
#include "../../asm.h"
#include "tracer.h"
#include "memview.h"
#include "cheat.h"
#include "gui.h"
#include "ntview.h"
#include "cdlogger.h"
#include "ppuview.h"
#include "richedit.h"

// ################################## Start of SP CODE ###########################

#include "debuggersp.h"

extern Name* pageNames[32];
extern Name* ramBankNames;
extern bool ramBankNamesLoaded;
extern int pageNumbersLoaded[32];
extern int myNumWPs;

// ################################## End of SP CODE ###########################

extern int vblankScanLines;
extern int vblankPixel;
extern bool DebuggerWasUpdated;

int childwnd;

extern readfunc ARead[0x10000];
int DbgPosX,DbgPosY;
int DbgSizeX=-1,DbgSizeY=-1;
int WP_edit=-1;
int ChangeWait=0,ChangeWait2=0;
uint8 debugger_open=0;
HWND hDebug;
static HMENU hDebugcontext;     //Handle to context menu
static HMENU hDebugcontextsub;  //Handle to context sub menu
static HMENU hDisasmcontext;     //Handle to context menu
static HMENU hDisasmcontextsub;  //Handle to context sub menu
WNDPROC IDC_DEBUGGER_DISASSEMBLY_oldWndProc = 0;

static SCROLLINFO si;

bool debuggerAutoload = false;
bool debuggerSaveLoadDEBFiles = true;
bool debuggerIDAFont = false;
unsigned int IDAFontSize = 16;
bool debuggerDisplayROMoffsets = false;

wchar_t* debug_wstr;
char* debug_cdl_str;
char* debug_str_decoration_comment;
char* debug_decoration_comment;
char* debug_decoration_comment_end_pos;

FINDTEXT newline;
FINDTEXT num;

int DefDbgRGB;
CHARFORMAT2 DefDbgChFmt;

struct DBGCOLORMENU {
	COLORMENU menu;
	CHARFORMAT2 *fmt;
} dbgcolormenu[] = {
	{ "PC",                PPCCF(DbgPC)   }, 
	{ NULL                                },
	{ "Mnemonic",          PPCCF(DbgMnem) },
	{ NULL                                },
	{ "Symbolic Name",     PPCCF(DbgSym)  },
	{ "Comment" ,          PPCCF(DbgComm) },
	{ NULL                                },
	{ "Operand" ,          PPCCF(DbgOper) },
	{ "Operand Note" ,     PPCCF(DbgOpNt) },
	{ "Effective Address", PPCCF(DbgEff)  },
	{ NULL                                },
	{ "RTS/RTI Line",      PPCCF(DbgRts)  }
};

#define ID_COLOR_DEBUGGER             2000

bool ChangeColor(HWND hwnd, DBGCOLORMENU* item)
{
	if (ChangeColor(hwnd, (COLORMENU*)item))
	{
		item->fmt->crTextColor = RGB(*item->menu.r, *item->menu.g, *item->menu.b);
		return true;
	}
	return false;
}

// this is used to keep track of addresses that lines of Disassembly window correspond to
std::vector<uint16> disassembly_addresses;
// this is used to keep track of addresses in operands of each printed instruction
std::vector<std::vector<uint16>> disassembly_operands;
// this is used to autoscroll the Disassembly window while keeping relative position of the ">" pointer inside this window
unsigned int PC_pointerOffset = 0;
int PCLine = -1;
// this is used for dirty, but unavoidable hack, which is necessary to ensure the ">" pointer is visible when stepping/seeking to PC
bool PCPointerWasDrawn = false;
// and another hack...
int beginningOfPCPointerLine = -1;	// index of the first char within debug_str[] string, where the ">" line starts

#define INVALID_START_OFFSET 1
#define INVALID_END_OFFSET 2

#define MAX_NAME_SIZE 200
#define MAX_CONDITION_SIZE 200

void UpdateOtherDebuggingDialogs()
{
	//This updates all the other dialogs such as ppu, nametable, logger, etc in one function, should be applied to all the step type buttons
	NTViewDoBlit(0);		//Nametable Viewer
	UpdateLogWindow();		//Trace Logger
	UpdateCDLogger();		//Code/Data Logger
	PPUViewDoBlit();		//PPU Viewer
}

#define DISASM_DEFAULT_WIDTH (debuggerIDAFont ? 540 : 470)

#define DEBUGGER_MIN_HEIGHT_LEFT 120 // Minimum height for the left part
#define DEBUGGER_MIN_HEIGHT_RIGHT 590 // Minimun height for the right part.

#define DEBUGGER_MIN_WIDTH 360 // Minimum width for debugger
#define DEBUGGER_DEFAULT_HEIGHT 594 // default height for debugger
// Default width of the debugger depends on the default width of disasm view, so it's not defined here.

void RestoreSize(HWND hwndDlg)
{
	HDC hdc = GetDC(hwndDlg);
	RECT wndRect, disasmRect;
	GetWindowRect(hwndDlg, &wndRect);
	GetWindowRect(GetDlgItem(hwndDlg, IDC_DEBUGGER_DISASSEMBLY), &disasmRect);

	int default_width = (disasmRect.left - wndRect.left) + DISASM_DEFAULT_WIDTH + (wndRect.right - disasmRect.right);
	int default_height = MulDiv(DEBUGGER_DEFAULT_HEIGHT, GetDeviceCaps(hdc, LOGPIXELSY), 96);

	ReleaseDC(hwndDlg, hdc);
	
	SetWindowPos(hwndDlg,HWND_TOP,DbgPosX,DbgPosY,default_width,default_height,SWP_SHOWWINDOW);
}

unsigned int NewBreakWindows(HWND hwndDlg, unsigned int num, bool enable)
{
	char startOffsetBuffer[5] = {0};
	char endOffsetBuffer[5] = {0};
	unsigned int type = 0;

	GetDlgItemText(hwndDlg, IDC_ADDBP_ADDR_START, startOffsetBuffer, sizeof(startOffsetBuffer));
	GetDlgItemText(hwndDlg, IDC_ADDBP_ADDR_END, endOffsetBuffer, sizeof(endOffsetBuffer));

	if (IsDlgButtonChecked(hwndDlg, IDC_ADDBP_MEM_CPU))
		type |= BT_C;
	else if (IsDlgButtonChecked(hwndDlg, IDC_ADDBP_MEM_PPU))
		type |= BT_P;
	else
		type |= BT_S;

	if (IsDlgButtonChecked(hwndDlg, IDC_ADDBP_MODE_R))
		type |= WP_R;

	if (IsDlgButtonChecked(hwndDlg, IDC_ADDBP_MODE_W))
		type |= WP_W;

	if (IsDlgButtonChecked(hwndDlg, IDC_ADDBP_MODE_X))
		type |= WP_X;

	//this overrides all
	if (IsDlgButtonChecked(hwndDlg, IDC_ADDBP_MODE_F))
		type = WP_F;

	int start = offsetStringToInt(type, startOffsetBuffer);

	if (start == -1)
	{
		return INVALID_START_OFFSET;
	}

	int end = offsetStringToInt(type, endOffsetBuffer);

	if (*endOffsetBuffer && end == -1)
	{
		return INVALID_END_OFFSET;
	}

	// Handle breakpoint conditions
	char name[MAX_NAME_SIZE] = {0};
	GetDlgItemText(hwndDlg, IDC_ADDBP_NAME, name, MAX_NAME_SIZE);
	
	char condition[MAX_CONDITION_SIZE] = {0};
	GetDlgItemText(hwndDlg, IDC_ADDBP_CONDITION, condition, MAX_CONDITION_SIZE);

	return NewBreak(name, start, end, type, condition, num, enable);
}

/**
* Adds a new breakpoint to the breakpoint list
*
* @param hwndDlg Handle of the debugger window
* @return 0 (success), 1 (Too many breakpoints), 2 (???), 3 (Invalid breakpoint condition)
**/
unsigned int AddBreak(HWND hwndDlg)
{
	if (numWPs == MAXIMUM_NUMBER_OF_BREAKPOINTS)
	{
		return TOO_MANY_BREAKPOINTS;
	}

	unsigned val = NewBreakWindows(hwndDlg,numWPs,1);
	
	if (val == 1)
	{
		return 2;
	}
	else if (val == 2)
	{
		return INVALID_BREAKPOINT_CONDITION;
	}

	numWPs++;
	myNumWPs++;
	return 0;
}

// This function is for "smart" scrolling...
// it attempts to scroll up one line by a whole instruction
int InstructionUp(int from)
{
	int i = std::min(16, from), j;

	while (i > 0)
	{
		j = i;
		while (j > 0)
		{
			if (GetMem(from - j) == 0x00)
				break;	// BRK usually signifies data
			if (opsize[GetMem(from - j)] == 0)
				break;	// invalid instruction!
			if (opsize[GetMem(from - j)] > j)
				break;	// instruction is too long!
			if (opsize[GetMem(from - j)] == j)
				return (from - j);	// instruction is just right! :D
			j -= opsize[GetMem(from - j)];
		}
		i--;
	}

	// if we get here, no suitable instruction was found
	if ((from >= 2) && (GetMem(from - 2) == 0x00))
		return (from - 2);	// if a BRK instruction is possible, use that
	if (from)
		return (from - 1);	// else, scroll up one byte
	return 0;	// of course, if we can't scroll up, just return 0!
}
int InstructionDown(int from)
{
	int tmp = opsize[GetMem(si.nPos)];
	if ((tmp))
		return from + tmp;
	else
		return from + 1;		// this is data or undefined instruction
}

static void UpdateDialog(HWND hwndDlg) {
	BOOL forbid = IsDlgButtonChecked(hwndDlg, IDC_ADDBP_MODE_F);
	BOOL enable = !forbid;
	EnableWindow(GetDlgItem(hwndDlg,IDC_ADDBP_MODE_R),enable);
	EnableWindow(GetDlgItem(hwndDlg,IDC_ADDBP_MODE_W),enable);
	EnableWindow(GetDlgItem(hwndDlg,IDC_ADDBP_MODE_X),enable);
	EnableWindow(GetDlgItem(hwndDlg,IDC_ADDBP_MEM_CPU),enable);
	EnableWindow(GetDlgItem(hwndDlg,IDC_ADDBP_MEM_PPU),enable);
	EnableWindow(GetDlgItem(hwndDlg,IDC_ADDBP_MEM_SPR),enable);	
}

INT_PTR CALLBACK AddbpCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	char str[8] = {0};
	int tmp;
				
	switch(uMsg)
	{
		case WM_INITDIALOG:
			CenterWindow(hwndDlg);
			SendDlgItemMessage(hwndDlg, IDC_ADDBP_ADDR_START, EM_SETLIMITTEXT, 4, 0);
			SendDlgItemMessage(hwndDlg, IDC_ADDBP_ADDR_END, EM_SETLIMITTEXT, 4, 0);

			// Don't limit address entry. See: debugcpp offsetStringToInt
			//DefaultEditCtrlProc = (WNDPROC)SetWindowLongPtr(GetDlgItem(hwndDlg, IDC_ADDBP_ADDR_START), GWLP_WNDPROC, (LONG_PTR)FilterEditCtrlProc);
			//SetWindowLongPtr(GetDlgItem(hwndDlg, IDC_ADDBP_ADDR_END), GWLP_WNDPROC, (LONG_PTR)FilterEditCtrlProc);

			if (WP_edit >= 0)
			{
				SetWindowText(hwndDlg,"Edit Breakpoint...");
						
				sprintf(str,"%04X",watchpoint[WP_edit].address);
				SetDlgItemText(hwndDlg,IDC_ADDBP_ADDR_START,str);
				sprintf(str,"%04X",watchpoint[WP_edit].endaddress);
				if (strcmp(str,"0000") != 0) SetDlgItemText(hwndDlg,IDC_ADDBP_ADDR_END,str);
				if (watchpoint[WP_edit].flags&WP_R) CheckDlgButton(hwndDlg, IDC_ADDBP_MODE_R, BST_CHECKED);
				if (watchpoint[WP_edit].flags&WP_W) CheckDlgButton(hwndDlg, IDC_ADDBP_MODE_W, BST_CHECKED);
				if (watchpoint[WP_edit].flags&WP_X) CheckDlgButton(hwndDlg, IDC_ADDBP_MODE_X, BST_CHECKED);
				if (watchpoint[WP_edit].flags&WP_F) CheckDlgButton(hwndDlg, IDC_ADDBP_MODE_F, BST_CHECKED);

				if (watchpoint[WP_edit].flags&BT_P) {
					CheckDlgButton(hwndDlg, IDC_ADDBP_MEM_PPU, BST_CHECKED);
					EnableWindow(GetDlgItem(hwndDlg,IDC_ADDBP_MODE_X),FALSE);
				}
				else if (watchpoint[WP_edit].flags&BT_S) {
					CheckDlgButton(hwndDlg, IDC_ADDBP_MEM_SPR, BST_CHECKED);
					EnableWindow(GetDlgItem(hwndDlg,IDC_ADDBP_MODE_X),FALSE);
				}
				else CheckDlgButton(hwndDlg, IDC_ADDBP_MEM_CPU, BST_CHECKED);

				UpdateDialog(hwndDlg);
				
// ################################## Start of SP CODE ###########################

				SendDlgItemMessage(hwndDlg,IDC_ADDBP_CONDITION,EM_SETLIMITTEXT,200,0);
				SendDlgItemMessage(hwndDlg,IDC_ADDBP_NAME,EM_SETLIMITTEXT,200,0);
				
				if (watchpoint[WP_edit].cond)
				{
					SetDlgItemText(hwndDlg, IDC_ADDBP_CONDITION, watchpoint[WP_edit].condText);
				}
				else
				{
					SetDlgItemText(hwndDlg, IDC_ADDBP_CONDITION, "");
				}
				
				if (watchpoint[WP_edit].desc)
				{
					SetDlgItemText(hwndDlg, IDC_ADDBP_NAME, watchpoint[WP_edit].desc);
				}
				else
				{
					SetDlgItemText(hwndDlg, IDC_ADDBP_NAME, "");
				}
				
// ################################## End of SP CODE ###########################
			} else
			{
				CheckDlgButton(hwndDlg, IDC_ADDBP_MEM_CPU, BST_CHECKED);
				// if lParam is not 0 then we should suggest to add PC breakpoint
				if (lParam)
				{
					CheckDlgButton(hwndDlg, IDC_ADDBP_MODE_X, BST_CHECKED);
					sprintf(str, "%04X", (unsigned int)lParam);
					SetDlgItemText(hwndDlg,IDC_ADDBP_ADDR_START,str);
					// also set the condition to only break at this Bank
					sprintf(str, "K==#%02X", getBank(lParam));
					SetDlgItemText(hwndDlg, IDC_ADDBP_CONDITION, str);
				}
			}
			break;
		case WM_CLOSE:
		case WM_QUIT:
			break;
		case WM_COMMAND:
			switch(HIWORD(wParam)) {
				case BN_CLICKED:
					switch(LOWORD(wParam)) {
						case IDC_ADDBP_MODE_F:
						{
							UpdateDialog(hwndDlg);
							break;
						}

						case IDOK:
							if (WP_edit >= 0) {
								int tmp = NewBreakWindows(hwndDlg,WP_edit,(BOOL)(watchpoint[WP_edit].flags&WP_E));
								if (tmp == 2 || tmp == INVALID_BREAKPOINT_CONDITION)
								{
									MessageBox(hwndDlg, "Invalid breakpoint condition", "Error", MB_OK | MB_ICONERROR);
									break;
								}
								EndDialog(hwndDlg,1);
								break;
							}
							if ((tmp=AddBreak(hwndDlg)) == TOO_MANY_BREAKPOINTS) {
								MessageBox(hwndDlg, "Too many breakpoints, please delete one and try again", "Breakpoint Error", MB_OK | MB_ICONERROR);
								goto endaddbrk;
							}
							if (tmp == 2) goto endaddbrk;
							else if (tmp == INVALID_BREAKPOINT_CONDITION)
							{
								MessageBox(hwndDlg, "Invalid breakpoint condition", "Error", MB_OK | MB_ICONERROR);
								break;
							}
							EndDialog(hwndDlg,1);
							break;
						case IDCANCEL:
							endaddbrk:
							EndDialog(hwndDlg,0);
							break;
						case IDC_ADDBP_MEM_CPU:
							EnableWindow(GetDlgItem(hwndDlg,IDC_ADDBP_MODE_X),TRUE);
							break;
						case IDC_ADDBP_MEM_PPU:
						case IDC_ADDBP_MEM_SPR:
							EnableWindow(GetDlgItem(hwndDlg,IDC_ADDBP_MODE_X),FALSE);
							break;
					}
					break;
			}
        	break;
	}
	return FALSE;
}

void HighlightPC(HWND hWnd)
{
	if (PCLine == -1)
		return;

	FINDTEXT ft;
	ft.lpstrText  = ">";
	ft.chrg.cpMin = 0;
	ft.chrg.cpMax = -1;
	int start = SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_FINDTEXT, (WPARAM)FR_DOWN, (LPARAM)&ft);
	if (start >= 0)
	{
		int old_start, old_end;
		SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_GETSEL, (WPARAM)&old_start, (LPARAM)&old_end);
		SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETSEL, (WPARAM)start, (LPARAM)start+20);
		SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETCHARFORMAT, (WPARAM)SCF_SELECTION, (LPARAM)PPCF(DbgPC));
		SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETSEL, (WPARAM)old_start, (LPARAM)old_end);
    }
}

void HighlightSyntax(HWND hWnd, int lines)
{
	int wordbreak = 0;
	int opbreak = 0;
	int numpos = 0;
	int old_start, old_end;
	bool commentline;
	SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_GETSEL, (WPARAM)&old_start, (LPARAM)&old_end);

	for (int line = 0; ; line++)
	{
		commentline = false;
		wordbreak = SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_FINDWORDBREAK, (WPARAM)WB_RIGHT, (LPARAM)newline.chrg.cpMin + 21);
		for (int ch = newline.chrg.cpMin; ; ch++)
		{
			if (debug_wstr[ch] == L'=' || debug_wstr[ch] == L'@' || debug_wstr[ch] == L'\n' || debug_wstr[ch] == L'-' || debug_wstr[ch] == L';')
			{
				opbreak = ch;
				break;
			}
		}
		if (debug_wstr[newline.chrg.cpMin] == L';')
			commentline = true;
		SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETSEL, (WPARAM)newline.chrg.cpMin + 20, (LPARAM)opbreak);
		int oldline = newline.chrg.cpMin;
		newline.chrg.cpMin = SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_FINDTEXT, (WPARAM)FR_DOWN, (LPARAM)&newline) + 1;
		if(newline.chrg.cpMin == 0) break;
        if (!commentline)
        {
            // symbolic address
            if (debug_wstr[newline.chrg.cpMin - 2] == L':')
		    {
			    SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETSEL, (WPARAM)oldline, (LPARAM)newline.chrg.cpMin);
			    SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETCHARFORMAT, (WPARAM)SCF_SELECTION, (LPARAM)PPCF(DbgSym));
			    continue;
		    }
            SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETCHARFORMAT, (WPARAM)SCF_SELECTION, (LPARAM)PPCF(DbgMnem));
        }
		// comment
		if (opbreak < newline.chrg.cpMin)
		{
			SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETSEL, (WPARAM)opbreak, (LPARAM)newline.chrg.cpMin);
			if (commentline)
				SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETCHARFORMAT, (WPARAM)SCF_SELECTION, (LPARAM)PPCF(DbgComm));
			else
			{
				if (debug_wstr[opbreak] == L'-')
					SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETCHARFORMAT, (WPARAM)SCF_SELECTION,
						(LPARAM)PPCF(DbgRts));
				else
				{
					SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETCHARFORMAT, (WPARAM)SCF_SELECTION, (LPARAM)PPCF(DbgOpNt));
					if (debug_wstr[opbreak] == L'@')
					{
						// effective address
						FINDTEXT ft = { { opbreak, newline.chrg.cpMin }, "=" };
						SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETSEL, opbreak, SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_FINDTEXT, (WPARAM)FR_DOWN, (LPARAM)&ft));
						SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETCHARFORMAT, (WPARAM)SCF_SELECTION, (LPARAM)PPCF(DbgEff));
					}
				}
			}
		}
		if (commentline)
			continue;
		// operand
		num.chrg.cpMin = wordbreak;
		num.chrg.cpMax = wordbreak + 6;
		numpos = SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_FINDTEXT, (WPARAM)FR_DOWN, (LPARAM)&num);
		if (numpos != 0)
		{
			if (debug_wstr[numpos + 3] == L',' || debug_wstr[numpos + 3] == L')' || debug_wstr[numpos + 3] == L'\n' || debug_wstr[numpos + 3] == L' ')
				wordbreak = numpos + 2;
			else
				wordbreak = numpos + 4;
			SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETSEL, (WPARAM)numpos, (LPARAM)wordbreak + 1);
			SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETCHARFORMAT, (WPARAM)SCF_SELECTION, (LPARAM)PPCF(DbgOper));
		}
		if (newline.chrg.cpMin == 0)
			break;
	}

	SendDlgItemMessage(hWnd, IDC_DEBUGGER_DISASSEMBLY, EM_SETSEL, (WPARAM)old_start, (LPARAM)old_end);
}

void UpdateDisassembleView(HWND hWnd, UINT id, int lines, bool text = false)
{
	// basic syntax highlighter and due richedit optimizations
	int eventMask = SendDlgItemMessage(hWnd, id, EM_SETEVENTMASK, 0, 0);
	SendDlgItemMessage(hWnd, id, WM_SETREDRAW, false, 0);

	if (text)
		SetDlgItemTextW(hWnd, id, debug_wstr);
	HighlightSyntax(hWnd, lines);
	HighlightPC(hWnd);

	SendDlgItemMessage(hWnd, id, WM_SETREDRAW, true, 0);
	InvalidateRect(GetDlgItem(hWnd, id), 0, true);
	SendDlgItemMessage(hWnd, id, EM_SETEVENTMASK, 0, eventMask);
}

// This is most reliable if you dump one subroutine at a time. If you call it with 0x8000 - 0xFFFF,
// it may misinterpret some things depending on how the data is packed. For instance:
// Say we have 2C A9 10. The A9 byte is supposted to be the start of a routine (with LDA #$10), but maybe that
// 2C gets interpreted as the opcode and we get nonsense: BIT $10A9. But the next instruction continues as normal...
// Could potentially have it use Name*s to inform its search!
// A lot of reused logic here, but we really don't want to write the whole dumpfile to a string in memory.
// Note that the endAddr is actually the address of the last INSTRUCTION. This function will grab the
// operands if they exist, and so may spill over a bit.
// Could add config to not dump address and raw data.
void Dump(FILE *fout, unsigned int startAddr, unsigned int endAddr)
{
	wchar_t chr[40] = { 0 };
	wchar_t debug_wbuf[2048] = { 0 };
	int size;
	uint8 opcode[3];
	unsigned int instruction_addr;
	unsigned int addr = startAddr; // Keeps track of which address to get the operands, etc. from

	if (symbDebugEnabled)
	{
		loadNameFiles();
		disassembly_operands.resize(0);
	}

	//figure out how many lines we can draw
	int lines = endAddr - startAddr + 1;

	// Could loop from startAddr to endAddr instead. Cut the bullshit
	unsigned int instructions_count = 0;
	for (int addr = startAddr; addr <= endAddr;)
	{
		// PC pointer
		if (addr > 0xFFFF) break;

		instruction_addr = addr;

		if (symbDebugEnabled)
		{
			// insert Name and Comment lines if needed
			Name* node = findNode(getNamesPointerForAddress(addr), addr);
			if (node)
			{
				if (node->name)
				{
					swprintf(debug_wbuf, L"%S:\n", node->name);
					// wcscat(debug_wstr, debug_wbuf);
					fprintf(fout, "%ls", debug_wbuf);
				}
				if (node->comment)
				{
					// make a copy
					strcpy(debug_str_decoration_comment, node->comment);
					strcat(debug_str_decoration_comment, "\r\n");
					// divide the debug_str_decoration_comment into strings (Comment1, Comment2, ...)
					debug_decoration_comment = debug_str_decoration_comment;
					debug_decoration_comment_end_pos = strstr(debug_decoration_comment, "\r\n");
					while (debug_decoration_comment_end_pos)
					{
						debug_decoration_comment_end_pos[0] = 0;		// set \0 instead of \r
						debug_decoration_comment_end_pos[1] = 0;		// set \0 instead of \n
						swprintf(debug_wbuf, L"; %S\n", debug_decoration_comment);
						// wcscat(debug_wstr, debug_wbuf);
						fprintf(fout, "%ls", debug_wbuf);

						debug_decoration_comment_end_pos += 2;
						debug_decoration_comment = debug_decoration_comment_end_pos;
						debug_decoration_comment_end_pos = strstr(debug_decoration_comment_end_pos, "\r\n");
					}
				}
			}
		}

		// Do we really want the PC arrow showing up in the dump?
		if (addr == X.PC)
		{ 
			// wcscat(debug_wstr, L">");
			fprintf(fout, "%ls", L">");
		}
		else
		{
			// wcscat(debug_wstr, L" ");
			fprintf(fout, "%ls", L" ");
		}

		if (addr >= 0x8000)
		{
			if (debuggerDisplayROMoffsets && GetNesFileAddress(addr) != -1)
			{
				swprintf(chr, L" %06X: ", GetNesFileAddress(addr));
			}
			else
			{
				swprintf(chr, L"%02X:%04X: ", getBank(addr), addr);
			}
		}
		else
		{
			swprintf(chr, L"  :%04X: ", addr);
		}

		// Add address
		// wcscat(debug_wstr, chr);
		fprintf(fout, "%ls", chr);

		size = opsize[GetMem(addr)];
		if (size == 0)
		{
			swprintf(chr, L"%02X        UNDEFINED", GetMem(addr++));
			// wcscat(debug_wstr, chr);
			fprintf(fout, "%ls", chr);
		}
		else
		{
			if ((addr + size) > 0xFFFF)
			{
				while (addr < 0xFFFF)
				{
					swprintf(chr, L"%02X        OVERFLOW\n", GetMem(addr++));
					// wcscat(debug_wstr, chr);
					fprintf(fout, "%ls", chr);
				}
				break;
			}
			Name* node;
			for (int j = 0; j < size; j++)
			{
				// Write the raw bytes of this instruction
				swprintf(chr, L"%02X ", opcode[j] = GetMem(addr++));
				// wcscat(debug_wstr, chr);
				fprintf(fout, "%ls", chr);
				if (j != size - 1 && (node = findNode(getNamesPointerForAddress(addr), addr)))
				{
					// We were treating this as an operand, but it's named!
					printf("$%04X - We were treating this as an operand, but it's named %s!\n", addr, node->name);
					size = j + 1;
					break;
				}
			}
			while (size < 3)
			{
				// wcscat(debug_wstr, L"   "); //pad output to align ASM
				fprintf(fout, "%ls", L"   ");
				size++;
			}
			if (node)
			{
				// TODO: Instead of this ominous and confusing message, could print ".byte $XX $YY..."
				fprintf(fout, " INTERRUPTED");
			}
			else
			{
				static char bufferForDisassemblyWithPlentyOfStuff[64 + NL_MAX_NAME_LEN * 10]; // "plenty"
				char* _a = Disassemble(addr, opcode);
				// This isn't a trace log, so we want to remove the data after the @ or =.
				// There are lots of hardcoded sprintfs in Disassemble. This really is the easiest way.
				char* traceInfoIndex = strstr(_a, "@");
				if (traceInfoIndex)
					traceInfoIndex[-1] = 0;
				traceInfoIndex = strstr(_a, "=");
				if (traceInfoIndex)
					traceInfoIndex[-1] = 0;

				strcpy(bufferForDisassemblyWithPlentyOfStuff, _a);

				if (symbDebugEnabled)
				{ // TODO: This will add in both the default name and custom name if you have inlineAddresses enabled.
					if (symbRegNames)
						replaceRegNames(bufferForDisassemblyWithPlentyOfStuff);
					replaceNames(ramBankNames, bufferForDisassemblyWithPlentyOfStuff, NULL);
					for (int p = 0; p<ARRAY_SIZE(pageNames); p++)
						if (pageNames[p] != NULL)
							replaceNames(pageNames[p], bufferForDisassemblyWithPlentyOfStuff, NULL);
				}

				uint8 opCode = GetMem(instruction_addr);

				// special case: an RTS or RTI opcode
				if (opCode == 0x60 || opCode == 0x40)
				{
					// add "----------" to emphasize the end of subroutine
					strcat(bufferForDisassemblyWithPlentyOfStuff, " ");
					for (int j = strlen(bufferForDisassemblyWithPlentyOfStuff); j < (LOG_DISASSEMBLY_MAX_LEN - 1); ++j)
						bufferForDisassemblyWithPlentyOfStuff[j] = '-';
					bufferForDisassemblyWithPlentyOfStuff[LOG_DISASSEMBLY_MAX_LEN - 1] = 0;
				}

				// append the disassembly to current line
				swprintf(debug_wbuf, L" %S", bufferForDisassemblyWithPlentyOfStuff);
				// wcscat(debug_wstr, debug_wbuf);
				fprintf(fout, "%ls", debug_wbuf);
			}
		}
		// wcscat(debug_wstr, L"\n");
		fprintf(fout, "%ls", L"\n");
		instructions_count++;
	}
}

void Disassemble(HWND hWnd, int id, int scrollid, unsigned int addr)
{
	wchar_t chr[40] = { 0 };
	wchar_t debug_wbuf[2048] = { 0 };
	int size;
	uint8 opcode[3];
	unsigned int instruction_addr;

	disassembly_addresses.resize(0);
	PCPointerWasDrawn = false;
	beginningOfPCPointerLine = -1;
	
	if (symbDebugEnabled)
	{
		loadNameFiles();
		disassembly_operands.resize(0);
	}

	si.nPos = addr;
	SetScrollInfo(GetDlgItem(hWnd,scrollid),SB_CTL,&si,TRUE);

	//figure out how many lines we can draw
	RECT rect;
	GetClientRect(GetDlgItem(hWnd, id), &rect);
	int lines = (rect.bottom-rect.top) / debugSystem->disasmFontHeight;

	debug_wstr[0] = 0;
	PCLine = -1;
	unsigned int instructions_count = 0;
	for (int i = 0; i < lines; i++)
	{
		// PC pointer
		if (addr > 0xFFFF) break;

		instruction_addr = addr;

		if (symbDebugEnabled)
		{
			// insert Name and Comment lines if needed
			Name* node = findNode(getNamesPointerForAddress(addr), addr);
			if (node)
			{
				if (node->name)
				{
					swprintf(debug_wbuf, L"%S:\n", node->name);
					wcscat(debug_wstr, debug_wbuf);
					// we added one line to the disassembly window
					disassembly_addresses.push_back(addr);
					disassembly_operands.resize(i + 1);
					i++;
				}
				if (node->comment)
				{
					// make a copy
					strcpy(debug_str_decoration_comment, node->comment);
					strcat(debug_str_decoration_comment, "\r\n");
					// divide the debug_str_decoration_comment into strings (Comment1, Comment2, ...)
					debug_decoration_comment = debug_str_decoration_comment;
					debug_decoration_comment_end_pos = strstr(debug_decoration_comment, "\r\n");
					while (debug_decoration_comment_end_pos)
					{
						debug_decoration_comment_end_pos[0] = 0;		// set \0 instead of \r
						debug_decoration_comment_end_pos[1] = 0;		// set \0 instead of \n
						swprintf(debug_wbuf, L"; %S\n", debug_decoration_comment);
						wcscat(debug_wstr, debug_wbuf);
						// we added one line to the disassembly window
						disassembly_addresses.push_back(addr);
						disassembly_operands.resize(i + 1);
						i++;

						debug_decoration_comment_end_pos += 2;
						debug_decoration_comment = debug_decoration_comment_end_pos;
						debug_decoration_comment_end_pos = strstr(debug_decoration_comment_end_pos, "\r\n");
					}
				}
			}
		}
		
		if (addr == X.PC)
		{
			PC_pointerOffset = instructions_count;
			PCPointerWasDrawn = true;
			beginningOfPCPointerLine = wcslen(debug_wstr);
			wcscat(debug_wstr, L">");
			PCLine = instructions_count;
		} else
		{
			wcscat(debug_wstr, L" ");
		}

		if (addr >= 0x8000)
		{
			if (debuggerDisplayROMoffsets && GetNesFileAddress(addr) != -1)
			{
				swprintf(chr, L" %06X: ", GetNesFileAddress(addr));
			} else
			{
				swprintf(chr, L"%02X:%04X: ", getBank(addr), addr);
			}
		} else
		{
			swprintf(chr, L"  :%04X: ", addr);
		}
		
		// Add address
		wcscat(debug_wstr, chr);
		disassembly_addresses.push_back(addr);
		if (symbDebugEnabled)
			disassembly_operands.resize(i + 1);

		size = opsize[GetMem(addr)];
		if (size == 0)
		{
			swprintf(chr, L"%02X        UNDEFINED", GetMem(addr++));
			wcscat(debug_wstr, chr);
		} else
		{
			if ((addr + size) > 0xFFFF)
			{
				while (addr < 0xFFFF)
				{
					swprintf(chr, L"%02X        OVERFLOW\n", GetMem(addr++));
					wcscat(debug_wstr, chr);
				}
				break;
			}
			for (int j = 0; j < size; j++)
			{
				swprintf(chr, L"%02X ", opcode[j] = GetMem(addr++));
				wcscat(debug_wstr, chr);
			}
			while (size < 3)
			{
				wcscat(debug_wstr, L"   "); //pad output to align ASM
				size++;
			}
			
			static char bufferForDisassemblyWithPlentyOfStuff[64+NL_MAX_NAME_LEN*10]; //"plenty"
			char* _a = Disassemble(addr, opcode);
			strcpy(bufferForDisassemblyWithPlentyOfStuff, _a);
			
			if (symbDebugEnabled)
			{ // TODO: This will add in both the default name and custom name if you have inlineAddresses enabled.
				if (symbRegNames)
				    replaceRegNames(bufferForDisassemblyWithPlentyOfStuff);
				replaceNames(ramBankNames, bufferForDisassemblyWithPlentyOfStuff, &disassembly_operands[i]);
				for(int p=0;p<ARRAY_SIZE(pageNames);p++)
					if(pageNames[p] != NULL)
						replaceNames(pageNames[p], bufferForDisassemblyWithPlentyOfStuff, &disassembly_operands[i]);
			}

            uint8 opCode = GetMem(instruction_addr);
            
			// special case: an RTS or RTI opcode
			if (opCode == 0x60 || opCode == 0x40)
			{
				// add "----------" to emphasize the end of subroutine
				strcat(bufferForDisassemblyWithPlentyOfStuff, " ");
				for (int j = strlen(bufferForDisassemblyWithPlentyOfStuff); j < (LOG_DISASSEMBLY_MAX_LEN - 1); ++j)
					bufferForDisassemblyWithPlentyOfStuff[j] = '-';
				bufferForDisassemblyWithPlentyOfStuff[LOG_DISASSEMBLY_MAX_LEN - 1] = 0;
			}

			// append the disassembly to current line
			swprintf(debug_wbuf, L" %S", bufferForDisassemblyWithPlentyOfStuff);
			wcscat(debug_wstr, debug_wbuf);
		}
		wcscat(debug_wstr, L"\n");
		instructions_count++;
	}
	UpdateDisassembleView(hWnd, id, lines, true);

	// fill the left panel data
	debug_cdl_str[0] = 0;
	if (cdloggerdataSize)
	{
		uint8 cdl_data;
		lines = disassembly_addresses.size();
		for (int i = 0; i < lines; ++i)
		{
			instruction_addr = GetNesFileAddress(disassembly_addresses[i]) - 16;
			if (instruction_addr >= 0 && instruction_addr < cdloggerdataSize)
			{
				cdl_data = cdloggerdata[instruction_addr] & 3;
				if (cdl_data == 3)
					strcat(debug_cdl_str, "cd\r\n");		// both Code and Data
				else if (cdl_data == 2)
					strcat(debug_cdl_str, " d\r\n");		// Data
				else if (cdl_data == 1)
					strcat(debug_cdl_str, "c\r\n");			// Code
				else
					strcat(debug_cdl_str, "\r\n");			// not logged
			} else
			{
				strcat(debug_cdl_str, "\r\n");				// cannot be logged
			}
		}
	}
	SetDlgItemText(hWnd, IDC_DEBUGGER_DISASSEMBLY_LEFT_PANEL, debug_cdl_str);
}
void PrintOffsetToSeekAndBookmarkFields(int offset)
{
	if (offset >= 0 && hDebug)
	{
		char offsetBuffer[5];
		sprintf(offsetBuffer, "%04X", offset);
		// send the address to "Seek To" field
		SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_PCSEEK, offsetBuffer);
		// send the address to "Bookmark Add" field
		SetDlgItemText(hDebug, IDC_DEBUGGER_BOOKMARK, offsetBuffer);
	}
}

char *DisassembleLine(int addr) {
	static char str[64]={0},chr[25]={0};
	char *c;
	int size,j;
	uint8 opcode[3];

	sprintf(str, "%02X:%04X: ", getBank(addr),addr);
	size = opsize[GetMem(addr)];
	if (size == 0)
	{
		sprintf(chr, "%02X        UNDEFINED", GetMem(addr++));
		strcat(str,chr);
	}
	else {
		if ((addr+size) > 0x10000) {
			sprintf(chr, "%02X        OVERFLOW", GetMem(addr));
			strcat(str,chr);
		}
		else {
			for (j = 0; j < size; j++) {
				sprintf(chr, "%02X ", opcode[j] = GetMem(addr++));
				strcat(str,chr);
			}
			while (size < 3) {
				strcat(str,"   "); //pad output to align ASM
				size++;
			}
			strcat(strcat(str," "),Disassemble(addr,opcode));
		}
	}
	if ((c=strchr(str,'='))) *(c-1) = 0;
	if ((c=strchr(str,'@'))) *(c-1) = 0;
	return str;
}

char *DisassembleData(int addr, uint8 *opcode) {
	static char str[64]={0},chr[25]={0};
	char *c;
	int size,j;

	sprintf(str, "%02X:%04X: ", getBank(addr), addr);
	size = opsize[opcode[0]];
	if (size == 0)
	{
		sprintf(chr, "%02X        UNDEFINED", opcode[0]);
		strcat(str,chr);
	} else
	{
		if ((addr+size) > 0x10000)
		{
			sprintf(chr, "%02X        OVERFLOW", opcode[0]);
			strcat(str,chr);
		} else
		{
			for (j = 0; j < size; j++)
			{
				sprintf(chr, "%02X ", opcode[j]);
				addr++;
				strcat(str,chr);
			}
			while (size < 3)
			{
				strcat(str,"   "); //pad output to align ASM
				size++;
			}
			strcat(strcat(str," "),Disassemble(addr,opcode));
		}
	}
	if ((c=strchr(str,'='))) *(c-1) = 0;
	if ((c=strchr(str,'@'))) *(c-1) = 0;
	return str;
}


int GetEditHex(HWND hwndDlg, int id) {
	char str[9];
	int tmp;
	GetDlgItemText(hwndDlg,id,str,9);
	tmp = strtol(str,NULL,16);
	return tmp;
}

int *GetEditHexData(HWND hwndDlg, int id){
	static int data[31];
	char str[60];
	int i,j, k;

	GetDlgItemText(hwndDlg,id,str,60);
	memset(data,0,31*sizeof(int));
	j=0;
	for(i = 0;i < 60;i++){
		if(str[i] == 0)break;
		if((str[i] >= '0') && (str[i] <= '9'))j++;
		if((str[i] >= 'A') && (str[i] <= 'F'))j++;
		if((str[i] >= 'a') && (str[i] <= 'f'))j++;
	}

	j=j&1;
	for(i = 0;i < 60;i++){
		if(str[i] == 0)break;
		k = -1;
		if((str[i] >= '0') && (str[i] <= '9'))k=str[i]-'0';
		if((str[i] >= 'A') && (str[i] <= 'F'))k=(str[i]-'A')+10;
		if((str[i] >= 'a') && (str[i] <= 'f'))k=(str[i]-'a')+10;
		if(k != -1){
			if(j&1)data[j>>1] |= k;
			else data[j>>1] |= k<<4;
			j++;
		}
	}
	data[j>>1]=-1;
	return data;
}

void UpdateRegs(HWND hwndDlg) {
	if (DebuggerWasUpdated) {
		X.A = GetEditHex(hwndDlg,IDC_DEBUGGER_VAL_A);
		X.X = GetEditHex(hwndDlg,IDC_DEBUGGER_VAL_X);
		X.Y = GetEditHex(hwndDlg,IDC_DEBUGGER_VAL_Y);
		X.PC = GetEditHex(hwndDlg,IDC_DEBUGGER_VAL_PC);
	}
}

///indicates whether we're under the control of the debugger
bool inDebugger = false;

//this code enters the debugger when a breakpoint was hit
void FCEUD_DebugBreakpoint(int bp_num)
{
	// log the Breakpoint Hit into Trace Logger log if needed
	if (logging)
	{
		log_old_emu_paused = false;		// force Trace Logger update
		if (logging_options & LOG_MESSAGES)
		{
			char str_temp[500];
			if (bp_num >= 0)
			{
				// normal breakpoint
				sprintf(str_temp, "Breakpoint %u Hit at $%04X: ", bp_num, X.PC);
				strcat(str_temp, BreakToText(bp_num));
				OutputLogLine(str_temp);
			} else if (bp_num == BREAK_TYPE_BADOP)
			{
				sprintf(str_temp, "Bad Opcode Breakpoint Hit at $%04X", X.PC);
				OutputLogLine(str_temp);
			} else if (bp_num == BREAK_TYPE_CYCLES_EXCEED)
			{
				sprintf(str_temp, "Breakpoint Hit at $%04X: cycles count %lu exceeds %lu", X.PC, (long)(timestampbase + timestamp - total_cycles_base), (long)break_cycles_limit);
				OutputLogLine(str_temp);
			} else if (bp_num == BREAK_TYPE_INSTRUCTIONS_EXCEED)
			{
				sprintf(str_temp, "Breakpoint Hit at $%04X: instructions count %lu exceeds %lu", X.PC, (long)total_instructions, (long)break_instructions_limit);
				OutputLogLine(str_temp);
			}
		}
	}

	DoDebug(0);
	UpdateOtherDebuggingDialogs(); // Keeps the debugging windows updating smoothly when stepping

	// highlight the ">" line
	if (bp_num != BREAK_TYPE_STEP)
		if (beginningOfPCPointerLine >= 0)
			SendMessage(GetDlgItem(hDebug, IDC_DEBUGGER_DISASSEMBLY), EM_SETSEL, beginningOfPCPointerLine + 1, beginningOfPCPointerLine + 8);
	
	// highlight breakpoint
	if (bp_num >= 0)
	{
		// highlight bp_num item in IDC_DEBUGGER_BP_LIST
		SendDlgItemMessage(hDebug, IDC_DEBUGGER_BP_LIST, LB_SETCURSEL, (WPARAM)bp_num, 0);
		EnableWindow(GetDlgItem(hDebug, IDC_DEBUGGER_BP_DEL), TRUE);
		EnableWindow(GetDlgItem(hDebug, IDC_DEBUGGER_BP_EDIT), TRUE);
	} else
	{
		// remove any selection from IDC_DEBUGGER_BP_LIST
		SendDlgItemMessage(hDebug, IDC_DEBUGGER_BP_LIST, LB_SETCURSEL, (WPARAM)(-1), 0);
		EnableWindow(GetDlgItem(hDebug, IDC_DEBUGGER_BP_DEL), FALSE);
		EnableWindow(GetDlgItem(hDebug, IDC_DEBUGGER_BP_EDIT), FALSE);
		// highlight IDC_DEBUGGER_VAL_CYCLES_COUNT or IDC_DEBUGGER_VAL_INSTRUCTIONS_COUNT if needed
		if (bp_num == BREAK_TYPE_CYCLES_EXCEED)
			SendMessage(GetDlgItem(hDebug, IDC_DEBUGGER_VAL_CYCLES_COUNT), EM_SETSEL, 0, -1);
		else if (bp_num == BREAK_TYPE_INSTRUCTIONS_EXCEED)
			SendMessage(GetDlgItem(hDebug, IDC_DEBUGGER_VAL_INSTRUCTIONS_COUNT), EM_SETSEL, 0, -1);
	}

	void win_debuggerLoop(); // HACK to let user interact with the Debugger while emulator isn't updating
	win_debuggerLoop();

	// since we unfroze emulation, reset delta_cycles counter
	ResetDebugStatisticsDeltaCounters();
}

void UpdateBreakpointsCaption()
{
	char str[32];
	// calculate the number of enabled breakpoints
	int tmp = 0;
	for (int i = 0; i < numWPs; i++)
		if (watchpoint[i].flags & WP_E)
			tmp++;
	sprintf(str, "Breakpoints %02X of %02X", tmp, numWPs);
	SetDlgItemText(hDebug, IDC_DEBUGGER_BREAKPOINTS, str);
}

void UpdateDebugger(bool jump_to_pc)
{
	//don't do anything if the debugger is not visible
	if(!hDebug)
		return;

	//but if the debugger IS visible, then focus it
	ShowWindow(hDebug, SW_SHOWNORMAL);
	SetForegroundWindow(hDebug);

	char str[512] = {0}, str2[512] = {0}, chr[8];
	int tmp, ret, i, starting_address;

	if (jump_to_pc || disassembly_addresses.size() == 0)
	{
		starting_address = X.PC;

		// ensure that PC pointer will be visible even after the window was resized
		RECT rect;
		GetClientRect(GetDlgItem(hDebug, IDC_DEBUGGER_DISASSEMBLY), &rect);
		unsigned int lines = (rect.bottom-rect.top) / debugSystem->disasmFontHeight;
		if (PC_pointerOffset >= lines)
			PC_pointerOffset = 0;

		// keep the relative position of the ">" pointer inside the Disassembly window
		for (int i = PC_pointerOffset; i > 0; i--)
		{
			starting_address = InstructionUp(starting_address);
		}
		Disassemble(hDebug, IDC_DEBUGGER_DISASSEMBLY, IDC_DEBUGGER_DISASSEMBLY_VSCR, starting_address);

		// HACK, but I don't see any other way to ensure the ">" pointer is visible when "Symbolic debug" is enabled
		if (!PCPointerWasDrawn && PC_pointerOffset)
		{
			// we've got a problem, probably due to Symbolic info taking so much space that PC pointer couldn't be seen with (PC_pointerOffset > 0)
			PC_pointerOffset = 0;
			starting_address = X.PC;
			// retry with (PC_pointerOffset = 0) now
			Disassemble(hDebug, IDC_DEBUGGER_DISASSEMBLY, IDC_DEBUGGER_DISASSEMBLY_VSCR, starting_address);
		}

		starting_address = X.PC;
	} else
	{
		starting_address = disassembly_addresses[0];
		Disassemble(hDebug, IDC_DEBUGGER_DISASSEMBLY, IDC_DEBUGGER_DISASSEMBLY_VSCR, starting_address);
	}
	

	// "Address Bookmark Add" follows the address

	// Update register values
	sprintf(str, "%02X", X.A);
	SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_A, str);
	sprintf(str, "%02X", X.X);
	SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_X, str);
	sprintf(str, "%02X", X.Y);
	SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_Y, str);
	sprintf(str, "%04X", (int)X.PC);
	SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_PC, str);

	sprintf(str, "%04X", (int)FCEUPPU_PeekAddress());
	SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_PPU, str);
	sprintf(str, "%02X", PPU[3]);
	SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_SPR, str);

	extern int linestartts;
	#define GETLASTPIXEL    (PAL?((timestamp*48-linestartts)/15) : ((timestamp*48-linestartts)/16) )
	
	int ppupixel = GETLASTPIXEL;

	if (ppupixel>341)	//maximum number of pixels per scanline
		ppupixel = 0;	//Currently pixel display is borked until Run 128 lines is clicked, this keeps garbage from displaying

	// If not in the 0-239 pixel range, make special cases for display
	if (scanline == 240 && vblankScanLines < (PAL?72:22))
	{
		if (!vblankScanLines)
		{
			// Idle scanline (240)
			sprintf(str, "%d", scanline);	// was "Idle %d"
		} else if (scanline + vblankScanLines == (PAL?311:261))
		{
			// Pre-render
			sprintf(str, "-1");	// was "Prerender -1"
		} else
		{
			// Vblank lines (241-260/310)
			sprintf(str, "%d", scanline + vblankScanLines);	// was "Vblank %d"
		}
		sprintf(str2, "%d", vblankPixel);
	} else
	{
		// Scanlines 0 - 239
		sprintf(str, "%d", scanline);
		sprintf(str2, "%d", ppupixel);
	}

	if(newppu)
	{
		sprintf(str,"%d",newppu_get_scanline());
		sprintf(str2,"%d",newppu_get_dot());
	}

	SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_SLINE, str);
	SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_PPUPIXEL, str2);

	// update counters
	int64 counter_value = timestampbase + (uint64)timestamp - total_cycles_base;
	if (counter_value < 0)	// sanity check
	{
		ResetDebugStatisticsCounters();
		counter_value = 0;
	}
	sprintf(str, "%llu", counter_value);
	SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_CYCLES_COUNT, str);
	counter_value = timestampbase + (uint64)timestamp - delta_cycles_base;
	if (counter_value < 0)	// sanity check
	{
		ResetDebugStatisticsCounters();
		counter_value = 0;
	}
	sprintf(str, "(+%llu)", counter_value);
	SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_CYCLES_COUNT2, str);
	sprintf(str, "%llu", total_instructions);
	SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_INSTRUCTIONS_COUNT, str);
	sprintf(str, "(+%llu)", delta_instructions);
	SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_INSTRUCTIONS_COUNT2, str);

	UpdateBreakpointsCaption();

	// Draw the stack
	tmp = X.S|0x0100;
	sprintf(str, "Stack $%04X", tmp);
	SetDlgItemText(hDebug, IDC_DEBUGGER_VAL_S, str);
	str[0] = 0;
	tmp++;
	if (tmp <= 0x1FF)
	{
		sprintf(str, "%02X", GetMem(tmp));
		for (i = 1; i < 128; i++)
		{
			tmp++;
			if (tmp > 0x1FF)
				break;
			if ((i & 3) == 0)
				sprintf(chr, ",\r\n%02X", GetMem(tmp));
			else
				sprintf(chr, ",%02X", GetMem(tmp));
			strcat(str, chr);
		}
	}
	SetDlgItemText(hDebug, IDC_DEBUGGER_STACK_CONTENTS, str);

	GetDlgItemText(hDebug,IDC_DEBUGGER_VAL_PCSEEK,str,5);
	if (((ret = sscanf(str,"%4X",&tmp)) == EOF) || (ret != 1)) tmp = 0;
	sprintf(str,"%04X",tmp);
	SetDlgItemText(hDebug,IDC_DEBUGGER_VAL_PCSEEK,str);

	CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_N, BST_UNCHECKED);
	CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_V, BST_UNCHECKED);
	CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_U, BST_UNCHECKED);
	CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_B, BST_UNCHECKED);
	CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_D, BST_UNCHECKED);
	CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_I, BST_UNCHECKED);
	CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_Z, BST_UNCHECKED);
	CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_C, BST_UNCHECKED);

	tmp = X.P;
	if (tmp & N_FLAG) CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_N, BST_CHECKED);
	if (tmp & V_FLAG) CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_V, BST_CHECKED);
	if (tmp & U_FLAG) CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_U, BST_CHECKED);
	if (tmp & B_FLAG) CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_B, BST_CHECKED);
	if (tmp & D_FLAG) CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_D, BST_CHECKED);
	if (tmp & I_FLAG) CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_I, BST_CHECKED);
	if (tmp & Z_FLAG) CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_Z, BST_CHECKED);
	if (tmp & C_FLAG) CheckDlgButton(hDebug, IDC_DEBUGGER_FLAG_C, BST_CHECKED);

	DebuggerWasUpdated = true;
}

char* BreakToText(unsigned int num)
{
	static char str[300], chr[8];

	sprintf(str, "$%04X", watchpoint[num].address);
	if (watchpoint[num].endaddress) {
		sprintf(chr, "-%04X", watchpoint[num].endaddress);
		strcat(str,chr);
	}
	if (watchpoint[num].flags&WP_E) strcat(str,":E"); else strcat(str,":-");
	if (watchpoint[num].flags&BT_P) strcat(str,"P"); else if (watchpoint[num].flags&BT_S) strcat(str,"S"); else strcat(str,"C");
	if (watchpoint[num].flags&WP_R) strcat(str,"R"); else strcat(str,"-");
	if (watchpoint[num].flags&WP_W) strcat(str,"W"); else strcat(str,"-");
	if (watchpoint[num].flags&WP_X) strcat(str,"X"); else strcat(str,"-");
	if (watchpoint[num].flags&WP_F) strcat(str,"F"); else strcat(str,"-");
	
// ################################## Start of SP CODE ###########################

	if (watchpoint[num].desc && strlen(watchpoint[num].desc))
	{
		strcat(str, " ");
		strcat(str, watchpoint[num].desc);
		strcat(str, " ");
	}

	if (watchpoint[num].condText && strlen(watchpoint[num].condText))
	{
		strcat(str, " Condition:");
		strcat(str, watchpoint[num].condText);
	}
// ################################## End of SP CODE ###########################

	return str;
}

void AddBreakList() {
	SendDlgItemMessage(hDebug,IDC_DEBUGGER_BP_LIST,LB_INSERTSTRING,-1,(LPARAM)(LPSTR)BreakToText(numWPs-1));
}

void EditBreakList() {
	if(WP_edit < 0) return;
	if(WP_edit >= numWPs) return;
	SendDlgItemMessage(hDebug,IDC_DEBUGGER_BP_LIST,LB_DELETESTRING,WP_edit,0);
	SendDlgItemMessage(hDebug,IDC_DEBUGGER_BP_LIST,LB_INSERTSTRING,WP_edit,(LPARAM)(LPSTR)BreakToText(WP_edit));
	SendDlgItemMessage(hDebug,IDC_DEBUGGER_BP_LIST,LB_SETCURSEL,WP_edit,0);
}

void FillBreakList(HWND hwndDlg)
{
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_BP_LIST,LB_RESETCONTENT,0,0);
	for (int i = 0; i < numWPs; i++)
		SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_BP_LIST,LB_INSERTSTRING,-1,(LPARAM)(LPSTR)BreakToText(i));
}

void EnableBreak(int sel)
{
	if(sel<0) return;
	if(sel>=numWPs) return;
	watchpoint[sel].flags^=WP_E;
	SendDlgItemMessage(hDebug,IDC_DEBUGGER_BP_LIST,LB_DELETESTRING,sel,0);
	SendDlgItemMessage(hDebug,IDC_DEBUGGER_BP_LIST,LB_INSERTSTRING,sel,(LPARAM)(LPSTR)BreakToText(sel));
	SendDlgItemMessage(hDebug,IDC_DEBUGGER_BP_LIST,LB_SETCURSEL,sel,0);
	UpdateBreakpointsCaption();
}

void DeleteBreak(int sel)
{
	if(sel<0) return;
	if(sel>=numWPs) return;
	if (watchpoint[sel].cond)
		freeTree(watchpoint[sel].cond);
	if (watchpoint[sel].condText)
		free(watchpoint[sel].condText);
	if (watchpoint[sel].desc)
		free(watchpoint[sel].desc);
	// move all BP items up in the list
	for (int i = sel; i < numWPs; i++) {
		watchpoint[i].address = watchpoint[i+1].address;
		watchpoint[i].endaddress = watchpoint[i+1].endaddress;
		watchpoint[i].flags = watchpoint[i+1].flags;
// ################################## Start of SP CODE ###########################
		watchpoint[i].cond = watchpoint[i+1].cond;
		watchpoint[i].condText = watchpoint[i+1].condText;
		watchpoint[i].desc = watchpoint[i+1].desc;
// ################################## End of SP CODE ###########################
	}
	// erase last BP item
	watchpoint[numWPs].address = 0;
	watchpoint[numWPs].endaddress = 0;
	watchpoint[numWPs].flags = 0;
	watchpoint[numWPs].cond = 0;
	watchpoint[numWPs].condText = 0;
	watchpoint[numWPs].desc = 0;
	numWPs--;
// ################################## Start of SP CODE ###########################
	myNumWPs--;
// ################################## End of SP CODE ###########################
	SendDlgItemMessage(hDebug,IDC_DEBUGGER_BP_LIST,LB_DELETESTRING,sel,0);
	// select next item in the list
	if (numWPs)
	{
		if (sel >= (numWPs - 1))
			// select last item
			SendDlgItemMessage(hDebug, IDC_DEBUGGER_BP_LIST, LB_SETCURSEL, numWPs - 1, 0);
		else
			SendDlgItemMessage(hDebug, IDC_DEBUGGER_BP_LIST, LB_SETCURSEL, sel, 0);
	} else
	{
		EnableWindow(GetDlgItem(hDebug,IDC_DEBUGGER_BP_DEL),FALSE);
		EnableWindow(GetDlgItem(hDebug,IDC_DEBUGGER_BP_EDIT),FALSE);
	}
	UpdateBreakpointsCaption();
}

void KillDebugger() {
	if (hDebug)
	{
		SendDlgItemMessage(hDebug,IDC_DEBUGGER_BP_LIST,LB_RESETCONTENT,0,0);
		DebuggerExit();
	}
	FCEUI_Debugger().reset();
	FCEUI_SetEmulationPaused(0);
}


int AddAsmHistory(HWND hwndDlg, int id, char *str) {
	int index;
	index = SendDlgItemMessage(hwndDlg,id,CB_FINDSTRINGEXACT,-1,(LPARAM)(LPSTR)str);
	if (index == CB_ERR) {
		SendDlgItemMessage(hwndDlg,id,CB_INSERTSTRING,-1,(LPARAM)(LPSTR)str);
		return 0;
	}
	return 1;
}

INT_PTR CALLBACK AssemblerCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	int romaddr,count,i,j;
	char str[128],*dasm;
	static int patchlen,applied,saved,lastundo;
	static uint8 patchdata[64][3],undodata[64*3];
	uint8 *ptr;

	switch(uMsg) {
		case WM_INITDIALOG:
			CenterWindow(hwndDlg);

			//set font
			SendDlgItemMessage(hwndDlg,IDC_ASSEMBLER_DISASSEMBLY,WM_SETFONT,(WPARAM)debugSystem->hFixedFont,FALSE);
			SendDlgItemMessage(hwndDlg,IDC_ASSEMBLER_PATCH_DISASM,WM_SETFONT,(WPARAM)debugSystem->hFixedFont,FALSE);

			//set limits
			SendDlgItemMessage(hwndDlg,IDC_ASSEMBLER_HISTORY,CB_LIMITTEXT,20,0);

			SetDlgItemText(hwndDlg,IDC_ASSEMBLER_DISASSEMBLY,DisassembleLine(iaPC));
			SetFocus(GetDlgItem(hwndDlg,IDC_ASSEMBLER_HISTORY));

			patchlen = 0;
			applied = 0;
			saved = 0;
			lastundo = 0;
			break;
		case WM_CLOSE:
		case WM_QUIT:
			EndDialog(hwndDlg,0);
			break;
		case WM_COMMAND:
		{
			switch (HIWORD(wParam))
			{
				case BN_CLICKED:
				{
					switch (LOWORD(wParam))
					{
						case IDC_ASSEMBLER_APPLY:
							if (patchlen) {
								ptr = GetNesPRGPointer(GetNesFileAddress(iaPC)-16);
								count = 0;
								for (i = 0; i < patchlen; i++) {
									for (j = 0; j < opsize[patchdata[i][0]]; j++) {
										if (count == lastundo) undodata[lastundo++] = ptr[count];
										ptr[count++] = patchdata[i][j];
									}
								}
								SetWindowText(hwndDlg, "Inline Assembler  *Patches Applied*");
								applied = 1;
							}
							break;
						case IDC_ASSEMBLER_SAVE:
							if (applied) {
								count = romaddr = GetNesFileAddress(iaPC);
								for (i = 0; i < patchlen; i++)
								{
									count += opsize[patchdata[i][0]];
								}
								if (patchlen) sprintf(str,"Write patch data to file at addresses 0x%06X - 0x%06X?",romaddr,count-1);
								else sprintf(str,"Undo all previously applied patches?");
								if (MessageBox(hwndDlg, str, "Save changes to file?", MB_YESNO|MB_ICONINFORMATION) == IDYES) {
									if (iNesSave()) {
										saved = 1;
										applied = 0;
									}
									else MessageBox(hwndDlg, "Unable to save changes to file", "Error saving to file", MB_OK | MB_ICONERROR);
								}
							}
							break;
						case IDC_ASSEMBLER_UNDO:
							if ((count = SendDlgItemMessage(hwndDlg,IDC_ASSEMBLER_PATCH_DISASM,LB_GETCOUNT,0,0))) {
								SendDlgItemMessage(hwndDlg,IDC_ASSEMBLER_PATCH_DISASM,LB_DELETESTRING,count-1,0);
								patchlen--;
								count = 0;
								for (i = 0; i < patchlen; i++)
								{
									count += opsize[patchdata[i][0]];
								}
								if (count < lastundo) {
									ptr = GetNesPRGPointer(GetNesFileAddress(iaPC)-16);
									j = opsize[patchdata[patchlen][0]];
									for (i = count; i < (count+j); i++) {
										ptr[i] = undodata[i];
									}
									lastundo -= j;
									applied = 1;
								}
								SetDlgItemText(hwndDlg,IDC_ASSEMBLER_DISASSEMBLY,DisassembleLine(iaPC+count));
							}
							break;
						case IDC_ASSEMBLER_DEFPUSHBUTTON:
							count = 0;
							for (i = 0; i < patchlen; i++)
							{
								count += opsize[patchdata[i][0]];
							}
							GetDlgItemText(hwndDlg,IDC_ASSEMBLER_HISTORY,str,21);
							if (!Assemble(patchdata[patchlen],(iaPC+count),str)) {
								count = iaPC;
								for (i = 0; i <= patchlen; i++)
								{
									count += opsize[patchdata[i][0]];
								}
								if (count > 0x10000) { //note: don't use 0xFFFF!
									MessageBox(hwndDlg, "Patch data cannot exceed address 0xFFFF", "Address error", MB_OK | MB_ICONERROR);
									break;
								}
								SetDlgItemText(hwndDlg,IDC_ASSEMBLER_HISTORY,"");
								if (count < 0x10000) SetDlgItemText(hwndDlg,IDC_ASSEMBLER_DISASSEMBLY,DisassembleLine(count));
								else SetDlgItemText(hwndDlg,IDC_ASSEMBLER_DISASSEMBLY,"OVERFLOW");
								dasm = DisassembleData((count-opsize[patchdata[patchlen][0]]),patchdata[patchlen]);
								SendDlgItemMessage(hwndDlg,IDC_ASSEMBLER_PATCH_DISASM,LB_INSERTSTRING,-1,(LPARAM)(LPSTR)dasm);
								AddAsmHistory(hwndDlg,IDC_ASSEMBLER_HISTORY,dasm+16);
								SetWindowText(hwndDlg, "Inline Assembler");
								patchlen++;
							}
							else { //ERROR!
								SetWindowText(hwndDlg, "Inline Assembler  *Syntax Error*");
								MessageBeep(MB_ICONEXCLAMATION);
							}
							break;
					}
					SetFocus(GetDlgItem(hwndDlg,IDC_ASSEMBLER_HISTORY)); //set focus to combo box after anything is pressed!
					break;
				}
			}
			break;
		}
	}
	return FALSE;
}

INT_PTR CALLBACK PatcherCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	char str[64];
	uint8 *c;
	int i;
	int *p;

	switch(uMsg) {
		case WM_INITDIALOG:
			CenterWindow(hwndDlg);

			//set limits
			SendDlgItemMessage(hwndDlg,IDC_ROMPATCHER_OFFSET,EM_SETLIMITTEXT,6,0);
			SendDlgItemMessage(hwndDlg,IDC_ROMPATCHER_PATCH_DATA,EM_SETLIMITTEXT,30,0);
			UpdatePatcher(hwndDlg);

			if(iapoffset != -1){
				CheckDlgButton(hwndDlg, IDC_ROMPATCHER_DOTNES_OFFSET, BST_CHECKED);
				sprintf((char*)str,"%X",iapoffset);
				SetDlgItemText(hwndDlg,IDC_ROMPATCHER_OFFSET,str);
			}

			SetFocus(GetDlgItem(hwndDlg,IDC_ROMPATCHER_OFFSET_BOX));
			break;
		case WM_CLOSE:
		case WM_QUIT:
			EndDialog(hwndDlg,0);
			break;
		case WM_COMMAND:
			switch(HIWORD(wParam)) {
				case BN_CLICKED:
					switch(LOWORD(wParam)) {
						case IDC_ROMPATCHER_BTN_EDIT: //todo: maybe get rid of this button and cause iapoffset to update every time you change the text
							if(IsDlgButtonChecked(hwndDlg,IDC_ROMPATCHER_DOTNES_OFFSET) == BST_CHECKED)
								iapoffset = GetEditHex(hwndDlg,IDC_ROMPATCHER_OFFSET);
							else
								iapoffset = GetNesFileAddress(GetEditHex(hwndDlg,IDC_ROMPATCHER_OFFSET));
							if((iapoffset < 16) && (iapoffset != -1)){
								MessageBox(hDebug, "Sorry, iNES Header editing isn't supported by this tool. If you want to edit the header, please use iNES Header Editor", "Error", MB_OK | MB_ICONASTERISK);
								iapoffset = -1;
							}
							if((iapoffset > PRGsize[0]) && (iapoffset != -1)){
								MessageBox(hDebug, "Error: .Nes offset outside of PRG rom", "Error", MB_OK | MB_ICONERROR);
								iapoffset = -1;
							}
							UpdatePatcher(hwndDlg);
							break;
						case IDC_ROMPATCHER_BTN_APPLY:
							p = GetEditHexData(hwndDlg,IDC_ROMPATCHER_PATCH_DATA);
							i=0;
							c = GetNesPRGPointer(iapoffset-16);
							while(p[i] != -1){
								c[i] = p[i];
								i++;
							}
							UpdatePatcher(hwndDlg);
							break;
						case IDC_ROMPATCHER_BTN_SAVE:
							if (!iNesSave())
								MessageBox(NULL, "Error Saving", "Error", MB_OK | MB_ICONERROR);
							break;
					}
					break;
			}
			break;
	}
	return FALSE;
}

extern char *iNesShortFName();

void DebuggerExit()
{
	debugger_open = 0;
	inDebugger = false;
	// in case someone calls it multiple times
	if (hDebug)
	{
		// release bitmap icons
		for (int i = 0; i < sizeof(dbgcolormenu) / sizeof(DBGCOLORMENU); ++i)
		{
			DeleteObject(dbgcolormenu[i].menu.bitmap);
			dbgcolormenu[i].menu.bitmap = NULL;
		}
		// Destroy menu
		DestroyMenu(hDebugcontext);
		DestroyMenu(hDisasmcontext);
		// Destroy debug window
		DestroyWindow(hDebug);
		hDebug = NULL;
		free(debug_wstr);
		free(debug_cdl_str);
		free(debug_str_decoration_comment);
	}
}

static RECT currDebuggerRect;
static RECT newDebuggerRect;

//moves all child items in the dialog when you resize (except for the dock fill controls which are resized)
BOOL CALLBACK DebuggerEnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	POINT* p = (POINT*)lParam;
	int dx = p[0].x;
	int dy_l = p[0].y;
	int dy_r = p[1].y;

	RECT crect;
	HWND parent = GetParent(hwnd);
	GetWindowRect(hwnd, &crect);					//Get rect of current child to be resized
	ScreenToClient(parent, (LPPOINT)&crect);		//Convert rect coordinates to client area coordinates
	ScreenToClient(parent, ((LPPOINT)&crect) + 1);

	switch (GetDlgCtrlID(hwnd))
	{
		case IDC_DEBUGGER_DISASSEMBLY:
			// horizontal and vertical stretch
			crect.right += dx;
			crect.bottom += dy_l;
			SetWindowPos(hwnd, 0, 0, 0, crect.right - crect.left, crect.bottom - crect.top, SWP_NOZORDER | SWP_NOMOVE);
			GetScrollInfo(GetDlgItem(parent, IDC_DEBUGGER_DISASSEMBLY_VSCR), SB_CTL, &si);
			Disassemble(parent, IDC_DEBUGGER_DISASSEMBLY, IDC_DEBUGGER_DISASSEMBLY_VSCR, si.nPos);
			break;
		case IDC_DEBUGGER_DISASSEMBLY_LEFT_PANEL:
			// vertical stretch, no movement
			crect.bottom += dy_l;
			SetWindowPos(hwnd, 0, 0, 0, crect.right - crect.left, crect.bottom - crect.top, SWP_NOZORDER | SWP_NOMOVE);
			break;
		case IDC_DEBUGGER_VAL_S:
		case IDC_DEBUGGER_STACK_CONTENTS:
		case IDC_DEBUGGER_BP_LIST:
		case IDC_DEBUGGER_BREAKPOINTS:
			// vertical stretch half length, move left and right full length, stick top
			crect.bottom += dy_r / 2;
			crect.left += dx;
			crect.right += dx;
			SetWindowPos(hwnd, 0, crect.left, crect.top, crect.right - crect.left, crect.bottom - crect.top, SWP_NOZORDER);
			break;
		case LIST_DEBUGGER_BOOKMARKS:
		case IDC_DEBUGGER_BOOKMARKS:
			// vertical strectch half length, move left and right full length, stick bottom
			crect.bottom += dy_r;
			crect.top += dy_r / 2;
			crect.left += dx;
			crect.right += dx;
			SetWindowPos(hwnd, 0, crect.left, crect.top, crect.right - crect.left, crect.bottom - crect.top, SWP_NOZORDER);
			break;
		case IDC_DEBUGGER_RUN:
		case IDC_DEBUGGER_STEP_IN:
		case IDC_DEBUGGER_STEP_OUT:
		case IDC_DEBUGGER_STEP_OVER:
		case IDC_DEBUGGER_RUN_LINE:
		case IDC_DEBUGGER_RUN_FRAME2:
		case IDC_DEBUGGER_SEEK_TO:
		case IDC_DEBUGGER_VAL_PCSEEK:
		case IDC_DEBUGGER_TEXT_PC:
		case IDC_DEBUGGER_VAL_PC:
		case IDC_DEBUGGER_SEEK_PC:
		case IDC_DEBUGGER_TEXT_A:
		case IDC_DEBUGGER_VAL_A:
		case IDC_DEBUGGER_TEXT_X:
		case IDC_DEBUGGER_VAL_X:
		case IDC_DEBUGGER_TEXT_Y:
		case IDC_DEBUGGER_VAL_Y:
			// no stretch, move left and right full length
			crect.left += dx;
			SetWindowPos(hwnd, 0, crect.left, crect.top, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
			break;
		case IDC_DEBUGGER_BP_ADD:
		case IDC_DEBUGGER_BP_DEL:
		case IDC_DEBUGGER_BP_EDIT:
		case IDC_DEBUGGER_BREAK_ON_BAD_OP:
		case IDC_DEBUGGER_STATUSFLAGS:
		case IDC_DEBUGGER_FLAG_N:
		case IDC_DEBUGGER_FLAG_V:
		case IDC_DEBUGGER_FLAG_U:
		case IDC_DEBUGGER_FLAG_B:
		case IDC_DEBUGGER_FLAG_D:
		case IDC_DEBUGGER_FLAG_I:
		case IDC_DEBUGGER_FLAG_Z:
		case IDC_DEBUGGER_FLAG_C:
		case IDC_DEBUGGER_VAL_S2:
		case IDC_DEBUGGER_TEXT_PPU:
		case IDC_DEBUGGER_VAL_PPU:
		case IDC_DEBUGGER_TEXT_SPR:
		case IDC_DEBUGGER_VAL_SPR:
		case IDC_DEBUGGER_TEXT_SLINE:
		case IDC_DEBUGGER_VAL_SLINE:
		case IDC_DEBUGGER_TEXT_PPUPIXEL:
		case IDC_DEBUGGER_VAL_PPUPIXEL:
		case IDC_DEBUGGER_TEXT_CYCLES_COUNT:
		case IDC_DEBUGGER_VAL_CYCLES_COUNT:
		case IDC_DEBUGGER_VAL_CYCLES_COUNT2:
		case IDC_DEBUGGER_BREAK_ON_CYCLES:
		case IDC_DEBUGGER_CYCLES_EXCEED:
		case IDC_DEBUGGER_BOOKMARK:
		case IDC_DEBUGGER_BOOKMARK_ADD:
		case IDC_DEBUGGER_BOOKMARK_DEL:
		case IDC_DEBUGGER_BOOKMARK_EDIT:
		case IDC_DEBUGGER_TEXT_INSTRUCTIONS_COUNT:
		case IDC_DEBUGGER_VAL_INSTRUCTIONS_COUNT:
		case IDC_DEBUGGER_VAL_INSTRUCTIONS_COUNT2:
		case IDC_DEBUGGER_BREAK_ON_INSTRUCTIONS:
		case IDC_DEBUGGER_INSTRUCTIONS_EXCEED:
		case IDC_DEBUGGER_RESET_COUNTERS:
			// no stretch, move up and down half length, move left and right full length
			crect.top += dy_r / 2;
			crect.left += dx;
			SetWindowPos(hwnd, 0, crect.left, crect.top, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
			break;
		case IDC_DEBUGGER_VAL_S3:
		case IDC_DEBUGGER_ROM_OFFSETS:
		case IDC_DEBUGGER_ENABLE_SYMBOLIC:
		case IDC_DEBUGGER_PREDEFINED_REGS:
		case IDC_DEBUGGER_RELOAD_SYMS:
		case IDC_DEBUGGER_ROM_PATCHER:
		case DEBUGAUTOLOAD:
		case DEBUGLOADDEB:
		case DEBUGIDAFONT:
			// no stretch, move up and down full length, move left and right full length
			crect.top += dy_r;
			crect.left += dx;
			SetWindowPos(hwnd, 0, crect.left, crect.top, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
			break;
		case IDC_DEBUGGER_ADDR_LINE:
			// horizontal stretch, move up and down full length 
			crect.top += dy_l;
			crect.bottom += dy_r;
			crect.right += dx;
			SetWindowPos(hwnd, 0, crect.left, crect.top, crect.right - crect.left, crect.bottom - crect.top, SWP_NOZORDER);
			break;
		case IDC_DEBUGGER_DISASSEMBLY_VSCR:
			// vertical stretch,  move left and right full length
			crect.bottom += dy_l;
			crect.left += dx;
			crect.right += dx;
			SetWindowPos(hwnd, 0, crect.left, crect.top, crect.right - crect.left, crect.bottom - crect.top, SWP_NOZORDER);
	}

	return TRUE;

}

void LoadGameDebuggerData(HWND hwndDlg = hDebug)
{
	if (!hwndDlg)
		return;

	numWPs = myNumWPs;
	FillDebuggerBookmarkListbox(hwndDlg);
	FillBreakList(hwndDlg);
}

// returns the address, or EOF if selection cursor points to something else
int Debugger_CheckClickingOnAnAddressOrSymbolicName(unsigned int lineNumber, bool onlyCheckWhenNothingSelected)
{
	// debug_str contains the text in the disassembly window
	int sel_start = 0, sel_end = 0;
	SendDlgItemMessage(hDebug, IDC_DEBUGGER_DISASSEMBLY, EM_GETSEL, (WPARAM)&sel_start, (LPARAM)&sel_end);
	if (onlyCheckWhenNothingSelected)
		if (sel_end > sel_start)
			return EOF;

	// find the ":" or "$" before sel_start
	int i = sel_start - 1;
	for (; i > sel_start - 6; i--)
		if ((i >= 0 && debug_wstr[i] == L':' || debug_wstr[i] == L'$') && debug_wstr[i+3] != L'\n')
			break;
	if (i > sel_start - 6)
	{
		wchar_t offsetBuffer[5];
		wcsncpy(offsetBuffer, debug_wstr + i + 1, 4);
		offsetBuffer[4] = 0;
		// invalidate the string if a space or \r is found in it
		wchar_t* firstspace = wcsstr(offsetBuffer, L" ");
		if (!firstspace)
			firstspace = wcsstr(offsetBuffer, L"\r");
		if (!firstspace)
		{
			unsigned int offset;
			int numend;
			if (swscanf(offsetBuffer, L"%4X", &offset) != EOF)
			{
				if (debug_wstr[i + 3] == L',' || debug_wstr[i+3] == L')')
					numend = 3;
				else
					numend = 5;
				// select the text
				SendDlgItemMessage(hDebug, IDC_DEBUGGER_DISASSEMBLY, EM_SETSEL, (WPARAM)(i + 1), (LPARAM)(i + numend));
				PrintOffsetToSeekAndBookmarkFields(offset);
				return (int)offset;
			}
		}
	}
	
	if (symbDebugEnabled && lineNumber < disassembly_addresses.size())
	{
		uint16 addr;
		Name* node;
		wchar_t* name;
		int nameLen;
		wchar_t* start_pos;
		wchar_t* pos;
	
		// first, try finding the name of disassembly_addresses[lineNumber]
		addr = disassembly_addresses[lineNumber];
		node = findNode(getNamesPointerForAddress(addr), addr);
		if (node && node->name && *(node->name))
		{
			nameLen = MultiByteToWideChar(CP_ACP, 0, node->name, -1, 0, 0);
			name = (wchar_t*)malloc(nameLen * sizeof(wchar_t));
			MultiByteToWideChar(CP_ACP, 0, node->name, -1, name, nameLen);
			if (sel_start - nameLen <= 0)
				start_pos = debug_wstr;
			else
				start_pos = debug_wstr + (sel_start - nameLen);
			pos = wcsstr(start_pos, name);
			free(name);
			if (pos && pos <= debug_wstr + sel_start)
			{
				// clicked on the Name
				// select the text
				SendDlgItemMessage(hDebug, IDC_DEBUGGER_DISASSEMBLY, EM_SETSEL, (WPARAM)(int)(pos - debug_wstr), (LPARAM)((int)(pos - debug_wstr) + nameLen));
				PrintOffsetToSeekAndBookmarkFields(addr);
				return (int)addr;
			}
		}

		// then, try finding the name of disassembly_operands
		for (i = disassembly_operands[lineNumber].size() - 1; i >= 0; i--)
		{
			addr = disassembly_operands[lineNumber][i];
			node = findNode(getNamesPointerForAddress(addr), addr);
			if (node && node->name && *(node->name))
			{
				nameLen = MultiByteToWideChar(CP_ACP, 0, node->name, -1, 0, 0);
				name = (wchar_t*)malloc(nameLen * sizeof(wchar_t));
				MultiByteToWideChar(CP_ACP, 0, node->name, -1, name, nameLen);
				if (sel_start - nameLen <= 0)
					start_pos = debug_wstr;
				else
					start_pos = debug_wstr + (sel_start - nameLen);
				pos = wcsstr(start_pos, name);
				free(name);
				if (pos && pos <= debug_wstr + sel_start)
				{
					// clicked on the operand name
					// select the text
					SendDlgItemMessage(hDebug, IDC_DEBUGGER_DISASSEMBLY, EM_SETSEL, (WPARAM)(int)(pos - debug_wstr), (LPARAM)((int)(pos - debug_wstr) + nameLen));
					PrintOffsetToSeekAndBookmarkFields(addr);
					return (int)addr;
				}
			}
		}
	}
	
	return EOF;
}

BOOL CALLBACK IDC_DEBUGGER_DISASSEMBLY_WndProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
		case WM_LBUTTONDBLCLK:
		{
			int offset = Debugger_CheckClickingOnAnAddressOrSymbolicName(GET_Y_LPARAM(lParam) / debugSystem->disasmFontHeight, false);
			if (offset != EOF)
			{
				// bring "Add Breakpoint" dialog
				childwnd = 1;
				if (DialogBoxParam(fceu_hInstance, "ADDBP", hwndDlg, AddbpCallB, offset))
					AddBreakList();
				childwnd = 0;
				UpdateDebugger(false);
			}
			return 0;
		}
		case WM_LBUTTONUP:
		{
			Debugger_CheckClickingOnAnAddressOrSymbolicName(GET_Y_LPARAM(lParam) / debugSystem->disasmFontHeight, true);
			break;
		}
		case WM_RBUTTONDOWN:
		{
			// if nothing is selected, simulate Left-click
			int sel_start, sel_end;
			SendDlgItemMessage(hDebug, IDC_DEBUGGER_DISASSEMBLY, EM_GETSEL, (WPARAM)&sel_start, (LPARAM)&sel_end);
			if (sel_start == sel_end)
			{
				CallWindowProc(IDC_DEBUGGER_DISASSEMBLY_oldWndProc, hwndDlg, WM_LBUTTONDOWN, wParam, lParam);
				CallWindowProc(IDC_DEBUGGER_DISASSEMBLY_oldWndProc, hwndDlg, WM_LBUTTONUP, wParam, lParam);
				return 0;
			}
			break;
		}
		case WM_RBUTTONUP:
		{
			// save current selection
			int sel_start = 0, sel_end = 0;
			SendDlgItemMessage(hDebug, IDC_DEBUGGER_DISASSEMBLY, EM_GETSEL, (WPARAM)&sel_start, (LPARAM)&sel_end);
			// simulate a click
			CallWindowProc(IDC_DEBUGGER_DISASSEMBLY_oldWndProc, hwndDlg, WM_LBUTTONDOWN, wParam, lParam);
			CallWindowProc(IDC_DEBUGGER_DISASSEMBLY_oldWndProc, hwndDlg, WM_LBUTTONUP, wParam, lParam);
			// try bringing Symbolic Debug Naming dialog
			int offset = Debugger_CheckClickingOnAnAddressOrSymbolicName(GET_Y_LPARAM(lParam) / debugSystem->disasmFontHeight, false);
			if (offset != EOF)
			{
				if (DoSymbolicDebugNaming(offset, hDebug))
				{
					// enable "Symbolic Debug" if not yet enabled
					if (!symbDebugEnabled)
					{
						symbDebugEnabled = true;
						CheckDlgButton(hDebug, IDC_DEBUGGER_ENABLE_SYMBOLIC, BST_CHECKED);
					}
					UpdateDebugger(false);
				} else
				{
					// then restore old selection
					SendDlgItemMessage(hDebug, IDC_DEBUGGER_DISASSEMBLY, EM_SETSEL, (WPARAM)sel_start, (LPARAM)sel_end);
				}
				return 0;
			} else
			{
				// then restore old selection
				SendDlgItemMessage(hDebug, IDC_DEBUGGER_DISASSEMBLY, EM_SETSEL, (WPARAM)sel_start, (LPARAM)sel_end);
			}
			break;
		}
		case WM_MOUSEMOVE:
		{
			char str[256] = {0}, *ptr, dotdot[4];
			int tmp, i;
			int mouse_x, mouse_y;

			mouse_x = GET_X_LPARAM(lParam);
			mouse_y = GET_Y_LPARAM(lParam);
			
			if(mouse_y < 0 || mouse_x < 0)
				break;

			tmp = mouse_y / debugSystem->disasmFontHeight;
			if (tmp < (int)disassembly_addresses.size())
			{
				i = disassembly_addresses[tmp];
				if (i >= 0x8000)
				{
					dotdot[0] = 0;
					ptr = iNesShortFName();
					if (!ptr)
						ptr = "...";
					if (strlen(ptr) > 60)
						strcpy(dotdot, "...");
					if (GetNesFileAddress(i) == -1)
						sprintf(str,"CPU Address $%04X, Error retreiving ROM File Address!",i);
					else
						sprintf(str,"CPU Address %02X:%04X, Offset 0x%06X in file \"%.40s%s\" (NL file: %X)",getBank(i),i,GetNesFileAddress(i),ptr,dotdot,getBank(i));
					SetDlgItemText(hDebug, IDC_DEBUGGER_ADDR_LINE,str);
				} else
				{
					SetDlgItemText(hDebug, IDC_DEBUGGER_ADDR_LINE, "Double-click on any address to prompt Add Breakpoint.");
				}
			} else
			{
				SetDlgItemText(hDebug, IDC_DEBUGGER_ADDR_LINE, "Double-click on any address to prompt Add Breakpoint.");
			}
			break;
		}
		case WM_MOUSEWHEEL:
		{
			SendMessage(GetDlgItem(hDebug, IDC_DEBUGGER_DISASSEMBLY_VSCR), uMsg, wParam, lParam);
			return 0;
		}
		case WM_CONTEXTMENU:
		{
			int sel_start = 0, sel_end = 0;
			SendDlgItemMessage(hDebug, IDC_DEBUGGER_DISASSEMBLY, EM_GETSEL, (WPARAM)&sel_start, (LPARAM)&sel_end);
			hDisasmcontextsub = GetSubMenu(hDisasmcontext, 0);
			if (sel_end - sel_start == 0)
				EnableMenuItem(hDisasmcontextsub, DISASM_CONTEXT_COPY, MF_GRAYED);
			else
				EnableMenuItem(hDisasmcontextsub, DISASM_CONTEXT_COPY, MF_ENABLED);
			if (lParam != -1)
				TrackPopupMenu(hDisasmcontextsub, TPM_RIGHTBUTTON, LOWORD(lParam), HIWORD(lParam), 0, hwndDlg, 0);
			else
			{
				// Handle the context menu keyboard key
				RECT wrect;
				GetWindowRect(GetDlgItem(hDebug, IDC_DEBUGGER_DISASSEMBLY), &wrect);
				TrackPopupMenu(hDisasmcontextsub, TPM_RIGHTBUTTON, wrect.left + 50, wrect.top + 50, 0, hwndDlg, 0);
			}
			break;
		}		
		case WM_COMMAND:
		{
			switch(LOWORD(wParam))
			{
				case DISASM_CONTEXT_COPY:
					SendMessage(hwndDlg, WM_COPY, 0, 0);
					break;
				case DISASM_CONTEXT_SELECTALL:
					CHARRANGE charr;
					charr.cpMin = 0;
					charr.cpMax = -1;
					SendMessage(hwndDlg, EM_SETSEL, 0, (LPARAM)&charr);
					break;
			}
			break;
		}
	}
	return CallWindowProc(IDC_DEBUGGER_DISASSEMBLY_oldWndProc, hwndDlg, uMsg, wParam, lParam);
}

// Need to coordinate these with res.rc
#define MENU_OPTIONS_POS 0
#define MENU_COLORS_POS 1
#define MENU_SYMBOLS_POS 2
#define MENU_TOOLS_POS 3

HMENU toolsPopup, symbolsPopup, optionsPopup;

INT_PTR CALLBACK DebuggerCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

inline int CheckedFlag(bool b)
{
	return b ? MF_CHECKED : MF_UNCHECKED;
}

inline int EnabledFlag(bool b)
{
	return b ? MF_ENABLED : MF_GRAYED;
}

inline void UpdateOptionsPopup(HMENU optionsPopup)
{
	CheckMenuItem(optionsPopup, ID_DEBUGGER_AUTO_OPEN, CheckedFlag(debuggerAutoload));
	CheckMenuItem(optionsPopup, ID_DEBUGGER_IDA_FONT, CheckedFlag(debuggerIDAFont));
	CheckMenuItem(optionsPopup, ID_DEBUGGER_SHOW_ROM_OFFSETS, CheckedFlag(debuggerDisplayROMoffsets));
}

inline void UpdateSymbolsPopup(HMENU symbolsPopup)
{
	CheckMenuItem(symbolsPopup, ID_DEBUGGER_LOAD_DEB_FILE, CheckedFlag(debuggerSaveLoadDEBFiles));
	CheckMenuItem(symbolsPopup, ID_DEBUGGER_SYMBOLIC_DEBUG, CheckedFlag(symbDebugEnabled));
	CheckMenuItem(symbolsPopup, ID_DEBUGGER_INLINE_ADDRESS, CheckedFlag(inlineAddressEnabled));
	CheckMenuItem(symbolsPopup, ID_DEBUGGER_DEFAULT_REG_NAMES, CheckedFlag(symbRegNames));

	// Gray out potentially irrelavant options
	EnableMenuItem(symbolsPopup, ID_DEBUGGER_DEFAULT_REG_NAMES, EnabledFlag(symbDebugEnabled));
	EnableMenuItem(symbolsPopup, ID_DEBUGGER_INLINE_ADDRESS, EnabledFlag(symbDebugEnabled));
	EnableMenuItem(symbolsPopup, ID_DEBUGGER_RELOAD_SYMBOLS, EnabledFlag(GameInfo));
}

inline void UpdateToolsPopup(HMENU symbolsPopup)
{
	EnableMenuItem(symbolsPopup, ID_DEBUGGER_ROM_PATCHER, EnabledFlag(GameInfo));
	EnableMenuItem(symbolsPopup, ID_DEBUGGER_CODE_DUMPER, EnabledFlag(GameInfo));
}

void DebuggerInitDialog(HWND hwndDlg)
{
	char str[256] = { 0 };
	CheckDlgButton(hwndDlg, IDC_DEBUGGER_BREAK_ON_CYCLES, break_on_cycles ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, IDC_DEBUGGER_BREAK_ON_INSTRUCTIONS, break_on_instructions ? BST_CHECKED : BST_UNCHECKED);
	sprintf(str, "%u", (unsigned)break_cycles_limit);
	SetDlgItemText(hwndDlg, IDC_DEBUGGER_CYCLES_EXCEED, str);
	sprintf(str, "%u", (unsigned)break_instructions_limit);
	SetDlgItemText(hwndDlg, IDC_DEBUGGER_INSTRUCTIONS_EXCEED, str);

	CheckDlgButton(hwndDlg, IDC_DEBUGGER_BREAK_ON_BAD_OP, FCEUI_Debugger().badopbreak ? BST_CHECKED : BST_UNCHECKED);

	CheckDlgButton(hwndDlg, DEBUGLOADDEB, debuggerSaveLoadDEBFiles ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, DEBUGIDAFONT, debuggerIDAFont ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, DEBUGAUTOLOAD, debuggerAutoload ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, IDC_DEBUGGER_ROM_OFFSETS, debuggerDisplayROMoffsets ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, IDC_DEBUGGER_ENABLE_SYMBOLIC, symbDebugEnabled ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, IDC_DEBUGGER_PREDEFINED_REGS, symbRegNames ? BST_CHECKED : BST_UNCHECKED);

	if (DbgPosX==-32000) DbgPosX=0; //Just in case
	if (DbgPosY==-32000) DbgPosY=0;
	SetWindowPos(hwndDlg,0,DbgPosX,DbgPosY,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER);

	GetWindowRect(hwndDlg,&currDebuggerRect);

	si.cbSize = sizeof(SCROLLINFO);
	si.fMask = SIF_ALL;
	si.nMin = 0;
	si.nMax = 0x10000;
	si.nPos = 0;
	si.nPage = 8;
	SetScrollInfo(GetDlgItem(hwndDlg,IDC_DEBUGGER_DISASSEMBLY_VSCR),SB_CTL,&si,TRUE);

	//setup font
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_DISASSEMBLY,WM_SETFONT,(WPARAM)debugSystem->hDisasmFont,FALSE);
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_DISASSEMBLY_LEFT_PANEL,WM_SETFONT,(WPARAM)debugSystem->hFixedFont,FALSE);
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_STACK_CONTENTS,WM_SETFONT,(WPARAM)debugSystem->hFixedFont,FALSE);
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_BP_LIST,WM_SETFONT,(WPARAM)debugSystem->hFixedFont,FALSE);

	//text limits
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_VAL_A,EM_SETLIMITTEXT,2,0);
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_VAL_X,EM_SETLIMITTEXT,2,0);
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_VAL_Y,EM_SETLIMITTEXT,2,0);
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_VAL_PC,EM_SETLIMITTEXT,4,0);
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_STACK_CONTENTS,EM_SETLIMITTEXT,383,0);
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_VAL_PCSEEK,EM_SETLIMITTEXT,4,0);
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_VAL_PPU,EM_SETLIMITTEXT,4,0);
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_VAL_SPR,EM_SETLIMITTEXT,2,0);

	// limit input
	// Don't limit address entry. See: debugcpp offsetStringToInt
	DefaultEditCtrlProc = (WNDPROC)
	SetWindowLongPtr(GetDlgItem(hwndDlg, IDC_DEBUGGER_VAL_PC), GWLP_WNDPROC, (LONG_PTR)FilterEditCtrlProc);
	SetWindowLongPtr(GetDlgItem(hwndDlg, IDC_DEBUGGER_VAL_A), GWLP_WNDPROC, (LONG_PTR)FilterEditCtrlProc);
	SetWindowLongPtr(GetDlgItem(hwndDlg, IDC_DEBUGGER_VAL_X), GWLP_WNDPROC, (LONG_PTR)FilterEditCtrlProc);
	SetWindowLongPtr(GetDlgItem(hwndDlg, IDC_DEBUGGER_VAL_Y), GWLP_WNDPROC, (LONG_PTR)FilterEditCtrlProc);

	//I'm lazy, disable the controls which I can't mess with right now
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_VAL_PPU,EM_SETREADONLY,TRUE,0);
	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_VAL_SPR,EM_SETREADONLY,TRUE,0);

// ################################## Start of SP CODE ###########################

	SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_BOOKMARK,EM_SETLIMITTEXT,4,0);
	
	LoadGameDebuggerData(hwndDlg);

	debuggerWasActive = 1;
			
// ################################## End of SP CODE ###########################

	// Enable Context Sub-Menus
	hDebugcontext = LoadMenu(fceu_hInstance,"DEBUGCONTEXTMENUS");
	hDisasmcontext = LoadMenu(fceu_hInstance,"DISASMCONTEXTMENUS");

	// prevent the font of the edit control from screwing up when it contains MBC or characters not contained the current font.
	SendDlgItemMessage(hwndDlg, IDC_DEBUGGER_DISASSEMBLY, EM_SETLANGOPTIONS, 0, SendDlgItemMessage(hwndDlg, IDC_DEBUGGER_DISASSEMBLY, EM_GETLANGOPTIONS, 0, 0) & ~IMF_AUTOFONT);

	// subclass editfield
	IDC_DEBUGGER_DISASSEMBLY_oldWndProc = (WNDPROC)SetWindowLongPtr(GetDlgItem(hwndDlg, IDC_DEBUGGER_DISASSEMBLY), GWLP_WNDPROC, (LONG_PTR)IDC_DEBUGGER_DISASSEMBLY_WndProc);

	// prepare menu
	HMENU hdbgmenu = GetMenu(hwndDlg);
	HMENU hcolorpopupmenu = GetSubMenu(hdbgmenu, MENU_COLORS_POS);
	for (int i = 0; i < sizeof(dbgcolormenu) / sizeof(DBGCOLORMENU); ++i)
		InsertColorMenu(hwndDlg, hcolorpopupmenu, &dbgcolormenu[i].menu, i, ID_COLOR_DEBUGGER + i);

	UpdateOptionsPopup(optionsPopup = GetSubMenu(hdbgmenu, MENU_OPTIONS_POS));
	UpdateSymbolsPopup(symbolsPopup = GetSubMenu(hdbgmenu, MENU_SYMBOLS_POS));
	UpdateToolsPopup(toolsPopup = GetSubMenu(hdbgmenu, MENU_TOOLS_POS));

	debugger_open = 1;
	inDebugger = true;
}

void DebuggerResizeWindow(HWND hwndDlg, UINT resizeType)
{
	if(resizeType == SIZE_RESTORED)				    //If dialog was resized
	{
		GetWindowRect(hwndDlg,&newDebuggerRect);	//Get new size

		//Force a minimum Dialog size-------------------------------
		DbgSizeX = newDebuggerRect.right - newDebuggerRect.left;	//Store new size (this will be stored in the .cfg file)	
		DbgSizeY = newDebuggerRect.bottom - newDebuggerRect.top;

		// convert minimum size to actual screen size.
		// the minimum height is different between left and right part,
		// but width is the same, similarly hereinafter
		HDC hdc = GetDC(hwndDlg);
		int min_w = MulDiv(DEBUGGER_MIN_WIDTH, GetDeviceCaps(hdc, LOGPIXELSX), 96);
		int min_h_l = MulDiv(DEBUGGER_MIN_HEIGHT_LEFT, GetDeviceCaps(hdc, LOGPIXELSY), 96);
		int min_h_r = MulDiv(DEBUGGER_MIN_HEIGHT_RIGHT, GetDeviceCaps(hdc, LOGPIXELSY), 96);
		ReleaseDC(hwndDlg, hdc);

		// calculate current width and height
		int curr_w = currDebuggerRect.right - currDebuggerRect.left;
		int curr_h_l = currDebuggerRect.bottom - currDebuggerRect.top;
		int curr_h_r = curr_h_l;
		// calculate new width and height
		int new_w = newDebuggerRect.right - newDebuggerRect.left;
		int new_h_l = newDebuggerRect.bottom - newDebuggerRect.top;
		int new_h_r = new_h_l;

		// when the size is smaller than the minimum, calculate it as the minimum size
		if (curr_w < min_w) curr_w = min_w;
		if (curr_h_l < min_h_l) curr_h_l = min_h_l;
		if (curr_h_r < min_h_r) curr_h_r = min_h_r;

		if (new_w < min_w) new_w = min_w;
		if (new_h_l < min_h_l) new_h_l = min_h_l;
		if (new_h_r < min_h_r) new_h_r = min_h_r;

		POINT p[2];
		// Calculate ditto with size
		p[0].x = p[1].x = new_w - curr_w;
		p[0].y = new_h_l - curr_h_l;
		p[1].y = new_h_r - curr_h_r;
		EnumChildWindows(hwndDlg, DebuggerEnumWindowsProc, (LPARAM)p);	//Initiate callback for resizing child windows
		InvalidateRect(hwndDlg, 0, TRUE);
		UpdateWindow(hwndDlg);
		currDebuggerRect = newDebuggerRect;						//Store current debugger window size (for future calculations in EnumChildWindows)
	}
}

void DebuggerBnClicked(HWND hwndDlg, uint16 btnId, HWND hwndBtn)
{
	// Buttons that don't need a rom loaded to do something, such as autoload
	switch (btnId)
	{
		case DEBUGAUTOLOAD: // TODO: delete/merge with ID_DEBUGGER_AUTO_OPEN
			debuggerAutoload ^= 1;
			break;
		case DEBUGLOADDEB: // TODO: delete/merge with ID_DEBUGGER_LOAD_DEB_FILE
			debuggerSaveLoadDEBFiles = !debuggerSaveLoadDEBFiles;
			break;
		case DEBUGIDAFONT: // TODO: delete/merge with ID_DEBUGGER_IDA_FONT
			debuggerIDAFont ^= 1;
			debugSystem->hDisasmFont = debuggerIDAFont ? debugSystem->hIDAFont : debugSystem->hFixedFont;
			debugSystem->disasmFontHeight = debuggerIDAFont ? IDAFontSize : debugSystem->fixedFontHeight;
			SendDlgItemMessage(hwndDlg, IDC_DEBUGGER_DISASSEMBLY, WM_SETFONT, (WPARAM)debugSystem->hDisasmFont, FALSE);
			UpdateDebugger(false);
			break;
		case IDC_DEBUGGER_CYCLES_EXCEED:
		{
			// Pretty sure this was dead code. BN_CLICKED is not equal to EN_CHANGE.
			// Does it need to be moved to an EN_CHANGED callback?
			//if (HIWORD(wParam) == EN_CHANGE)
			//{
			//	char str[16];
			//	GetDlgItemText(hwndDlg, IDC_DEBUGGER_CYCLES_EXCEED, str, 16);
			//	break_cycles_limit = strtoul(str, NULL, 10);
			//}
			break;
		}
		case IDC_DEBUGGER_INSTRUCTIONS_EXCEED:
		{
			// Pretty sure this was dead code. BN_CLICKED is not equal to EN_CHANGE.
			// Does it need to be moved to an EN_CHANGED callback?
			//if (HIWORD(wParam) == EN_CHANGE)
			//{
			//	char str[16];
			//	GetDlgItemText(hwndDlg, IDC_DEBUGGER_INSTRUCTIONS_EXCEED, str, 16);
			//	break_instructions_limit = strtoul(str, NULL, 10);
			//}
			break;
		}
		case ID_DEBUGGER_DEFCOLOR:
		{
			if (!IsDebugColorDefault() && MessageBox(hwndDlg, "Do you want to restore all the colors to default?", "Restore default colors", MB_YESNO | MB_ICONINFORMATION) == IDYES)
			{
				RestoreDefaultDebugColor();
				RECT rect;
				GetClientRect(GetDlgItem(hwndDlg, IDC_DEBUGGER_DISASSEMBLY), &rect);
				UpdateDisassembleView(hwndDlg, IDC_DEBUGGER_DISASSEMBLY, (rect.bottom - rect.top) / debugSystem->disasmFontHeight);
				HMENU hcolorpopupmenu = GetSubMenu(GetMenu(hwndDlg), 1);
				for (int i = 0; i < sizeof(dbgcolormenu) / sizeof(DBGCOLORMENU); ++i)
					ModifyColorMenu(hwndDlg, hcolorpopupmenu, &dbgcolormenu[i].menu, i, ID_COLOR_DEBUGGER + i);
			}
		}
		break;
		case ID_COLOR_DEBUGGER:
		case ID_COLOR_DEBUGGER + 1:
		case ID_COLOR_DEBUGGER + 2:
		case ID_COLOR_DEBUGGER + 3:
		case ID_COLOR_DEBUGGER + 4:
		case ID_COLOR_DEBUGGER + 5:
		case ID_COLOR_DEBUGGER + 6:
		case ID_COLOR_DEBUGGER + 7:
		case ID_COLOR_DEBUGGER + 8:
		case ID_COLOR_DEBUGGER + 9:
		case ID_COLOR_DEBUGGER + 10:
		case ID_COLOR_DEBUGGER + 11:
		case ID_COLOR_DEBUGGER + 12:
		{
			int index = btnId - ID_COLOR_DEBUGGER;
			if (ChangeColor(hwndDlg, &dbgcolormenu[index]))
			{
				RECT rect;
				GetClientRect(GetDlgItem(hwndDlg, IDC_DEBUGGER_DISASSEMBLY), &rect);
				UpdateDisassembleView(hwndDlg, IDC_DEBUGGER_DISASSEMBLY, (rect.bottom - rect.top) / debugSystem->disasmFontHeight);
				ModifyColorMenu(hwndDlg, GetSubMenu(GetMenu(hwndDlg), 1), &dbgcolormenu[index].menu, index, btnId);
			}
		}
		break;
		// Options menu
		// TODO: Reuse/merge with the old IDs from the persistent buttons.
		case ID_DEBUGGER_AUTO_OPEN:
			debuggerAutoload ^= 1;
			break;
		case ID_DEBUGGER_IDA_FONT:
			debuggerIDAFont ^= 1;
			debugSystem->hDisasmFont = debuggerIDAFont ? debugSystem->hIDAFont : debugSystem->hFixedFont;
			debugSystem->disasmFontHeight = debuggerIDAFont ? IDAFontSize : debugSystem->fixedFontHeight;
			SendDlgItemMessage(hwndDlg, IDC_DEBUGGER_DISASSEMBLY, WM_SETFONT, (WPARAM)debugSystem->hDisasmFont, FALSE);
			UpdateDebugger(false);
			break;
		case ID_DEBUGGER_SHOW_ROM_OFFSETS:
			debuggerDisplayROMoffsets ^= 1;
			UpdateDebugger(false);
			break;
		case ID_DEBUGGER_RESTORE_SIZE:
			RestoreSize(hwndDlg);
			break;
		// Symbols menu
		case ID_DEBUGGER_RELOAD_SYMBOLS:
			ramBankNamesLoaded = false;
			for (int i = 0; i < ARRAYSIZE(pageNumbersLoaded); i++)
				pageNumbersLoaded[i] = -1;
			loadNameFiles();
			UpdateDebugger(false);
			break;
		case ID_DEBUGGER_INLINE_ADDRESS:
			inlineAddressEnabled ^= 1;
			UpdateDebugger(false);
			break;
		case ID_DEBUGGER_LOAD_DEB_FILE:
			debuggerSaveLoadDEBFiles ^= 1;
			break;
		case ID_DEBUGGER_SYMBOLIC_DEBUG:
			symbDebugEnabled ^= 1;
			UpdateDebugger(false);
			break;
		case ID_DEBUGGER_DEFAULT_REG_NAMES:
			symbRegNames ^= 1;
			UpdateDebugger(false);
			break;
		// Tools menu
		case ID_DEBUGGER_ROM_PATCHER:
			DoPatcher(-1, hwndDlg);
			break;
		case ID_DEBUGGER_CODE_DUMPER:
			FILE *fout = fopen("testdump.txt", "w");
			// Hardcoded start and end addresses for Rockman 2's NMI routine. Lol
			// Dump(fout, 0xCFED, 0xD0D3);
			printf("Taking a dump...\n");
			Dump(fout, 0x8000, 0xFFFF);
			printf("I'M DONE!\n");
			fclose(fout);
			break;
	}

	// Buttons that only get handled when a game is loaded
	if (GameInfo)
	{
		switch (btnId)
		{
			case IDC_DEBUGGER_BP_ADD:
				childwnd = 1;
				if (DialogBoxParam(fceu_hInstance,"ADDBP",hwndDlg,AddbpCallB, 0)) AddBreakList();
				childwnd = 0;
				UpdateDebugger(false);
				break;
			case IDC_DEBUGGER_BP_DEL:
				DeleteBreak(SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_BP_LIST,LB_GETCURSEL,0,0));
				break;
			case IDC_DEBUGGER_BP_EDIT:
				WP_edit = SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_BP_LIST,LB_GETCURSEL,0,0);
				if (DialogBoxParam(fceu_hInstance, "ADDBP", hwndDlg, AddbpCallB, 0)) EditBreakList();
				WP_edit = -1;
				UpdateDebugger(false);
				break;
			case IDC_DEBUGGER_RUN:
				if (FCEUI_EmulationPaused()) {
					UpdateRegs(hwndDlg);
					FCEUI_ToggleEmulationPause();
					//DebuggerWasUpdated = false done in above function;
				}
				break;
			case IDC_DEBUGGER_STEP_IN:
				if (FCEUI_EmulationPaused())
					UpdateRegs(hwndDlg);
				FCEUI_Debugger().step = true;
				FCEUI_SetEmulationPaused(0);
				UpdateOtherDebuggingDialogs();
								
				break;
			case IDC_DEBUGGER_RUN_LINE:
				if (FCEUI_EmulationPaused())
					UpdateRegs(hwndDlg);
				FCEUI_Debugger().runline = true;
				{
					uint64 ts=timestampbase;
					ts+=timestamp;
					ts+=341/3;
					FCEUI_Debugger().runline_end_time=ts;
				}
				FCEUI_SetEmulationPaused(0);
				UpdateOtherDebuggingDialogs();
				break;
			case IDC_DEBUGGER_RUN_FRAME2:
				if (FCEUI_EmulationPaused())
					UpdateRegs(hwndDlg);
				FCEUI_Debugger().runline = true;
				{
					uint64 ts=timestampbase;
					ts+=timestamp;
					ts+=128*341/3;
					FCEUI_Debugger().runline_end_time=ts;
				}
				FCEUI_SetEmulationPaused(0);
				UpdateOtherDebuggingDialogs();
				break;
			case IDC_DEBUGGER_STEP_OUT:
				if (FCEUI_EmulationPaused() > 0) {
					DebuggerState &dbgstate = FCEUI_Debugger();
					UpdateRegs(hwndDlg);
					if ((dbgstate.stepout) && (MessageBox(hwndDlg,"Step Out is currently in process. Cancel it and setup a new Step Out watch?","Step Out Already Active",MB_YESNO|MB_ICONINFORMATION) != IDYES)) break;
					if (GetMem(X.PC) == 0x20) dbgstate.jsrcount = 1;
					else dbgstate.jsrcount = 0;
					dbgstate.stepout = 1;
					FCEUI_SetEmulationPaused(0);
				}
				break;
			case IDC_DEBUGGER_STEP_OVER:
				if (FCEUI_EmulationPaused()) {
					UpdateRegs(hwndDlg);
					int tmp = X.PC;
					uint8 opcode = GetMem(tmp);
					bool jsr = opcode==0x20;
					bool call = jsr;
					#ifdef BRK_3BYTE_HACK
					//with this hack, treat BRK similar to JSR
					if(opcode == 0x00)
						call = true;
					#endif
					if (call) {
						if ((watchpoint[64].flags) && (MessageBox(hwndDlg,"Step Over is currently in process. Cancel it and setup a new Step Over watch?","Step Over Already Active",MB_YESNO|MB_ICONINFORMATION) != IDYES)) break;
						watchpoint[64].address = (tmp+3);
						watchpoint[64].flags = WP_E|WP_X;
					}
					else FCEUI_Debugger().step = true;
					FCEUI_SetEmulationPaused(0);
				}
				break;
			case IDC_DEBUGGER_SEEK_PC:
				if (FCEUI_EmulationPaused())
				{
					UpdateRegs(hwndDlg);
					UpdateDebugger(true);
				}
				break;
			case IDC_DEBUGGER_SEEK_TO:
			{
				if (FCEUI_EmulationPaused())
					UpdateRegs(hwndDlg);
				char str[16];
				GetDlgItemText(hwndDlg,IDC_DEBUGGER_VAL_PCSEEK,str,5);
				int tmp = offsetStringToInt(BT_C, str);
				if (tmp != -1)
				{
					sprintf(str,"%04X", tmp);
					SetDlgItemText(hwndDlg,IDC_DEBUGGER_VAL_PCSEEK,str);
					Disassemble(hDebug, IDC_DEBUGGER_DISASSEMBLY, IDC_DEBUGGER_DISASSEMBLY_VSCR, tmp);
					// "Address Bookmark Add" follows the address
					sprintf(str,"%04X", si.nPos);
					SetDlgItemText(hDebug, IDC_DEBUGGER_BOOKMARK, str);
				}
				break;
			}
			case IDC_DEBUGGER_BREAK_ON_BAD_OP: //Break on bad opcode
				FCEUI_Debugger().badopbreak ^= 1;
				break;
			case IDC_DEBUGGER_FLAG_N: X.P^=N_FLAG; UpdateDebugger(false); break;
			case IDC_DEBUGGER_FLAG_V: X.P^=V_FLAG; UpdateDebugger(false); break;
			case IDC_DEBUGGER_FLAG_U: X.P^=U_FLAG; UpdateDebugger(false); break;
			case IDC_DEBUGGER_FLAG_B: X.P^=B_FLAG; UpdateDebugger(false); break;
			case IDC_DEBUGGER_FLAG_D: X.P^=D_FLAG; UpdateDebugger(false); break;
			case IDC_DEBUGGER_FLAG_I: X.P^=I_FLAG; UpdateDebugger(false); break;
			case IDC_DEBUGGER_FLAG_Z: X.P^=Z_FLAG; UpdateDebugger(false); break;
			case IDC_DEBUGGER_FLAG_C: X.P^=C_FLAG; UpdateDebugger(false); break;

			case IDC_DEBUGGER_RESET_COUNTERS:
			{
				ResetDebugStatisticsCounters();
				UpdateDebugger(false);
				break;
			}
			case IDC_DEBUGGER_BREAK_ON_CYCLES:
			{
				break_on_cycles ^= 1;
				break;
			}
			case IDC_DEBUGGER_BREAK_ON_INSTRUCTIONS:
			{
				break_on_instructions ^= 1;
				break;
			}

// ################################## Start of SP CODE ###########################

			case IDC_DEBUGGER_RELOAD_SYMS:
			{ // TODO: delete/merge with ID_DEBUGGER_RELOAD_SYMBOLS
				ramBankNamesLoaded = false;
				for(int i=0;i<ARRAYSIZE(pageNumbersLoaded);i++)
					pageNumbersLoaded[i] = -1;
				loadNameFiles();
				UpdateDebugger(false);
				break;
			}
			case IDC_DEBUGGER_BOOKMARK_ADD: AddDebuggerBookmark(hwndDlg); break;
			case IDC_DEBUGGER_BOOKMARK_DEL: DeleteDebuggerBookmark(hwndDlg); break;
			case IDC_DEBUGGER_BOOKMARK_EDIT: EditDebuggerBookmark(hwndDlg); break;
			case IDC_DEBUGGER_ENABLE_SYMBOLIC:
			{ // TODO: delete/merge with ID_DEBUGGER_SYMBOLIC_DEBUG
				symbDebugEnabled ^= 1;
				CheckDlgButton(hwndDlg, IDC_DEBUGGER_ENABLE_SYMBOLIC, symbDebugEnabled ? BST_CHECKED : BST_UNCHECKED);
				UpdateDebugger(false);
				break;
			}
			case IDC_DEBUGGER_PREDEFINED_REGS:
			{ // TODO: delete/merge with ID_DEBUGGER_DEFAULT_REG_NAMES
				symbRegNames ^= 1;
				CheckDlgButton(hwndDlg, IDC_DEBUGGER_PREDEFINED_REGS, symbRegNames ? BST_CHECKED : BST_UNCHECKED);
				UpdateDebugger(false);
				break;
			}
							
// ################################## End of SP CODE ###########################
							
			case IDC_DEBUGGER_ROM_OFFSETS:
			{ // TODO: delete/merge with ID_DEBUGGER_SHOW_ROM_OFFSETS
				debuggerDisplayROMoffsets ^= 1;
				UpdateDebugger(false);
				break;
			}
			case IDC_DEBUGGER_ROM_PATCHER: DoPatcher(-1,hwndDlg); break; // TODO: delete/merge with ID_DEBUGGER_ROM_PATCHER
			// Double click the breakpoint list
			// TODO: wait for the clean double-click callback!
			case DEBUGGER_CONTEXT_TOGGLEBREAK: DebuggerCallB(hwndDlg, WM_COMMAND, (LBN_DBLCLK * 0x10000) | (IDC_DEBUGGER_BP_LIST), (LPARAM)hwndBtn); break;
		}
	}
}

void DebuggerDblClk(HWND hwndDlg, uint16 itemId, HWND hwndBtn)
{
	// None of these events can be processed without a game loaded.
	if (!GameInfo)
		return;

	switch (itemId)
	{
		case IDC_DEBUGGER_BP_LIST: 
			EnableBreak(SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_BP_LIST,LB_GETCURSEL,0,0));
			break;
		case LIST_DEBUGGER_BOOKMARKS:
			GoToDebuggerBookmark(hwndDlg);
			break;
	}
}

void DebuggerSelCancel(HWND hwndDlg, uint16 listBoxId, HWND hwndList)
{
	// None of these events can be processed without a game loaded.
	if (!GameInfo)
		return;

	switch (listBoxId)
	{
		case IDC_DEBUGGER_BP_LIST:
			// Disable the delete and edit buttons
			EnableWindow(GetDlgItem(hwndDlg,IDC_DEBUGGER_BP_DEL),FALSE);
			EnableWindow(GetDlgItem(hwndDlg,IDC_DEBUGGER_BP_EDIT),FALSE);
			break;
	}
}

void DebuggerSelChange(HWND hwndDlg, uint16 listBoxId, HWND hwndList)
{
	// None of these events can be processed without a game loaded.
	if (!GameInfo)
		return;

	switch (listBoxId)
	{
		case IDC_DEBUGGER_BP_LIST:
		{
			// If something is selected, enabled the delete and edit buttons.
			if (SendDlgItemMessage(hwndDlg,IDC_DEBUGGER_BP_LIST,LB_GETCURSEL,0,0) >= 0)
			{
				EnableWindow(GetDlgItem(hwndDlg,IDC_DEBUGGER_BP_DEL),TRUE);
				EnableWindow(GetDlgItem(hwndDlg,IDC_DEBUGGER_BP_EDIT),TRUE);
			} else
			{
				EnableWindow(GetDlgItem(hwndDlg,IDC_DEBUGGER_BP_DEL),FALSE);
				EnableWindow(GetDlgItem(hwndDlg,IDC_DEBUGGER_BP_EDIT),FALSE);
			}
			break;
		}
	}
}

void DebuggerInitMenuPopup(HWND hwndDlg, HMENU hmenu, uint16 pos, bool isWindowMenu)
{
	// We have position in parent menu, but we can't get a handle to the parent...
	if (hmenu == toolsPopup)
	{
		UpdateToolsPopup(hmenu);
		return;
	}
	if (hmenu == symbolsPopup)
	{
		UpdateSymbolsPopup(symbolsPopup);
		return;
	}
	if (hmenu == optionsPopup)
	{
		UpdateOptionsPopup(optionsPopup);
		return;
	}
}

void DebuggerMoveWindow(HWND hwndDlg, uint16 x, uint16 y)
{
	if (!IsIconic(hwndDlg))
	{
		RECT wrect;
		GetWindowRect(hwndDlg,&wrect);
		DbgPosX = wrect.left;
		DbgPosY = wrect.top;

		#ifdef WIN32
		WindowBoundsCheckResize(DbgPosX,DbgPosY,DbgSizeX,wrect.right);
		#endif
	}
}

void DebuggerWindowActivate(HWND hwndDlg, uint16 eventType, HWND hwndActivated)
{
	// None of these events can be processed without a game loaded.
	if (!GameInfo)
		return;

	// Prevents numerous situations where the debugger is out of sync with the data
	if (eventType != WA_INACTIVE)
	{
		UpdateDebugger(false);
	}
	else
	{
		if (FCEUI_EmulationPaused())
			UpdateRegs(hwndDlg);
	}
}

void DebuggerVScroll(HWND hwndDlg, uint16 eventType, uint16 scrollPos, HWND hwndScrollBar)
{
	// None of these events can be processed without a game loaded.
	if (!GameInfo)
		return;

	if (FCEUI_EmulationPaused())
		UpdateRegs(hwndDlg);

	if (hwndScrollBar)
	{
		GetScrollInfo(hwndScrollBar, SB_CTL, &si);
		switch (eventType)
		{
			case SB_ENDSCROLL:
			case SB_TOP:
			case SB_BOTTOM: break;
			case SB_LINEUP:
			{
				si.nPos = InstructionUp(si.nPos);
				break;
			}
			case SB_LINEDOWN:
			{
				si.nPos = InstructionDown(si.nPos);
				break;
			}
			case SB_PAGEUP:
			{
				for (int i = si.nPage; i > 0; i--)
				{
					si.nPos = InstructionUp(si.nPos);
					if (si.nPos < si.nMin)
					{
						si.nPos = si.nMin;
						break;
					}
				}
				break;
			}
			case SB_PAGEDOWN:
			{
				for (int i = si.nPage; i > 0; i--)
				{
					si.nPos = InstructionDown(si.nPos);
					if ((si.nPos + (int)si.nPage) > si.nMax)
					{
						si.nPos = si.nMax - si.nPage;
						break;
					}
				}
				break;
			}
			case SB_THUMBPOSITION:
			case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
		}
		if (si.nPos < si.nMin)
			si.nPos = si.nMin;
		if ((si.nPos + (int)si.nPage) > si.nMax)
			si.nPos = si.nMax - si.nPage;
		SetScrollInfo(hwndScrollBar, SB_CTL, &si, TRUE);

		Disassemble(hwndDlg, IDC_DEBUGGER_DISASSEMBLY, IDC_DEBUGGER_DISASSEMBLY_VSCR, si.nPos);
		// The address in the Add Bookmark textbox follows the scroll pos.
		char str[16];
		sprintf(str,"%04X", si.nPos);
		SetDlgItemText(hwndDlg, IDC_DEBUGGER_BOOKMARK, str);
	}
}

void DebuggerMouseWheel(HWND hwndDlg, int16 rotAmt, uint16 cursorX, uint16 cursorY, uint16 vkeyFlags, LPARAM jankyHandle)
{
	// None of these events can be processed without a game loaded.
	if (!GameInfo)
		return;

	// Try to scroll up or down in units of 8 lines
	int i = rotAmt / WHEEL_DELTA;
	if (i < 0)
	{
		for (i *= -(int)si.nPage; i > 0; i--)
		{
			si.nPos = InstructionDown(si.nPos);
			if ((si.nPos + (int)si.nPage) > si.nMax)
			{
				si.nPos = si.nMax - si.nPage;
				break;
			}
		}
	} else if (i > 0)
	{
		for (i *= si.nPage; i > 0; i--)
		{
			si.nPos = InstructionUp(si.nPos);
			if (si.nPos < si.nMin)
			{
				si.nPos = si.nMin;
				break;
			}
		}
	}

	// This function calls UpdateScrollInfo. Don't worry!
	Disassemble(hDebug, IDC_DEBUGGER_DISASSEMBLY, IDC_DEBUGGER_DISASSEMBLY_VSCR, si.nPos);
	// The address in the Add Bookmark textbox follows the scroll pos.
	char str[16];
	sprintf(str,"%04X", si.nPos);
	SetDlgItemText(hDebug, IDC_DEBUGGER_BOOKMARK, str);
}

void DebuggerContextMenu(HWND hwndDlg, HWND hwndReceiver, int16 cursorX, int16 cursorY)
{
	// None of these events can be processed without a game loaded.
	if (!GameInfo)
		return;

	// Handle certain stubborn context menus for nearly incapable controls.
	if (hwndReceiver == GetDlgItem(hwndDlg, IDC_DEBUGGER_BP_LIST))
	{
		// Only open the menu if a breakpoint is selected
		if (SendDlgItemMessage(hwndDlg, IDC_DEBUGGER_BP_LIST, LB_GETCURSEL, 0, 0) >= 0)
		{
			hDebugcontextsub = GetSubMenu(hDebugcontext,0);
			if (cursorX != -1)
				// Create menu at cursor pos
				TrackPopupMenu(hDebugcontextsub, TPM_RIGHTBUTTON, cursorX, cursorY, 0, hwndDlg, NULL);
			else
			{ // Handle the context menu keyboard key
				RECT wrect;
				GetWindowRect(GetDlgItem(hwndDlg, IDC_DEBUGGER_BP_LIST), &wrect);
				// Create menu over breakpoints list
				TrackPopupMenu(hDebugcontextsub, TPM_RIGHTBUTTON, wrect.left + int((wrect.right - wrect.left) / 3), wrect.top + int((wrect.bottom - wrect.top) / 3), 0, hwndDlg, NULL);
			}						
		}
	}
}

void DebuggerMouseMove(HWND hwndDlg, int cursorX, int cursorY, int vkFlags)
{
	// None of these events can be processed without a game loaded.
	if (!GameInfo)
		return;

	bool setString = false;
	if ((cursorX > 6) && (cursorX < 30) && (cursorY > 10))
	{
		setString = true;
		RECT rectDisassembly;
		GetClientRect(GetDlgItem(hDebug, IDC_DEBUGGER_DISASSEMBLY), &rectDisassembly);
		int height = rectDisassembly.bottom - rectDisassembly.top;
		cursorY -= 10;
		if (cursorY > height)
			setString = false;
		cursorY /= debugSystem->disasmFontHeight;
	}

	if (setString)
		SetDlgItemText(hDebug, IDC_DEBUGGER_ADDR_LINE, "Left click = Inline Assembler. Middle click = Game Genie. Right click = Hex Editor.");
	else
		SetDlgItemText(hDebug, IDC_DEBUGGER_ADDR_LINE, "");
}

void DebuggerLButtonDown(HWND hwndDlg, int cursorX, int cursorY, int vkFlags)
{
	// None of these events can be processed without a game loaded.
	if (!GameInfo)
		return;

	if (FCEUI_EmulationPaused() && (cursorX > 6) && (cursorX < 30) && (cursorY > 10))
	{
		int tmp = (cursorY - 10) / debugSystem->disasmFontHeight;
		if (tmp < (int)disassembly_addresses.size())
		{
			int i = disassembly_addresses[tmp];
			iaPC=i;
			if (iaPC >= 0x8000)
			{
				DialogBox(fceu_hInstance, "ASSEMBLER", hwndDlg, AssemblerCallB);
				UpdateDebugger(false);
			}
		}
	}
}

void DebuggerRButtonDown(HWND hwndDlg, int cursorX, int cursorY, int vkFlags)
{
	// None of these events can be processed without a game loaded.
	if (!GameInfo)
		return;

	if (FCEUI_EmulationPaused() && (cursorX > 6) && (cursorX < 30) && (cursorY > 10))
	{
		int tmp = (cursorY - 10) / debugSystem->disasmFontHeight;
		if (tmp < (int)disassembly_addresses.size())
		{
			int i = disassembly_addresses[tmp];
			if (i >= 0x8000)
				// show ROM data in hex editor
				ChangeMemViewFocus(3, GetNesFileAddress(i), -1);
			else
				// show RAM data in hex editor
				ChangeMemViewFocus(0, i, -1);
		}
	}
}

void DebuggerMButtonDown(HWND hwndDlg, int cursorX, int cursorY, int vkFlags)
{
	// None of these events can be processed without a game loaded.
	if (!GameInfo)
		return;

	if (FCEUI_EmulationPaused() && (cursorX > 6) && (cursorX < 30) && (cursorY > 10))
	{
		int tmp = (cursorY - 10) / debugSystem->disasmFontHeight;
		if (tmp < (int)disassembly_addresses.size())
		{
			int i = disassembly_addresses[tmp];
			SetGGConvFocus(i, GetMem(i));
		}
	}
}

INT_PTR CALLBACK DebuggerCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
			DebuggerInitDialog(hwndDlg);
			break;
		case WM_SIZE:
			DebuggerResizeWindow(hwndDlg, wParam);
			break;
		case WM_CLOSE:
		case WM_QUIT:
			DebuggerExit();
			break;
		case WM_MOVING:
			break;
		case WM_MOVE:
			DebuggerMoveWindow(hwndDlg, LOWORD(lParam), HIWORD(lParam));
			break;
		case WM_COMMAND:
			switch (HIWORD(wParam))
			{
				case BN_CLICKED:
					DebuggerBnClicked(hwndDlg, LOWORD(wParam), (HWND)lParam);
					break;
				case LBN_DBLCLK:
					DebuggerDblClk(hwndDlg, LOWORD(wParam), (HWND)lParam);
					break;
				case LBN_SELCANCEL:
					DebuggerSelCancel(hwndDlg, LOWORD(wParam), (HWND)lParam);
					break;
				case LBN_SELCHANGE:
					DebuggerSelChange(hwndDlg, LOWORD(wParam), (HWND)lParam);
					break;
			}
		case WM_INITMENUPOPUP:
			DebuggerInitMenuPopup(hwndDlg, (HMENU)wParam, LOWORD(lParam), HIWORD(lParam));
			break;
		case WM_ACTIVATE:
			DebuggerWindowActivate(hwndDlg, LOWORD(wParam), (HWND)lParam);
			break;
		case WM_VSCROLL:
			DebuggerVScroll(hwndDlg, LOWORD(wParam), HIWORD(wParam), (HWND)lParam);
			break;
		case WM_MOUSEWHEEL:
			DebuggerMouseWheel(hwndDlg, HIWORD(wParam), LOWORD(lParam), HIWORD(lParam), LOWORD(wParam), lParam);
			break;
		case WM_CONTEXTMENU:
			DebuggerContextMenu(hwndDlg, (HWND)wParam, LOWORD(lParam), HIWORD(lParam));
			break;
		case WM_MOUSEMOVE:
			DebuggerMouseMove(hwndDlg, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
			break;
		case WM_LBUTTONDOWN:
			DebuggerLButtonDown(hwndDlg, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
			break;
		case WM_RBUTTONDOWN:
			DebuggerRButtonDown(hwndDlg, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
			break;
		case WM_MBUTTONDOWN:
			DebuggerMButtonDown(hwndDlg, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
			break;
		case WM_KEYDOWN:
			MessageBox(hwndDlg, "Die!", "I'm dead!", MB_YESNO | MB_ICONINFORMATION);
			break;
	}
	return FALSE;
}

extern void iNESGI(GI h);

void DoDebuggerStepInto()
{
	if (!hDebug)
		return;
	// Click the Step Into button
	DebuggerCallB(hDebug, WM_COMMAND, IDC_DEBUGGER_STEP_IN, 0);
}

void DoPatcher(int address, HWND hParent)
{
	iapoffset = address;
	if (GameInterface == iNESGI)
		DialogBox(fceu_hInstance, "ROMPATCHER", hParent, PatcherCallB);
	else
		MessageBox(hDebug, "Sorry, The Patcher only works on INES rom images", "Error", MB_OK | MB_ICONASTERISK);
	UpdateDebugger(false);
}

void UpdatePatcher(HWND hwndDlg){
	char str[75];
	uint8 *p;
	if(iapoffset != -1){
		EnableWindow(GetDlgItem(hwndDlg,IDC_ROMPATCHER_PATCH_DATA),TRUE);
		EnableWindow(GetDlgItem(hwndDlg,IDC_ROMPATCHER_BTN_APPLY),TRUE);

		if(GetRomAddress(iapoffset) != -1)sprintf(str,"Current Data at NES ROM Address: %04X, .NES file Address: %04X",GetRomAddress(iapoffset),iapoffset);
		else sprintf(str,"Current Data at .NES file Address: %04X",iapoffset);

		SetDlgItemText(hwndDlg,IDC_ROMPATCHER_CURRENT_DATA_BOX,str);

		sprintf(str,"%04X",GetRomAddress(iapoffset));
		SetDlgItemText(hwndDlg,IDC_ROMPATCHER_DISASSEMBLY,str);

		if(GetRomAddress(iapoffset) != -1)SetDlgItemText(hwndDlg,IDC_ROMPATCHER_DISASSEMBLY,DisassembleLine(GetRomAddress(iapoffset)));
		else SetDlgItemText(hwndDlg,IDC_ROMPATCHER_DISASSEMBLY,"Not Currently Loaded in ROM for disassembly");

		p = GetNesPRGPointer(iapoffset-16);
		sprintf(str,"%02X %02X %02X %02X %02X %02X %02X %02X",
			p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);
		SetDlgItemText(hwndDlg,IDC_ROMPATCHER_CURRENT_DATA,str);

	} else {
		SetDlgItemText(hwndDlg,IDC_ROMPATCHER_CURRENT_DATA_BOX,"No Offset Selected");
		SetDlgItemText(hwndDlg,IDC_ROMPATCHER_CURRENT_DATA,"");
		SetDlgItemText(hwndDlg,IDC_ROMPATCHER_DISASSEMBLY,"");
		EnableWindow(GetDlgItem(hwndDlg,IDC_ROMPATCHER_PATCH_DATA),FALSE);
		EnableWindow(GetDlgItem(hwndDlg,IDC_ROMPATCHER_BTN_APPLY),FALSE);
	}
	if(GameInfo->type != GIT_CART)EnableWindow(GetDlgItem(hwndDlg,IDC_ROMPATCHER_BTN_SAVE),FALSE);
	else EnableWindow(GetDlgItem(hwndDlg,IDC_ROMPATCHER_BTN_SAVE),TRUE);
}

/// Updates debugger controls that should be enabled/disabled if a game is loaded.
/// @param enable Flag that indicates whether the menus should be enabled (1) or disabled (0). 
void updateGameDependentMenusDebugger() {
	if (!hDebug)
		return;

	// EnableWindow(GetDlgItem(hDebug,DEBUGLOADDEB), GameInfo != 0 ? FALSE : TRUE);
}

void DoDebug(uint8 halt)
{
	if (!debugger_open)
	{
		// init buffers
		// initialize buffers even before the debugger is open,
		// because some of the operations about hDebug may occur before
		// its WM_INITDIALOG runs.
		debug_wstr = (wchar_t*)malloc(16384 * sizeof(wchar_t));
		debug_cdl_str = (char*)malloc(512);
		debug_str_decoration_comment = (char*)malloc(NL_MAX_MULTILINE_COMMENT_LEN + 10);
		hDebug = CreateDialog(fceu_hInstance,"DEBUGGER",NULL,DebuggerCallB);
		if(DbgSizeX != -1 && DbgSizeY != -1)
			SetWindowPos(hDebug,0,0,0,DbgSizeX,DbgSizeY,SWP_NOMOVE|SWP_NOZORDER|SWP_NOOWNERZORDER);
	}
	if (hDebug)
	{
		ShowWindow(hDebug, SW_SHOWNORMAL);
		SetForegroundWindow(hDebug);
		
		updateGameDependentMenusDebugger();

		if (GameInfo)
			UpdateDebugger(true);
	}
}

//-----------------------------------------
DebugSystem* debugSystem;
unsigned int debuggerFontSize = 15;
unsigned int hexeditorFontHeight = 15;
unsigned int hexeditorFontWidth = 7;
char* hexeditorFontName = 0;

DebugSystem::DebugSystem()
{
}

void DebugSystem::init()
{
	hFixedFont = CreateFont(debuggerFontSize, debuggerFontSize / 2, /*Height,Width*/
		0,0, /*escapement,orientation*/
		FW_REGULAR,FALSE,FALSE,FALSE, /*weight, italic, underline, strikeout*/
		ANSI_CHARSET,OUT_DEVICE_PRECIS,CLIP_MASK, /*charset, precision, clipping*/
		DEFAULT_QUALITY, DEFAULT_PITCH, /*quality, and pitch*/
		"Courier New"); /*font name*/

	hIDAFont = CreateFont(IDAFontSize, IDAFontSize / 2, /*Height,Width*/
		0,0, /*escapement,orientation*/
		FW_REGULAR,FALSE,FALSE,FALSE, /*weight, italic, underline, strikeout*/
		ANSI_CHARSET,OUT_DEVICE_PRECIS,CLIP_MASK, /*charset, precision, clipping*/
		DEFAULT_QUALITY, DEFAULT_PITCH, /*quality, and pitch*/
		"Fixedsys"); /*font name*/

	//if the user provided his own courier font, use that
	extern std::string BaseDirectory;
	std::string courefon_path = BaseDirectory + "\\coure.fon";
	AddFontResourceEx(courefon_path.c_str(), FR_PRIVATE, NULL);

	char* hexfn = hexeditorFontName;
	if(!hexfn) hexfn = "Courier";

	hHexeditorFont = CreateFont(hexeditorFontHeight, hexeditorFontWidth, /*Height,Width*/
		0,0, /*escapement,orientation*/
		FW_REGULAR,FALSE,FALSE,FALSE, /*weight, italic, underline, strikeout*/
		ANSI_CHARSET,OUT_DEVICE_PRECIS,CLIP_MASK, /*charset, precision, clipping*/
		DEFAULT_QUALITY, DEFAULT_PITCH, /*quality, and pitch*/
		hexfn); /*font name*/

	HDC hdc = GetDC(GetDesktopWindow());
	HGDIOBJ old = SelectObject(hdc,hFixedFont);
	TEXTMETRIC tm;
	GetTextMetrics(hdc,&tm);
	fixedFontHeight = tm.tmHeight;
	fixedFontWidth = tm.tmAveCharWidth;
	SelectObject(hdc, hHexeditorFont);
	GetTextMetrics(hdc,&tm);
	HexeditorFontHeight = tm.tmHeight;
	HexeditorFontWidth = tm.tmAveCharWidth;
	SelectObject(hdc,old);
	DeleteDC(hdc);
	hDisasmFont = debuggerIDAFont ? hIDAFont : hFixedFont;
	disasmFontHeight = debuggerIDAFont ? IDAFontSize : fixedFontHeight;

	InitDbgCharFormat();

	newline.lpstrText  = "\r";
	newline.chrg.cpMin = 0;
	newline.chrg.cpMax = -1;

	num.lpstrText  = "$";
	num.chrg.cpMin = 0;
	num.chrg.cpMax = -1;

	LoadLibrary(TEXT("Riched20.dll"));
}

DebugSystem::~DebugSystem()
{
	if (hFixedFont)
	{
		DeleteObject(hFixedFont);
		hFixedFont = 0;
	}
	if (hHexeditorFont)
	{
		DeleteObject(hHexeditorFont);
		hHexeditorFont = 0;
	}
	if (hIDAFont)
	{
		DeleteObject(hIDAFont);
		hIDAFont = 0;
	}
}
