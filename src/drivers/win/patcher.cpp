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

int *GetEditHexData(HWND hwndDlg, int id) {
	static int data[31];
	char str[60];
	int i, j, k;

	GetDlgItemText(hwndDlg, id, str, 60);
	memset(data, 0, 31 * sizeof(int));
	j = 0;
	for (i = 0; i < 60; i++) {
		if (str[i] == 0)break;
		if ((str[i] >= '0') && (str[i] <= '9'))j++;
		if ((str[i] >= 'A') && (str[i] <= 'F'))j++;
		if ((str[i] >= 'a') && (str[i] <= 'f'))j++;
	}

	j = j & 1;
	for (i = 0; i < 60; i++) {
		if (str[i] == 0)break;
		k = -1;
		if ((str[i] >= '0') && (str[i] <= '9'))k = str[i] - '0';
		if ((str[i] >= 'A') && (str[i] <= 'F'))k = (str[i] - 'A') + 10;
		if ((str[i] >= 'a') && (str[i] <= 'f'))k = (str[i] - 'a') + 10;
		if (k != -1) {
			if (j & 1)data[j >> 1] |= k;
			else data[j >> 1] |= k << 4;
			j++;
		}
	}
	data[j >> 1] = -1;
	return data;
}

void UpdatePatcher(HWND hwndDlg) {
	char str[75];
	uint8 *p;
	if (iapoffset != -1) {
		EnableWindow(GetDlgItem(hwndDlg, IDC_ROMPATCHER_PATCH_DATA), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_ROMPATCHER_BTN_APPLY), TRUE);

		if (GetRomAddress(iapoffset) != -1)sprintf(str, "Current Data at NES ROM Address: %04X, .NES file Address: %04X", GetRomAddress(iapoffset), iapoffset);
		else sprintf(str, "Current Data at .NES file Address: %04X", iapoffset);

		SetDlgItemText(hwndDlg, IDC_ROMPATCHER_CURRENT_DATA_BOX, str);

		sprintf(str, "%04X", GetRomAddress(iapoffset));
		SetDlgItemText(hwndDlg, IDC_ROMPATCHER_DISASSEMBLY, str);

		if (GetRomAddress(iapoffset) != -1)SetDlgItemText(hwndDlg, IDC_ROMPATCHER_DISASSEMBLY, DisassembleLine(GetRomAddress(iapoffset)));
		else SetDlgItemText(hwndDlg, IDC_ROMPATCHER_DISASSEMBLY, "Not Currently Loaded in ROM for disassembly");

		p = GetNesPRGPointer(iapoffset - 16);
		sprintf(str, "%02X %02X %02X %02X %02X %02X %02X %02X",
			p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
		SetDlgItemText(hwndDlg, IDC_ROMPATCHER_CURRENT_DATA, str);

	}
	else {
		SetDlgItemText(hwndDlg, IDC_ROMPATCHER_CURRENT_DATA_BOX, "No Offset Selected");
		SetDlgItemText(hwndDlg, IDC_ROMPATCHER_CURRENT_DATA, "");
		SetDlgItemText(hwndDlg, IDC_ROMPATCHER_DISASSEMBLY, "");
		EnableWindow(GetDlgItem(hwndDlg, IDC_ROMPATCHER_PATCH_DATA), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_ROMPATCHER_BTN_APPLY), FALSE);
	}
	if (GameInfo->type != GIT_CART)EnableWindow(GetDlgItem(hwndDlg, IDC_ROMPATCHER_BTN_SAVE), FALSE);
	else EnableWindow(GetDlgItem(hwndDlg, IDC_ROMPATCHER_BTN_SAVE), TRUE);
}

INT_PTR CALLBACK PatcherCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	char str[64];
	uint8 *c;
	int i;
	int *p;

	switch (uMsg) {
	case WM_INITDIALOG:
		CenterWindow(hwndDlg);

		//set limits
		SendDlgItemMessage(hwndDlg, IDC_ROMPATCHER_OFFSET, EM_SETLIMITTEXT, 6, 0);
		SendDlgItemMessage(hwndDlg, IDC_ROMPATCHER_PATCH_DATA, EM_SETLIMITTEXT, 30, 0);
		UpdatePatcher(hwndDlg);

		if (iapoffset != -1) {
			CheckDlgButton(hwndDlg, IDC_ROMPATCHER_DOTNES_OFFSET, BST_CHECKED);
			sprintf((char*)str, "%X", iapoffset);
			SetDlgItemText(hwndDlg, IDC_ROMPATCHER_OFFSET, str);
		}

		SetFocus(GetDlgItem(hwndDlg, IDC_ROMPATCHER_OFFSET_BOX));
		break;
	case WM_CLOSE:
	case WM_QUIT:
		EndDialog(hwndDlg, 0);
		break;
	case WM_COMMAND:
		switch (HIWORD(wParam)) {
		case BN_CLICKED:
			switch (LOWORD(wParam)) {
			case IDC_ROMPATCHER_BTN_EDIT: //todo: maybe get rid of this button and cause iapoffset to update every time you change the text
				if (IsDlgButtonChecked(hwndDlg, IDC_ROMPATCHER_DOTNES_OFFSET) == BST_CHECKED)
					iapoffset = GetEditHex(hwndDlg, IDC_ROMPATCHER_OFFSET);
				else
					iapoffset = GetNesFileAddress(GetEditHex(hwndDlg, IDC_ROMPATCHER_OFFSET));
				if ((iapoffset < 16) && (iapoffset != -1)) {
					MessageBox(hDebug, "Sorry, iNES Header editing isn't supported by this tool. If you want to edit the header, please use iNES Header Editor", "Error", MB_OK | MB_ICONASTERISK);
					iapoffset = -1;
				}
				if ((iapoffset > PRGsize[0]) && (iapoffset != -1)) {
					MessageBox(hDebug, "Error: .Nes offset outside of PRG rom", "Error", MB_OK | MB_ICONERROR);
					iapoffset = -1;
				}
				UpdatePatcher(hwndDlg);
				break;
			case IDOK:
			case IDC_ROMPATCHER_BTN_APPLY:
				p = GetEditHexData(hwndDlg, IDC_ROMPATCHER_PATCH_DATA);
				i = 0;
				c = GetNesPRGPointer(iapoffset - 16);
				while (p[i] != -1) {
					c[i] = p[i];
					i++;
				}
				UpdatePatcher(hwndDlg);
				break;
			case IDC_ROMPATCHER_BTN_SAVE:
				if (!iNesSave())
					MessageBox(NULL, "Error Saving", "Error", MB_OK | MB_ICONERROR);
				break;
			case IDCANCEL:
				EndDialog(hwndDlg, 0);
				break;
			}
			break;
		}
		break;
	}
	return FALSE;
}
