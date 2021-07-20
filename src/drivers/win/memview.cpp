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

#include <algorithm>

#include "common.h"
#include "../../types.h"
#include "../../debug.h"
#include "../../fceu.h"
#include "../../cheat.h"
#include "../../cart.h"
#include "../../ines.h"
#include "memview.h"
#include "debugger.h"
#include "cdlogger.h"
#include "memviewsp.h"
#include "debuggersp.h"
#include "cheat.h"
#include <assert.h>
#include "main.h"
#include "string.h"
#include "help.h"
#include "Win32InputBox.h"
#include "utils/xstring.h"

extern Name* lastBankNames;
extern Name* loadedBankNames;
extern Name* ramBankNames;
extern int RegNameCount;
extern MemoryMappedRegister RegNames[];

extern unsigned char *cdloggervdata;
extern unsigned int cdloggerVideoDataSize;

extern bool JustFrameAdvanced;

using namespace std;

#define MODE_NES_MEMORY   0
#define MODE_NES_PPU      1
#define MODE_NES_OAM      2
#define MODE_NES_FILE     3

#define ID_ADDRESS_FRZ_SUBMENU          1
#define ID_ADDRESS_ADDBP_R              2
#define ID_ADDRESS_ADDBP_W              3
#define ID_ADDRESS_ADDBP_X              4
#define ID_ADDRESS_SEEK_IN_ROM          5
#define ID_ADDRESS_CREATE_GG_CODE       6
#define ID_ADDRESS_ADD_BOOKMARK         20
#define ID_ADDRESS_REMOVE_BOOKMARK      21
#define ID_ADDRESS_EDIT_BOOKMARK        22
#define ID_ADDRESS_SYMBOLIC_NAME        30
#define BOOKMARKS_SUBMENU_POS			4

#define ID_ADDRESS_FRZ_TOGGLE_STATE     1
#define ID_ADDRESS_FRZ_FREEZE           50
#define ID_ADDRESS_FRZ_UNFREEZE         51
#define ID_ADDRESS_FRZ_SEP              52
#define ID_ADDRESS_FRZ_UNFREEZE_ALL     53

#define HIGHLIGHTING_SUBMENU_POS        3
#define HEXEDITOR_COLOR_SUBMENU_POS     4 
#define CDLOGGER_COLOR_SUBMENU_POS      5
#define ID_COLOR_HEXEDITOR              600
#define ID_COLOR_CDLOGGER               650

#define HIGHLIGHT_ACTIVITY_MIN_VALUE 0
#define HIGHLIGHT_ACTIVITY_NUM_COLORS 16
#define PREVIOUS_VALUE_UNDEFINED -1

COLORREF highlightActivityColors[HIGHLIGHT_ACTIVITY_NUM_COLORS] = { 0x0, 0x004035, 0x185218, 0x5e5c34, 0x804c00, 0xba0300, 0xd10038, 0xb21272, 0xba00ab, 0x6f00b0, 0x3700c2, 0x000cba, 0x002cc9, 0x0053bf, 0x0072cf, 0x3c8bc7 };

COLORREF custom_color[16] = { 0 }; // User defined color for ChooseColor()

string memviewhelp = "HexEditor"; //Hex Editor Help Page

int HexRowHeightBorder = 0;		//adelikat:  This will determine the number of pixels between rows in the hex editor, to alter this, the user can change it in the .cfg file, changing one will revert to the way FCEUX2.1.0 did it
int HexCharSpacing = 1;		// pixels between chars

int DefHexRGB, DefCdlRGB;

// This defines all of our right click popup menus
struct
{
	int   minaddress;  //The minimum address where this popup will appear
	int   maxaddress;  //The maximum address where this popup will appear
	int   editingmode; //The editing mode which this popup appears in
	int   id;          //The menu ID for this popup
	char  *text;    //the text for the menu item (some of these need to be dynamic)
}
popupmenu[] =
{
	{0x0000,0xFFFF, MODE_NES_MEMORY, ID_ADDRESS_SYMBOLIC_NAME,       "Add symbolic debug name"},
	{0x0000,0x2000, MODE_NES_MEMORY, ID_ADDRESS_FRZ_SUBMENU,         "Freeze/Unfreeze This Address"},
	{0x6000,0x7FFF, MODE_NES_MEMORY, ID_ADDRESS_FRZ_SUBMENU,         "Freeze/Unfreeze This Address"},
	{0x0000,0xFFFF, MODE_NES_MEMORY, ID_ADDRESS_ADDBP_R,             "Add Debugger Read Breakpoint"},
	{0x0000,0x3FFF, MODE_NES_PPU,    ID_ADDRESS_ADDBP_R,             "Add Debugger Read Breakpoint"},
	{0x0000,0xFFFF, MODE_NES_MEMORY, ID_ADDRESS_ADDBP_W,             "Add Debugger Write Breakpoint"},
	{0x0000,0x3FFF, MODE_NES_PPU,    ID_ADDRESS_ADDBP_W,             "Add Debugger Write Breakpoint"},
	{0x0000,0xFFFF, MODE_NES_MEMORY, ID_ADDRESS_ADDBP_X,             "Add Debugger Execute Breakpoint"},
	{0x8000,0xFFFF, MODE_NES_MEMORY, ID_ADDRESS_SEEK_IN_ROM,         "Go Here In ROM File"},
	{0x8000,0xFFFF, MODE_NES_MEMORY, ID_ADDRESS_CREATE_GG_CODE,      "Create Game Genie Code At This Address"}
};

#define POPUPNUM (sizeof popupmenu / sizeof popupmenu[0])
// The color configure menu
COLORMENU hexcolormenu[] = {
	{ "Normal text",                     PPRGB(HexFore)      },
	{ "Address header",                  PPRGB(HexAddr)      },
//	{ "Normal text background",          PPRGB(HexBack)      },
	{ NULL                                                   },
	{ "Selected text",                   PPRGB(HexHlFore)    },
	{ "Selected background",             PPRGB(HexHlBack)    },
	{ "Selected text (unfocused)",       PPRGB(HexHlShdFore) },
	{ "Selected background (unfocused)", PPRGB(HexHlShdBack) },
	{ NULL                                                   },
	{ "Freezed address",                 PPRGB(HexFreeze),   },
//  { "Freezed ROM address",             PPRGB(RomFreeze),   },
	{ "Bookmark",                        PPRGB(HexBookmark)  }
},
cdlcolormenu[] = {
	{ "Code",           PPRGB(CdlCode)       },
	{ "Data",           PPRGB(CdlData)       },
	{ "PCM Data",       PPRGB(CdlPcm)        },
	{ "Code && Data",   PPRGB(CdlCodeData)   },
	{ NULL                                   },
	{ "Render",         PPRGB(CdlRender)     },
	{ "Read",           PPRGB(CdlRead)       },
	{ "Render && Read", PPRGB(CdlRenderRead) }
};

struct {
	COLORMENU* items;
	int base_id;
	int sub;
	int size;
} colormenu[] =
{
	{ hexcolormenu, ID_COLOR_HEXEDITOR, HEXEDITOR_COLOR_SUBMENU_POS, sizeof(hexcolormenu) / sizeof(hexcolormenu[0]) },
	{ cdlcolormenu, ID_COLOR_CDLOGGER, CDLOGGER_COLOR_SUBMENU_POS, sizeof(cdlcolormenu) / sizeof(cdlcolormenu[0]) }
};

int LoadTableFile();
void UnloadTableFile();
void InputData(char *input);
int GetMemViewData(uint32 i);
//int UpdateCheatColorCallB(char *name, uint32 a, uint8 v, int compare,int s,int type, void *data); //mbg merge 6/29/06 - added arg
//int DeleteCheatCallB(char *name, uint32 a, uint8 v, int compare,int s,int type); //mbg merge 6/29/06 - added arg
void FreezeRam(int address, int mode, int final);
int GetHexScreenCoordx(int offset);
int GetHexScreenCoordy(int offset);
int GetAddyFromCoord(int x,int y);
void AutoScrollFromCoord(int x,int y);
LRESULT CALLBACK MemViewCallB(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK MemFindCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ImportBookmarkCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
void FindNext();
void OpenFindDialog();
static int GetFileData(uint32 offset);
static int WriteFileData(uint32 offset,int data);
static void PalettePoke(uint32 addr, uint8 data);
void SwitchEditingText(int editingText);


HWND hMemView, hMemFind;

int CurOffset;
int ClientHeight;
int NoColors;
int EditingMode;
int EditingText;
int AddyWasText; //used by the GetAddyFromCoord() function.
int TableFileLoaded;

int MemView_wndx, MemView_wndy;
int MemFind_wndx, MemFind_wndy;
bool MemView_HighlightActivity = true;
unsigned int MemView_HighlightActivity_FadingPeriod = HIGHLIGHT_ACTIVITY_NUM_COLORS;
bool MemView_HighlightActivity_FadeWhenPaused = false;
int MemViewSizeX = 630, MemViewSizeY = 300;
static RECT newMemViewRect;

static char chartable[256];

HDC HDataDC;
int CursorX=2, CursorY=9;
int CursorStartAddy, CursorEndAddy = PREVIOUS_VALUE_UNDEFINED;
int CursorDragPoint = -1;//, CursorShiftPoint = -1;
//int CursorStartNibble=1, CursorEndNibble; //1 means that only half of the byte is selected
int TempData = PREVIOUS_VALUE_UNDEFINED;
int DataAmount;
int MaxSize;

COLORREF *CurBGColorList;
COLORREF *CurTextColorList;
COLORREF *DimBGColorList;
COLORREF *DimTextColorList;
COLORREF *HexBGColorList;
COLORREF *HexTextColorList;
COLORREF *AnsiBGColorList;
COLORREF *AnsiTextColorList;

int PreviousCurOffset;
int *PreviousValues;	// for HighlightActivity feature and for speedhack too
unsigned int *HighlightedBytes;

int lbuttondown;

int FindAsText;
int FindDirectionUp;
char FindTextBox[60];

// int temp_offset;

extern iNES_HEADER head;

//undo structure
struct UNDOSTRUCT {
	int addr;
	int size;
	unsigned char *data;
	UNDOSTRUCT *last; //mbg merge 7/18/06 removed struct qualifier
};

struct UNDOSTRUCT *undo_list=0;

void resetHighlightingActivityLog()
{
	// clear the HighlightActivity data
	for (int i = 0; i < DataAmount; ++i)
	{
		PreviousValues[i] = PREVIOUS_VALUE_UNDEFINED;
		HighlightedBytes[i] = HIGHLIGHT_ACTIVITY_MIN_VALUE;
	}
}

void ApplyPatch(int addr,int size, uint8* data){
	UNDOSTRUCT *tmp = (UNDOSTRUCT*)malloc(sizeof(UNDOSTRUCT)); //mbg merge 7/18/06 removed struct qualifiers and added cast

	int i;

	//while(tmp != 0){tmp=tmp->next;x++;};
	//tmp = malloc(sizeof(struct UNDOSTRUCT));
	//sprintf(str,"%d",x);
	//MessageBox(hMemView,str,"info", MB_OK);
	tmp->addr = addr;
	tmp->size = size;
	tmp->data = (uint8*)malloc(sizeof(uint8)*size);
	tmp->last=undo_list;

	for(i = 0;i < size;i++){
		tmp->data[i] = GetFileData((uint32)addr+i);
		WriteFileData((uint32)addr+i,data[i]);
	}

	undo_list=tmp;

	//UpdateColorTable();
	return;
}

void UndoLastPatch(){
	struct UNDOSTRUCT *tmp=undo_list;
	int i;
	if(undo_list == 0)return;
	//while(tmp->next != 0){tmp=tmp->next;}; //traverse to the one before the last one

	for(i = 0;i < tmp->size;i++){
		WriteFileData((uint32)tmp->addr+i,tmp->data[i]);
	}

	undo_list=undo_list->last;

	ChangeMemViewFocus(MODE_NES_FILE,tmp->addr, -1); //move to the focus to where we are undoing at.

	free(tmp->data);
	free(tmp);
	return;
}

void GotoAddress(HWND hwnd) {
	char gotoaddressstring[8];
	int gotoaddress;
	char gototitle[18];

	gotoaddressstring[0] = '\0';
	sprintf(gototitle, "%s%X%s", "Goto (0-", MaxSize-1, ")");
	if(CWin32InputBox::InputBox(gototitle, "Goto which address:", gotoaddressstring, 8, false, hwnd) == IDOK)
	{
		if(EOF != sscanf(gotoaddressstring, "%x", &gotoaddress))
		{
			SetHexEditorAddress(gotoaddress);
		}
	}
}

void SetHexEditorAddress(int gotoaddress)
{

	if (gotoaddress < 0)
		gotoaddress = 0;
	if (gotoaddress > (MaxSize-1))
		gotoaddress = (MaxSize-1);
	
	CursorStartAddy = gotoaddress;
	CursorEndAddy = -1;
	ChangeMemViewFocus(EditingMode, CursorStartAddy, -1);
}

static void FlushUndoBuffer(){
	struct UNDOSTRUCT *tmp;
	while(undo_list!= 0){
		tmp=undo_list;
		undo_list=undo_list->last;
		free(tmp->data);
		free(tmp);
	}
	UpdateColorTable();
	return;
}


static int GetFileData(uint32 offset){
	if(offset < 16) return *((unsigned char *)&head+offset);
	if(offset < 16+PRGsize[0])return PRGptr[0][offset-16];
	if(offset < 16+PRGsize[0]+CHRsize[0])return CHRptr[0][offset-16-PRGsize[0]];
	return -1;
}

static int WriteFileData(uint32 addr,int data){
	if (addr < 16) MessageBox(hMemView, "You can't edit ROM header here, however you can use iNES Header Editor to edit the header if it's an iNES format file.", "Sorry", MB_OK | MB_ICONERROR);
	if((addr >= 16) && (addr < PRGsize[0]+16)) *(uint8 *)(GetNesPRGPointer(addr-16)) = data;
	if((addr >= PRGsize[0]+16) && (addr < CHRsize[0]+PRGsize[0]+16)) *(uint8 *)(GetNesCHRPointer(addr-16-PRGsize[0])) = data;

	return 0;
}

static int GetRomFileSize(){ //todo: fix or remove this?
	return 0;
}

void SaveRomAs()
{
	const char filter[]="NES ROM file (*.nes)\0*.nes\0All Files (*.*)\0*.*\0\0";
	char nameo[2048];

	OPENFILENAME ofn;
	memset(&ofn,0,sizeof(ofn));
	ofn.lStructSize=sizeof(ofn);
	ofn.hInstance=fceu_hInstance;
	ofn.lpstrTitle="Save Nes ROM as...";
	ofn.lpstrFilter=filter;
	strcpy(nameo, mass_replace(GetRomName(), "|", ".").c_str());
	ofn.lpstrFile=nameo;
	ofn.lpstrDefExt="nes";
	ofn.nMaxFile=256;
	ofn.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_HIDEREADONLY;
	ofn.hwndOwner = hMemView;
	if (GetSaveFileName(&ofn))
		iNesSaveAs(nameo);
}

int LoadTable(const char* nameo)
{
	char str[50];
	FILE *FP;
	int i, line, charcode1, charcode2;
	
	for(i = 0;i < 256;i++){
		chartable[i] = 0;
	}

	FP = fopen(nameo,"r");
	line = 0;
	while((fgets(str, 45, FP)) != NULL){/* get one line from the file */
		line++;

		if(strlen(str) < 3)continue;

		charcode1 = charcode2 = -1;

		if((str[0] >= 'a') && (str[0] <= 'f')) charcode1 = str[0]-('a'-0xA);
		if((str[0] >= 'A') && (str[0] <= 'F')) charcode1 = str[0]-('A'-0xA);
		if((str[0] >= '0') && (str[0] <= '9')) charcode1 = str[0]-'0';

		if((str[1] >= 'a') && (str[1] <= 'f')) charcode2 = str[1]-('a'-0xA);
		if((str[1] >= 'A') && (str[1] <= 'F')) charcode2 = str[1]-('A'-0xA);
		if((str[1] >= '0') && (str[1] <= '9')) charcode2 = str[1]-'0';

		if(charcode1 == -1){
			UnloadTableFile();
			fclose(FP);
			return line; //we have an error getting the first input
		}

		if(charcode2 != -1) charcode1 = (charcode1<<4)|charcode2;

		for(i = 0;i < (int)strlen(str);i++)if(str[i] == '=')break;

		if(i == strlen(str)){
			UnloadTableFile();
			fclose(FP);
			return line; //error no '=' found
		}

		i++;
		//ORing i with 32 just converts it to lowercase if it isn't
		if(((str[i]|32) == 'r') && ((str[i+1]|32) == 'e') && ((str[i+2]|32) == 't'))
			charcode2 = 0x0D;
		else charcode2 = str[i];

		chartable[charcode1] = charcode2;
	}
	TableFileLoaded = 1;
	fclose(FP);
	return -1;
}

//should return -1, otherwise returns the line number it had the error on
int LoadTableFile()
{
	const char filter[]="Table Files (*.TBL)\0*.tbl\0All Files (*.*)\0*.*\0\0";
	char nameo[2048];
	OPENFILENAME ofn;
	memset(&ofn,0,sizeof(ofn));
	ofn.lStructSize=sizeof(ofn);
	ofn.hInstance=fceu_hInstance;
	ofn.lpstrTitle="Load Table File...";
	ofn.lpstrFilter=filter;
	nameo[0]=0;
	ofn.lpstrFile=nameo;
	ofn.nMaxFile=256;
	ofn.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_HIDEREADONLY;
	ofn.hwndOwner = hMemView;
	if(!GetOpenFileName(&ofn))return -1;

	int result = LoadTable(nameo);
	return result;
}

void UnloadTableFile(){
	for(int i = 0;i < 256;i++){
		int j = i;
		if(j < 0x20) j = 0x2E;
//		if(j > 0x7e) j = 0x2E;
		chartable[i] = j;
	}
	TableFileLoaded = 0;
	return;
}
void UpdateMemoryView(int draw_all)
{
	if (!hMemView) return;
	const int MemFontWidth = debugSystem->HexeditorFontWidth + HexCharSpacing;
	const int MemFontHeight = debugSystem->HexeditorFontHeight + HexRowHeightBorder;
	const char hex[] = "0123456789ABCDEF";
	const COLORREF CBackColor = MKRGB(HexBack);
	const COLORREF CForeColor = MKRGB(HexFore);
	int i, j;
	int byteValue;
	int byteHighlightingValue;
	char str[100];

	if (PreviousCurOffset != CurOffset)
		resetHighlightingActivityLog();

	for (i = CurOffset; i < CurOffset + DataAmount; i += 16)
	{
		const int MemLineRow = MemFontHeight * ((i - CurOffset) / 16);
		int MemLinePos = 8 * MemFontWidth;
		int pos = i - CurOffset;
		if ((PreviousCurOffset != CurOffset) || draw_all)
		{
			SetBkColor(HDataDC, CBackColor);		//addresses back color
			if (i < MaxSize)
				SetTextColor(HDataDC, MKRGB(HexAddr));	//addresses text color #000000 = black, #FFFFFF = white
			else
				SetTextColor(HDataDC, MKRGB(HexBound));	//addresses out of bounds
			sprintf(str, "%06X:                                                         :", i);
			ExtTextOut(HDataDC, 0, MemLineRow, NULL, NULL, str, strlen(str), NULL);
		}
		for (j = 0; j < 16; j++)
		{
			byteValue = GetMemViewData(i + j);
			if (MemView_HighlightActivity && ((PreviousValues[pos] != byteValue) && (PreviousValues[pos] != PREVIOUS_VALUE_UNDEFINED)))
				byteHighlightingValue = HighlightedBytes[pos] = MemView_HighlightActivity_FadingPeriod;
			else
				byteHighlightingValue = HighlightedBytes[pos];

			if ((CursorEndAddy == -1) && (CursorStartAddy == i + j))
			{
				//print up single highlighted text
				if (TempData != PREVIOUS_VALUE_UNDEFINED)
				{
					// User is typing New Data
					// 1st nibble
					SetBkColor(HDataDC, RGB(255, 255, 255));
					SetTextColor(HDataDC, RGB(255, 0, 0));
					str[0] = hex[(byteValue >> 4) & 0xF];
					str[1] = 0;
					ExtTextOut(HDataDC, MemLinePos, MemLineRow, NULL, NULL, str, 1, NULL);
					// 2nd nibble
					SetBkColor(HDataDC, CForeColor);
					SetTextColor(HDataDC, CBackColor);
					str[0] = hex[(byteValue >> 0) & 0xF];
					str[1] = 0;
					ExtTextOut(HDataDC, MemLinePos + MemFontWidth, MemLineRow, NULL, NULL, str, 1, NULL);
				}
				else
				{
					// Single Byte highlight
					// 1st nibble
					SetBkColor(HDataDC, EditingText ? MKRGB(HexHlShdBack) : MKRGB(HexHlBack));
					SetTextColor(HDataDC, EditingText ? MKRGB(HexHlShdFore) : MKRGB(HexHlFore));
					str[0] = hex[(byteValue >> 4) & 0xF];
					str[1] = 0;
					ExtTextOut(HDataDC, MemLinePos, MemLineRow, NULL, NULL, str, 1, NULL);
					// 2nd nibble
					SetBkColor(HDataDC, HexBGColorList[pos]);
					SetTextColor(HDataDC, HexTextColorList[pos]);
					str[0] = hex[(byteValue >> 0) & 0xF];
					str[1] = 0;
					ExtTextOut(HDataDC, MemLinePos + MemFontWidth, MemLineRow, NULL, NULL, str, 1, NULL);
				}

				// single address highlight - right column
				SetBkColor(HDataDC, EditingText ? MKRGB(HexHlBack) : MKRGB(HexHlShdBack));
				SetTextColor(HDataDC, EditingText ? MKRGB(HexHlFore) : MKRGB(HexHlShdFore));
				str[0] = chartable[byteValue];
				if ((u8)str[0] < 0x20) str[0] = 0x2E;
//				if ((u8)str[0] > 0x7e) str[0] = 0x2E;
				str[1] = 0;
				ExtTextOut(HDataDC, (59 + j) * MemFontWidth, MemLineRow, NULL, NULL, str, 1, NULL);

				PreviousValues[pos] = PREVIOUS_VALUE_UNDEFINED; //set it to redraw this one next time
			}
			else if (draw_all || (PreviousValues[pos] != byteValue) || byteHighlightingValue)
			{
				COLORREF hexTmpColor = HexTextColorList[pos];
				COLORREF ansiTmpColor = AnsiTextColorList[pos];
				// print up normal text
				if (byteHighlightingValue)
				{
					// fade out 1 step
					if (MemView_HighlightActivity_FadeWhenPaused || !FCEUI_EmulationPaused() || JustFrameAdvanced)
						byteHighlightingValue = (--HighlightedBytes[pos]);

					if (byteHighlightingValue > 0)
					{
						// if the byte was changed in current frame, use brightest color, even if the "fading period" demands different color
						// also use the last color if byteHighlightingValue points outside the array of predefined colors
						if (byteHighlightingValue == MemView_HighlightActivity_FadingPeriod - 1 || byteHighlightingValue >= HIGHLIGHT_ACTIVITY_NUM_COLORS)
						{
							hexTmpColor = highlightActivityColors[HIGHLIGHT_ACTIVITY_NUM_COLORS - 1];
							ansiTmpColor = highlightActivityColors[HIGHLIGHT_ACTIVITY_NUM_COLORS - 1];
						}
						else
						{
							hexTmpColor = highlightActivityColors[byteHighlightingValue];
							ansiTmpColor = highlightActivityColors[byteHighlightingValue];
						}
					}
				}
				SetBkColor(HDataDC, HexBGColorList[pos]);
				SetTextColor(HDataDC, hexTmpColor);
				str[0] = hex[(byteValue >> 4) & 0xF];
				str[1] = hex[(byteValue >> 0) & 0xF];
				str[2] = 0;
				ExtTextOut(HDataDC, MemLinePos, MemLineRow, NULL, NULL, str, 2, NULL);

				SetBkColor(HDataDC, AnsiBGColorList[pos]);
				SetTextColor(HDataDC, ansiTmpColor);
				str[0] = chartable[byteValue];
				if ((u8)str[0] < 0x20) str[0] = 0x2E;
//				if ((u8)str[0] > 0x7e) str[0] = 0x2E;
				str[1] = 0;
				ExtTextOut(HDataDC, (59 + j) * MemFontWidth, MemLineRow, NULL, NULL, str, 1, NULL);

				PreviousValues[pos] = byteValue;
			}
			MemLinePos += MemFontWidth * 3;
			pos++;
		}
	}

	SetTextColor(HDataDC, RGB(0, 0, 0));
	SetBkColor(HDataDC, RGB(0, 0, 0));
	MoveToEx(HDataDC, 0, 0, NULL);
	PreviousCurOffset = CurOffset;
	return;
}

char* EditString[4] = {"RAM","PPU","OAM","ROM"};

void UpdateCaption()
{
	static char str[1000];

	if (CursorEndAddy == -1)
	{
		if (EditingMode == MODE_NES_FILE)
		{
			if (CursorStartAddy < 16)
				sprintf(str, "Hex Editor - ROM Header: 0x%X", CursorStartAddy);
			else if (CursorStartAddy - 16 < (int)PRGsize[0])
				sprintf(str, "Hex Editor - (PRG) ROM: 0x%X", CursorStartAddy);
			else if (CursorStartAddy - 16 - PRGsize[0] < (int)CHRsize[0])
				sprintf(str, "Hex Editor - (CHR) ROM: 0x%X", CursorStartAddy);
		} else
		{
			sprintf(str, "Hex Editor - %s: 0x%X", EditString[EditingMode], CursorStartAddy);
		}

		if (EditingMode == MODE_NES_MEMORY && symbDebugEnabled)
		{
			// when watching RAM we may as well see Symbolic Debug names
			loadNameFiles();
			Name* node = findNode(getNamesPointerForAddress(CursorStartAddy), CursorStartAddy);
			if (node && node->name)
			{
				strcat(str, " - ");
				strcat(str, node->name);
			}
			for (int i = 0; i < RegNameCount; i++) {
				if (!symbRegNames) break;
				int test = 0;
				sscanf(RegNames[i].offset, "$%4x", &test);
				if (test == CursorStartAddy) {
					strcat(str, " - ");
					strcat(str, RegNames[i].name);
				}
			}
		}
	} else
	{
		sprintf(str, "Hex Editor - %s: 0x%X - 0x%X (0x%X)",
			EditString[EditingMode], CursorStartAddy, CursorEndAddy, CursorEndAddy - CursorStartAddy + 1);
	}
	SetWindowText(hMemView,str);
	return;
}

int GetMemViewData(uint32 i)
{
	switch (EditingMode)
	{
		case MODE_NES_MEMORY:
			return GetMem(i);
		case MODE_NES_PPU:
			i &= 0x3FFF;
			if (i < 0x2000)return VPage[(i) >> 10][(i)];
			//NSF PPU Viewer crash here (UGETAB) (Also disabled by 'MaxSize = 0x2000')
			if (GameInfo->type == GIT_NSF)
				return (0);
			else
			{
				if (i < 0x3F00)
					return vnapage[(i >> 10) & 0x3][i & 0x3FF];
				return READPAL_MOTHEROFALL(i & 0x1F);
			}
			break;
		case MODE_NES_OAM:
			return SPRAM[i & 0xFF];
		case MODE_NES_FILE:
			//todo: use getfiledata() here
			if (i < 16) return *((unsigned char *)&head + i);
			if (i < 16 + PRGsize[0])return PRGptr[0][i - 16];
			if (i < 16 + PRGsize[0] + CHRsize[0])return CHRptr[0][i - 16 - PRGsize[0]];
	}
	return 0;
}

void UpdateColorTable()
{
	UNDOSTRUCT *tmp; //mbg merge 7/18/06 removed struct qualifier
	int i,j;
	if(!hMemView)return;
	for(i = 0;i < DataAmount;i++)
	{
		if((i+CurOffset >= CursorStartAddy) && (i+CurOffset <= CursorEndAddy))
		{
			CurBGColorList[i] = MKRGB(HexHlBack);			//Highlighter color bg	- 2 columns
			DimBGColorList[i] = MKRGB(HexHlShdBack);
			CurTextColorList[i] = MKRGB(HexHlFore);		//Highlighter color text - 2 columns
			DimTextColorList[i] = MKRGB(HexHlShdFore);
			continue;
		}

		CurBGColorList[i] = MKRGB(HexBack);			//Regular color bb - 2columns
		DimBGColorList[i] = MKRGB(HexBack);			//Regular color bb - 2columns
		CurTextColorList[i] = MKRGB(HexFore);		//Regular color text - 2 columns
		DimTextColorList[i] = MKRGB(HexFore);		//Regular color text - 2 columns
	}

	for (j=0;j<hexBookmarks.bookmarkCount;j++)
	{
		if(hexBookmarks[j].editmode != EditingMode) continue;
		if(((int)hexBookmarks[j].address >= CurOffset) && ((int)hexBookmarks[j].address < CurOffset+DataAmount))
		{
			CurTextColorList[hexBookmarks[j].address - CurOffset] = MKRGB(HexBookmark); // Green for Bookmarks
			DimTextColorList[hexBookmarks[j].address - CurOffset] = MKRGB(HexBookmark); // Green for Bookmarks
		}
	}

	switch (EditingMode)
	{
		case MODE_NES_MEMORY:
			for (int a = CurOffset; a < CurOffset + DataAmount; ++a)
				if (FCEUI_FindCheatMapByte(a))
				{
					CurTextColorList[a - CurOffset] = MKRGB(HexFreeze);
					DimTextColorList[a - CurOffset] = MKRGB(HexFreeze);
				}
			break;
		case MODE_NES_FILE:
			if (cdloggerdataSize)
			{
				int temp_offset;
				for (i = 0; i < DataAmount; i++)
				{
					temp_offset = CurOffset + i - 16;	// (minus iNES header)
					if (temp_offset >= 0)
					{
						if ((unsigned int)temp_offset < cdloggerdataSize)
						{
							// PRG
							if ((cdloggerdata[temp_offset] & 3) == 3)
							{
								// the byte is both Code and Data - green
								CurTextColorList[i] = MKRGB(CdlCodeData);
								DimTextColorList[i] = MKRGB(CdlCodeData);
							}
							else if ((cdloggerdata[temp_offset] & 3) == 1)
							{
								// the byte is Code - dark-yellow
								CurTextColorList[i] = MKRGB(CdlCode);
								DimTextColorList[i] = MKRGB(CdlCode);
							}
							else if ((cdloggerdata[temp_offset] & 3) == 2)
							{
								// the byte is Data - blue/cyan
								if (cdloggerdata[temp_offset] & 0x40)
								{
									// PCM data - cyan
									CurTextColorList[i] = MKRGB(CdlPcm);
									DimTextColorList[i] = MKRGB(CdlPcm);
								}
								else
								{
									// non-PCM data - blue
									CurTextColorList[i] = MKRGB(CdlData);
									DimTextColorList[i] = MKRGB(CdlData);
								}
							}
						}
						else
						{
							temp_offset -= cdloggerdataSize;
							if (((unsigned int)temp_offset < cdloggerVideoDataSize))
							{
								// CHR
								if ((cdloggervdata[temp_offset] & 3) == 3)
								{
									// the byte was both rendered and read programmatically - light-green
									CurTextColorList[i] = MKRGB(CdlRenderRead);
									DimTextColorList[i] = MKRGB(CdlRenderRead);
								}
								else if ((cdloggervdata[temp_offset] & 3) == 1)
								{
									// the byte was rendered - yellow
									CurTextColorList[i] = MKRGB(CdlRender);
									DimTextColorList[i] = MKRGB(CdlRender);
								}
								else if ((cdloggervdata[temp_offset] & 3) == 2)
								{
									// the byte was read programmatically - light-blue
									CurTextColorList[i] = MKRGB(CdlRead);
									DimTextColorList[i] = MKRGB(CdlRead);
								}
							}
						}
					}
				}
			}

			tmp = undo_list;
			while (tmp != 0)
			{
				//if((tmp->addr < CurOffset+DataAmount) && (tmp->addr+tmp->size > CurOffset))
				for (i = tmp->addr; i < tmp->addr + tmp->size; i++) {
					if ((i > CurOffset) && (i < CurOffset + DataAmount))
					{
						CurTextColorList[i - CurOffset] = MKRGB(RomFreeze);
						DimTextColorList[i - CurOffset] = MKRGB(RomFreeze);
					}
				}
				tmp = tmp->last;
			}
			break;
	}

	UpdateMemoryView(1); //anytime the colors change, the memory viewer needs to be completely redrawn
}

int addrtodelete;    // This is a very ugly hackish method of doing this
int cheatwasdeleted; // but it works and that is all that matters here.
int DeleteCheatCallB(char *name, uint32 a, uint8 v, int compare,int s,int type, void *data){  //mbg merge 6/29/06 - added arg
	if(cheatwasdeleted == -1)return 1;
	cheatwasdeleted++;
	if(a == addrtodelete){
		FCEUI_DelCheat(cheatwasdeleted-1);
		cheatwasdeleted = -1;
		return 0;
	}
	return 1;
}

void dumpToFile(const char* buffer, unsigned int size)
{
	char name[513] = {0};

	OPENFILENAME ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize=sizeof(ofn);
	ofn.hInstance=fceu_hInstance;
	ofn.lpstrTitle="Save to file ...";
	ofn.lpstrFilter="Binary File (*.BIN)\0*.bin\0All Files (*.*)\0*.*\0\0";
	strcpy(name, mass_replace(GetRomName(), "|", ".").c_str());
	ofn.lpstrFile=name;
	ofn.lpstrDefExt="bin";
	ofn.nMaxFile=256;
	ofn.Flags=OFN_EXPLORER|OFN_HIDEREADONLY;

	if (GetSaveFileName(&ofn))
	{
		FILE* memfile = fopen(name, "wb");

		if (!memfile || fwrite(buffer, 1, size, memfile) != size)
		{
			MessageBox(0, "Saving failed", "Error", 0);
		}

		if (memfile)
			fclose(memfile);
	}
}

bool loadFromFile(char* buffer, unsigned int size)
{
	char name[513] = { 0 };

	OPENFILENAME ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize=sizeof(ofn);
	ofn.hInstance=fceu_hInstance;
	ofn.lpstrTitle="Save to file ...";
	ofn.lpstrFilter="Binary File (*.BIN)\0*.bin\0All Files (*.*)\0*.*\0\0";
	strcpy(name, mass_replace(GetRomName(), "|", ".").c_str());
	ofn.lpstrFile=name;
	ofn.lpstrDefExt="bin";
	ofn.nMaxFile=256;
	ofn.Flags=OFN_EXPLORER | OFN_FILEMUSTEXIST;

	if (GetOpenFileName(&ofn))
	{
		FILE* memfile = fopen(name, "rb");

		if (!memfile || fread(buffer, 1, size, memfile) != size)
		{
			MessageBox(0, "Load failed", "Error", 0);
			return false;
		}

		if (memfile)
			fclose(memfile);

		return true;
	}
	return false;
}

void UnfreezeAllRam() {

	int i=0;

	char * Cname;
	uint32 Caddr;
	int Ctype;

	// Get last cheat number + 1
	while (FCEUI_GetCheat(i,NULL,NULL,NULL,NULL,NULL,NULL)) {
		i = i + 1;
	}

	// Subtract 1 to be on last cheat
	i = i - 1;

	while (i >= 0) {

		// Since this is automated, only remove unnamed variables, as they
		// would be added by the freeze command. Manual unfreeze should let them
		// make that mistake once or twice, in case they like it that way.
		FCEUI_GetCheat(i,&Cname,&Caddr,NULL,NULL,NULL,&Ctype);
		if ((Cname[0] == '\0') && ((Caddr < 0x2000) || ((Caddr >= 0x6000) && (Caddr < 0x8000))) && (Ctype == 1)) {
			// Already Added, so consider it a success
			FreezeRam(Caddr,-1,1);

		}

		i = i - 1;
	}

	return;
}

void FreezeRam(int address, int mode, int final){
	// mode: -1 == Unfreeze; 0 == Toggle; 1 == Freeze
	if(FrozenAddressCount <= 256 && (address < 0x2000) || ((address >= 0x6000) && (address <= 0x7FFF))){
		//adelikat:  added FrozenAddressCount check to if statement to prevent user from freezing more than 256 address (unfreezing when > 256 crashes)
		addrtodelete = address;
		cheatwasdeleted = 0;

		if (mode == 0 || mode == -1)
		{
			//mbg merge 6/29/06 - added argument
			FCEUI_ListCheats(DeleteCheatCallB, 0);
			if(mode == 0 && cheatwasdeleted != -1)
				FCEUI_AddCheat("", address, GetMem(address), -1, 1);
		}
		else
		{
			//mbg merge 6/29/06 - added argument
			FCEUI_ListCheats(DeleteCheatCallB, 0);
			FCEUI_AddCheat("", address, GetMem(address), -1, 1);
		}

		UpdateCheatsAdded();
		UpdateCheatRelatedWindow();
	}
}

//input is expected to be an ASCII string
void InputData(char *input){
	//CursorEndAddy = -1;
	int addr, i, j, datasize = 0;
	unsigned char *data;
	char inputc;
	//char str[100];
	//mbg merge 7/18/06 added cast:
	data = (uint8 *)malloc(strlen(input) + 1); //it can't be larger than the input string, so use that as the size

	for(i = 0;input[i] != 0;i++){
		if(!EditingText){
			inputc = -1;
			if((input[i] >= 'a') && (input[i] <= 'f')) inputc = input[i]-('a'-0xA);
			if((input[i] >= 'A') && (input[i] <= 'F')) inputc = input[i]-('A'-0xA);
			if((input[i] >= '0') && (input[i] <= '9')) inputc = input[i]-'0';
			if(inputc == -1)continue;

			if(TempData != PREVIOUS_VALUE_UNDEFINED)
			{
				data[datasize++] = inputc|(TempData<<4);
				TempData = PREVIOUS_VALUE_UNDEFINED;
			} else
			{
				TempData = inputc;
			}
		} else {
			for(j = 0;j < 256;j++)if(chartable[j] == input[i])break;
			if(j == 256)continue;
			data[datasize++] = j;
		}
	}

	if(datasize+CursorStartAddy >= MaxSize){ //too big
		datasize = MaxSize-CursorStartAddy;
		//free(data);
		//return;
	}

	if (datasize < 1) return; // avoid adjusting cursor and accidentally writing at end

	//its possible for this loop not to get executed at all
	//	for(addr = CursorStartAddy;addr < datasize+CursorStartAddy;addr++){
	//sprintf(str,"datasize = %d",datasize);
	//MessageBox(hMemView,str, "debug", MB_OK);

	for(i = 0;i < datasize;i++){
		addr = CursorStartAddy+i;

		if (addr >= MaxSize) continue;

		switch(EditingMode)
		{
			case MODE_NES_MEMORY:
				// RAM (system bus)
				BWrite[addr](addr, data[i]);
				break;
			case MODE_NES_PPU:
				// PPU
				addr &= 0x3FFF;
				if (addr < 0x2000)
					VPage[addr >> 10][addr] = data[i]; //todo: detect if this is vrom and turn it red if so
				if ((addr >= 0x2000) && (addr < 0x3F00))
					vnapage[(addr >> 10) & 0x3][addr & 0x3FF] = data[i]; //todo: this causes 0x3000-0x3f00 to mirror 0x2000-0x2f00, is this correct?
				if ((addr >= 0x3F00) && (addr < 0x3FFF))
					PalettePoke(addr, data[i]);
				break;
			case MODE_NES_OAM:
				addr &= 0xFF;
				SPRAM[addr] = data[i];
				break;
			case MODE_NES_FILE:
				// ROM
				ApplyPatch(addr, 1, &data[i]);
				break;
		}
	}
	CursorStartAddy+=datasize;
	CursorEndAddy=-1;
	if(CursorStartAddy >= MaxSize)CursorStartAddy = MaxSize-1;

	free(data);
	ChangeMemViewFocus(EditingMode, CursorStartAddy, -1);
	UpdateColorTable();
	return;
}


void ChangeMemViewFocus(int newEditingMode, int StartOffset,int EndOffset){

	if(!hMemView)DoMemView();
	if(EditingMode != newEditingMode)
		MemViewCallB(hMemView,WM_COMMAND,MENU_MV_VIEW_RAM+newEditingMode,0); //let the window handler change this for us

	if((EndOffset == StartOffset) || (EndOffset == -1)){
		CursorEndAddy = -1;
		CursorStartAddy = StartOffset;
	} else {
		CursorStartAddy = std::min(StartOffset,EndOffset);
		CursorEndAddy = std::max(StartOffset,EndOffset);
	}
	CursorDragPoint = -1;


	if(std::min(StartOffset,EndOffset) >= MaxSize)return; //this should never happen

	if(StartOffset < CurOffset){
		CurOffset = (StartOffset/16)*16;
	}

	if(StartOffset >= CurOffset+DataAmount){
		CurOffset = ((StartOffset/16)*16)-DataAmount+0x10;
		if(CurOffset < 0)CurOffset = 0;
	}

	SetFocus(hMemView);

	SCROLLINFO si;
	ZeroMemory(&si, sizeof(SCROLLINFO));
	si.fMask = SIF_POS;
	si.cbSize = sizeof(SCROLLINFO);
	si.nPos = CurOffset / 16;
	SetScrollInfo(hMemView,SB_VERT,&si,TRUE);
	UpdateCaption();
	UpdateColorTable();
	return;
}


int GetHexScreenCoordx(int offset)
{
	return (8 * (debugSystem->HexeditorFontWidth + HexCharSpacing)) + ((offset % 16) * 3 * (debugSystem->HexeditorFontWidth + HexCharSpacing)); //todo: add Curoffset to this and to below function
}

int GetHexScreenCoordy(int offset)
{
	return (offset - CurOffset) / 16 * (debugSystem->HexeditorFontHeight + HexRowHeightBorder);
}

//0000E0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  : ................

//if the mouse is in the text field, this function will set AddyWasText to 1 otherwise it is 0
//if the mouse wasn't in any range, this function returns -1
int GetAddyFromCoord(int x,int y)
{
	int MemFontWidth = debugSystem->HexeditorFontWidth + HexCharSpacing;
	int MemFontHeight = debugSystem->HexeditorFontHeight + HexRowHeightBorder;

	if(y < 0)y = 0;
	if(x < 8*MemFontWidth)x = 8*MemFontWidth+1;

	if(y > DataAmount*MemFontHeight) return -1;

	if(x < 55*MemFontWidth){
		AddyWasText = 0;
		return ((y/MemFontHeight)*16)+((x-(8*MemFontWidth))/(3*MemFontWidth))+CurOffset;
	}

	if((x > 59*MemFontWidth) && (x < 75*MemFontWidth)){
		AddyWasText = 1;
		return ((y/MemFontHeight)*16)+((x-(59*MemFontWidth))/(MemFontWidth))+CurOffset;
	}

	return -1;
}

void AutoScrollFromCoord(int x,int y)
{
	SCROLLINFO si;
	if(y < 0){
		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.fMask = SIF_ALL;
		si.cbSize = sizeof(SCROLLINFO);
		GetScrollInfo(hMemView,SB_VERT,&si);
		si.nPos += y / 16;
		if (si.nPos < si.nMin) si.nPos = si.nMin;
		if ((si.nPos+(int)si.nPage) > si.nMax) si.nPos = si.nMax-si.nPage;
		CurOffset = si.nPos*16;
		if (CurOffset + DataAmount >= MaxSize) CurOffset = MaxSize - DataAmount;
		if (CurOffset < 0) CurOffset = 0;
		SetScrollInfo(hMemView,SB_VERT,&si,TRUE);
		return;
	}

	if(y > ClientHeight){
		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.fMask = SIF_ALL;
		si.cbSize = sizeof(SCROLLINFO);
		GetScrollInfo(hMemView,SB_VERT,&si);
		si.nPos -= (ClientHeight-y) / 16;
		if (si.nPos < si.nMin) si.nPos = si.nMin;
		if ((si.nPos+(int)si.nPage) > si.nMax) si.nPos = si.nMax-si.nPage;
		CurOffset = si.nPos*16;
		if (CurOffset + DataAmount >= MaxSize) CurOffset = MaxSize - DataAmount;
		if (CurOffset < 0) CurOffset = 0;
		SetScrollInfo(hMemView,SB_VERT,&si,TRUE);
		return;
	}
}

void KillMemView()
{
	if (hMemView)
	{
		ReleaseDC(hMemView, HDataDC);
		DestroyWindow(hMemView);
		UnregisterClass("MEMVIEW", fceu_hInstance);
		hMemView = NULL;
		hMemFind = NULL;
		free(CurTextColorList);
		free(DimTextColorList);
		free(CurBGColorList);
		free(DimBGColorList);
		if (EditingMode == MODE_NES_MEMORY)
			ReleaseCheatMap();
		for (int i = 0; i < sizeof(colormenu) / sizeof(colormenu[0]); ++i)
			for (int j = 0; j < colormenu[i].size; ++j)
			{
				DeleteObject(colormenu[i].items[j].bitmap);
				colormenu[i].items[j].bitmap = NULL;
			}
	}
}

int GetMaxSize(int EditingMode)
{
	switch (EditingMode)
	{
		case MODE_NES_MEMORY: return 0x10000;
		case MODE_NES_PPU: return (GameInfo->type == GIT_NSF ? 0x2000 : 0x4000);
		case MODE_NES_OAM: return 0x100;
		case MODE_NES_FILE: return 16 + CHRsize[0] + PRGsize[0]; //todo: add trainer size
	}
	return 0;
}


LRESULT CALLBACK MemViewCallB(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static PAINTSTRUCT ps;

	// int tempAddy;
	const int MemFontWidth = debugSystem->HexeditorFontWidth;
	const int MemFontHeight = debugSystem->HexeditorFontHeight + HexRowHeightBorder;

//	char str[100];
	extern int debuggerWasActive;

	static int tmpStartAddy = -1;
	static int tmpEndAddy = -1;

	switch (message) {

	case WM_ENTERMENULOOP:return 0;
	case WM_INITMENUPOPUP:
		if(undo_list != 0)EnableMenuItem(GetMenu(hMemView),MENU_MV_EDIT_UNDO,MF_BYCOMMAND | MF_ENABLED);
		else EnableMenuItem(GetMenu(hMemView),MENU_MV_EDIT_UNDO,MF_BYCOMMAND | MF_GRAYED);

		if(TableFileLoaded)EnableMenuItem(GetMenu(hMemView),MENU_MV_FILE_UNLOAD_TBL,MF_BYCOMMAND | MF_ENABLED);
		else EnableMenuItem(GetMenu(hMemView),MENU_MV_FILE_UNLOAD_TBL,MF_BYCOMMAND | MF_GRAYED);

		return 0;

	case WM_CREATE:
	{
		SetWindowPos(hwnd, 0, MemView_wndx, MemView_wndy, MemViewSizeX, MemViewSizeY, SWP_NOZORDER | SWP_NOOWNERZORDER);

		debuggerWasActive = 1;
		HDataDC = GetDC(hwnd);
		SelectObject(HDataDC, debugSystem->hHexeditorFont);
		SetTextAlign(HDataDC, TA_NOUPDATECP | TA_TOP | TA_LEFT);

		//		TEXTMETRIC textMetric;
		//		GetTextMetrics (HDataDC, &textMetric);

		MaxSize = 0x10000;
		//Allocate Memory for color lists
		DataAmount = 0x100;
		//mbg merge 7/18/06 added casts:
		CurTextColorList = (COLORREF*)malloc(DataAmount*sizeof(COLORREF));
		CurBGColorList = (COLORREF*)malloc(DataAmount*sizeof(COLORREF));
		DimTextColorList = (COLORREF*)malloc(DataAmount*sizeof(COLORREF));
		DimBGColorList = (COLORREF*)malloc(DataAmount*sizeof(COLORREF));
		HexTextColorList = CurTextColorList;
		HexBGColorList = CurBGColorList;
		AnsiTextColorList = DimTextColorList;
		AnsiBGColorList = DimBGColorList;
		PreviousValues = (int*)malloc(DataAmount*sizeof(int));
		HighlightedBytes = (unsigned int*)malloc(DataAmount*sizeof(unsigned int));
		resetHighlightingActivityLog();
		EditingText = CurOffset = 0;
		EditingMode = MODE_NES_MEMORY;
		CreateCheatMap();

		//set the default table
		UnloadTableFile();
		UpdateColorTable(); //draw it

		// update menus
		HMENU menu = GetMenu(hwnd);

		for (int i = MODE_NES_MEMORY; i <= MODE_NES_FILE; i++)
			if (EditingMode == i) {
				CheckMenuRadioItem(menu, MENU_MV_VIEW_RAM, MENU_MV_VIEW_ROM, MENU_MV_VIEW_RAM, MF_BYCOMMAND);
				break;
			}
		CheckMenuItem(menu, ID_HIGHLIGHTING_HIGHLIGHT_ACTIVITY, (MemView_HighlightActivity) ? MF_CHECKED : MF_UNCHECKED);
		CheckMenuItem(menu, ID_HIGHLIGHTING_FADEWHENPAUSED, (MemView_HighlightActivity_FadeWhenPaused) ? MF_CHECKED : MF_UNCHECKED);

		updateBookmarkMenus(GetSubMenu(menu, BOOKMARKS_SUBMENU_POS));

		HMENU hilightMenu = GetSubMenu(menu, HIGHLIGHTING_SUBMENU_POS);
		for (int i = 0; i < sizeof(colormenu) / sizeof(colormenu[0]); ++i)
			for (int j = 0; j < colormenu[i].size; ++j)
				InsertColorMenu(hwnd, GetSubMenu(hilightMenu, colormenu[i].sub), &colormenu[i].items[j], j, colormenu[i].base_id + j);
	}
	return 0;
	case WM_PAINT:
		BeginPaint(hwnd, &ps);
		EndPaint(hwnd, &ps);
		UpdateMemoryView(1);
		return 0;

	case WM_DROPFILES:
	{
		UINT len;
		char *ftmp;

		len=DragQueryFile((HDROP)wParam,0,0,0)+1; 
		if(ftmp = (char*)malloc(len)) 
		{
			DragQueryFile((HDROP)wParam,0,ftmp,len); 
			string fileDropped = ftmp;
			//adelikat:  Drag and Drop only checks file extension, the internal functions are responsible for file error checking
			//-------------------------------------------------------
			//Check if .tbl
			//-------------------------------------------------------
			if (!(fileDropped.find(".tbl") == string::npos) && (fileDropped.find(".tbl") == fileDropped.length()-4))
			{
				LoadTable(fileDropped.c_str());
			}
			else
			{
				std::string str = "Could not open " + fileDropped;
				MessageBox(hwnd, str.c_str(), "File error", 0);
			}
			free(ftmp);
		}            
	}
	break;
	case WM_VSCROLL:
	{
		SCROLLINFO si;
		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.fMask = SIF_ALL;
		si.cbSize = sizeof(SCROLLINFO);
		GetScrollInfo(hwnd, SB_VERT, &si);
		switch (LOWORD(wParam)) {
			case SB_ENDSCROLL:
			case SB_TOP:
			case SB_BOTTOM: break;
			case SB_LINEUP: si.nPos--; break;
			case SB_LINEDOWN:si.nPos++; break;
			case SB_PAGEUP: si.nPos -= si.nPage; break;
			case SB_PAGEDOWN: si.nPos += si.nPage; break;
			case SB_THUMBPOSITION: //break;
			case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
		}
		if (si.nPos < si.nMin) si.nPos = si.nMin;
		if ((si.nPos + (int)si.nPage) > si.nMax) si.nPos = si.nMax - si.nPage; //mbg merge 7/18/06 added cast
		CurOffset = si.nPos * 16;
		SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
		UpdateColorTable();
		return 0;
	}
	case WM_CHAR:
		if(GetKeyState(VK_CONTROL) & 0x8000) return 0; //prevents input when pressing ctrl+c
		char c[2];
		c[0] = (char)(wParam&0xFF);
		c[1] = 0;
		InputData(c);
		UpdateColorTable();
		UpdateCaption();
		return 0;

	case WM_KEYDOWN:
	{

		if (wParam == VK_DOWN || wParam == VK_UP || wParam == VK_RIGHT || wParam == VK_LEFT || wParam == VK_PRIOR || wParam == VK_NEXT || wParam == VK_HOME || wParam == VK_END)
		{
			if (GetKeyState(VK_SHIFT) & 0x8000)
			{
				// Shift+ArrowKeys to select a range data
				if (CursorEndAddy == -1)
				{
					tmpStartAddy = CursorStartAddy;
					tmpEndAddy = CursorStartAddy;
				}
				if (wParam == VK_RIGHT)
				{
					if (tmpEndAddy >= MaxSize - 1)
						return 0;
					++tmpEndAddy;
				}
				else if (wParam == VK_LEFT)
				{
					if (tmpEndAddy <= 0)
						return 0;
					--tmpEndAddy;
				}
				else if (wParam == VK_DOWN)
				{	
					if (tmpEndAddy >= MaxSize - 1)
						return 0;
					tmpEndAddy += 16;
				}
				else if (wParam == VK_UP)
				{
					if (tmpEndAddy <= 0)
						return 0;
					tmpEndAddy -= 16;
				}
				else if (wParam == VK_PRIOR)
				{
					if (tmpEndAddy <= 0)
						return 0;
					tmpEndAddy -= DataAmount;
					CurOffset -= DataAmount;
				}
				else if (wParam == VK_NEXT)
				{
					if (tmpEndAddy >= MaxSize - 1)
						return 0;
					tmpEndAddy += DataAmount;
					CurOffset += DataAmount;
				}
				else if (wParam == VK_HOME)
				{
					if (tmpEndAddy <= 0)
						return 0;
					if (GetKeyState(VK_CONTROL) & 0x8000)
						tmpEndAddy = 0;
					else
						tmpEndAddy = tmpEndAddy / 16 * 16;
				}
				else if (wParam == VK_END)
				{
					if (tmpEndAddy >= MaxSize - 1)
						return 0;
					if (GetKeyState(VK_CONTROL) & 0x8000)
						tmpEndAddy = MaxSize - 1;
					else 
						tmpEndAddy = tmpEndAddy / 16 * 16 + 15;
				}

				if (tmpEndAddy < 0)
					tmpEndAddy = 0;
				else if (tmpEndAddy >= MaxSize)
					tmpEndAddy = MaxSize - 1;
				if (tmpStartAddy < 0)
					tmpStartAddy = 0;
				else if (tmpStartAddy >= MaxSize)
					tmpStartAddy = MaxSize - 1;

				if (tmpEndAddy < tmpStartAddy)
				{
					CursorStartAddy = tmpEndAddy;
					CursorEndAddy = tmpStartAddy;
					if (CursorStartAddy < CurOffset)
						CurOffset = CursorStartAddy / 16 * 16;
					else if (CursorStartAddy >= CurOffset + DataAmount)
						CurOffset = CursorStartAddy / 16 * 16 - DataAmount + 0x10;
				}
				else if (tmpEndAddy > tmpStartAddy)
				{
					CursorStartAddy = tmpStartAddy;
					CursorEndAddy = tmpEndAddy;
					if (CursorEndAddy < CurOffset)
						CurOffset = CursorEndAddy / 16 * 16;
					else if (CursorEndAddy > CurOffset + DataAmount - 0x10)
						CurOffset = CursorEndAddy / 16 * 16 - DataAmount + 0x10;
				}
				else
				{
					CursorStartAddy = tmpStartAddy;
					CursorEndAddy = -1;
				}
			}
			else if (GetKeyState(VK_CONTROL) & 0x8000)
			{
				if (wParam == VK_UP)
				{
					if (CurOffset <= 0)
						return 0;
					CurOffset -= 16;
					if (CursorEndAddy == -1 && CursorStartAddy >= CurOffset + DataAmount)
						CursorStartAddy -= 16;
				}
				else if (wParam == VK_DOWN)
				{
					if (CurOffset >= (MaxSize - DataAmount) / 16 * 16)
						return 0;
					CurOffset += 16;
					if (CursorEndAddy == -1 && CursorStartAddy < CurOffset)
						CursorStartAddy += 16;
				}
				else if (wParam == VK_PRIOR)
				{
					if (CurOffset <= 0)
						return 0;
					CurOffset -= DataAmount;
				}
				else if (wParam == VK_NEXT)
				{
					if (CurOffset >= (MaxSize - DataAmount) / 16 * 16)
						return 0;
					CurOffset += DataAmount;
				}
				else if (wParam == VK_HOME)
				{
					if (CursorStartAddy == 0 && CursorEndAddy == -1 && CurOffset == 0)
						return 0;
					CursorStartAddy = 0;
					CursorEndAddy = -1;
					CurOffset = 0;
				}
				else if (wParam == VK_END)
				{
					if (CursorStartAddy == 0 && CursorEndAddy == -1 && CurOffset ==(MaxSize - DataAmount) / 16 * 16)
						return 0;
					CurOffset = (MaxSize - DataAmount) / 16 * 16;
					CursorStartAddy = MaxSize - 1;
					CursorEndAddy = -1;
				}

				TempData = PREVIOUS_VALUE_UNDEFINED;
			}
			else
			{
				// Move the cursor

				if (wParam == VK_RIGHT)
				{
					if (CursorStartAddy >= MaxSize - 1)
						return 0;
					CursorStartAddy++;
				}
				else if (wParam == VK_DOWN)
				{
					if (CursorStartAddy >= MaxSize - 1)
						return 0;
					CursorStartAddy += 16;
				}
				else if (wParam == VK_UP)
				{
					if (CursorStartAddy <= 0)
						return 0;
					CursorStartAddy -= 16;
				}
				else if (wParam == VK_LEFT)
				{
					if (CursorStartAddy <= 0)
						return 0;
					CursorStartAddy--;
				} else if (wParam == VK_PRIOR)
				{
					if (CurOffset <= 0)
						return 0;
					CurOffset -= DataAmount;
					CursorStartAddy -= DataAmount;
				}
				else if (wParam == VK_NEXT)
				{
					if (CurOffset >= (MaxSize - DataAmount) / 16 * 16)
						return 0;
					CurOffset += DataAmount;
					CursorStartAddy += DataAmount;
				}
				else if (wParam == VK_HOME)
				{
					if (CurOffset <= 0)
						return 0;
					CurOffset = 0;
					CursorStartAddy = 0;
				}
				else if (wParam == VK_END) {
					if (CurOffset >= (MaxSize - DataAmount) / 16 * 16)
						CurOffset = (MaxSize - DataAmount) / 16 * 16;
					CursorStartAddy = MaxSize - 1;
				}

				CursorEndAddy = -1;
				if (CursorStartAddy < CurOffset)
					CurOffset = (CursorStartAddy / 16) * 16;
				if (CursorStartAddy > CurOffset + DataAmount - 0x10)
					CurOffset = (CursorStartAddy - DataAmount + 0x10) / 16 * 16;
			}
			TempData = PREVIOUS_VALUE_UNDEFINED;


			// Cursor Out of bound check
			if (CursorStartAddy < 0)
				CursorStartAddy = 0;
			if (CursorStartAddy >= MaxSize)
				CursorStartAddy = MaxSize - 1;
			if (CursorEndAddy >= MaxSize)
				CursorEndAddy = MaxSize - 1;
			if (CursorEndAddy == CursorStartAddy)
				CursorEndAddy = -1;
			if (CurOffset + DataAmount >= MaxSize)
				CurOffset = (MaxSize - DataAmount) / 16 * 16;
			if (CurOffset < 0)
				CurOffset = 0;
		}
		else if (GetKeyState(VK_CONTROL) & 0x8000) {

			if (wParam >= '0' && wParam <= '9')
			{
				char buf[3];
				sprintf(buf, "%c", (int)wParam);
				int key_num;
				sscanf(buf, "%d", &key_num);
				key_num = (key_num + 9) % 10;
				if (hexBookmarks.shortcuts[key_num] != -1)
				{
					int address = hexBookmarks[hexBookmarks.shortcuts[key_num]].address;
					if (address != -1)
					{
						ChangeMemViewFocus(hexBookmarks[hexBookmarks.shortcuts[key_num]].editmode, address, -1);
						// it stops here to prevent update the scroll info, color table and caption twice,
						// because ChangeMemViewFocus already contained those codes.
						return 0;
					}
				}
			}

			switch (wParam) {
			case 0x43: //Ctrl+C
				MemViewCallB(hMemView, WM_COMMAND, MENU_MV_EDIT_COPY, 0); //recursion at work
				return 0;
			case 0x56: //Ctrl+V
				MemViewCallB(hMemView, WM_COMMAND, MENU_MV_EDIT_PASTE, 0);
				return 0;
			case 0x5a: //Ctrl+Z
				UndoLastPatch();
				break;
			case 0x41: //Ctrl+A
					   // Fall through to Ctrl+G
			case 0x47: //Ctrl+G
				GotoAddress(hwnd);
				break;
			case 0x46: //Ctrl+F
				OpenFindDialog();
				break;
			default:
				return 0;
			}
		}
		else if (wParam == VK_TAB && (GetKeyState(VK_CONTROL) & 0x8000) == 0 && (GetKeyState(VK_MENU) & 0x8000) == 0)
		{
			SwitchEditingText(!EditingText);
			TempData = PREVIOUS_VALUE_UNDEFINED;
		}
		else
			// Pressed a key without any function, stop
			return 0;

		//This updates the scroll bar to curoffset
		SCROLLINFO si;
		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.fMask = SIF_POS;
		si.cbSize = sizeof(SCROLLINFO);
		si.nPos = CurOffset / 16;
		SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
		UpdateColorTable();
		UpdateCaption();
		return 0;
	}
	case WM_LBUTTONDOWN:
		SetCapture(hwnd);
		lbuttondown = 1;
		tmpStartAddy = GetAddyFromCoord(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		if(tmpStartAddy < 0)
		{
			CursorDragPoint = -1;
			return 0;
		}
		if(tmpStartAddy > MaxSize) 
		{
			CursorDragPoint = -1;
			return 0;
		}
		SwitchEditingText(AddyWasText);
		CursorStartAddy = tmpStartAddy;
		CursorDragPoint = tmpStartAddy;
		CursorEndAddy = -1;
		UpdateCaption();
		UpdateColorTable();
		return 0;
	case WM_RBUTTONDOWN:
	{
		if (!lbuttondown && CursorEndAddy == -1)
		{
			int addr = GetAddyFromCoord(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			if (addr >= 0 && addr < MaxSize)
			{
				SwitchEditingText(AddyWasText);
				tmpStartAddy = addr;
				UpdateCaption();
				UpdateColorTable();
				return 0;
			}
		}
		break;
	}
	case WM_MOUSEMOVE:
	{
		if (CursorDragPoint < 0)
			return 0;
		int x = GET_X_LPARAM(lParam); 
		int y = GET_Y_LPARAM(lParam); 
		if(lbuttondown){
			AutoScrollFromCoord(x,y);
			tmpEndAddy = GetAddyFromCoord(x,y);
			if (tmpEndAddy >= MaxSize)
				tmpEndAddy = MaxSize - 1;
			SwitchEditingText(AddyWasText);
			if(tmpEndAddy >= 0){
				CursorStartAddy = std::min(tmpStartAddy, tmpEndAddy);
				CursorEndAddy = std::max(tmpStartAddy, tmpEndAddy);
				if(CursorEndAddy == CursorStartAddy)
					CursorEndAddy = -1;
			}

			UpdateCaption();
			UpdateColorTable();
		}
		return 0;
	}
	case WM_LBUTTONUP:
		lbuttondown = 0;
		if (CursorEndAddy == CursorStartAddy)
			CursorEndAddy = -1;
		if((CursorEndAddy < CursorStartAddy) && (CursorEndAddy != -1)){ //this reverses them if they're not right
			int tmpAddy = CursorStartAddy;
			CursorStartAddy = CursorEndAddy;
			CursorEndAddy = tmpAddy;
		}
		UpdateCaption();
		UpdateColorTable();
		ReleaseCapture();
		return 0;
	case WM_CONTEXTMENU:
	{
		POINT ptScreen = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		POINT ptClient;
		int curAddy;
		if (ptScreen.x == -1 && ptScreen.y == -1)
		{
			ptClient.x = GetHexScreenCoordx(CursorStartAddy);
			ptClient.y = GetHexScreenCoordy(CursorEndAddy == -1 ? CursorStartAddy + 16 : CursorEndAddy + 16);
			ptScreen = ptClient;
			ClientToScreen(hMemView, &ptScreen);
			curAddy = CursorStartAddy;
		}
		else
		{
			ptClient = ptScreen;
			ScreenToClient(hMemView, &ptClient);
			curAddy = GetAddyFromCoord(ptClient.x, ptClient.y);
		}

		int bank = getBank(curAddy);
		HMENU hMenu = CreatePopupMenu();

		char str[128];
		for(int i = 0;i < POPUPNUM;i++)
		{
			if((curAddy >= popupmenu[i].minaddress) && (curAddy <= popupmenu[i].maxaddress) && (EditingMode == popupmenu[i].editingmode))
			{
				switch(popupmenu[i].id)
				{
					case ID_ADDRESS_SYMBOLIC_NAME:
					{
						if (curAddy <= CursorEndAddy && curAddy >= CursorStartAddy)
						{
							if (curAddy >= 0x8000 && bank != -1)
								sprintf(str, "Add Symbolic Debug Name For Address %02X:%04X-%02X:%04X", bank, CursorStartAddy, bank, CursorEndAddy);
							else
								sprintf(str, "Add Symbolic Debug Name For Address %04X-%04X", CursorStartAddy, CursorEndAddy);
						} else
						{
							if (curAddy >= 0x8000 && bank != -1)
								sprintf(str, "Add Symbolic Debug Name For Address %02X:%04X", bank, curAddy);
							else
								sprintf(str, "Add Symbolic Debug Name For Address %04X", curAddy);
						}
						popupmenu[i].text = str;
						break;
					}
					//this will set the text for the menu dynamically based on the id
					case ID_ADDRESS_FRZ_SUBMENU:
					{
						HMENU sub = CreatePopupMenu();
						AppendMenu(hMenu, MF_POPUP | MF_STRING, (UINT_PTR)sub, "Freeze / Unfreeze Address");
						AppendMenu(sub, MF_STRING, ID_ADDRESS_FRZ_TOGGLE_STATE, "Toggle state");
						AppendMenu(sub, MF_STRING, ID_ADDRESS_FRZ_FREEZE, "Freeze");
						AppendMenu(sub, MF_STRING, ID_ADDRESS_FRZ_UNFREEZE, "Unfreeze");
						AppendMenu(sub, MF_SEPARATOR, ID_ADDRESS_FRZ_SEP, "-");
						AppendMenu(sub, MF_STRING, ID_ADDRESS_FRZ_UNFREEZE_ALL, "Unfreeze all");
			
						int tempAddy;
						if (CursorEndAddy == -1) tempAddy = CursorStartAddy;
						else tempAddy = CursorEndAddy;								//This is necessary because CursorEnd = -1 if only 1 address is selected
						if (tempAddy - CursorStartAddy + FrozenAddressCount > 255)	//There is a limit of 256 possible frozen addresses, therefore if the user has selected more than this limit, disable freeze menu items
						{														
							EnableMenuItem(sub,ID_ADDRESS_FRZ_TOGGLE_STATE,MF_GRAYED);
							EnableMenuItem(sub,ID_ADDRESS_FRZ_FREEZE,MF_GRAYED);				
						}
						continue;
					}
					case ID_ADDRESS_ADDBP_R:
					{
						// We want this to give the address to add the read breakpoint for
						if ((curAddy <= CursorEndAddy) && (curAddy >= CursorStartAddy))
						{
							if (curAddy >= 0x8000 && bank != -1)
								sprintf(str,"Add Read Breakpoint For Address %02X:%04X-%02X:%04X", bank, CursorStartAddy, bank, CursorEndAddy);
							else
								sprintf(str,"Add Read Breakpoint For Address %04X-%04X", CursorStartAddy, CursorEndAddy);
						} else
						{
							if (curAddy >= 0x8000 && bank != -1)
								sprintf(str,"Add Read Breakpoint For Address %02X:%04X", bank, curAddy);
							else
								sprintf(str,"Add Read Breakpoint For Address %04X", curAddy);
						}
						popupmenu[i].text = str;
						break;
					}
					case ID_ADDRESS_ADDBP_W:
					{
						if ((curAddy <= CursorEndAddy) && (curAddy >= CursorStartAddy))
						{
							if (curAddy >= 0x8000 && bank != -1)
								sprintf(str,"Add Write Breakpoint For Address %02X:%04X-%02X:%04X", bank, CursorStartAddy, bank, CursorEndAddy);
							else
								sprintf(str,"Add Write Breakpoint For Address %04X-%04X", CursorStartAddy, CursorEndAddy);
						} else
						{
							if (curAddy >= 0x8000 && bank != -1)
								sprintf(str,"Add Write Breakpoint For Address %02X:%04X", bank, curAddy);
							else
								sprintf(str,"Add Write Breakpoint For Address %04X", curAddy);
						}
						popupmenu[i].text = str;
						break;
					}
					case ID_ADDRESS_ADDBP_X:
					{
						if ((curAddy <= CursorEndAddy) && (curAddy >= CursorStartAddy))
						{
							if (curAddy >= 0x8000 && bank != -1)
								sprintf(str,"Add Execute Breakpoint For Address %02X:%04X-%02X:%04X", bank, CursorStartAddy, bank, CursorEndAddy);
							else
								sprintf(str,"Add Execute Breakpoint For Address %04X-%04X", CursorStartAddy, CursorEndAddy);
						} else
						{
							if (curAddy >= 0x8000 && bank != -1)
								sprintf(str,"Add Execute Breakpoint For Address %02X:%04X", bank, curAddy);
							else
								sprintf(str,"Add Execute Breakpoint For Address %04X", curAddy);
						}
						popupmenu[i].text = str;
						break;
					}
				}
				AppendMenu(hMenu, MF_STRING, popupmenu[i].id, popupmenu[i].text);
			}
		}

		// Add / Edit / Remove bookmark
		int foundBookmark = findBookmark(CursorStartAddy, EditingMode);
		if (foundBookmark != -1)
		{
			AppendMenu(hMenu, MF_STRING, ID_ADDRESS_EDIT_BOOKMARK, "Edit Bookmark");
			AppendMenu(hMenu, MF_STRING, ID_ADDRESS_REMOVE_BOOKMARK, "Remove Bookmark");
		}
		else
			AppendMenu(hMenu, MF_STRING, ID_ADDRESS_ADD_BOOKMARK, "Add Bookmark");

		int id = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, ptScreen.x, ptScreen.y, hMemView, NULL);
		switch(id)
		{
			case ID_ADDRESS_FRZ_TOGGLE_STATE:
			{
				for (int frzAddr = CursorStartAddy; (CursorEndAddy == -1 && frzAddr == CursorStartAddy) || frzAddr <= CursorEndAddy; frzAddr++)
				{
					FreezeRam(frzAddr, 0, frzAddr == CursorEndAddy);
				}
				break;
			}
			case ID_ADDRESS_FRZ_FREEZE:
			{
				for (int frzAddr = CursorStartAddy; (CursorEndAddy == -1 && frzAddr == CursorStartAddy) || frzAddr <= CursorEndAddy; frzAddr++)
				{
					FreezeRam(frzAddr, 1, frzAddr == CursorEndAddy);
				}
				break;
			}
			case ID_ADDRESS_FRZ_UNFREEZE:
			{
				for (int frzAddr = CursorStartAddy; (CursorEndAddy == -1 && frzAddr == CursorStartAddy) || frzAddr <= CursorEndAddy; frzAddr++)
				{
					FreezeRam(frzAddr, -1, frzAddr == CursorEndAddy);
				}
				break;
			}
			case ID_ADDRESS_FRZ_UNFREEZE_ALL:
			{
				UnfreezeAllRam();
				break;
			}
			break;

			case ID_ADDRESS_ADDBP_R:
			{
				if (numWPs < MAXIMUM_NUMBER_OF_BREAKPOINTS)
				{
					watchpoint[numWPs].flags = WP_E | WP_R;
					if (EditingMode == MODE_NES_PPU)
						watchpoint[numWPs].flags |= BT_P;
					if ((curAddy <= CursorEndAddy) && (curAddy >= CursorStartAddy))
					{
						watchpoint[numWPs].address = CursorStartAddy;
						watchpoint[numWPs].endaddress = CursorEndAddy;
					} else
					{
						watchpoint[numWPs].address = curAddy;
						watchpoint[numWPs].endaddress = 0;
					}
					char condition[10] = {0};
					if (EditingMode == MODE_NES_MEMORY)
					{
						// only break at this Bank
						if (curAddy >= 0x8000 && bank != -1)
							sprintf(condition, "T==#%02X", bank);
					}
					checkCondition(condition, numWPs);

					numWPs++;
					{
						extern int myNumWPs;
						myNumWPs++;
					}
					if (hDebug)
						AddBreakList();
					else
						DoDebug(0);
				}
				break;
			}
			case ID_ADDRESS_ADDBP_W:
			{
				if (numWPs < MAXIMUM_NUMBER_OF_BREAKPOINTS)
				{
					watchpoint[numWPs].flags = WP_E | WP_W;
					if (EditingMode == MODE_NES_PPU)
						watchpoint[numWPs].flags |= BT_P;
					if ((curAddy <= CursorEndAddy) && (curAddy >= CursorStartAddy))
					{
						watchpoint[numWPs].address = CursorStartAddy;
						watchpoint[numWPs].endaddress = CursorEndAddy;
					} else
					{
						watchpoint[numWPs].address = curAddy;
						watchpoint[numWPs].endaddress = 0;
					}
					char condition[10] = {0};
					if (EditingMode == MODE_NES_MEMORY)
					{
						// only break at this Bank
						if (curAddy >= 0x8000 && bank != -1)
							sprintf(condition, "T==#%02X", bank);
					}
					checkCondition(condition, numWPs);

					numWPs++;
					{ extern int myNumWPs;
					myNumWPs++; }
					if (hDebug)
						AddBreakList();
					else
						DoDebug(0);
				}
				break;
			}
			case ID_ADDRESS_ADDBP_X:
			{
				if (numWPs < MAXIMUM_NUMBER_OF_BREAKPOINTS)
				{
					watchpoint[numWPs].flags = WP_E | WP_X;
					if((curAddy <= CursorEndAddy) && (curAddy >= CursorStartAddy))
					{
						watchpoint[numWPs].address = CursorStartAddy;
						watchpoint[numWPs].endaddress = CursorEndAddy;
					} else
					{
						watchpoint[numWPs].address = curAddy;
						watchpoint[numWPs].endaddress = 0;
					}
					char condition[10] = {0};
					if (EditingMode == MODE_NES_MEMORY)
					{
						// only break at this Bank
						if (curAddy >= 0x8000 && bank != -1)
							sprintf(condition, "T==#%02X", bank);
					}
					checkCondition(condition, numWPs);

					numWPs++;
					{ extern int myNumWPs;
					myNumWPs++; }
					if (hDebug)
						AddBreakList();
					else
						DoDebug(0);
				}
				break;
			}
			case ID_ADDRESS_SEEK_IN_ROM:
				ChangeMemViewFocus(MODE_NES_FILE, GetNesFileAddress(curAddy), -1);
				break;
			case ID_ADDRESS_CREATE_GG_CODE:
				SetGGConvFocus(curAddy, GetMem(curAddy));
				break;
			case ID_ADDRESS_ADD_BOOKMARK:
			{
				if (foundBookmark == -1)
				{
					if (hexBookmarks.bookmarkCount >= 64)
						MessageBox(hwnd, "Can't set more than 64 bookmarks.", "Error", MB_OK | MB_ICONERROR);
					else
					{
						int ret = addBookmark(hwnd, CursorStartAddy, EditingMode);
						if (ret == -1)
							MessageBox(hwnd, "Error adding bookmark.", "Error", MB_OK | MB_ICONERROR);
						else if (ret == 0)
						{
							updateBookmarkMenus(GetSubMenu(GetMenu(hwnd), BOOKMARKS_SUBMENU_POS));
							UpdateColorTable();
						}
					}
				}
				else // usually it cannot reach here.
					MessageBox(hwnd, "This address already has a bookmark.", "Error", MB_OK | MB_ICONERROR);
				break;
			}
			case ID_ADDRESS_EDIT_BOOKMARK:
				if (foundBookmark != -1)
				{
					int ret = editBookmark(hwnd, foundBookmark);
					if (ret == -1)
						MessageBox(hwnd, "Error editing bookmark.", "Error", MB_OK | MB_ICONERROR);
					else if (ret == 0)
					{
						updateBookmarkMenus(GetSubMenu(GetMenu(hwnd), BOOKMARKS_SUBMENU_POS));
						UpdateColorTable();
					}
				}
				else // usually it cannot reach here.
					MessageBox(hwnd, "This address doesn't have a bookmark.", "Error", MB_OK | MB_ICONERROR);
				break;
			case ID_ADDRESS_REMOVE_BOOKMARK:
				if (foundBookmark != -1)
				{
					int ret = removeBookmark(foundBookmark);
					if (ret == -1)
						MessageBox(hwnd, "Error removing bookmark.", "Error", MB_OK | MB_ICONERROR);
					else if (ret == 0)
					{
						updateBookmarkMenus(GetSubMenu(GetMenu(hwnd), BOOKMARKS_SUBMENU_POS));
						UpdateColorTable();
					}
				}
				else
					// usually it cannot reach here.
					MessageBox(hwnd, "This address doesn't have a bookmark.", "Error", MB_OK | MB_ICONERROR);
				break;
			case ID_ADDRESS_SYMBOLIC_NAME:
			{
				if (curAddy <= CursorEndAddy && curAddy >= CursorStartAddy ? DoSymbolicDebugNaming(CursorStartAddy, CursorEndAddy - CursorStartAddy + 1, hMemView) : DoSymbolicDebugNaming(curAddy, hMemView))
				{
					// enable "Symbolic Debug" if not yet enabled
					if (!symbDebugEnabled)
					{
						symbDebugEnabled = true;
						if (hDebug)
							CheckDlgButton(hDebug, IDC_DEBUGGER_ENABLE_SYMBOLIC, BST_CHECKED);
					}
					UpdateCaption();
				}
				break;
			}
			break;
		}
		//6 = Create GG Code

		return 0;
	}
	case WM_MBUTTONDOWN:
	{
		if (EditingMode != MODE_NES_MEMORY) return 0;
		int addr = GetAddyFromCoord(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		if(addr < 0) return 0;
		FreezeRam(addr, 0, 1);
		return 0;
	}
	case WM_MOUSEWHEEL:
	{
		SCROLLINFO si;
		int delta = (short)HIWORD(wParam);///WHEEL_DELTA;
		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.fMask = SIF_ALL;
		si.cbSize = sizeof(SCROLLINFO);
		GetScrollInfo(hwnd,SB_VERT,&si);
		if(delta < 0)si.nPos+=si.nPage;
		if(delta > 0)si.nPos-=si.nPage;
		if (si.nPos < si.nMin) si.nPos = si.nMin;
		if ((si.nPos+(int)si.nPage) > si.nMax) si.nPos = si.nMax-si.nPage; //added cast
		CurOffset = si.nPos*16;
		if (CurOffset >= MaxSize - DataAmount) CurOffset = MaxSize - DataAmount;
		if (CurOffset < 0) CurOffset = 0;
		SetScrollInfo(hwnd,SB_VERT,&si,TRUE);
		UpdateColorTable();
		return 0;
	}
	case WM_SIZE:
		if(wParam == SIZE_RESTORED)										//If dialog was resized
		{
			GetWindowRect(hwnd,&newMemViewRect);						//Get new size
			MemViewSizeX = newMemViewRect.right-newMemViewRect.left;	//Store new size (this will be used to store in the .cfg file)	
			MemViewSizeY = newMemViewRect.bottom-newMemViewRect.top;
		}
		ClientHeight = HIWORD (lParam);
		if (DataAmount != ((ClientHeight/MemFontHeight)*16))
		{
			DataAmount = ((ClientHeight/MemFontHeight)*16);
			if (CurOffset >= MaxSize - DataAmount) CurOffset = MaxSize - DataAmount;
			if (CurOffset < 0) CurOffset = 0;
			//mbg merge 7/18/06 added casts:
			CurTextColorList = (COLORREF*)realloc(CurTextColorList, DataAmount*sizeof(COLORREF));
			CurBGColorList = (COLORREF*)realloc(CurBGColorList, DataAmount*sizeof(COLORREF));
			DimTextColorList = (COLORREF*)realloc(DimTextColorList, DataAmount*sizeof(COLORREF));
			DimBGColorList = (COLORREF*)realloc(DimBGColorList, DataAmount*sizeof(COLORREF));
			HexTextColorList = EditingText ? DimTextColorList : CurTextColorList;
			HexBGColorList = EditingText ? DimBGColorList : CurBGColorList;
			AnsiTextColorList = EditingText ? CurTextColorList : DimTextColorList;
			AnsiBGColorList = EditingText ? CurBGColorList : DimBGColorList;
			PreviousValues = (int*)realloc(PreviousValues,(DataAmount)*sizeof(int));
			HighlightedBytes = (unsigned int*)realloc(HighlightedBytes,(DataAmount)*sizeof(unsigned int));
			resetHighlightingActivityLog();
		}
		//Set vertical scroll bar range and page size
		SCROLLINFO si;
		ZeroMemory(&si, sizeof(SCROLLINFO));
		si.cbSize = sizeof (si) ;
		si.fMask  = (SIF_RANGE|SIF_PAGE) ;
		si.nMin   = 0 ;
		si.nMax   = MaxSize/16 ;
		si.nPage  = ClientHeight/MemFontHeight;
		SetScrollInfo (hwnd, SB_VERT, &si, TRUE);
		UpdateColorTable();
		return 0 ;

	case WM_COMMAND:

		switch(wParam)
		{
		case MENU_MV_FILE_SAVE:
			FlushUndoBuffer();
			iNesSave();
			UpdateColorTable();
			return 0;

		case MENU_MV_FILE_SAVE_AS:
			SaveRomAs();
			return 0;

		case MENU_MV_FILE_LOAD_TBL:
		{
			int errLine = LoadTableFile();
			if (errLine != -1) {
				char str[128];
				sprintf(str, "Error loading table file at Line %d", errLine);
				MessageBox(hMemView, str, "Error", MB_OK | MB_ICONERROR);
			}
			UpdateColorTable();
		}
		return 0;
		case MENU_MV_FILE_UNLOAD_TBL:
			UnloadTableFile();
			UpdateColorTable();
			return 0;

		case MENU_MV_FILE_DUMP_RAM:
		{
			char bar[0x800];
			unsigned int i;
			for (i=0;i<sizeof(bar);i++) bar[i] = GetMem(i);

			dumpToFile(bar, sizeof(bar));
			return 0;
		}
		case MENU_MV_FILE_DUMP_64K:
		{
			char *bar = new char[65536];
			unsigned int i;
			for (i=0;i<65536;i++) bar[i] = GetMem(i);

			dumpToFile(bar, 65536);
			delete [] bar;
			return 0;
		}
		case MENU_MV_FILE_DUMP_PPU:
		{
			char bar[0x4000];
			unsigned int i;
			for (i=0;i<sizeof(bar);i++)
			{
				i &= 0x3FFF;
				if(i < 0x2000) bar[i] = VPage[(i)>>10][(i)];
				else if(i < 0x3F00) bar[i] = vnapage[(i>>10)&0x3][i&0x3FF];
				else bar[i] = READPAL_MOTHEROFALL(i & 0x1F);
			}
			dumpToFile(bar, sizeof(bar));
			return 0;
		}
		case MENU_MV_FILE_DUMP_OAM:
			{
				char bar[0x100];
				unsigned int i;
				for (i=0;i<0x100;i++) bar[i] = SPRAM[i];
				dumpToFile(bar,0x100);
				return 0;
			}

		case MENU_MV_FILE_LOAD_RAM:
		{
			char bar[0x800];
			if (loadFromFile(bar, sizeof(bar)))
			{
				for (uint16 addr=0; addr<sizeof(bar); ++addr)
					BWrite[addr](addr,bar[addr]);
			}
			return 0;
		}
		case MENU_MV_FILE_LOAD_PPU:
		{
			char bar[0x4000];
			if (loadFromFile(bar, sizeof(bar)))
			{
				for (uint16 addr=0; addr<sizeof(bar); ++addr)
				{
					char v = bar[addr];
					if(addr < 0x2000)
						VPage[addr>>10][addr] = v; //todo: detect if this is vrom and turn it red if so
					if((addr >= 0x2000) && (addr < 0x3F00))
						vnapage[(addr>>10)&0x3][addr&0x3FF] = v; //todo: this causes 0x3000-0x3f00 to mirror 0x2000-0x2f00, is this correct?
					if((addr >= 0x3F00) && (addr < 0x3FFF))
						PalettePoke(addr,v);
				}
			}
			return 0;
		}
		case MENU_MV_FILE_LOAD_OAM:
		{
			char bar[0x100];
			if (loadFromFile(bar, sizeof(bar)))
			{
				for (uint16 addr=0; addr<sizeof(bar); ++addr)
					SPRAM[addr] = bar[addr];
			}
			return 0;
		}
		case ID_MEMWVIEW_FILE_CLOSE:
			KillMemView();
			return 0;
		
		case MENU_MV_FILE_GOTO_ADDRESS:
			GotoAddress(hwnd);
			return 0;

		case MENU_MV_EDIT_UNDO:
			UndoLastPatch();
			return 0;

		case MENU_MV_EDIT_COPY:
		{
			int size;
			if(CursorEndAddy == -1)
				size = 1;
			else
				size = CursorEndAddy - CursorStartAddy + 1;

			//i*2 is two characters per byte, plus terminating null
			HGLOBAL hGlobal = GlobalAlloc(GHND, size * 2  + 1);

			char* pGlobal = (char*)GlobalLock(hGlobal) ; //mbg merge 7/18/06 added cast
			char str[4];
			if(!EditingText){
				for(int i = 0; i < size; i++){
					str[0] = 0;
					sprintf(str,"%02X", GetMemViewData((uint32)i + CursorStartAddy));
					strcat(pGlobal, str);
				}
			} else {
				for(int i = 0;i < size; i++){
					str[0] = 0;
					sprintf(str, "%c", chartable[GetMemViewData((uint32)i + CursorStartAddy)]);
					strcat(pGlobal, str);
				}
			}
			GlobalUnlock(hGlobal);
			OpenClipboard(hwnd);
			EmptyClipboard();
			SetClipboardData(CF_TEXT, hGlobal);
			SetClipboardData(CF_LOCALE, hGlobal);
			CloseClipboard();
			return 0;
		}
		case MENU_MV_EDIT_PASTE:
		{
			OpenClipboard(hwnd);
			HANDLE hGlobal = GetClipboardData(CF_TEXT);
			if(hGlobal == NULL){
				CloseClipboard();
				return 0;
			}
			char* pGlobal = (char*)GlobalLock (hGlobal) ; //mbg merge 7/18/06 added cast
			//for(i = 0;pGlobal[i] != 0;i++){
			InputData(pGlobal);
			//}
			GlobalUnlock (hGlobal);
			CloseClipboard();
			return 0;
		}
		case MENU_MV_EDIT_FIND:
			OpenFindDialog();
			return 0;


		case MENU_MV_VIEW_RAM:
		case MENU_MV_VIEW_PPU:
		case MENU_MV_VIEW_OAM:
		case MENU_MV_VIEW_ROM:
		{
			int _EditingMode = wParam - MENU_MV_VIEW_RAM;
			// Leave NES Memory
			if (_EditingMode == MODE_NES_MEMORY && EditingMode != MODE_NES_MEMORY)
				CreateCheatMap();
			// Enter NES Memory
			if (_EditingMode != MODE_NES_MEMORY && EditingMode == MODE_NES_MEMORY)
				ReleaseCheatMap();
			EditingMode = _EditingMode;
			for (int i = MODE_NES_MEMORY; i <= MODE_NES_FILE; i++)
				if(EditingMode == i)
				{
					CheckMenuRadioItem(GetMenu(hMemView), MENU_MV_VIEW_RAM, MENU_MV_VIEW_ROM, MENU_MV_VIEW_RAM + i, MF_BYCOMMAND);
					break;
				}

			MaxSize = GetMaxSize(EditingMode);

			if (CurOffset >= MaxSize - DataAmount) CurOffset = MaxSize - DataAmount;
			if (CurOffset < 0) CurOffset = 0;
			if(CursorEndAddy >= MaxSize) CursorEndAddy = -1;
			if(CursorStartAddy >= MaxSize) CursorStartAddy= MaxSize-1;

			//Set vertical scroll bar range and page size
			ZeroMemory(&si, sizeof(SCROLLINFO));
			si.cbSize = sizeof (si) ;
			si.fMask  = (SIF_RANGE|SIF_PAGE) ;
			si.nMin   = 0 ;
			si.nMax   = MaxSize/16 ;
			si.nPage  = ClientHeight/MemFontHeight;
			SetScrollInfo (hwnd, SB_VERT, &si, TRUE);

			resetHighlightingActivityLog();
			UpdateColorTable();
			UpdateCaption();
			return 0;
		}
		case ID_HIGHLIGHTING_HIGHLIGHT_ACTIVITY:
		{
			MemView_HighlightActivity ^= 1;
			CheckMenuItem(GetMenu(hMemView), ID_HIGHLIGHTING_HIGHLIGHT_ACTIVITY, (MemView_HighlightActivity) ? MF_CHECKED: MF_UNCHECKED);
			resetHighlightingActivityLog();
			if (!MemView_HighlightActivity)
				UpdateMemoryView(1);
			return 0;
		}
		case ID_HIGHLIGHTING_SETFADINGPERIOD:
		{
			int newValue = MemView_HighlightActivity_FadingPeriod - 1;
			if (CWin32InputBox::GetInteger("Highlighting fading period", "Highlight changed bytes for how many frames?", newValue, hMemView) == IDOK)
			{
				if (newValue <= 0)
					newValue = HIGHLIGHT_ACTIVITY_NUM_COLORS;
				else
					newValue++;

				if (MemView_HighlightActivity_FadingPeriod != newValue)
				{
					MemView_HighlightActivity_FadingPeriod = newValue;
					resetHighlightingActivityLog();
				}
			}
			return 0;
		}
		case ID_HIGHLIGHTING_FADEWHENPAUSED:
		{
			MemView_HighlightActivity_FadeWhenPaused ^= 1;
			CheckMenuItem(GetMenu(hMemView), ID_HIGHLIGHTING_FADEWHENPAUSED, (MemView_HighlightActivity_FadeWhenPaused) ? MF_CHECKED: MF_UNCHECKED);
			resetHighlightingActivityLog();
			return 0;
		}
		case ID_COLOR_HEXEDITOR:
		case ID_COLOR_HEXEDITOR + 1:
		case ID_COLOR_HEXEDITOR + 2:
		case ID_COLOR_HEXEDITOR + 3:
		case ID_COLOR_HEXEDITOR + 4:
		case ID_COLOR_HEXEDITOR + 5:
		case ID_COLOR_HEXEDITOR + 6:
		case ID_COLOR_HEXEDITOR + 7:
		case ID_COLOR_HEXEDITOR + 8:
		case ID_COLOR_HEXEDITOR + 9:
		{
			int index = wParam - ID_COLOR_HEXEDITOR;
			if (ChangeColor(hwnd, (CHOOSECOLORINFO*)&hexcolormenu[index]))
			{
				UpdateColorTable();
				ModifyColorMenu(hwnd, GetHexColorMenu(hwnd), &hexcolormenu[index], index, wParam);
			}
		}
		break;
		case ID_HEXEDITOR_DEFCOLOR:
		{
			if (!IsHexColorDefault() && MessageBox(hwnd, "Do you want to restore all the colors to default?", "Restore default colors", MB_YESNO | MB_ICONINFORMATION) == IDYES)
			{
				RestoreDefaultHexColor();
				UpdateColorTable();
				for (int i = 0; i < sizeof(hexcolormenu) / sizeof(COLORMENU); ++i)
					ModifyColorMenu(hwnd, GetHexColorMenu(hwnd), &hexcolormenu[i], i, ID_COLOR_HEXEDITOR + i);
			}
		}
		break;
		case ID_COLOR_CDLOGGER:
		case ID_COLOR_CDLOGGER + 1:
		case ID_COLOR_CDLOGGER + 2:
		case ID_COLOR_CDLOGGER + 3:
		case ID_COLOR_CDLOGGER + 4:
		case ID_COLOR_CDLOGGER + 5:
		case ID_COLOR_CDLOGGER + 6:
		case ID_COLOR_CDLOGGER + 7:
		case ID_COLOR_CDLOGGER + 8:
		case ID_COLOR_CDLOGGER + 9:
		{
			int index = wParam - ID_COLOR_CDLOGGER;
			if (ChangeColor(hwnd, (CHOOSECOLORINFO*)&cdlcolormenu[index]))
			{
				UpdateColorTable();
				ModifyColorMenu(hwnd, GetCdlColorMenu(hwnd), &cdlcolormenu[index], index, wParam);
			}
		}
		break;
		case ID_CDLOGGER_DEFCOLOR:
		if (!IsCdlColorDefault() && MessageBox(hwnd, "Do you want to restore all the colors to default?", "Restore default colors", MB_YESNO | MB_ICONINFORMATION) == IDYES)
		{
			RestoreDefaultCdlColor();
			UpdateColorTable();
			for (int i = 0; i < sizeof(cdlcolormenu) / sizeof(COLORMENU); ++i)
				ModifyColorMenu(hwnd, GetCdlColorMenu(hwnd), &cdlcolormenu[i], i, ID_COLOR_CDLOGGER + i);
		}
		break;
		case MENU_MV_BOOKMARKS_RM_ALL:
			if (hexBookmarks.bookmarkCount)
			{
				if (MessageBox(hwnd, "Remove All Bookmarks?", "Bookmarks", MB_YESNO | MB_ICONINFORMATION) == IDYES)
				{
					removeAllBookmarks(GetSubMenu(GetMenu(hwnd), BOOKMARKS_SUBMENU_POS));
					UpdateColorTable();
				}
			}
			return 0;
		case ID_BOOKMARKS_EXPORT:
		{
			char name[2048] = { 0 };

			OPENFILENAME ofn;
			memset(&ofn, 0, sizeof(ofn));
			ofn.lStructSize = sizeof(ofn);
			ofn.hInstance = fceu_hInstance;
			ofn.lpstrTitle = "Save bookmarks as...";
			ofn.lpstrFilter = "Hex Editor Bookmark list (*.hbm)\0*.hbm\0All Files (*.*)\0*.*\0\0";
			strcpy(name, mass_replace(GetRomName(), "|", ".").c_str());
			ofn.lpstrFile = name;
			ofn.lpstrDefExt = "hbm";
			ofn.nMaxFile = 2048;
			ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
			ofn.hwndOwner = hwnd;

			int success = false;
			if (GetSaveFileName(&ofn))
			{
				FILE* bld = fopen(name, "wb");
				if (bld)
				{
					// write the header
					fwrite("HexBookmarkList", strlen("HexBookmarkList"), 1, bld);
					// it shares the same logic of creating the hexpreference part of .deb file
					extern int storeHexPreferences(FILE*, HexBookmarkList& = hexBookmarks);
					if (!storeHexPreferences(bld))
						success = true;
					fclose(bld);
				}
				if (!success)
					MessageBox(hwnd, "Error saving bookmarks.", "Error saving bookmarks", MB_OK | MB_ICONERROR);
			}
			return 0;
		}
		case ID_BOOKMARKS_IMPORT:
		{
			char nameo[2048] = { 0 };
			OPENFILENAME ofn;
			memset(&ofn, 0, sizeof(ofn));
			ofn.lStructSize = sizeof(ofn);
			ofn.hInstance = fceu_hInstance;
			ofn.lpstrTitle = "Load bookmarks...";
			ofn.lpstrFilter = "Hex Editor Bookmarks (*.hbm)\0*.hbm\0All Files (*.*)\0*.*\0\0";
			ofn.lpstrFile = nameo;
			ofn.lpstrDefExt = "hbm";
			ofn.nMaxFile = 2048;
			ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
			ofn.hwndOwner = hwnd;

			bool success = false;
			if (GetOpenFileName(&ofn))
			{
				char buffer[256] = { 0 };
				FILE* bld = fopen(nameo, "r");
				if (bld)
				{
					// Read the header to know it's hex bookmark list
					fread(buffer, strlen("HexBookmarkList"), 1, bld);
					if (!strcmp(buffer, "HexBookmarkList"))
					{
						HexBookmarkList import;
						// it shares the same logic of creating the hexpreference part of .deb file
						extern int loadHexPreferences(FILE*, HexBookmarkList& = hexBookmarks);
						if (!loadHexPreferences(bld, import) && import.bookmarkCount > 0)
						{
							if (importBookmarkProps & IMPORT_DISCARD_ORIGINAL)
							{
								discard_original:
								if (importBookmarkProps & IMPORT_OVERWRITE_NO_PROMPT || hexBookmarks.bookmarkCount == 0 || MessageBox(hwnd, "All your existing bookmarks will be discarded after importing the new bookmarks! Do you want to continue?", "Bookmark Import", MB_YESNO | MB_ICONWARNING) == IDYES)
								{
									removeAllBookmarks(GetSubMenu(GetMenu(hwnd), BOOKMARKS_SUBMENU_POS));
									for (int i = 0; i < import.bookmarkCount; ++i)
									{
										hexBookmarks[i].address = import[i].address;
										hexBookmarks[i].editmode = import[i].editmode;
										strcpy(hexBookmarks[i].description, import[i].description);
										memcpy(hexBookmarks.shortcuts, import.shortcuts, sizeof(hexBookmarks.shortcuts));
										hexBookmarks.bookmarkCount = import.bookmarkCount;
										hexBookmarks.shortcutCount = import.shortcutCount;
									}
									updateBookmarkMenus(GetSubMenu(GetMenu(hwnd), BOOKMARKS_SUBMENU_POS));
									UpdateColorTable();
								}
							}
							else
							{
								// conflict bookmark count
								int conflictBookmarkCount = 0;
								// the conflict bookmark indexes of the main list
								int conflictBookmarkIndex[64];
								// conflict shortcut count
								int conflictShortcutCount = 0;
								// discarded bookmark count
								int discardBookmarkCount = 0;
								// bookmark that is out of scope
								int outOfRangeBookmarkCount = 0;

								// store the count of bookmarks after importing finished
								int finalBookmarkCount = hexBookmarks.bookmarkCount;
								// the reference index number in main bookmark list
								// -1 means this bookmark is not be indexed
								int indexRef[64];
								memset(indexRef, -1, sizeof(indexRef));

								for (int i = 0, j; i < import.bookmarkCount; ++i)
								{
									bool conflict = false;
									for (j = 0; j < hexBookmarks.bookmarkCount; ++j)
									{
										// to find if there are any conflict bookmarks
										// currently, one address can have only one bookmark
										// if the address and editmode are the same, then they are considered conflict
										if (import[i].address == hexBookmarks[j].address && import[j].editmode == hexBookmarks[j].editmode)
										{
											conflictBookmarkIndex[conflictBookmarkCount] = i;
											indexRef[i] = j;
											++conflictBookmarkCount;
											conflict = true;
											break;
										}
									}

									// after a loop, this bookmark doesn't have conflict with the original one
									if (!conflict)
										if (finalBookmarkCount >= 64)
											// the total bookmark count has reached the limit of bookmarks (64),
											// discard it
											++discardBookmarkCount;
										else if (import[j].address > (unsigned int)GetMaxSize(import[j].editmode))
											// the bookmark is out of valid range for current game,
											// discard it.
											++outOfRangeBookmarkCount;
										else
										{
											// if the bookmark is still not discarded,
											// then it's not a conflict one, append it to the last of the main list
											indexRef[i] = finalBookmarkCount;
											hexBookmarks[finalBookmarkCount].address = import[i].address;
											hexBookmarks[finalBookmarkCount].editmode = import[i].editmode;
											strcpy(hexBookmarks[finalBookmarkCount].description, import[i].description);
											++finalBookmarkCount;
										}
								}

								// the result of overwriting the shortcuts
								int shortcutListOverwrite[10];
								memcpy(shortcutListOverwrite, hexBookmarks.shortcuts, sizeof(hexBookmarks.shortcuts));
								int shortcutListOverwriteCount = hexBookmarks.shortcutCount;
								// the result of keep the shortcut as original
								int shortcutListKeep[10];
								memcpy(shortcutListKeep, hexBookmarks.shortcuts, sizeof(hexBookmarks.shortcuts));
								int shortcutListKeepCount = hexBookmarks.shortcutCount;

								for (int i = 0, j; i < 10; ++i)
									if (import.shortcuts[i] != -1)
									{
										bool repeat = false;
										for (j = 0; j < 10; ++j)
											if (indexRef[import.shortcuts[i]] == hexBookmarks.shortcuts[j] && i != j)
											{
												// the slot in the original list had this bookmark but different
												// slot, remove the bookmark in the original slot
												shortcutListOverwrite[j] = -1;
												--shortcutListOverwriteCount;
												++conflictShortcutCount;
												repeat = true;
												break;
											}

										if (shortcutListOverwrite[i] == -1)
											++shortcutListOverwriteCount;
										shortcutListOverwrite[i] = indexRef[import.shortcuts[i]];

										// after a loop, the original list doesn't have a slot with same
										// bookmark but different slot, and the slot in original list
										// is empty, then the bookmark can occupy it.
										if (!repeat && hexBookmarks.shortcuts[i] == -1)
										{
											shortcutListKeep[i] = indexRef[import.shortcuts[i]];
											++shortcutListKeepCount;
										}
									}

								int tmpImportBookmarkProps = importBookmarkProps;
								bool continue_ = true;

								// show the prompt message if there are conflicts
								if (!(tmpImportBookmarkProps & IMPORT_OVERWRITE_NO_PROMPT) && (conflictBookmarkCount || conflictShortcutCount))
								{
									char units[32];
									strcpy(buffer, "The importing bookmark list has ");
									sprintf(units, "%d duplicate bookmark", conflictBookmarkCount);
									if (conflictBookmarkCount != 1)
										strcat(units, "s");
									strcat(buffer, units);

									if (conflictShortcutCount)
									{
										if (conflictBookmarkCount) strcat(buffer, " and ");
										sprintf(units, "%d conflict shortcut", conflictShortcutCount);
										if (conflictShortcutCount != 1)
											strcat(units, "s");
										strcat(buffer, units);
									}
									strcat(buffer, " with yours.\r\nYou must choose which side would be reserved. Do you want to continue?");

									continue_ = MessageBox(hwnd, buffer, "Bookmark conflict", MB_YESNO | MB_ICONEXCLAMATION) == IDYES && DialogBoxParam(fceu_hInstance, "IMPORTBOOKMARKOPTIONDIALOG", hwnd, ImportBookmarkCallB, (LPARAM)&tmpImportBookmarkProps);
									
									if (tmpImportBookmarkProps & IMPORT_OVERWRITE_NO_PROMPT)
										importBookmarkProps = tmpImportBookmarkProps;

									// in case user's mind changes on the fly
									if (tmpImportBookmarkProps & IMPORT_DISCARD_ORIGINAL)
										goto discard_original;

								}

								if (continue_)
								{
									if (tmpImportBookmarkProps & IMPORT_OVERWRITE_BOOKMARK)
										// when it is set to overwrite conflicted bookmark, otherwise do nothing
										for (int i = 0; i < conflictBookmarkCount; ++i)
											// the conflict bookmarks are all before the bookmark count in main list
											strcpy(hexBookmarks[indexRef[conflictBookmarkIndex[i]]].description, import[conflictBookmarkIndex[i]].description);

									// update the bookmark shortcut mapping 
									if (tmpImportBookmarkProps & IMPORT_OVERWRITE_SHORTCUT)
									{
										memcpy(hexBookmarks.shortcuts, shortcutListOverwrite, sizeof(hexBookmarks.shortcuts));
										hexBookmarks.shortcutCount = shortcutListOverwriteCount;
									}
									else
									{
										memcpy(hexBookmarks.shortcuts, shortcutListKeep, sizeof(hexBookmarks.shortcuts));
										hexBookmarks.shortcutCount = shortcutListKeepCount;
									}

									// set the count of the main list to the imported count
									hexBookmarks.bookmarkCount = finalBookmarkCount;

									updateBookmarkMenus(GetSubMenu(GetMenu(hwnd), BOOKMARKS_SUBMENU_POS));
									UpdateColorTable();
								}

								// tell user there are bookmarks that imported failed.
								if (discardBookmarkCount || outOfRangeBookmarkCount)
								{
									char reason[64];
									sprintf(buffer, "Import complete, but %d bookmark%s %s not imported:\n", discardBookmarkCount + outOfRangeBookmarkCount, discardBookmarkCount + outOfRangeBookmarkCount == 1 ? "" : "s", discardBookmarkCount + outOfRangeBookmarkCount == 1 ? "is" : "are");
									if (outOfRangeBookmarkCount)
									{
										sprintf(reason, "%d %s outside the valid address range of the game.", outOfRangeBookmarkCount, outOfRangeBookmarkCount == 1 ? "is" : "are");
										strcat(buffer, reason);
									}

									if (discardBookmarkCount)
									{
										if (outOfRangeBookmarkCount)
											strcat(buffer, "\n");
										sprintf(reason, "%d %s discaded due to the list has reached its max limit (64).", discardBookmarkCount, discardBookmarkCount == 1 ? "is" : "are");
										strcat(buffer, reason);
									}
									MessageBox(hwnd, buffer, "Loading Hex Editor bookmarks", MB_OK | MB_ICONEXCLAMATION);
								}
							}
						}
						else
							MessageBox(hwnd, "An error occurred while loading bookmarks.", "Error loading bookmarks", MB_OK | MB_ICONERROR);
					}
					else
						MessageBox(hwnd, "This file is not a Hex Editor bookmark list.", "Error loading bookmarks", MB_OK | MB_ICONERROR);
					fclose(bld);
				} 
				else
					MessageBox(hwnd, "Error opening bookmark file", "Error loading bookmarks", MB_OK | MB_ICONERROR);
			}
		}
		return 0;
		case ID_BOOKMARKS_OPTION:
		{
			int tmpImportBookmarkProps = importBookmarkProps;
			if (DialogBoxParam(fceu_hInstance, "IMPORTBOOKMARKOPTIONDIALOG", hwnd, ImportBookmarkCallB, (LPARAM)&tmpImportBookmarkProps))
				importBookmarkProps = tmpImportBookmarkProps;
		}
		return 0;
		case MENU_MV_HELP:
			OpenHelpWindow(memviewhelp);
			return 0;

		default:
			if (wParam >= ID_FIRST_BOOKMARK && wParam < (ID_FIRST_BOOKMARK + 64))
			{
				int bookmark = wParam - ID_FIRST_BOOKMARK;
				int newValue = handleBookmarkMenu(bookmark);

				if (newValue != -1)
				{
					ChangeMemViewFocus(hexBookmarks[bookmark].editmode,newValue,-1);
					UpdateColorTable();
				}
				return 0;
			}
		}

	case WM_MOVE: {
		if (!IsIconic(hwnd)) {
		RECT wrect;
		GetWindowRect(hwnd,&wrect);
		MemView_wndx = wrect.left;
		MemView_wndy = wrect.top;

		#ifdef WIN32
		WindowBoundsCheckResize(MemView_wndx,MemView_wndy,MemViewSizeX,wrect.right);
		#endif
		}
		return 0;
				  }

	case WM_CLOSE:
		KillMemView();
		return 0;
	}
	return DefWindowProc (hwnd, message, wParam, lParam) ;
}



void DoMemView()
{
	WNDCLASSEX     wndclass ;
	//static RECT al;

	if (!GameInfo) {
		FCEUD_PrintError("You must have a game loaded before you can use the Hex Editor.");
		return;
	}
	//if (GameInfo->type==GIT_NSF) {
	//	FCEUD_PrintError("Sorry, you can't yet use the Memory Viewer with NSFs.");
	//	return;
	//}

	if (!hMemView)
	{
		memset(&wndclass,0,sizeof(wndclass));
		wndclass.cbSize=sizeof(WNDCLASSEX);
		wndclass.style         = CS_HREDRAW | CS_VREDRAW ;
		wndclass.lpfnWndProc   = MemViewCallB ;
		wndclass.cbClsExtra    = 0 ;
		wndclass.cbWndExtra    = 0 ;
		wndclass.hInstance     = fceu_hInstance;
		wndclass.hIcon         = LoadIcon(fceu_hInstance, "ICON_1");
		wndclass.hIconSm       = LoadIcon(fceu_hInstance, "ICON_1");
		wndclass.hCursor       = LoadCursor (NULL, IDC_ARROW);
		wndclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
		wndclass.lpszMenuName  = "MEMVIEWMENU";
		wndclass.lpszClassName = "MEMVIEW";

		if(!RegisterClassEx(&wndclass)) 
		{
			FCEUD_PrintError("Error Registering MEMVIEW Window Class.");
			return;
		}

		hMemView = CreateWindowEx(0,"MEMVIEW","Memory Editor",
			//WS_OVERLAPPEDWINDOW|WS_CLIPSIBLINGS,  /* Style */
			WS_SYSMENU|WS_MAXIMIZEBOX|WS_MINIMIZEBOX|WS_THICKFRAME|WS_VSCROLL,
			CW_USEDEFAULT,CW_USEDEFAULT,580,248,  /* X,Y ; Width, Height */
			NULL,NULL,fceu_hInstance,NULL ); 
		ShowWindow (hMemView, SW_SHOW) ;
		UpdateCaption();
	} else
	{
		//SetWindowPos(hMemView, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE|SWP_NOMOVE|SWP_NOOWNERZORDER);
		ShowWindow(hMemView, SW_SHOWNORMAL);
		SetForegroundWindow(hMemView);
		UpdateCaption();
	}

	DragAcceptFiles(hMemView, 1);

	if (hMemView)
	{
		//UpdateMemView(0);
		//MemViewDoBlit();
	}
}

INT_PTR CALLBACK MemFindCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {

	switch(uMsg)
	{
		case WM_INITDIALOG:
			SetWindowPos(hwndDlg,0,MemFind_wndx,MemFind_wndy,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER);

			if(FindDirectionUp) CheckDlgButton(hwndDlg, IDC_MEMVIEWFIND_DIR_UP, BST_CHECKED);
			else CheckDlgButton(hwndDlg, IDC_MEMVIEWFIND_DIR_DOWN, BST_CHECKED);

			if(FindAsText) CheckDlgButton(hwndDlg, IDC_MEMVIEWFIND_TYPE_TEXT, BST_CHECKED);
			else CheckDlgButton(hwndDlg, IDC_MEMVIEWFIND_TYPE_HEX, BST_CHECKED);

			if(FindTextBox[0])SetDlgItemText(hwndDlg,IDC_MEMVIEWFIND_WHAT,FindTextBox);

			SendDlgItemMessage(hwndDlg,IDC_MEMVIEWFIND_WHAT,EM_SETLIMITTEXT,59,0);
			break;
		case WM_CREATE:

			break;
		case WM_PAINT:
			break;
		case WM_CLOSE:
		case WM_QUIT:
			GetDlgItemText(hwndDlg,IDC_MEMVIEWFIND_WHAT,FindTextBox,60);
			DestroyWindow(hwndDlg);
			hMemFind = 0;
			hwndDlg = 0;
			break;
		case WM_MOVING:
			break;
		case WM_MOVE: {
			if (!IsIconic(hwndDlg)) {
				RECT wrect;
				GetWindowRect(hwndDlg,&wrect);
				MemFind_wndx = wrect.left;
				MemFind_wndy = wrect.top;
		
				#ifdef WIN32
				WindowBoundsCheckNoResize(MemFind_wndx,MemFind_wndy,wrect.right);
				#endif
			}
			break;
		}
		case WM_RBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
			break;
		case WM_MOUSEMOVE:
			break;

		case WM_COMMAND:
			switch(HIWORD(wParam)) {
				case BN_CLICKED:
					switch(LOWORD(wParam))
					{
						case IDC_MEMVIEWFIND_TYPE_HEX :
							FindAsText=0;
							break;
						case IDC_MEMVIEWFIND_TYPE_TEXT :
							FindAsText=1;
							break;

						case IDC_MEMVIEWFIND_DIR_UP :
							FindDirectionUp = 1;
							break;
						case IDC_MEMVIEWFIND_DIR_DOWN :
							FindDirectionUp = 0;
							break;
						case IDC_MEMVIEWFIND_NEXT :
							FindNext();
							break;
					}
				break;
		}
		break;
		case WM_HSCROLL:
			break;
	}
	return FALSE;
}
void FindNext(){
	char str[60];
	unsigned char data[60];
	int datasize = 0, i, j, inputc = -1, found;

	if(hMemFind) GetDlgItemText(hMemFind,IDC_MEMVIEWFIND_WHAT,str,60);
	else strcpy(str,FindTextBox);

	for(i = 0;str[i] != 0;i++){
		if(!FindAsText){
			if(inputc == -1){
				if((str[i] >= 'a') && (str[i] <= 'f')) inputc = str[i]-('a'-0xA);
				if((str[i] >= 'A') && (str[i] <= 'F')) inputc = str[i]-('A'-0xA);
				if((str[i] >= '0') && (str[i] <= '9')) inputc = str[i]-'0';
			} else {
				if((str[i] >= 'a') && (str[i] <= 'f')) inputc = (inputc<<4)|(str[i]-('a'-0xA));
				if((str[i] >= 'A') && (str[i] <= 'F')) inputc = (inputc<<4)|(str[i]-('A'-0xA));
				if((str[i] >= '0') && (str[i] <= '9')) inputc = (inputc<<4)|(str[i]-'0');

				if(((str[i] >= 'a') && (str[i] <= 'f')) ||
					((str[i] >= 'A') && (str[i] <= 'F')) ||
					((str[i] >= '0') && (str[i] <= '9'))){
						data[datasize++] = inputc;
						inputc = -1;
				}
			}
		} else {
			for(j = 0;j < 256;j++)if(chartable[j] == str[i])break;
			if(j == 256)continue;
			data[datasize++] = j;
		}
	}

	if(datasize < 1){
		MessageBox(hMemView,"Invalid String","Error", MB_OK);
		return;
	}
	if(!FindDirectionUp){
		for(i = CursorStartAddy+1;i+datasize <= MaxSize;i++){
			found = 1;
			for(j = 0;j < datasize;j++){
				if(GetMemViewData(i+j) != data[j])found = 0;
			}
			if(found == 1){
				ChangeMemViewFocus(EditingMode,i, i+datasize-1);
				return;
			}
		}
		for(i = 0;i < CursorStartAddy;i++){
			found = 1;
			for(j = 0;j < datasize;j++){
				if(GetMemViewData(i+j) != data[j])found = 0;
			}
			if(found == 1){
				ChangeMemViewFocus(EditingMode,i, i+datasize-1);
				return;
			}
		}
	} else { //FindDirection is up
		for(i = CursorStartAddy-1;i >= 0;i--){
			found = 1;
			for(j = 0;j < datasize;j++){
				if(GetMemViewData(i+j) != data[j])found = 0;
			}
			if(found == 1){
				ChangeMemViewFocus(EditingMode,i, i+datasize-1);
				return;
			}
		}
		for(i = MaxSize-datasize;i > CursorStartAddy;i--){
			found = 1;
			for(j = 0;j < datasize;j++){
				if(GetMemViewData(i+j) != data[j])found = 0;
			}
			if(found == 1){
				ChangeMemViewFocus(EditingMode,i, i+datasize-1);
				return;
			}
		}
	}


	MessageBox(hMemView,"String Not Found","Error", MB_OK);
	return;
}


void OpenFindDialog()
{
	if (!hMemView)
		return;
	if (hMemFind)
		// set focus to the text field
		SendMessage(hMemFind, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hMemFind, IDC_MEMVIEWFIND_WHAT), true);
	else
		hMemFind = CreateDialog(fceu_hInstance,"MEMVIEWFIND",hMemView,MemFindCallB);
	return;
}

void PalettePoke(uint32 addr, uint8 data)
{
	data = data & 0x3F;
	addr = addr & 0x1F;
	if ((addr & 3) == 0)
	{
		addr = (addr & 0xC) >> 2;
		if (addr == 0)
		{
			PALRAM[0x00] = PALRAM[0x04] = PALRAM[0x08] = PALRAM[0x0C] = data;
		}
		else
		{
			UPALRAM[addr-1] = data;
		}
	}
	else
	{
		PALRAM[addr] = data;
	}
}

INT_PTR CALLBACK ImportBookmarkCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static int* tmpImportBookmarkProps;

	static HWND hTipMerge;
	static HWND hTipIgnore;
	static HWND hTipOverwrite;
	static HWND hTipKeep;
	static HWND hTipReassign;
	static HWND hTipDiscard;
	static HWND hTipConfirm;

	switch (uMsg)
	{
		case WM_INITDIALOG:
			CenterWindow(hwndDlg);
			tmpImportBookmarkProps = (int*)lParam;
			if (!(*tmpImportBookmarkProps & IMPORT_OVERWRITE_NO_PROMPT))
				CheckDlgButton(hwndDlg, IDC_CHECK_BOOKMARKIMPORTOPTION_CONFIRMEVERYTIMEONCONFLICT, BST_CHECKED);

			if (*tmpImportBookmarkProps & IMPORT_DISCARD_ORIGINAL)
			{
				CheckDlgButton(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_DISCARD, BST_CHECKED);

				EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_BOOKMARKIMPORTOPTION_SOLVECONFLICT), FALSE);
				EnableWindow(GetDlgItem(hwndDlg, IDC_TEXT_BOOKMARKIMPORTOPTION_BOOKMARK), FALSE);
				EnableWindow(GetDlgItem(hwndDlg, IDC_TEXT_BOOKMARKIMPORTOPTION_SHORTCUT), FALSE);
				EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKIGNORE), FALSE);
				EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKOVERWRITE), FALSE);
				EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTKEEP), FALSE);
				EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTREASSIGN), FALSE);
			}
			else
			{
				CheckDlgButton(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_MERGE, BST_CHECKED);

				EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_BOOKMARKIMPORTOPTION_SOLVECONFLICT), TRUE);
				EnableWindow(GetDlgItem(hwndDlg, IDC_TEXT_BOOKMARKIMPORTOPTION_BOOKMARK), TRUE);
				EnableWindow(GetDlgItem(hwndDlg, IDC_TEXT_BOOKMARKIMPORTOPTION_SHORTCUT), TRUE);
				EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKIGNORE), TRUE);
				EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKOVERWRITE), TRUE);
				EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTKEEP), TRUE);
				EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTREASSIGN), TRUE);
			}

			if (*tmpImportBookmarkProps & IMPORT_OVERWRITE_BOOKMARK)
				CheckDlgButton(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKOVERWRITE, BST_CHECKED);
			else
				CheckDlgButton(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKIGNORE, BST_CHECKED);

			if (*tmpImportBookmarkProps & IMPORT_OVERWRITE_SHORTCUT)
				CheckDlgButton(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTREASSIGN, BST_CHECKED);
			else
				CheckDlgButton(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTKEEP, BST_CHECKED);

			TOOLINFO info;
			memset(&info, 0, sizeof(TOOLINFO));
			info.cbSize = sizeof(TOOLINFO);
			info.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
			info.hwnd = hwndDlg;

			hTipMerge = CreateWindow(TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndDlg, NULL, fceu_hInstance, NULL);
			info.lpszText = "Merge the importing bookmarks into my list. \nThe non-conflict bookmarks will be append to the last. \nIf bookmarks or shortcuts have conflicts, \nsolve them according to the import settings.";
			info.uId = (UINT_PTR)GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_MERGE);
			SendMessage(hTipMerge, TTM_ADDTOOL, 0, (LPARAM)&info);
			SendMessage(hTipMerge, TTM_SETMAXTIPWIDTH, 0, 8000);
			SendMessage(hTipMerge, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);

			hTipIgnore = CreateWindow(TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndDlg, NULL, fceu_hInstance, NULL);
			info.lpszText = "If the importing bookmark has the same address \nand edit mode with one of the original bookmarks,\nkeep the exsiting one unchanged.";
			info.uId = (UINT_PTR)GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKIGNORE);
			SendMessage(hTipIgnore, TTM_ADDTOOL, 0, (LPARAM)&info);
			SendMessage(hTipIgnore, TTM_SETMAXTIPWIDTH, 0, 8000);
			SendMessage(hTipIgnore, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);

			hTipOverwrite = CreateWindow(TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndDlg, NULL, fceu_hInstance, NULL);
			info.lpszText = "If the importing bookmark has the same address \nand edit mode with one of the existing bookmarks,\noverwrite the information of the existing one with the importing one.";
			info.uId = (UINT_PTR)GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKOVERWRITE);
			SendMessage(hTipOverwrite, TTM_ADDTOOL, 0, (LPARAM)&info);
			SendMessage(hTipOverwrite, TTM_SETMAXTIPWIDTH, 0, 8000);
			SendMessage(hTipOverwrite, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);

			hTipKeep = CreateWindow(TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndDlg, NULL, fceu_hInstance, NULL);
			info.lpszText = "If two different bookmarks from importing and existing \nuse the same shortcut,\nthe shortcut is remained using by the existing one.\nthe importing one will not use the shortcut.";
			info.uId = (UINT_PTR)GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTKEEP);
			SendMessage(hTipKeep, TTM_ADDTOOL, 0, (LPARAM)&info);
			SendMessage(hTipKeep, TTM_SETMAXTIPWIDTH, 0, 8000);
			SendMessage(hTipKeep, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);

			hTipReassign = CreateWindow(TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndDlg, NULL, fceu_hInstance, NULL);
			info.lpszText = "If two different bookmarks from importing and existing \nuse the same shortcut,\nthe shortcut is assigned to the importing one.\nthe existing one will not use the shortcut.";
			info.uId = (UINT_PTR)GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTREASSIGN);
			SendMessage(hTipReassign, TTM_ADDTOOL, 0, (LPARAM)&info);
			SendMessage(hTipReassign, TTM_SETMAXTIPWIDTH, 0, 8000);
			SendMessage(hTipReassign, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);

			hTipDiscard = CreateWindow(TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndDlg, NULL, fceu_hInstance, NULL);
			info.lpszText = "Discard all existing bookmarks and then import.\nThis is not recommended.";
			info.uId = (UINT_PTR)GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_DISCARD);
			SendMessage(hTipDiscard, TTM_ADDTOOL, 0, (LPARAM)&info);
			SendMessage(hTipDiscard, TTM_SETMAXTIPWIDTH, 0, 8000);
			SendMessage(hTipDiscard, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);

			hTipConfirm = CreateWindow(TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndDlg, NULL, fceu_hInstance, NULL);
			info.lpszText = "Ask what to do every time when conflict occurs between existing and importing bookmarks.";
			info.uId = (UINT_PTR)GetDlgItem(hwndDlg, IDC_CHECK_BOOKMARKIMPORTOPTION_CONFIRMEVERYTIMEONCONFLICT);
			SendMessage(hTipConfirm, TTM_ADDTOOL, 0, (LPARAM)&info);
			SendMessage(hTipConfirm, TTM_SETMAXTIPWIDTH, 0, 8000);
			SendMessage(hTipConfirm, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);

			break;
		case WM_CLOSE:
		case WM_QUIT:
			DestroyWindow(hTipMerge);
			DestroyWindow(hTipIgnore);
			DestroyWindow(hTipOverwrite);
			DestroyWindow(hTipKeep);
			DestroyWindow(hTipReassign);
			DestroyWindow(hTipDiscard);
			DestroyWindow(hTipConfirm);

			EndDialog(hwndDlg, 0);
			break;
		case WM_COMMAND:
			switch (HIWORD(wParam))
			{
				case BN_CLICKED:
					switch (LOWORD(wParam))
					{
						case IDC_RADIO_BOOKMARKIMPORTOPTION_DISCARD:
							EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_BOOKMARKIMPORTOPTION_SOLVECONFLICT), FALSE);
							EnableWindow(GetDlgItem(hwndDlg, IDC_TEXT_BOOKMARKIMPORTOPTION_BOOKMARK), FALSE);
							EnableWindow(GetDlgItem(hwndDlg, IDC_TEXT_BOOKMARKIMPORTOPTION_SHORTCUT), FALSE);
							EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKIGNORE), FALSE);
							EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKOVERWRITE), FALSE);
							EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTKEEP), FALSE);
							EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTREASSIGN), FALSE);
							break;
						case IDC_RADIO_BOOKMARKIMPORTOPTION_MERGE:
							EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_BOOKMARKIMPORTOPTION_SOLVECONFLICT), TRUE);
							EnableWindow(GetDlgItem(hwndDlg, IDC_TEXT_BOOKMARKIMPORTOPTION_BOOKMARK), TRUE);
							EnableWindow(GetDlgItem(hwndDlg, IDC_TEXT_BOOKMARKIMPORTOPTION_SHORTCUT), TRUE);
							EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKIGNORE), TRUE);
							EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKOVERWRITE), TRUE);
							EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTKEEP), TRUE);
							EnableWindow(GetDlgItem(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTREASSIGN), TRUE);
						break;
						case IDOK:
							if (IsDlgButtonChecked(hwndDlg, IDC_CHECK_BOOKMARKIMPORTOPTION_CONFIRMEVERYTIMEONCONFLICT) == BST_UNCHECKED)
								*tmpImportBookmarkProps |= IMPORT_OVERWRITE_NO_PROMPT;
							else
								*tmpImportBookmarkProps &= ~IMPORT_OVERWRITE_NO_PROMPT;

							if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_DISCARD))
								*tmpImportBookmarkProps |= IMPORT_DISCARD_ORIGINAL;
							if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_MERGE))
								*tmpImportBookmarkProps &= ~IMPORT_DISCARD_ORIGINAL;

							if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKOVERWRITE))
								*tmpImportBookmarkProps |= IMPORT_OVERWRITE_BOOKMARK;
							if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_BOOKMARKIGNORE))
								*tmpImportBookmarkProps &= ~IMPORT_OVERWRITE_BOOKMARK;

							if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTREASSIGN))
								*tmpImportBookmarkProps |= IMPORT_OVERWRITE_SHORTCUT;
							if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_BOOKMARKIMPORTOPTION_SHORTCUTKEEP))
								*tmpImportBookmarkProps &= ~IMPORT_OVERWRITE_SHORTCUT;
							EndDialog(hwndDlg, 1);
						break;
					case IDCANCEL:
					case IDCLOSE:
						EndDialog(hwndDlg, 0);
						break;
					}
			}
	}
	return FALSE;
}

void SwitchEditingText(int editingText) {
	if (EditingText != editingText)
	{
		if (editingText) {
			HexTextColorList = DimTextColorList;
			HexBGColorList = DimBGColorList;
			AnsiTextColorList = CurTextColorList;
			AnsiBGColorList = CurBGColorList;
		}
		else
		{
			HexTextColorList = CurTextColorList;
			HexBGColorList = CurBGColorList;
			AnsiTextColorList = DimTextColorList;
			AnsiBGColorList = DimBGColorList;
		}
		EditingText = editingText;
	}
}

bool ChangeColor(HWND hwnd, CHOOSECOLORINFO* item)
{
	int backup = RGB(*item->r, *item->g, *item->b);
	CHOOSECOLOR choose;
	memset(&choose, 0, sizeof(CHOOSECOLOR));
	choose.lStructSize = sizeof(CHOOSECOLOR);
	choose.hwndOwner = hwnd;
	choose.rgbResult = backup;
	choose.lCustData = (LPARAM)item;
	choose.lpCustColors = custom_color;
	choose.lpfnHook = (LPCCHOOKPROC)ChooseColorHookProc;
	choose.Flags = CC_RGBINIT | CC_FULLOPEN | CC_ANYCOLOR | CC_ENABLEHOOK;
	if (ChooseColor(&choose) && choose.rgbResult != backup)
	{
		*item->r = GetRValue(choose.rgbResult);
		*item->g = GetGValue(choose.rgbResult);
		*item->b = GetBValue(choose.rgbResult);
		return true;
	}
	return false;

}


BOOL OpColorMenu(HWND hwnd, HMENU menu, COLORMENU* item, int pos, int id, BOOL (WINAPI *opMenu)(HMENU hmenu, UINT item, BOOL byPos, LPCMENUITEMINFO info))
{

	MENUITEMINFO info;
	memset(&info, 0, sizeof(MENUITEMINFO));
	info.cbSize = sizeof(MENUITEMINFO);

	CHOOSECOLORINFO *color_info = (CHOOSECOLORINFO*)item;
	if (color_info->name)
	{

		if (item->bitmap)
			DeleteObject(item->bitmap);
		item->bitmap = CreateColorBitmap(hwnd, GetSystemMetrics(SM_CXMENUCHECK), GetSystemMetrics(SM_CYMENUCHECK), RGB(*color_info->r, *color_info->g, *color_info->b));

		char menu_str[64];
		sprintf(menu_str, "%s\t#%02X%02X%02X", color_info->name, *color_info->r, *color_info->g, *color_info->b);

		info.dwTypeData = menu_str;
		info.cch = strlen(menu_str);
		info.hbmpUnchecked = item->bitmap;
		info.wID = id;

		info.fMask = MIIM_ID | MIIM_TYPE | MIIM_CHECKMARKS;
		info.fType = MFT_STRING;
	}
	else
	{
		info.fMask = MIIM_TYPE;
		info.fType = MFT_SEPARATOR;
	}

	return opMenu(menu, pos, TRUE, &info);
}

UINT CALLBACK ChooseColorHookProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
	static HICON icon;

	switch (uiMsg)
	{
		case WM_INITDIALOG:
		{
			CHOOSECOLORINFO* item = (CHOOSECOLORINFO*)((CHOOSECOLOR*)lParam)->lCustData;

			char title[128];
			strcpy(title, "Choose color for ");
			strcat(title, item->name);
			SetWindowText(hdlg, title);

			icon = CreateChooseColorIcon(hdlg, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), RGB(*item->r, *item->g, *item->b));
			SendMessage(hdlg, WM_SETICON, ICON_SMALL, (LPARAM)icon);
			break;
		}
		case WM_CLOSE:
		case WM_QUIT:
			DestroyIcon(icon);
	}

	return FALSE;
}

HBITMAP CreateColorBitmap(HWND hwnd, int width, int height, COLORREF color)
{

	HDC hdc = GetDC(hwnd);
	HDC memdc = CreateCompatibleDC(hdc);

	HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
	HGDIOBJ oldObj = SelectObject(memdc, bitmap);
	HBRUSH brush = CreateSolidBrush(color);
	RECT rect = { 1, 1, width - 1, height - 1 };
	FillRect(memdc, &rect, brush);

	SelectObject(memdc, oldObj);
	DeleteObject(brush);
	DeleteDC(memdc);
	ReleaseDC(hwnd, hdc);

	return bitmap;
}

HICON CreateChooseColorIcon(HWND hwnd, int width, int height, COLORREF color)
{

	HBITMAP bitmap = CreateColorBitmap(hwnd, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), color);

	ICONINFO info;
	memset(&info, 0, sizeof(ICONINFO));
	info.hbmColor = bitmap;
	info.hbmMask = bitmap;
	info.fIcon = true;
	HICON icon = CreateIconIndirect(&info);

	DeleteObject(bitmap);

	return icon;
}