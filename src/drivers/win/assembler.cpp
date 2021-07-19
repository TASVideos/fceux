#include <Windows.h>

#include "types.h"
#include "gui.h"
#include "resource.h"
#include "debugger.h"
#include "../../debug.h"
#include "asm.h"
#include "../../x6502.h"
#include "../../fceu.h"
#include "../../debug.h"
#include "../../nsf.h"
#include "../../ppu.h"
#include "../../cart.h"
#include "../../ines.h"
#include "../../asm.h"

int AddAsmHistory(HWND hwndDlg, int id, char *str) {
	int index;
	index = SendDlgItemMessage(hwndDlg, id, CB_FINDSTRINGEXACT, -1, (LPARAM)(LPSTR)str);
	if (index == CB_ERR) {
		SendDlgItemMessage(hwndDlg, id, CB_INSERTSTRING, -1, (LPARAM)(LPSTR)str);
		return 0;
	}
	return 1;
}

INT_PTR CALLBACK AssemblerCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	int romaddr, count, i, j;
	char str[128], *dasm;
	static int patchlen, applied, saved, lastundo;
	static uint8 patchdata[64][3], undodata[64 * 3];
	uint8 *ptr;

	switch (uMsg) {
	case WM_INITDIALOG:
		CenterWindow(hwndDlg);

		//set font
		SendDlgItemMessage(hwndDlg, IDC_ASSEMBLER_DISASSEMBLY, WM_SETFONT, (WPARAM)debugSystem->hFixedFont, FALSE);
		SendDlgItemMessage(hwndDlg, IDC_ASSEMBLER_PATCH_DISASM, WM_SETFONT, (WPARAM)debugSystem->hFixedFont, FALSE);

		//set limits
		SendDlgItemMessage(hwndDlg, IDC_ASSEMBLER_HISTORY, CB_LIMITTEXT, 20, 0);

		SetDlgItemText(hwndDlg, IDC_ASSEMBLER_DISASSEMBLY, DisassembleLine(iaPC));
		SetFocus(GetDlgItem(hwndDlg, IDC_ASSEMBLER_HISTORY));

		patchlen = 0;
		applied = 0;
		saved = 0;
		lastundo = 0;
		break;
	case WM_CLOSE:
	case WM_QUIT:
		EndDialog(hwndDlg, 0);
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
					ptr = GetNesPRGPointer(GetNesFileAddress(iaPC) - 16);
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
					if (patchlen) sprintf(str, "Write patch data to file at addresses 0x%06X - 0x%06X?", romaddr, count - 1);
					else sprintf(str, "Undo all previously applied patches?");
					if (MessageBox(hwndDlg, str, "Save changes to file?", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
						if (iNesSave()) {
							saved = 1;
							applied = 0;
						}
						else MessageBox(hwndDlg, "Unable to save changes to file", "Error saving to file", MB_OK | MB_ICONERROR);
					}
				}
				break;
			case IDC_ASSEMBLER_UNDO:
				if ((count = SendDlgItemMessage(hwndDlg, IDC_ASSEMBLER_PATCH_DISASM, LB_GETCOUNT, 0, 0))) {
					SendDlgItemMessage(hwndDlg, IDC_ASSEMBLER_PATCH_DISASM, LB_DELETESTRING, count - 1, 0);
					patchlen--;
					count = 0;
					for (i = 0; i < patchlen; i++)
					{
						count += opsize[patchdata[i][0]];
					}
					if (count < lastundo) {
						ptr = GetNesPRGPointer(GetNesFileAddress(iaPC) - 16);
						j = opsize[patchdata[patchlen][0]];
						for (i = count; i < (count + j); i++) {
							ptr[i] = undodata[i];
						}
						lastundo -= j;
						applied = 1;
					}
					SetDlgItemText(hwndDlg, IDC_ASSEMBLER_DISASSEMBLY, DisassembleLine(iaPC + count));
				}
				break;
			case IDC_ASSEMBLER_DEFPUSHBUTTON:
				count = 0;
				for (i = 0; i < patchlen; i++)
				{
					count += opsize[patchdata[i][0]];
				}
				GetDlgItemText(hwndDlg, IDC_ASSEMBLER_HISTORY, str, 21);
				if (!Assemble(patchdata[patchlen], (iaPC + count), str)) {
					count = iaPC;
					for (i = 0; i <= patchlen; i++)
					{
						count += opsize[patchdata[i][0]];
					}
					if (count > 0x10000) { //note: don't use 0xFFFF!
						MessageBox(hwndDlg, "Patch data cannot exceed address 0xFFFF", "Address error", MB_OK | MB_ICONERROR);
						break;
					}
					SetDlgItemText(hwndDlg, IDC_ASSEMBLER_HISTORY, "");
					if (count < 0x10000) SetDlgItemText(hwndDlg, IDC_ASSEMBLER_DISASSEMBLY, DisassembleLine(count));
					else SetDlgItemText(hwndDlg, IDC_ASSEMBLER_DISASSEMBLY, "OVERFLOW");
					dasm = DisassembleData((count - opsize[patchdata[patchlen][0]]), patchdata[patchlen]);
					SendDlgItemMessage(hwndDlg, IDC_ASSEMBLER_PATCH_DISASM, LB_INSERTSTRING, -1, (LPARAM)(LPSTR)dasm);
					AddAsmHistory(hwndDlg, IDC_ASSEMBLER_HISTORY, dasm + 16);
					SetWindowText(hwndDlg, "Inline Assembler");
					patchlen++;
				}
				else { //ERROR!
					SetWindowText(hwndDlg, "Inline Assembler  *Syntax Error*");
					MessageBeep(MB_ICONEXCLAMATION);
				}
				break;
			}
			SetFocus(GetDlgItem(hwndDlg, IDC_ASSEMBLER_HISTORY)); //set focus to combo box after anything is pressed!
			break;
		}
		}
		break;
	}
	}
	return FALSE;
}
