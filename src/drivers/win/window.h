#ifndef WIN_WINDOW_H
#define WIN_WINDOW_H

#include "common.h"
#include <string>

using namespace std;

// Type definitions

struct CreateMovieParameters
{
	std::string szFilename;				// on Dialog creation, this is the default filename to display.  On return, this is the filename that the user chose.
	int recordFrom;				// 0 = "Power-On", 1 = "Reset", 2 = "Now", 3+ = savestate file in szSavestateFilename
	std::string szSavestateFilename;
	std::wstring authorName;
};

extern char *recent_files[];
extern char *recent_lua[];
extern char *recent_movie[];
// extern HWND pwindow;

HWND GetMainHWND();

void SetMainWindowText();
void HideFWindow(int h);
void SetMainWindowStuff();
int GetClientAbsRect(LPRECT lpRect);
void FixWXY(int pref, bool shift_held = false);
void ByebyeWindow();
void DoTimingConfigFix();
int CreateMainWindow();
void UpdateCheckedMenuItems();
void LoadNewGamey(HWND hParent, const char *initialdir);
int BrowseForFolder(HWND hParent, const char *htext, char *buf);
void SetMainWindowStuff();
void GetMouseData(uint32 (&md)[3]);
void GetMouseRelative(int32 (&md)[3]);
//void ChangeMenuItemText(int menuitem, string text);
POINT CalcSubWindowPos(HWND hDlg, POINT* conf);

template<int BUFSIZE>
inline std::string GetDlgItemText(HWND hDlg, int nIDDlgItem) {
	char buf[BUFSIZE];
	GetDlgItemText(hDlg, nIDDlgItem, buf, BUFSIZE);
	return buf;
}

template<int BUFSIZE>
inline std::wstring GetDlgItemTextW(HWND hDlg, int nIDDlgItem) {
	wchar_t buf[BUFSIZE];
	GetDlgItemTextW(hDlg, nIDDlgItem, buf, BUFSIZE);
	return buf;
}

#define FCEUMENU_INDEX int
// the index start from 0 should exaclty match the submenu index defined in the resource file until the last one;
#define FCEUMENU_CONTEXT_OFF 0 // NoGame
#define FCEUMENU_CONTEXT_GAME 1 // Game+NoMovie
#define FCEUMENU_CONTEXT_PLAYING_READONLY 2 //Game+Movie+Playing+ReadOnly
#define	FCEUMENU_CONTEXT_PLAYING_READWRITE 3 //Game+Movie+Playing+ReadWrite
#define FCEUMENU_CONTEXT_RECORDING_READONLY 4 //Game+Movie+Recording+ReadOnly
#define FCEUMENU_CONTEXT_RECORDING_READWRITE 5 //Game+Movie+Recording+Readwrite
#define FCEUMENU_CONTEXT 6 // parent menu of all context menus,
						   // not recommended to use it since there's duplicate ids in context menus,
						   // unless you're quite sure the id is unique in all submenus.
#define FCEUMENU_MAIN 7 // main fceux menu
#define FCEUMENU_MAIN_INPUTDISPLAY 8 // a special one for the input display
                                     // "&Config"->"&Display"-> "&Input Display"
#define FCEUMENU_LIMIT 9

#define GetHMENU(index, hmenu) \
	switch (index) \
	{ \
	case FCEUMENU_MAIN: hmenu = fceumenu; break; \
	case FCEUMENU_MAIN_INPUTDISPLAY: hmenu = GetSubMenu(GetSubMenu(GetSubMenu(fceumenu, 2), 2), 0); break; \
	case FCEUMENU_CONTEXT: hmenu = hfceuxcontext; break; \
	default: hmenu = GetSubMenu(hfceuxcontext, index); \
	} \

/* used to indicate which parent menu is the HOTKEYMENUINDEX belongs to.
   How to use:
   use MENU_BELONG(menu_name) to make it to the correct bit, menu_name must
   be the FCEUMENU_XXXX defined above, and FCEUMENU_ should be ignored. For
   example, MENU_BELONG(CONTEXT_RECORDING_READONLY) means a menu is belong
   to FCEUMENU_CONTEXT_PLAYING_READONLY as Game+Movie+Recording+ReadOnly.
   Since the menus with the same id can coexist in different parent menus,
   the identifier can be multipled by OR. You can also the predefined
   BELONG_XXXX_XXXX.
*/
#define MENU_BELONG_INT int
#define MENU_BELONG(menu_index) (1 << FCEUMENU_##menu_index)
// ReadOnly and ReadWrite in playing status
#define BELONG_CONTEXT_PLAYING (MENU_BELONG(CONTEXT_PLAYING_READONLY) | MENU_BELONG(CONTEXT_PLAYING_READWRITE))
// ReadOnly and ReadWrite in recording status
#define BELONG_CONTEXT_RECORDING (MENU_BELONG(CONTEXT_RECORDING_READONLY) | MENU_BELONG(CONTEXT_RECORDING_READWRITE))
// ReadOnly in playing and recording status
#define BELONG_CONTEXT_READONLY (MENU_BELONG(CONTEXT_PLAYING_READONLY) | MENU_BELONG(CONTEXT_RECORDING_READONLY))
// Readwrite in playing and recording status
#define BELONG_CONTEXT_READWRITE (MENU_BELONG(CONTEXT_PLAYING_READWRITE) | MENU_BELONG(CONTEXT_RECORDING_READWRITE))
// All status with movie
#define BELONG_CONTEXT_MOVIE_ALL (MENU_BELONG(CONTEXT_PLAYING_READONLY) | MENU_BELONG(CONTEXT_PLAYING_READWRITE) | MENU_BELONG(CONTEXT_RECORDING_READONLY) | MENU_BELONG(CONTEXT_RECORDING_READWRITE))
// All status without movie
#define BELONG_CONTEXT_NOMOVIE (MENU_BELONG(CONTEXT_OFF)| MENU_BELONG(CONTEXT_GAME))
// All status when game is loaded
#define BELONG_CONTEXT_RUNNING (BELONG_CONTEXT_MOVIE_ALL | MENU_BELONG(CONTEXT_GAME))
// All context menus
#define BELONG_CONTEXT_ALL (BELONG_CONTEXT_NOMOVIE | BELONG_CONTEXT_MOVIE_ALL)
// ALL menus
#define BELONG_ALL (BELONG_CONTEXT_ALL | MENU_BELONG(MAIN))

#define IsMenuBelongsTo(menu_index, hmenu_ident) (1 << menu_index & hmenu_ident)

struct HOTKEYMENUINDEX {
	int menu_id; // menu ID
	int cmd_id;  // hotkey ID
	MENU_BELONG_INT hmenu_ident = MENU_BELONG(MAIN); // whitch menu it belongs to, can be multiple, refers to HOTKEYMENU_PARENTMENU_INDEX
	int flags = MF_BYCOMMAND; // flags when searching and modifying menu item, usually MF_BYCOMMAND
	// returns an std::string contains original menu text followed with shortcut keys.
	std::string getQualifiedMenuText(MENU_BELONG_INT parent_index = MENU_BELONG(MAIN));
	// this is used when you only want to create a qualified menu text String
	static std::string getQualifiedMenuText(char* text, int cmdid);
	void updateMenuText(FCEUMENU_INDEX index);
	HOTKEYMENUINDEX(int _id, int _cmd, MENU_BELONG_INT _menu = MENU_BELONG(MAIN), int _flags = MF_BYCOMMAND) : menu_id(_id), cmd_id(_cmd), hmenu_ident(_menu), flags(_flags) {}
};

void UpdateMenuHotkeys(FCEUMENU_INDEX index);
int GetCurrentContextIndex();

inline bool (*GetIsLetterLegal(UINT id))(char letter);
inline wchar_t* GetLetterIllegalErrMsg(bool(*IsLetterLegal)(char letter));
void ShowLetterIllegalBalloonTip(HWND hwnd, bool(*IsLetterLegal)(char letter));
inline bool IsInputLegal(bool(*IsLetterLegal)(char letter), char letter);
inline bool IsLetterLegalGG(char letter);
inline bool IsLetterLegalHex(char letter);
inline bool IsLetterLegalHexList(char letter);
inline bool IsLetterLegalCheat(char letter);
inline bool IsLetterLegalDec(char letter);
inline bool IsLetterLegalSize(char letter);
inline bool IsLetterLegalFloat(char letter);
inline bool IsLetterLegalDecHexMixed(char letter);
inline bool IsLetterLegalUnsignedDecHexMixed(char letter);
inline bool IsLetterLegalMemoryWatch(char letter);

extern WNDPROC DefaultEditCtrlProc;
extern LRESULT APIENTRY FilterEditCtrlProc(HWND hDlg, UINT msg, WPARAM wP, LPARAM lP);

#endif
