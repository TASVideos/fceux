/* FCEUXD SP - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2005 Sebastian Porst
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
#include "utils/xstring.h"
#include "debuggersp.h"
#include "debugger.h"
#include "../../fceu.h"
#include "../../debug.h"
#include "../../conddebug.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

int GetNesFileAddress(int A);

inline int RomPageIndexForAddress(int addr) { return (addr-0x8000)>>(debuggerPageSize); }

Name* pageNames[32] = {0}; //the maximum number of pages we could have is 32, based on 1KB debuggerPageSize

int pageNumbersLoaded[32] = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

Name* ramBankNames = 0;
bool ramBankNamesLoaded = false;

extern char LoadedRomFName[2048];
char NLfilename[2048];
bool symbDebugEnabled = true;
bool symbRegNames = true;
bool inlineAddressEnabled = false;
int debuggerWasActive = 0;
char temp_chr[40] = {0};
char delimiterChar[2] = "#";

INT_PTR CALLBACK nameDebuggerBookmarkCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
extern WNDPROC DefaultEditCtrlProc;
extern LRESULT APIENTRY FilterEditCtrlProc(HWND hDlg, UINT uMsg, WPARAM wP, LPARAM lP);

MemoryMappedRegister RegNames[] = {
	{"$2000", "PPU_CTRL"},
	{"$2001", "PPU_MASK"},
	{"$2002", "PPU_STATUS"},
	{"$2003", "PPU_OAM_ADDR"},
	{"$2004", "PPU_OAM_DATA"},
	{"$2005", "PPU_SCROLL"},
	{"$2006", "PPU_ADDRESS"},
	{"$2007", "PPU_DATA"},
	{"$4000", "SQ1_VOL"},
	{"$4001", "SQ1_SWEEP"},
	{"$4002", "SQ1_LO"},
	{"$4003", "SQ1_HI"},
	{"$4004", "SQ2_VOL"},
	{"$4005", "SQ2_SWEEP"},
	{"$4006", "SQ2_LO"},
	{"$4007", "SQ2_HI"},
	{"$4008", "TRI_LINEAR"},
//	{"$4009", "UNUSED"},
	{"$400A", "TRI_LO"},
	{"$400B", "TRI_HI"},
	{"$400C", "NOISE_VOL"},
//	{"$400D", "UNUSED"},
	{"$400E", "NOISE_LO"},
	{"$400F", "NOISE_HI"},
	{"$4010", "DMC_FREQ"},
	{"$4011", "DMC_RAW"},
	{"$4012", "DMC_START"},
	{"$4013", "DMC_LEN"},
	{"$4014", "OAM_DMA"},
	{"$4015", "APU_STATUS"},
	{"$4016", "JOY1"},
	{"$4017", "JOY2_FRAME"}
};

int RegNameCount = sizeof(RegNames)/sizeof(MemoryMappedRegister);

/**
* Parses a line from a NL file.
* @param line The line to parse
* @param n The name structure to write the information to
*
* @return 0 if everything went OK. Otherwise an error code is returned.
**/
int parseLine(char* line, Name* n)
{
	char* pos;
	int llen;

	// Check parameters
	
	if (!line)
	{
		MessageBox(0, "Invalid parameter \"line\" in function parseLine", "Error", MB_OK | MB_ICONERROR);
		return 1;
	}
	
	if (!n)
	{
		MessageBox(0, "Invalid parameter \"n\" in function parseLine", "Error", MB_OK | MB_ICONERROR);
		return 2;
	}
	
	// Allow empty lines
	if (*line == '\r' || *line == '\n')
	{
		return -1;
	}
	
	// Attempt to tokenize the given line
	
	pos = strstr(line, delimiterChar);

	if (!pos)
	{
		// Found an invalid line
		return 3;
	}
	
	// Check if the first tokenized part (the offset) is valid
	
	*pos = 0;
	
	llen = (int)strlen(line);
	
	if (llen == 5) // Offset size of normal lines of the form $XXXX
	{
		if (line[0] != '$'
			|| !IsLetterLegalHex(line[1])
			|| !IsLetterLegalHex(line[2])
			|| !IsLetterLegalHex(line[3])
			|| !IsLetterLegalHex(line[4])
		)
		{
			return 4;
		}
	}
	else if (llen >= 7) // Offset size of array definition lines of the form $XXXX/YY
	{
		int i;
		if (line[0] != '$'
			|| !IsLetterLegalHex(line[1])
			|| !IsLetterLegalHex(line[2])
			|| !IsLetterLegalHex(line[3])
			|| !IsLetterLegalHex(line[4])
			|| line[5] != '/'
		)
		{
			return 5;
		}
		
		for (i=6;line[i];i++)
		{
			if (!IsLetterLegalHex(line[i]))
			{
				return 6;
			}
		}
	}
	else // Lines that have an invalid size
	{
		return 7;
	}
	
	
	// TODO: Validate if the offset is in the correct NL file.

	// After validating the offset it's OK to store it
	n->offset = (char*)malloc(strlen(line) + 1);
	strcpy(n->offset, line);
	line = pos + 1;
	
	// Attempt to tokenize the string again to find the name of the address

	pos = strstr(line, delimiterChar);

	if (!pos)
	{
		// Found an invalid line
		return 8;
	}

	*pos = 0;
	
	if (*line)
	{
		if (strlen(line) > NL_MAX_NAME_LEN)
			line[NL_MAX_NAME_LEN + 1] = 0;
		n->name = (char*)malloc(strlen(line) + 1);
		strcpy(n->name, line);
	}
	else
	{
		// Properly initialize zero-length names too
		n->name = 0;
	}
	
	// Now it's clear that the line was valid. The rest of the line is the comment
	// that belongs to that line. Once again a real string is required though.
	
	line = pos + 1;

	if ((u8)*line > 0x0D)
	{
		if (strlen(line) > NL_MAX_MULTILINE_COMMENT_LEN)
			line[NL_MAX_MULTILINE_COMMENT_LEN + 1] = 0;
		// remove all backslashes after \r\n
		char* crlf_pos = strstr(line, "\r\n\\");
		while (crlf_pos)
		{
			strcpy(crlf_pos + 2, crlf_pos + 3);
			crlf_pos = strstr(crlf_pos + 2, "\r\n\\");
		}

		n->comment = (char*)malloc(strlen(line) + 1);
		strcpy(n->comment, line);
	}
	else
	{
		// Properly initialize zero-length comments too
		n->comment = 0;
	}
	
	// Everything went fine.
	return 0;
}

/**
* Parses an array of lines read from a NL file.
* @param lines The lines to parse
* @param filename The name of the file the lines were read from.
* 
* @return A pointer to the first Name structure built from the parsed lines.
**/
Name* parse(char* lines, const char* filename)
{
	char* pos, *size;
	Name* prev = 0, *cur, *first = 0;

	// Check the parameters
	if (!lines)
	{
		MessageBox(0, "Invalid parameter \"lines\" in function parse", "Error", MB_OK | MB_ICONERROR);
		return 0;
	}
	
	if (!filename)
	{
		MessageBox(0, "Invalid parameter \"filename\" in function parse", "Error", MB_OK | MB_ICONERROR);
		return 0;
	}
	
	// Begin the actual parsing
	
	do
	{
		int fail;

		// Allocate a name structure to hold the parsed data from the next line
		
		cur = (Name*)malloc(sizeof(Name));
		cur->offsetNumeric = 0;
		cur->offset = 0;
		cur->next = 0;
		cur->name = 0;
		cur->comment = 0;
		
		pos = lines;

		// This first loop attempts to read potential multi-line comments and add them
		// into a single comment.
		for(;;)
		{
			// Get the end of the next line
			pos = strstr(pos, "\n");

			// If there's no end of line or if the next line does not begin with a \ character
			// we can stop.
			if (!pos || pos[1] != '\\') break;
			
			// At this point we have the following situation. pos[0] and pos[1] can be overwritten
			/*
				pos	-1	0	1	2
					 ?	\n	\   ?
			*/
			
			// \r\n is needed in text boxes
			if (pos[-1] != '\r')
			{
				pos[0] = '\r';
				pos[1] = '\n';
			}
			pos += 2;
		}
		
		if (!pos)
		{
			// All lines were parsed
			break;
		}

		if (pos[-1] == '\r')
			pos[-1] = 0;
		else
			*pos = 0;

		// Attempt to parse the current line
		fail = parseLine(lines, cur);
		
		if (fail == -1)
		{
			continue;
		} else if (fail)
		{
			// Show an error to allow the user to correct the defect line
			const char* fmtString = "Error (Code: %d): Invalid line \"%s\" in NL file \"%s\"\n";
			char* msg = (char*)malloc(strlen(fmtString) + 8 + strlen(lines) + strlen(filename) + 1);
			sprintf(msg, fmtString, fail, lines, filename);
			MessageBox(0, msg, "Error", MB_OK | MB_ICONERROR);
			free(msg);
			lines = pos + 1;
			//MessageBox(0, lines, "Error", MB_OK | MB_ICONERROR);
			continue;
		}
		
		lines = pos + 1;
		
		// Check if the line is an array definition line
		
		size = strstr(cur->offset, "/");
			
		if (size && cur->name) // Array definition line
		{
			int arrlen, offset;
			
			*size = 0;
			
			// Attempt to read the length of the array and the array offset
			if (sscanf(size + 1, "%x", &arrlen) > 0 && sscanf(cur->offset + 1, "%x", &offset) > 0)
			{
				Name* nn = 0;
				int i;
				
				// Create a node for each element of the array
				for (i = 0; i < arrlen; i++)
				{
					char numbuff[10] = {0};

					nn = (Name*)malloc(sizeof(Name));
					nn->next = 0;
					
					// The comment is the same for each array element
					nn->comment = strdup(cur->comment);
					
					// The offset of the node
					nn->offset = (char*)malloc(6);
					sprintf(nn->offset, "$%04X", offset + i);
					nn->offsetNumeric = offset + i;
					
					// The name of an array address is of the form NAME[INDEX]
					sprintf(numbuff, "[%X]", i);
					
					nn->name = (char*)malloc(strlen(cur->name) + strlen(numbuff) + 1);
					strcpy(nn->name, cur->name);
					strcat(nn->name, numbuff);
					
					// Add the new node to the list of address nodes
					if (prev)
					{
						prev->next = nn;
						prev = prev->next;
					} else
					{
						first = prev = nn;
					}
				}
				
				// Free the allocated node
				free(cur->name);
				free(cur->comment);
				free(cur->offset);
				free(cur);
				
				cur = nn;
			}
			else
			{
				// I don't think it's possible to get here as the validity of
				// offset and array size has already been validated in parseLine
				continue;
			}
		} else
		{
			unsigned int tempOffset;
			sscanf(cur->offset, "%*[$]%4X", &(tempOffset));
			cur->offsetNumeric = (uint16)tempOffset;
			// Add the node to the list of address nodes
			if (prev)
			{
				prev->next = cur;
				prev = prev->next;
			}
			else
			{
				first = prev = cur;
			}
		}
	} while (pos);

	// Return the first node in the list of address nodes
	return first;
}

/**
* Load and parse an entire NL file
* @param filename Name of the file to parse
*
* @return A pointer to the first Name structure built from the file data.
**/
Name* parseNameFile(const char* filename)
{
	char* buffer;
	Name* n = 0;

	// Attempt to read the file
	FILE* f = fopen(filename, "rb");

	if (f)
	{
//		__asm (".byte 0xcc");
		// Get the file size
		int lSize;
		fseek (f , 0 , SEEK_END);
		lSize = ftell(f) + 1;
		
		// Allocate sufficient buffer space
		rewind (f);
		
		buffer = (char*)malloc(lSize);

		if (buffer)
		{
			// Read the file and parse it
			memset(buffer, 0, lSize);
			fread(buffer, 1, lSize - 1, f);
			n = parse(buffer, filename);

			fclose(f);
			free(buffer);
		}
	}

	return n;
}

/**
* Frees an entire list of Name nodes starting with the given node.
* 
* @param n The node to start with (0 is a valid value)
**/
void freeList(Name* n)
{
	Name* next;
	
	while (n)
	{
		if (n->offset) free(n->offset);
		if (n->name) free(n->name);
		if (n->comment) free(n->comment);
		
		next = n->next;
		free(n);
		n = next;
	}
}

/**
* Replaces all offsets in a string with the names that were given to those offsets
* The caller needs to make sure that str is large enough.
* 
* @param list NL list of address definitions
* @param str The string where replacing takes place, including trace data.
* @param addressesLog Vector for collecting addresses that were replaced by names
**/
void replaceNames(Name* list, char* str, std::vector<uint16>* addressesLog)
{
	static char buff[1001];
	char* pos;
	char* src;

	Name* listRoot = list;

	while (list)
	{
		if (list->name)
		{
			//don't do replacements if we're not mapped appropriately
			if(listRoot != getNamesPointerForAddress(list->offsetNumeric))
			{
				list = list->next;
				continue;
			}

			*buff = 0;
			src = str;

			int addrlen = 5;

			//zero 30-nov-2017 - handle zero page differently
			char test[64];
			strcpy(test, list->offset);
			if(!strncmp(test,"$00",3))
			{
				strcpy(test,"$");
				strcat(test,list->offset+3);
				addrlen = 3;
			}

			while (src)
			{
				int myAddrlen = addrlen;

				//first, check for the (potentially) shortened zero page address
				pos = strstr(src, test);
				if(pos)
				{
					//next character must not be another part of a longer hex value
					if(pos[3] >= '0' && pos[3] <= '9') pos = NULL;
					else if(pos[3] >= 'A' && pos[3] <= 'F') pos = NULL;
				}
				if(!pos)
				{
					//if we didnt find that, check for the full $ABCD thing
					pos = strstr(src, list->offset);
					//if we didnt find that, bail
					if(!pos) break;
					
					//otherwise we found a length-5 token to replace
					myAddrlen = 5;
				}

				//we actually found an immediate value, don't replace it
				if(pos[-1] == '#')
				{
					src = pos + myAddrlen;
					continue;
				}

				if (inlineAddressEnabled)
				{
					//append up to and including the address, + a space
					strncat(buff, src, pos - src + myAddrlen);
					strcat(buff, " ");
				}
				else
				{
					//append up to but not including the address
					strncat(buff, src, pos - src);
				}

				//append the label
				strcat(buff, list->name);
				//begin processing after this offset
				src = pos + myAddrlen;

				if (addressesLog)
					addressesLog->push_back(list->offsetNumeric);
			}
			// if any offsets were changed, replace str by buff
			if (*buff)
			{
				strcat(buff, src);
				// replace whole str
				strcpy(str, buff);
			}
		}
		list = list->next;
	}
}

/**
* Replaces all offsets in a string with the names of the corresponding NES hardware registers
* The caller needs to make sure that str is large enough.
*
* @param str The string where replacing takes place, including trace data.
**/
void replaceRegNames(char* str)
{
	static char buff[1001];
	char* pos;
	char* src;

	for (int i = 0; i < RegNameCount; i++)
	{
		// copypaste, because Name* is too complex to abstract
		*buff = 0;
		src = str;

		while (pos = strstr(src, RegNames[i].offset))
		{
			if (inlineAddressEnabled)
			{
				strncat(buff, src, pos - src + 5);
				strcat(buff, " ");
			}
			else
			{
				strncat(buff, src, pos - src);
			}

			strcat(buff, RegNames[i].name);
			src = pos + 5;
		}
		if (*buff)
		{
			strcat(buff, src);
			strcpy(str, buff);
		}
	}
}

/**
* Searches an address node in a list of address nodes. The found node
* has the same offset as the passed parameter offs.
* 
* Could speed this up with an actual data structure...
*
* @param node The address node list
* @offs The offset to search
* @return The node that has the given offset or 0.
**/
Name* findNode(Name* node, const char* offset)
{
	while (node)
	{
		if (!strcmp(node->offset, offset))
			return node;
		
		node = node->next;
	}
	return 0;
}
// same, but with offsetNumeric
Name* findNode(Name* node, uint16 offsetNumeric)
{
	while (node)
	{
		if (node->offsetNumeric == offsetNumeric)
			return node;
		
		node = node->next;
	}
	return 0;
}

char* generateNLFilenameForAddress(uint16 address)
{
	if (address < 0x8000)
	{
		// The NL file for the RAM addresses has the name nesrom.nes.ram.nl
		strcpy(NLfilename, mass_replace(LoadedRomFName, "|", ".").c_str());
		strcat(NLfilename, ".ram.nl");
	} else
	{
		int bank = getBank(address);
		#ifdef DW3_NL_0F_1F_HACK
		if(bank == 0x0F)
			bank = 0x1F;
		#endif
		sprintf(NLfilename, "%s.%X.nl", mass_replace(LoadedRomFName, "|", ".").c_str(), bank);
	}
	return NLfilename;
}
Name* getNamesPointerForAddress(uint16 address)
{
	if(address >= 0x8000)
	{
		return pageNames[RomPageIndexForAddress(address)];
	}
	else
	{
		return ramBankNames;
	}
}
void setNamesPointerForAddress(uint16 address, Name* newNode)
{
	if (address >= 0x8000)
	{
		pageNames[RomPageIndexForAddress(address)] = newNode;
	}
	else
	{
		ramBankNames = newNode;
	}
}

/**
* Loads the necessary NL files
**/
// TODO: instead of loading from disk every time the "loadedBankNames" changes, it's better to cache loaded linkedlists in memory
void loadNameFiles()
{
	int cb;

	if (!ramBankNamesLoaded)
	{
		ramBankNamesLoaded = true;
		// load RAM names
		if (ramBankNames)
			free(ramBankNames);
		
		// Load the address descriptions for the RAM addresses
		ramBankNames = parseNameFile(generateNLFilenameForAddress(0x0000));
	}

	int nPages = 1<<(15-debuggerPageSize);

	for(int i=0;i<nPages;i++)
	{
		int pageIndexAddress = 0x8000 + (1<<debuggerPageSize)*i;

		// Find out which bank is loaded at the page index
		cb = getBank(pageIndexAddress);
		if (cb == -1) // No bank was loaded at that offset
		{
			free(pageNames[i]);
			pageNames[i] = 0;
		}
		else if (cb != pageNumbersLoaded[i])
		{
			// If the bank changed since loading the NL files the last time it's necessary
			// to load the address descriptions of the new bank.
			pageNumbersLoaded[i] = cb;

			if (pageNames[i])
				freeList(pageNames[i]);

			// Load new address definitions
			pageNames[i] = parseNameFile(generateNLFilenameForAddress(pageIndexAddress));
		}
	} //loop across pages
}

// bookmarks
std::vector <std::pair<unsigned int, std::string>> bookmarks; // first:address second:name

/**
* Returns the bookmark address of a CPU bookmark identified by its index.
* The caller must make sure that the index is valid.
*
* @param hwnd HWND of the debugger window
* @param index Index of the bookmark
**/
unsigned int getBookmarkAddress(unsigned int index)
{
	if (index < bookmarks.size())
		return bookmarks[index].first;
	else
		return 0;
}

/**
* Takes the offset from the debugger bookmark edit field and adds a debugger
* bookmark with that offset to the bookmark list if the offset is valid.
*
* @param hwnd HWMD of the debugger window
**/
void AddDebuggerBookmark(HWND hwnd)
{
	char buffer[5] = {0};
	GetDlgItemText(hwnd, IDC_DEBUGGER_BOOKMARK, buffer, 5);
	int address = offsetStringToInt(BT_C, buffer);
	// Make sure the offset is valid
	if (address == -1 || address > 0xFFFF)
	{
		MessageBox(hwnd, "Invalid offset", "Error", MB_OK | MB_ICONERROR);
		return;
	}
/*
	int index = 0;
	for (std::vector<std::pair<unsigned int, std::string>>::iterator it = bookmarks.begin(); it != bookmarks.end(); ++it)
	{
		if (it->first == address)
		{
			// select this bookmark to notify it already have a bookmark
			SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_SETCURSEL, index, 0);
			return;
		}
		++index;
	}
	*/

	int index = bookmarks.size();
	// try to find Symbolic name for this address
	Name* node = findNode(getNamesPointerForAddress(address), address);
	std::pair<unsigned int, std::string> bookmark(address, node && node->name ? node->name : "");
	if (DialogBoxParam(fceu_hInstance, "NAMEBOOKMARKDLGDEBUGGER", hwnd, nameDebuggerBookmarkCallB, (LPARAM)&bookmark))
	{
		bookmarks.push_back(bookmark);
		// add new item to ListBox
		char buffer[256];
		sprintf(buffer, "%04X %s", bookmark.first, bookmark.second.c_str());
		SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_ADDSTRING, 0, (LPARAM)buffer);
		// select this item
		SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_SETCURSEL, index, 0);
	}

}

/**
* Removes a debugger bookmark
* 
* @param hwnd HWND of the debugger window
**/
void DeleteDebuggerBookmark(HWND hwnd)
{
	// Get the selected bookmark
	unsigned int selectedItem = SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_GETCURSEL, 0, 0);
	
	if (selectedItem == LB_ERR)
	{
		MessageBox(hwnd, "Please select a bookmark from the list", "Error", MB_OK | MB_ICONERROR);
		return;
	} else
	{
		// Erase the selected bookmark
		bookmarks.erase(bookmarks.begin() + selectedItem);
		SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_DELETESTRING, selectedItem, 0);
		// Select next item
		if (selectedItem >= (bookmarks.size() - 1))
			// select last item
			SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_SETCURSEL, bookmarks.size() - 1, 0);
		else
			SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_SETCURSEL, selectedItem, 0);

	}
}

void EditDebuggerBookmark(HWND hwnd)
{
	// Get the selected bookmark
	int selectedItem = SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_GETCURSEL, 0, 0);
	if (selectedItem == LB_ERR || selectedItem >= (int)bookmarks.size())
	{
		MessageBox(hwnd, "Please select a bookmark from the list", "Error", MB_OK | MB_ICONERROR);
		return;
	} else
	{
		std::pair<unsigned int, std::string> bookmark = bookmarks[selectedItem];
		if (!bookmark.second.size())
		{
			// try to find the same address in bookmarks
			for (int i = bookmarks.size() - 1; i>= 0; i--)
			{
				if (i != selectedItem && bookmarks[i].first == bookmarks[selectedItem].first && bookmarks[i].second.size())
				{
					bookmark.second = bookmarks[i].second;
					break;
				}
			}
		}
		// Show the bookmark name dialog
		if (DialogBoxParam(fceu_hInstance, "NAMEBOOKMARKDLGDEBUGGER", hwnd, nameDebuggerBookmarkCallB, (LPARAM)&bookmark))
		{
			// Rename the selected bookmark
			bookmarks[selectedItem] = bookmark;
			SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_DELETESTRING, selectedItem, 0);
			char buffer[256];
			sprintf(buffer, "%04X %s", bookmarks[selectedItem].first, bookmarks[selectedItem].second.c_str());
			SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_INSERTSTRING, selectedItem, (LPARAM)buffer);
			// Reselect the item (selection disappeared when it was deleted)
			SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_SETCURSEL, selectedItem, 0);
		}
	}
}

void DeleteAllDebuggerBookmarks()
{
	bookmarks.resize(0);
}

void FillDebuggerBookmarkListbox(HWND hwnd)
{		
	SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_RESETCONTENT, 0, 0);
	char buffer[256];
	for (unsigned int i = 0; i < bookmarks.size(); ++i)
	{
		sprintf(buffer, "%04X %s", bookmarks[i].first, bookmarks[i].second.c_str());
		SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_ADDSTRING, 0, (LPARAM)buffer);
	}
}

/**
* Shows the code at the bookmark address in the disassembly window
*
* @param hwnd HWND of the debugger window
**/
void GoToDebuggerBookmark(HWND hwnd)
{
	int selectedItem = SendDlgItemMessage(hwnd, LIST_DEBUGGER_BOOKMARKS, LB_GETCURSEL, 0, 0);
	// If no bookmark is selected just return
	if (selectedItem == LB_ERR) return;
	unsigned int n = getBookmarkAddress(selectedItem);
	DisassembleToWindow(hwnd, IDC_DEBUGGER_DISASSEMBLY, IDC_DEBUGGER_DISASSEMBLY_VSCR, n);
}

INT_PTR CALLBACK SymbolicNamingCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
		case WM_INITDIALOG:
		{
			CenterWindow(hwndDlg);
			unsigned int* range = (unsigned int*)lParam;
			unsigned int newAddress = range[0];
			unsigned int arraySize = range[1];
			// filename
			generateNLFilenameForAddress(newAddress);
			SetDlgItemText(hwndDlg, IDC_SYMBOLIC_FILENAME, NLfilename);
			// offset
			sprintf(temp_chr, "$%04X", newAddress);
			SetDlgItemText(hwndDlg, IDC_SYMBOLIC_ADDRESS, temp_chr);
			Name* node = findNode(getNamesPointerForAddress(newAddress), newAddress);
			if (node)
			{
				if (node->name && node->name[0])
					SetDlgItemText(hwndDlg, IDC_SYMBOLIC_NAME, node->name);
				if (node->comment && node->comment[0])
					SetDlgItemText(hwndDlg, IDC_SYMBOLIC_COMMENT, node->comment);
			}
			SetDlgItemText(hwndDlg, IDC_EDIT_SYMBOLIC_INIT, "0");
			// set focus to IDC_SYMBOLIC_NAME
			SendMessage(hwndDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hwndDlg, IDC_SYMBOLIC_NAME), true);
			//always set the limits
			SendDlgItemMessage(hwndDlg, IDC_SYMBOLIC_NAME, EM_SETLIMITTEXT, NL_MAX_NAME_LEN, 0);
			SendDlgItemMessage(hwndDlg, IDC_SYMBOLIC_COMMENT, EM_SETLIMITTEXT, NL_MAX_MULTILINE_COMMENT_LEN, 0);
			SendDlgItemMessage(hwndDlg, IDC_EDIT_SYMBOLIC_ARRAY, EM_SETLIMITTEXT, 4, 0);
			SendDlgItemMessage(hwndDlg, IDC_EDIT_SYMBOLIC_INIT, EM_SETLIMITTEXT, 4, 0);
			DefaultEditCtrlProc = (WNDPROC)SetWindowLongPtr(GetDlgItem(hwndDlg, IDC_EDIT_SYMBOLIC_ARRAY), GWLP_WNDPROC, (LONG_PTR)FilterEditCtrlProc);
			DefaultEditCtrlProc = (WNDPROC)SetWindowLongPtr(GetDlgItem(hwndDlg, IDC_EDIT_SYMBOLIC_INIT), GWLP_WNDPROC, (LONG_PTR)FilterEditCtrlProc);

			CheckDlgButton(hwndDlg, IDC_CHECK_SYMBOLIC_NAME_OVERWRITE, BST_CHECKED);
			CheckDlgButton(hwndDlg, IDC_CHECK_SYMBOLIC_COMMENT_ARRAY_HEAD, BST_CHECKED);
			if (arraySize != -1)
			{
				char str[4];
				sprintf(str, "%X", arraySize);
				SetDlgItemText(hwndDlg, IDC_EDIT_SYMBOLIC_ARRAY, str);
				CheckDlgButton(hwndDlg, IDC_CHECK_SYMBOLIC_ARRAY, BST_CHECKED);
				goto symbolic_array;
			} else
				SetDlgItemText(hwndDlg, IDC_EDIT_SYMBOLIC_ARRAY, "10");

			break;
		}
		case WM_CLOSE:
		case WM_QUIT:
			break;
		case WM_COMMAND:
		{
			switch(HIWORD(wParam))
			{
				case BN_CLICKED:
				{
					switch(LOWORD(wParam))
					{
						case IDC_CHECK_SYMBOLIC_DELETE:
						{
							bool delete_mode = IsDlgButtonChecked(hwndDlg, IDC_CHECK_SYMBOLIC_DELETE) == BST_CHECKED;
							EnableWindow(GetDlgItem(hwndDlg, IDC_STATIC_SYMBOLIC_NAME), !delete_mode);
							EnableWindow(GetDlgItem(hwndDlg, IDC_STATIC_SYMBOLIC_COMMENT), !delete_mode);
							EnableWindow(GetDlgItem(hwndDlg, IDC_SYMBOLIC_NAME), !delete_mode);
							EnableWindow(GetDlgItem(hwndDlg, IDC_SYMBOLIC_COMMENT), !delete_mode);
							EnableWindow(GetDlgItem(hwndDlg, IDC_CHECK_SYMBOLIC_NAME_OVERWRITE), !delete_mode);
							if (delete_mode)
							{
								EnableWindow(GetDlgItem(hwndDlg, IDC_CHECK_SYMBOLIC_COMMENT_OVERWRITE), FALSE);
								EnableWindow(GetDlgItem(hwndDlg, IDC_CHECK_SYMBOLIC_COMMENT_ARRAY_HEAD), FALSE);
								EnableWindow(GetDlgItem(hwndDlg, IDC_STATIC_SYMBOLIC_INIT), FALSE);
								EnableWindow(GetDlgItem(hwndDlg, IDC_EDIT_SYMBOLIC_INIT), FALSE);
							} else
							{
								bool array_chk = IsDlgButtonChecked(hwndDlg, IDC_CHECK_SYMBOLIC_ARRAY) == BST_CHECKED;
								bool comment_head_only = IsDlgButtonChecked(hwndDlg, IDC_CHECK_SYMBOLIC_COMMENT_ARRAY_HEAD) == BST_CHECKED;
								EnableWindow(GetDlgItem(hwndDlg, IDC_CHECK_SYMBOLIC_COMMENT_ARRAY_HEAD), array_chk);
								EnableWindow(GetDlgItem(hwndDlg, IDC_CHECK_SYMBOLIC_NAME_OVERWRITE), array_chk);
								EnableWindow(GetDlgItem(hwndDlg, IDC_CHECK_SYMBOLIC_COMMENT_OVERWRITE), array_chk && !comment_head_only);
								EnableWindow(GetDlgItem(hwndDlg, IDC_STATIC_SYMBOLIC_INIT), array_chk);
								EnableWindow(GetDlgItem(hwndDlg, IDC_EDIT_SYMBOLIC_INIT), array_chk);
							}
							break;
						}
						case IDC_CHECK_SYMBOLIC_ARRAY:
						{
							symbolic_array:
							bool array_chk = IsDlgButtonChecked(hwndDlg, IDC_CHECK_SYMBOLIC_ARRAY) == BST_CHECKED;
							bool delete_mode = IsDlgButtonChecked(hwndDlg, IDC_CHECK_SYMBOLIC_DELETE) == BST_CHECKED;
							bool comment_head_only = IsDlgButtonChecked(hwndDlg, IDC_CHECK_SYMBOLIC_COMMENT_ARRAY_HEAD) == BST_CHECKED;
							EnableWindow(GetDlgItem(hwndDlg, IDC_CHECK_SYMBOLIC_NAME_OVERWRITE), array_chk && !delete_mode);
							EnableWindow(GetDlgItem(hwndDlg, IDC_CHECK_SYMBOLIC_COMMENT_ARRAY_HEAD), array_chk && !delete_mode);
							EnableWindow(GetDlgItem(hwndDlg, IDC_CHECK_SYMBOLIC_COMMENT_OVERWRITE), array_chk && !comment_head_only && !delete_mode);
							EnableWindow(GetDlgItem(hwndDlg, IDC_EDIT_SYMBOLIC_ARRAY), array_chk);
							EnableWindow(GetDlgItem(hwndDlg, IDC_STATIC_SYMBOLIC_BYTE), array_chk);
							EnableWindow(GetDlgItem(hwndDlg, IDC_STATIC_SYMBOLIC_INIT), array_chk && !delete_mode);
							EnableWindow(GetDlgItem(hwndDlg, IDC_EDIT_SYMBOLIC_INIT), array_chk && !delete_mode);
							break;
						}
						case IDC_CHECK_SYMBOLIC_COMMENT_ARRAY_HEAD:
						{
							bool comment_head_only = IsDlgButtonChecked(hwndDlg, IDC_CHECK_SYMBOLIC_COMMENT_ARRAY_HEAD) == BST_CHECKED;
							EnableWindow(GetDlgItem(hwndDlg, IDC_CHECK_SYMBOLIC_COMMENT_OVERWRITE), !comment_head_only);
							break;
						}
						case IDOK:
						{
							unsigned int newAddress = 0;
							char newOffset[6] = { 0 };
							GetDlgItemText(hwndDlg, IDC_SYMBOLIC_ADDRESS, newOffset, 6);
							if (sscanf(newOffset, "%*[$]%4X", &newAddress) != EOF)
							{

								unsigned int arraySize = 0;
								unsigned int arrayInit = 0;
								if (IsDlgButtonChecked(hwndDlg, IDC_CHECK_SYMBOLIC_ARRAY) == BST_CHECKED)
								{
									char strArraySize[6] = { 0 };
									GetDlgItemText(hwndDlg, IDC_EDIT_SYMBOLIC_ARRAY, strArraySize, 6);
									if (*strArraySize)
										sscanf(strArraySize, "%4X", &arraySize);
								}

								if (IsDlgButtonChecked(hwndDlg, IDC_CHECK_SYMBOLIC_DELETE) == BST_UNCHECKED)
								{
									char newName[NL_MAX_NAME_LEN + 1] = { 0 };
									GetDlgItemText(hwndDlg, IDC_SYMBOLIC_NAME, newName, NL_MAX_NAME_LEN + 1);
									char newComment[NL_MAX_MULTILINE_COMMENT_LEN + 1] = { 0 };
									GetDlgItemText(hwndDlg, IDC_SYMBOLIC_COMMENT, newComment, NL_MAX_MULTILINE_COMMENT_LEN + 1);

									char strArrayInit[6] = { 0 };
									GetDlgItemText(hwndDlg, IDC_EDIT_SYMBOLIC_INIT, strArrayInit, 6);
									if (*strArrayInit)
										sscanf(strArrayInit, "%4X", &arrayInit);

									AddNewSymbolicName(newAddress, newOffset, newName, newComment, arraySize, arrayInit, IsDlgButtonChecked(hwndDlg, IDC_CHECK_SYMBOLIC_NAME_OVERWRITE) == BST_CHECKED, IsDlgButtonChecked(hwndDlg, IDC_CHECK_SYMBOLIC_COMMENT_ARRAY_HEAD) == BST_CHECKED, IsDlgButtonChecked(hwndDlg, IDC_CHECK_SYMBOLIC_COMMENT_OVERWRITE) == BST_CHECKED);
								} else
									DeleteSymbolicName(newAddress, arraySize);

								WriteNameFileToDisk(generateNLFilenameForAddress(newAddress), getNamesPointerForAddress(newAddress));
							}
							EndDialog(hwndDlg, 1);
							break;
						}
						case IDCANCEL:
						{
							EndDialog(hwndDlg, 0);
							break;
						}
					}
					break;
				}
			}
        	break;
		}
	}
	return FALSE;
}

// returns true if user pressed OK, false if Cancel
bool DoSymbolicDebugNaming(int offset, HWND parentHWND)
{
	return DoSymbolicDebugNaming(offset, -1, parentHWND);
}

bool DoSymbolicDebugNaming(int offsetStart, int size, HWND parentHWND)
{
	if (!FCEUI_EmulationPaused())
		FCEUI_ToggleEmulationPause();
	loadNameFiles();
	int range[2] = { offsetStart, size };
	if (DialogBoxParam(fceu_hInstance, MAKEINTRESOURCE(IDD_SYMBOLIC_DEBUG_NAMING), parentHWND, SymbolicNamingCallB, (LPARAM)range))
		return true;
	return false;
}

void AddNewSymbolicName(uint16 newAddress, char* newOffset, char* newName, char* newComment, int size, int init, bool nameOverwrite, bool commentHeadOnly, bool commentOverwrite)
{
	// remove all delimiterChars from name and comment
	char* pos = newName;
	while (pos < newName + strlen(newName))
	{
		pos = strstr(pos, delimiterChar);
		if (pos)
			strcpy(pos, pos + 1);
		else
			break;
	}
	pos = newComment;
	while (pos < newComment + strlen(newComment))
	{
		pos = strstr(pos, delimiterChar);
		if (pos)
			strcpy(pos, pos + 1);
		else
			break;
	}

	if (*newName || *newComment)
	{
		int i = 0;
		char* tmpNewOffset = (char*)malloc(strlen(newOffset) + 1);
		strcpy(tmpNewOffset, newOffset);
		uint16 tmpNewAddress = newAddress;
		int tmpInit = init;
		do {
			Name* node = getNamesPointerForAddress(tmpNewAddress);
			if (!node)
			{
				node = (Name*)malloc(sizeof(Name));
				node->offset = (char*)malloc(strlen(tmpNewOffset) + 1);
				strcpy(node->offset, tmpNewOffset);
				node->offsetNumeric = tmpNewAddress;

				// Node name
				if (strlen(newName))
				{
					if (size)
					{
						char arr_index[16];
						sprintf(arr_index, "[%X]", tmpInit);
						node->name = (char*)malloc(strlen(newName) + strlen(arr_index) + 1);
						strcpy(node->name, newName);
						strcat(node->name, arr_index);
					}
					else
					{
						node->name = (char*)malloc(strlen(newName) + 1);
						strcpy(node->name, newName);
					}
				}
				else
					node->name = 0;

				if ((i == 0 || !commentHeadOnly) && strlen(newComment))
				{
					node->comment = (char*)malloc(strlen(newComment) + 1);
					strcpy(node->comment, newComment);
				}
				else
					node->comment = 0;

				node->next = 0;
				setNamesPointerForAddress(tmpNewAddress, node);
			}
			else
			{
				while (node)
				{
					if (node->offsetNumeric == tmpNewAddress)
					{
						// found matching address, proccessing its name and comment based on the configuration
						if ((i == 0 || nameOverwrite) && node->name)
						{
							free(node->name);
							node->name = 0;
						}
						if (!node->name && strlen(newName))
						{
							if (size)
							{
								char arr_index[16];
								sprintf(arr_index, "[%X]", tmpInit);
								node->name = (char*)malloc(strlen(newName) + strlen(arr_index) + 1);
								strcpy(node->name, newName);
								strcat(node->name, arr_index);
							}
							else
							{
								node->name = (char*)malloc(strlen(newName) + 1);
								strcpy(node->name, newName);
							}
						}

						if ((i == 0 || !commentHeadOnly && commentOverwrite) && node->comment)
						{
							free(node->comment);
							node->comment = 0;
						}
						if (!node->comment && strlen(newComment))
						{
							node->comment = (char*)malloc(strlen(newComment) + 1);
							strcpy(node->comment, newComment);
						}

						break;
					}

					if (node->next)
						node = node->next;
					else {
						// this is the last node in the list - so just append the address
						Name* newNode = (Name*)malloc(sizeof(Name));
						newNode->offset = (char*)malloc(strlen(tmpNewOffset) + 1);
						strcpy(newNode->offset, tmpNewOffset);
						newNode->offsetNumeric = tmpNewAddress;

						// Node name
						if (strlen(newName))
						{
							if (size)
							{
								char arr_index[16];
								sprintf(arr_index, "[%X]", tmpInit);
								newNode->name = (char*)malloc(strlen(newName) + strlen(arr_index) + 1);
								strcpy(newNode->name, newName);
								strcat(newNode->name, arr_index);
							}
							else
							{
								newNode->name = (char*)malloc(strlen(newName) + 1);
								strcpy(newNode->name, newName);
							}
						}
						else
							newNode->name = 0;

						if ((i == 0 || !commentHeadOnly) && strlen(newComment))
						{
							newNode->comment = (char*)malloc(strlen(newComment) + 1);
							strcpy(newNode->comment, newComment);
						}
						else
							newNode->comment = 0;

						newNode->next = 0;
						node->next = newNode;
						break;
					}
				}
			}
			++tmpNewAddress;
			++tmpInit;
			sprintf(tmpNewOffset, "$%04X", tmpNewAddress);
		} while (++i < size);
	}
}

void DeleteSymbolicName(uint16 address, int size)
{
	int i = 0;
	uint16 tmpAddress = address;
	do {
		Name* prev = 0;
		Name* initialNode = getNamesPointerForAddress(tmpAddress);
		Name* node = initialNode;
		while (node)
		{
			if (node->offsetNumeric == tmpAddress)
			{
				if (node->offset)
					free(node->offset);
				if (node->name)
					free(node->name);
				if (prev)
					prev->next = node->next;
				if (node == initialNode)
					setNamesPointerForAddress(tmpAddress, node->next);
				free(node);
				break;
			}
			prev = node;
			node = node->next;
		}
		++tmpAddress;
	} while (++i < size);
}

void WriteNameFileToDisk(const char* filename, Name* node)
{
	FILE* f = fopen(filename, "wb");
	char tempComment[NL_MAX_MULTILINE_COMMENT_LEN + 10];
	if (f)
	{
		char tempString[10 + 1 + NL_MAX_NAME_LEN + 1 + NL_MAX_MULTILINE_COMMENT_LEN + 1];
		while (node)
		{
			strcpy(tempString, node->offset);
			strcat(tempString, delimiterChar);
			if (node->name)
				strcat(tempString, node->name);
			strcat(tempString, delimiterChar);
			if (node->comment)
			{
				// dump multiline comment
				strcpy(tempComment, node->comment);
				char* remainder_pos = tempComment;
				char* crlf_pos = strstr(tempComment, "\r\n");
				while (crlf_pos)
				{
					*crlf_pos = 0;
					strcat(tempString, remainder_pos);
					strcat(tempString, "\r\n\\");
					crlf_pos = remainder_pos = crlf_pos + 2;
					crlf_pos = strstr(crlf_pos, "\r\n");
				}
				strcat(tempString, remainder_pos);
				strcat(tempString, "\r\n");
			} else
			{
				strcat(tempString, "\r\n");
			}
			// write to the file
			fwrite(tempString, 1, strlen(tempString), f);
			node = node->next;
		}
		fclose(f);
	}
}

INT_PTR CALLBACK nameDebuggerBookmarkCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static std::pair<unsigned int, std::string>* debuggerBookmark;

	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			// Limit bookmark descriptions to 50 characters
			SendDlgItemMessage(hwndDlg, IDC_BOOKMARK_DESCRIPTION, EM_SETLIMITTEXT, 50, 0);

			// Limit the address text
			SendDlgItemMessage(hwndDlg, IDC_BOOKMARK_ADDRESS, EM_SETLIMITTEXT, 4, 0);
			DefaultEditCtrlProc = (WNDPROC)SetWindowLongPtr(GetDlgItem(hwndDlg, IDC_BOOKMARK_ADDRESS), GWLP_WNDPROC, (LONG_PTR)FilterEditCtrlProc);

			debuggerBookmark = (std::pair<unsigned int, std::string>*)lParam;
			char addr[8];
			sprintf(addr, "%04X", debuggerBookmark->first);

			// Set address and description
			sprintf(addr, "%04X", debuggerBookmark->first);
			SetDlgItemText(hwndDlg, IDC_BOOKMARK_ADDRESS, addr);
			SetDlgItemText(hwndDlg, IDC_BOOKMARK_DESCRIPTION, debuggerBookmark->second.c_str());

			// Set focus to the edit field.
			SetFocus(GetDlgItem(hwndDlg, IDC_BOOKMARK_DESCRIPTION));
			SendDlgItemMessage(hwndDlg, IDC_BOOKMARK_DESCRIPTION, EM_SETSEL, 0, 52);

			break;
		}
		case WM_QUIT:
		case WM_CLOSE:
			EndDialog(hwndDlg, 0);
			break;
		case WM_COMMAND:
			switch (HIWORD(wParam))
			{
				case BN_CLICKED:
					switch (LOWORD(wParam))
					{
						case IDOK:
						{
							char addr_str[8];
							GetDlgItemText(hwndDlg, IDC_BOOKMARK_ADDRESS, addr_str, 8);
							sscanf(addr_str, "%X", &debuggerBookmark->first);

							if (debuggerBookmark->first > 0xFFFE)
							{
								// if the address is out of range
								char errmsg[64];
								sprintf(errmsg, "The address must be in range of 0-%X", 0xFFFE);
								MessageBox(hwndDlg, errmsg, "Address out of range", MB_OK | MB_ICONERROR);
								SetFocus(GetDlgItem(hwndDlg, IDC_BOOKMARK_ADDRESS));
								return FALSE;
							}

						/*
							extern std::vector<std::pair<unsigned int, std::string>> bookmarks;
							for (std::vector<std::pair<unsigned int, std::string>>::iterator it = bookmarks.begin(); it != bookmarks.end(); ++it)
							{
								if (it->first == debuggerBookmark->first && it->second == debuggerBookmark->second)
								{
									// if the address already have a bookmark
									MessageBox(hwndDlg, "This address already have a bookmark", "Bookmark duplicated", MB_OK | MB_ICONASTERISK);
									return FALSE;
								}
							}
						*/

							// Update the description
							char description[51];
							GetDlgItemText(hwndDlg, IDC_BOOKMARK_DESCRIPTION, description, 50);
							debuggerBookmark->second = description;
							EndDialog(hwndDlg, 1);
							break;
						}
						case IDCANCEL:
							EndDialog(hwndDlg, 0);
					}
			}
	}

	return FALSE;
}

