#include "../../version.h"
#include "common.h"
#include "main.h"
#include "window.h"
#include "gui.h"
#include "../../palette.h"
#include "memview.h"

#define ToGrey(r, g, b) ((int)((float)(r) * 0.299F + (float)(g) * 0.587F + (float)(b) * 0.114F))
#define GetPaletteIndex(x, y) ((x) > palpv->pvRect.left && (x) < palpv->pvRect.right && (y) > palpv->pvRect.top && (y) < palpv->pvRect.bottom ? floor((float)((x) - palpv->pvRect.left) / palpv->cell_width) + (int)floor((float)((y) - palpv->pvRect.top) / palpv->cell_height) * 16 : -1)

int cpalette_count = 0;
u8 cpalette[64*8*3] = {0};
extern int palnotch;
extern int palsaturation;
extern int palsharpness;
extern int palcontrast;
extern int palbrightness;
extern bool paldeemphswap;
bool palcolorindex = false;
HWND hWndPal = NULL;
extern pal palette[512], rp2c04001[512], rp2c04002[512], rp2c04003[512], rp2c05004[512], palette_game[64*8], palette_ntsc[64*8], palette_user[64*8], palette_unvarying[23];

struct PALPV {
	int mouse_index;
	float cell_height, cell_width;
	char buf[4];
	RECT pvRect;
	SIZE pvSize;
	HBITMAP pvBitmap;
	HFONT font;
} *palpv;

bool SetPalette(const char* nameo)
{
	FILE *fp;
	if((fp = FCEUD_UTF8fopen(nameo, "rb")))
	{
		int readed = fread(cpalette, 1, sizeof(cpalette), fp);
		fclose(fp);
		cpalette_count = readed/3;
		FCEUI_SetUserPalette(cpalette,cpalette_count);
		eoptions |= EO_CPALETTE;
		return true;
	}
	else
	{
		FCEUD_PrintError("Error opening palette file!");
		return false;
	}
}

/**
* Prompts the user for a palette file and opens that file.
*
* @return Flag that indicates failure (0) or success (1)
**/
int LoadPaletteFile(HWND hwnd)
{
	const char filter[]="Palette file (*.pal)\0*.pal\0All Files (*.*)\0*.*\0\0";

	bool success = false;
	char nameo[2048];

	// Display open file dialog
	OPENFILENAME ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hInstance = fceu_hInstance;
	ofn.lpstrTitle = FCEU_NAME" Open Palette File...";
	ofn.lpstrFilter = filter;
	nameo[0] = 0;
	ofn.lpstrFile = nameo;
	ofn.nMaxFile = 256;
	ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
	ofn.lpstrInitialDir = 0;

	if(GetOpenFileName(&ofn))
	{
		success = SetPalette(nameo);
		if (!success)
			MessageBox(hwnd, "Error loading palette file.", "Load palette file", MB_OK | MB_ICONERROR);
	}

	return success;
}

/**
* Prompts the user for a palette file and saves that file.
* @return Flag that indicates failure (0) or success (1)
**/
int SavePaletteFile(HWND hwnd)
{
	const char filter[] = "Palette file (*.pal)\0*.pal\0All Files (*.*)\0*.*\0\0";

	bool success = false;
	char nameo[2048];

	// Display save file dialog
	OPENFILENAME ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hInstance = fceu_hInstance;
	ofn.lpstrTitle = FCEU_NAME" Save Palette File...";
	ofn.lpstrFilter = filter;
	ofn.lpstrDefExt = "pal";
	nameo[0] = 0;
	ofn.lpstrFile = nameo;
	ofn.nMaxFile = 256;
	ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
	ofn.lpstrInitialDir = 0;

	if (GetSaveFileName(&ofn))
	{
		FILE* pal_file = fopen(nameo, "wb");
		if (pal_file)
		{
			fwrite(palo, 64 * 3, 1, pal_file);
			fclose(pal_file);
			success = true;
		}
		else
			MessageBox(hwnd, "Error saving palette file.", "Save palette file", MB_OK | MB_ICONERROR);
	}

	return success;
}

/**
* Notify the dialog to redraw the palette preview area
**/
void InvalidatePalettePreviewRect(HWND hwnd)
{
	InvalidateRect(hwnd, &palpv->pvRect, FALSE);
	UpdateWindow(hwnd);
}

void UpdatePalettePreviewCaption(HWND hwnd, int x, int y)
{
	int mouse_index = GetPaletteIndex(x, y);

	if (palpv->mouse_index != mouse_index)
	{
		if (mouse_index != -1)
		{
			char str[64];
			if (force_grayscale)
				sprintf(str, "Greyscale $%02X: #%02X (Actural: #%02X%02X%02X)", mouse_index, ToGrey(palo[mouse_index].r, palo[mouse_index].g, palo[mouse_index].b), palo[mouse_index].r, palo[mouse_index].g, palo[mouse_index].b);
			else
				sprintf(str, "Color $%02X: #%02X%02X%02X", mouse_index, palo[mouse_index].r, palo[mouse_index].g, palo[mouse_index].b);
			SetDlgItemText(hwnd, IDC_PALETTE_PREVIEW_GROUPBOX, str);
		}
		else
			SetDlgItemText(hwnd, IDC_PALETTE_PREVIEW_GROUPBOX, "Preview");
		palpv->mouse_index = mouse_index;
	}
}

void UpdatePalettePreviewCaption(HWND hwnd)
{
	POINT p;
	GetCursorPos(&p);
	ScreenToClient(hwnd, &p);

	palpv->mouse_index = -1;
	UpdatePalettePreviewCaption(hwnd, p.x, p.y);

}

void UpdateCurrentPaletteName(HWND hwnd)
{
	if (!palo)
		SetDlgItemText(hwnd, IDC_PALETTE_CURRENT, "None");
	else if (palo == (pal*)&palette)
		SetDlgItemText(hwnd, IDC_PALETTE_CURRENT, "NES Default");
	else if (palo == (pal*)&rp2c04001)
		SetDlgItemText(hwnd, IDC_PALETTE_CURRENT, "RP2C04-01");
	else if (palo == (pal*)&rp2c04002)
		SetDlgItemText(hwnd, IDC_PALETTE_CURRENT, "RP2C04-02");
	else if (palo == (pal*)&rp2c04003)
		SetDlgItemText(hwnd, IDC_PALETTE_CURRENT, "RP2C04-03");
	else if (palo == (pal*)&rp2c05004)
		SetDlgItemText(hwnd, IDC_PALETTE_CURRENT, "RC2C05-04");
	else if (palo == (pal*)&palette_game)
		SetDlgItemText(hwnd, IDC_PALETTE_CURRENT, "Game specific");
	else if (palo == (pal*)&palette_ntsc)
		SetDlgItemText(hwnd, IDC_PALETTE_CURRENT, "NTSC");
	else if (palo == (pal*)&palette_user)
		SetDlgItemText(hwnd, IDC_PALETTE_CURRENT, "Custom");
	else if (palo == (pal*)&palette_unvarying)
		SetDlgItemText(hwnd, IDC_PALETTE_CURRENT, "Internal");
	else
		SetDlgItemText(hwnd, IDC_PALETTE_CURRENT, "Unknown");
}

/**
* Callback function for the palette configuration dialog.
**/
INT_PTR CALLBACK PaletteConCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{

	switch(uMsg)
	{
		case WM_INITDIALOG:
		{

			if(ntsccol_enable)
				CheckDlgButton(hwndDlg, CHECK_PALETTE_ENABLED, BST_CHECKED);

			if(paldeemphswap)
				CheckDlgButton(hwndDlg, CHECK_DEEMPH_SWAP, BST_CHECKED);

			if(force_grayscale)
				CheckDlgButton(hwndDlg, CHECK_PALETTE_GRAYSCALE, BST_CHECKED);

			if (eoptions & EO_CPALETTE)
				CheckDlgButton(hwndDlg, CHECK_PALETTE_CUSTOM, BST_CHECKED);

			if (palcolorindex)
				CheckDlgButton(hwndDlg, CHECK_PALETTE_COLOR_INDEX, BST_CHECKED);

			SendDlgItemMessage(hwndDlg, CTL_TINT_TRACKBAR,       TBM_SETRANGE, 1, MAKELONG(0, 128));
			SendDlgItemMessage(hwndDlg, CTL_HUE_TRACKBAR,        TBM_SETRANGE, 1, MAKELONG(0, 128));
			SendDlgItemMessage(hwndDlg, CTL_PALNOTCH_TRACKBAR,   TBM_SETRANGE, 1, MAKELONG(0, 100));
			SendDlgItemMessage(hwndDlg, CTL_PALSAT_TRACKBAR,     TBM_SETRANGE, 1, MAKELONG(0, 200));
			SendDlgItemMessage(hwndDlg, CTL_PALSHARP_TRACKBAR,   TBM_SETRANGE, 1, MAKELONG(0,  50));
			SendDlgItemMessage(hwndDlg, CTL_PALCONTRAST_TRACKBAR,TBM_SETRANGE, 1, MAKELONG(0, 200));
			SendDlgItemMessage(hwndDlg, CTL_PALBRIGHT_TRACKBAR,  TBM_SETRANGE, 1, MAKELONG(0, 100));

			char text[40];
			FCEUI_GetNTSCTH(&ntsctint, &ntschue);
			sprintf(text, "Notch: %d%%", palnotch);
			SendDlgItemMessage(hwndDlg, STATIC_NOTCHVALUE,   WM_SETTEXT, 0, (LPARAM) text);
			sprintf(text, "Saturation: %d%%", palsaturation);
			SendDlgItemMessage(hwndDlg, STATIC_SATVALUE,     WM_SETTEXT, 0, (LPARAM) text);
			sprintf(text, "Sharpness: %d%%", palsharpness);
			SendDlgItemMessage(hwndDlg, STATIC_SHARPVALUE,   WM_SETTEXT, 0, (LPARAM) text);
			sprintf(text, "Contrast: %d%%", palcontrast);
			SendDlgItemMessage(hwndDlg, STATIC_CONTRASTVALUE,WM_SETTEXT, 0, (LPARAM) text);
			sprintf(text, "Brightness: %d%%", palbrightness);
			SendDlgItemMessage(hwndDlg, STATIC_BRIGHTVALUE,  WM_SETTEXT, 0, (LPARAM) text);

			SendDlgItemMessage(hwndDlg, CTL_TINT_TRACKBAR,       TBM_SETPOS, 1, ntsctint);
			SendDlgItemMessage(hwndDlg, CTL_HUE_TRACKBAR,        TBM_SETPOS, 1, ntschue);
			SendDlgItemMessage(hwndDlg, CTL_PALNOTCH_TRACKBAR,   TBM_SETPOS, 1, palnotch);
			SendDlgItemMessage(hwndDlg, CTL_PALSAT_TRACKBAR,     TBM_SETPOS, 1, palsaturation);
			SendDlgItemMessage(hwndDlg, CTL_PALSHARP_TRACKBAR,   TBM_SETPOS, 1, palsharpness);
			SendDlgItemMessage(hwndDlg, CTL_PALCONTRAST_TRACKBAR,TBM_SETPOS, 1, palcontrast);
			SendDlgItemMessage(hwndDlg, CTL_PALBRIGHT_TRACKBAR,  TBM_SETPOS, 1, palbrightness);

			CenterWindowOnScreen(hwndDlg);

			EnableWindow(GetDlgItem(hwndDlg, 65463), ntsccol_enable);
			EnableWindow(GetDlgItem(hwndDlg, 64395), ntsccol_enable);
			EnableWindow(GetDlgItem(hwndDlg, CTL_HUE_TRACKBAR), ntsccol_enable);
			EnableWindow(GetDlgItem(hwndDlg, CTL_TINT_TRACKBAR), ntsccol_enable);

			palpv = (PALPV*)malloc(sizeof(PALPV));
			
			HWND hpalpv = GetDlgItem(hwndDlg, IDC_PALETTE_PREVIEW);
			GetClientRect(hpalpv, &palpv->pvRect);
			palpv->pvSize = *((SIZE*)&palpv->pvRect + 1);
			ClientToScreen(hpalpv, (POINT*)&palpv->pvRect);
			ClientToScreen(hpalpv, ((POINT*)&palpv->pvRect) + 1);
			ScreenToClient(hwndDlg, (POINT*)&palpv->pvRect);
			ScreenToClient(hwndDlg, ((POINT*)&palpv->pvRect) + 1);

			LOGFONT logFont;
			GetObject((HANDLE)SendMessage(hwndDlg, WM_GETFONT, NULL, NULL), sizeof(logFont), &logFont);
			palpv->font = (HFONT)CreateFontIndirect(&logFont);

			palpv->cell_width = (float)palpv->pvSize.cx / 16;
			palpv->cell_height = (float)palpv->pvSize.cy / 4;

			HDC hdc = GetDC(hwndDlg);
			HDC memdc = CreateCompatibleDC(hdc);
			palpv->pvBitmap = CreateCompatibleBitmap(hdc, palpv->pvSize.cx, palpv->pvSize.cy);
			DeleteDC(memdc);
			ReleaseDC(hwndDlg, hdc);

			UpdateCurrentPaletteName(hwndDlg);
			UpdatePalettePreviewCaption(hwndDlg);
		}
		break;
		case WM_HSCROLL:
		{
			ntsctint      = SendDlgItemMessage(hwndDlg, CTL_TINT_TRACKBAR,       TBM_GETPOS, 0, (LPARAM)(LPSTR)0);
			ntschue       = SendDlgItemMessage(hwndDlg, CTL_HUE_TRACKBAR,        TBM_GETPOS, 0, (LPARAM)(LPSTR)0);
			palnotch      = SendDlgItemMessage(hwndDlg, CTL_PALNOTCH_TRACKBAR,   TBM_GETPOS, 0, (LPARAM)(LPSTR)0);
			palsaturation = SendDlgItemMessage(hwndDlg, CTL_PALSAT_TRACKBAR,     TBM_GETPOS, 0, (LPARAM)(LPSTR)0);
			palsharpness  = SendDlgItemMessage(hwndDlg, CTL_PALSHARP_TRACKBAR,   TBM_GETPOS, 0, (LPARAM)(LPSTR)0);
			palcontrast   = SendDlgItemMessage(hwndDlg, CTL_PALCONTRAST_TRACKBAR,TBM_GETPOS, 0, (LPARAM)(LPSTR)0);
			palbrightness = SendDlgItemMessage(hwndDlg, CTL_PALBRIGHT_TRACKBAR,  TBM_GETPOS, 0, (LPARAM)(LPSTR)0);
			FCEUI_SetNTSCTH(ntsccol_enable, ntsctint, ntschue);

			char text[40];
			sprintf(text, "Notch: %d%%", palnotch);
			SendDlgItemMessage(hwndDlg, STATIC_NOTCHVALUE,   WM_SETTEXT, 0, (LPARAM) text);
			sprintf(text, "Saturation: %d%%", palsaturation);
			SendDlgItemMessage(hwndDlg, STATIC_SATVALUE,     WM_SETTEXT, 0, (LPARAM) text);
			sprintf(text, "Sharpness: %d%%", palsharpness);
			SendDlgItemMessage(hwndDlg, STATIC_SHARPVALUE,   WM_SETTEXT, 0, (LPARAM) text);
			sprintf(text, "Contrast: %d%%", palcontrast);
			SendDlgItemMessage(hwndDlg, STATIC_CONTRASTVALUE,WM_SETTEXT, 0, (LPARAM) text);
			sprintf(text, "Brightness: %d%%", palbrightness);
			SendDlgItemMessage(hwndDlg, STATIC_BRIGHTVALUE,  WM_SETTEXT, 0, (LPARAM) text);

			if (~eoptions & EO_CPALETTE)
			{
				UpdatePalettePreviewCaption(hwndDlg);
				InvalidatePalettePreviewRect(hwndDlg);
			}
		}
		break;
		case WM_PAINT:
		{
			if (!palo)
				break;

			PAINTSTRUCT paint;
			HDC hdc = BeginPaint(hwndDlg, &paint);
			HDC memdc = CreateCompatibleDC(hdc);

			HGDIOBJ oldObj = SelectObject(memdc, palpv->pvBitmap);
			HFONT oldFont = SelectFont(memdc, palpv->font);

			RECT rect;
			float left, top = -palpv->cell_height, right, bottom = 0.0F;
			for (int i = 0; i < 64; ++i)
			{
				if (i % 16 == 0)
				{
					left = 0.0F;
					right = palpv->cell_width;
					top += palpv->cell_height;
					bottom += palpv->cell_height;
				}
				else
				{
					left += palpv->cell_width;
					right += palpv->cell_width;
				}

				rect.left = round(left);
				rect.right = round(right);
				rect.top = round(top);
				rect.bottom = round(bottom);

				int grey = ToGrey(palo[i].r, palo[i].g, palo[i].b);
				COLORREF color = force_grayscale ? RGB(grey, grey, grey) : RGB(palo[i].r, palo[i].g, palo[i].b);
				HBRUSH brush = CreateSolidBrush(color);
				FillRect(memdc, &rect, brush);
				DeleteObject(brush);

				if (palcolorindex)
				{
					SetTextColor(memdc, grey > 127 ? RGB(0, 0, 0) : RGB(255, 255, 255));
					SetBkColor(memdc, color);

					sprintf(palpv->buf, "%X", i);
					SIZE str_size;
					GetTextExtentPoint(memdc, palpv->buf, strlen(palpv->buf), &str_size);
					ExtTextOut(memdc, rect.left + (rect.right - rect.left - str_size.cx) / 2, rect.top + (rect.bottom - rect.top - str_size.cy) / 2, NULL, NULL, palpv->buf, strlen(palpv->buf), NULL);
				}
			}

			BitBlt(hdc, palpv->pvRect.left, palpv->pvRect.top,  palpv->pvSize.cx, palpv->pvSize.cy, memdc, 0, 0, SRCCOPY);

			EndPaint(hwndDlg, &paint);

			SelectFont(memdc, oldFont);
			SelectObject(memdc, oldObj);
			DeleteDC(memdc);

			break;
		}
		case WM_MOUSEMOVE:
			if (palo)
				UpdatePalettePreviewCaption(hwndDlg, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			break;

		case WM_LBUTTONDOWN:
			if (palo && palpv->mouse_index != -1)
			{
				CHOOSECOLORINFO info;
				char str[16];
				sprintf(str, "$%02X", palpv->mouse_index);
				info.name = str;

				int r = palo[palpv->mouse_index].r;
				int g = palo[palpv->mouse_index].g;
				int b = palo[palpv->mouse_index].b;

				info.r = &r;
				info.g = &g;
				info.b = &b;

				if (ChangeColor(hwndDlg, &info))
				{
					if (palo[palpv->mouse_index].r != r || palo[palpv->mouse_index].g != g || palo[palpv->mouse_index].b != b)
					{
						if (eoptions & EO_CPALETTE)
						{
							palo[palpv->mouse_index].r = r;
							palo[palpv->mouse_index].g = g;
							palo[palpv->mouse_index].b = b;
							FCEU_ResetPalette();
						}
						else
						{
							memcpy(cpalette, palo, 64 * 3);
							pal* tmp_palo = (pal*)&cpalette;
							tmp_palo[palpv->mouse_index].r = r;
							tmp_palo[palpv->mouse_index].g = g;
							tmp_palo[palpv->mouse_index].b = b;
							FCEUI_SetUserPalette(cpalette, 64);
							eoptions |= EO_CPALETTE;
							UpdateCurrentPaletteName(hwndDlg);
							CheckDlgButton(hwndDlg, CHECK_PALETTE_CUSTOM, BST_CHECKED);
						}

						UpdatePalettePreviewCaption(hwndDlg);
						InvalidatePalettePreviewRect(hwndDlg);
					}
				}
			}
			break;

		case WM_CLOSE:
		case WM_QUIT:
			goto gornk;

		case WM_COMMAND:
			switch (HIWORD(wParam))
			{
				case BN_CLICKED:
					switch (LOWORD(wParam))
					{
						case CHECK_PALETTE_ENABLED:
							ntsccol_enable ^= 1;
							FCEUI_SetNTSCTH(ntsccol_enable, ntsctint, ntschue); // it recalculates everything, use it for PAL block too!
							EnableWindow(GetDlgItem(hwndDlg, CTL_HUE_TEXT), ntsccol_enable);
							EnableWindow(GetDlgItem(hwndDlg, CTL_TINT_TEXT), ntsccol_enable);
							EnableWindow(GetDlgItem(hwndDlg, CTL_HUE_TRACKBAR), ntsccol_enable);
							EnableWindow(GetDlgItem(hwndDlg, CTL_TINT_TRACKBAR), ntsccol_enable);
							UpdateCurrentPaletteName(hwndDlg);
							UpdatePalettePreviewCaption(hwndDlg);
							InvalidatePalettePreviewRect(hwndDlg);
							break;

						case CHECK_PALETTE_GRAYSCALE:
							force_grayscale ^= 1;
							FCEUI_SetNTSCTH(ntsccol_enable, ntsctint, ntschue);
							UpdatePalettePreviewCaption(hwndDlg);
							InvalidatePalettePreviewRect(hwndDlg);
							break;

						case CHECK_DEEMPH_SWAP:
							paldeemphswap ^= 1;
							FCEUI_SetNTSCTH(ntsccol_enable, ntsctint, ntschue);
							UpdatePalettePreviewCaption(hwndDlg);
							InvalidatePalettePreviewRect(hwndDlg);
							break;

						case CHECK_PALETTE_CUSTOM:
							if (eoptions & EO_CPALETTE)
							{
								//disable user palette
								FCEUI_SetUserPalette(0, 0);
								eoptions &= ~EO_CPALETTE;
							}
							else
							{
								//switch to user palette (even if it isn't loaded yet!?)
								FCEUI_SetUserPalette(cpalette, 64); //just have to guess the size I guess
								eoptions |= EO_CPALETTE;
							}
							UpdateCurrentPaletteName(hwndDlg);
							UpdatePalettePreviewCaption(hwndDlg);
							InvalidatePalettePreviewRect(hwndDlg);
							break;

						case CHECK_PALETTE_COLOR_INDEX:
							palcolorindex ^= 1;
							InvalidatePalettePreviewRect(hwndDlg);
							break;

						case BTN_PALETTE_LOAD:
							if (LoadPaletteFile(hwndDlg))
							{
								CheckDlgButton(hwndDlg, CHECK_PALETTE_CUSTOM, BST_CHECKED);
								UpdateCurrentPaletteName(hwndDlg);
								UpdatePalettePreviewCaption(hwndDlg);
								InvalidatePalettePreviewRect(hwndDlg);
							}
							break;

						case BTN_PALETTE_SAVE:
							SavePaletteFile(hwndDlg);
							break;

						case BTN_PALETTE_RESET:
						{
							palnotch      = 100;
							palsaturation = 100;
							palsharpness  = 0;
							palcontrast   = 100;
							palbrightness = 50;

							char text[40];
							sprintf(text, "Notch: %d%%", palnotch);
							SendDlgItemMessage(hwndDlg, STATIC_NOTCHVALUE, WM_SETTEXT, 0, (LPARAM)text);
							sprintf(text, "Saturation: %d%%", palsaturation);
							SendDlgItemMessage(hwndDlg, STATIC_SATVALUE, WM_SETTEXT, 0, (LPARAM)text);
							sprintf(text, "Sharpness: %d%%", palsharpness);
							SendDlgItemMessage(hwndDlg, STATIC_SHARPVALUE, WM_SETTEXT, 0, (LPARAM)text);
							sprintf(text, "Contrast: %d%%", palcontrast);
							SendDlgItemMessage(hwndDlg, STATIC_CONTRASTVALUE, WM_SETTEXT, 0, (LPARAM)text);
							sprintf(text, "Brightness: %d%%", palbrightness);
							SendDlgItemMessage(hwndDlg, STATIC_BRIGHTVALUE, WM_SETTEXT, 0, (LPARAM)text);

							SendDlgItemMessage(hwndDlg, CTL_PALNOTCH_TRACKBAR, TBM_SETPOS, 1, palnotch);
							SendDlgItemMessage(hwndDlg, CTL_PALSAT_TRACKBAR, TBM_SETPOS, 1, palsaturation);
							SendDlgItemMessage(hwndDlg, CTL_PALSHARP_TRACKBAR, TBM_SETPOS, 1, palsharpness);
							SendDlgItemMessage(hwndDlg, CTL_PALCONTRAST_TRACKBAR, TBM_SETPOS, 1, palcontrast);
							SendDlgItemMessage(hwndDlg, CTL_PALBRIGHT_TRACKBAR, TBM_SETPOS, 1, palbrightness);

							FCEUI_SetNTSCTH(ntsccol_enable, ntsctint, ntschue);

							UpdatePalettePreviewCaption(hwndDlg);
							InvalidatePalettePreviewRect(hwndDlg);

							break;
						}
						case BUTTON_CLOSE:
							gornk:
								DestroyWindow(hwndDlg);
								hWndPal = 0; // Yay for user race conditions.
								DeleteFont(palpv->font);
								DeleteObject(palpv->pvBitmap);
								free(palpv);
								return 0;
					}
			}
	}

	return 0;
}

/**
* Shows palette configuration dialog.
**/
void ConfigPalette()
{
	if(!hWndPal)
		hWndPal = CreateDialog(fceu_hInstance, "PALCONFIG", hAppWnd, PaletteConCallB);
	else
		SetFocus(hWndPal);
}
