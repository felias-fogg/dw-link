#include <stdio.h>
#include <string.h>
#include <stdbool.h>

typedef  unsigned char byte;
bool small, illegal;


bool targetIllegalOpcode(unsigned int opcode)
{
  byte lsb = opcode & 0xFF;
  byte msb = (opcode & 0xFF00)>>8;

  switch(msb) {
  case 0x00: // nop
    return lsb != 0; 
  case 0x02: // muls
  case 0x03: // mulsu/fmuls
    return true;
  case 0x90: 
  case 0x91: // lds, ld, lpm, elpm
    if ((lsb & 0x0F) == 0x3 || (lsb & 0x0F) == 0x6 || (lsb & 0x0F) == 0x7 ||
	(lsb & 0x0F) == 0x8 || (lsb & 0x0F) == 0xB) return true; // reserved + elpm
    if (opcode == 0x91E1 || opcode == 0x91E2 || opcode == 0x91F1 || opcode == 0x91F2 ||
	opcode == 0x91E5 || opcode == 0x91F5 || 
	opcode == 0x91C9 || opcode == 0x91CA || opcode == 0x91D9 || opcode == 0x91DA ||
	opcode == 0x91AD || opcode == 0x91AE || opcode == 0x91BD || opcode == 0x91BE)
      return true; // undefined behavior for ld and lpm with increment
    return false;
  case 0x92:
  case 0x93:  // sts, st, push
    if (((lsb & 0xF) >= 0x3 && (lsb & 0xF) <= 0x8) || ((lsb & 0xF) == 0xB)) return true;
    if (opcode == 0x93E1 || opcode == 0x93E2 || opcode == 0x93F1 || opcode == 0x93F2 ||
	opcode == 0x93C9 || opcode == 0x93CA || opcode == 0x93D9 || opcode == 0x93DA ||
	opcode == 0x93AD || opcode == 0x93AE || opcode == 0x93BD || opcode == 0x93BE)
      return true; // undefined behavior for st with increment
    return false;
  case 0x94:
  case 0x95: // ALU, ijmp, icall, ret, reti, jmp, call, des
    if (opcode == 0x9409 || opcode == 0x9509) return false; //ijmp + icall
    if (opcode == 0x9508 || opcode == 0x9518 || opcode == 0x9588 || opcode == 0x95A8 ||
	opcode == 0x95C8 || opcode == 0x95E8) return false;
    if ((lsb & 0xF) == 0x4 || (lsb & 0xF) == 0x9 || (lsb & 0xF) == 0xB) return true;
    if ((lsb & 0xF) == 0x8 && msb == 0x95) return true;
    break;
  case 0x9c:
  case 0x9d:
  case 0x9e:
  case 0x9f: // mul
    return true;
  default: if (((msb & 0xF8) == 0xF8) && ((lsb & 0xF) >= 8)) return true; 
    return false;
  }
  if (small)  // small ATtinys where CALL and JMP are not needed/permitted
    if ((opcode & 0x0FE0E) == 0x940C || // jmp
	(opcode & 0x0FE0E) == 0x940E)  // call
      return true;
  return false;
}

bool twoWordInstr(unsigned int opcode) {
    return ((opcode & ~0x1F0) == 0x9000 || (opcode & ~0x1F0) == 0x9200 ||
	    (opcode & 0x0FE0E) == 0x940C || (opcode & 0x0FE0E) == 0x940E);
}

int main(int argc, char *argv[]) {
  unsigned int op;
  if (argc != 3) {
    printf("Provide two arguments: ill/well and small/large\n");
    return 1;
  }
  small = (strcmp(argv[2],"small") == 0);
  illegal = (strncmp(argv[1],"ill",3) == 0);

  printf("\t.org 0x0000\n");
  if (small) printf("\t; for small MCUs\n");
  else  printf("\t; for larger MCUs\n");
  if (illegal) printf("\t; all illegal opcodes\n");
  else printf("\t; all legal opcodes\n");
  for (op=0; op <= 0xFFFF; op++) {
    if (targetIllegalOpcode(op)) {
      if (illegal) {
	printf("\t.word 0x%04x\n", op);
	if (twoWordInstr(op))
	  printf("\t.word 0x0000\n");
      }
    } else {
      if (!illegal)  {
	printf("\t.word 0x%04x\n", op);
	if (twoWordInstr(op))
	  printf("\t.word 0x0000\n");
      }
    }
  }
  return 0;
}
