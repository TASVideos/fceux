#pragma once

int Assemble(unsigned char *output, int addr, char *str);
char *Disassemble(int addr, uint8 *opcode, bool showTrace = false);
char *DisassembleLine(int addr, bool showTrace = false, bool showRomOffsets = false);
char *DisassembleData(int addr, uint8 *opcode, bool showTrace = false, bool showRomOffsets = false);
