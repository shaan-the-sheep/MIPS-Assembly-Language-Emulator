#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "emulator.h"

#define XSTR(x) STR(x)		//can be used for MAX_ARG_LEN in sscanf
#define STR(x) #x

#define ADDR_TEXT    0x00400000 //where the .text area starts in which the program lives
#define TEXT_POS(a)  ((a==ADDR_TEXT)?(0):(a - ADDR_TEXT)/4) //can be used to access text[]
#define ADDR_POS(j)  (j*4 + ADDR_TEXT)                      //convert text index to address


const char *register_str[] = {"$zero",
                              "$at", "$v0", "$v1",
                              "$a0", "$a1", "$a2", "$a3",
                              "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
                              "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
                              "$t8", "$t9",
                              "$k0", "$k1",
                              "$gp",
                              "$sp", "$fp", "$ra"};

/* Space for the assembler program */
char prog[MAX_PROG_LEN][MAX_LINE_LEN];
int prog_len = 0;

/* Elements for running the emulator */
unsigned int registers[MAX_REGISTER] = {0}; // the registers
unsigned int pc = 0;                        // the program counter
unsigned int text[MAX_PROG_LEN] = {0}; // the text memory with our instructions

/* function to create bytecode for instruction nop
   conversion result is passed in bytecode
   function always returns 0 (conversion OK) */
typedef int (*opcode_function)(unsigned int, unsigned int*, char*, char*, char*, char*);

int add_imi(unsigned int *bytecode, int imi){
	if (imi<-32768 || imi>32767) return (-1);
	*bytecode|= (0xFFFF & imi);
	return(0);
}

int add_sht(unsigned int *bytecode, int sht){
	if (sht<0 || sht>31) return(-1);
	*bytecode|= (0x1F & sht) << 6;
	return(0);
}

int add_reg(unsigned int *bytecode, char *reg, int pos){
	int i;
	for(i=0;i<MAX_REGISTER;i++){
		if(!strcmp(reg,register_str[i])){
		*bytecode |= (i << pos);
			return(0);
		}
	}
	return(-1);
}

int add_addr(unsigned int *bytecode, int addr){
    *bytecode |= ((addr>>2) & 0x3FFFFF);
    return 0;
}

int add_lbl(unsigned int offset, unsigned int *bytecode, char *label){
	char l[MAX_ARG_LEN+1];
	int j=0;
	while(j<prog_len){
		memset(l,0,MAX_ARG_LEN+1);
		sscanf(&prog[j][0],"%" XSTR(MAX_ARG_LEN) "[^:]:", l);
		if (label!=NULL && !strcmp(l, label)) return(add_imi( bytecode, j-(offset+1)) );
		j++;
	}
	return (-1);
}

int add_text_addr(unsigned int *bytecode, char *label){
	char l[MAX_ARG_LEN+1];
	int j=0;
	while(j<prog_len){
		memset(l,0,MAX_ARG_LEN+1);
		sscanf(&prog[j][0],"%" XSTR(MAX_ARG_LEN) "[^:]:", l);
		if (label!=NULL && !strcmp(l, label)) return(add_addr( bytecode, ADDR_POS(j)));
		j++;
	}
	return (-1);
}

int opcode_nop(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0;
	return (0);
}

int opcode_add(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x20; 				// op,shamt,funct
	if (add_reg(bytecode,arg1,11)<0) return (-1); 	// destination register
	if (add_reg(bytecode,arg2,21)<0) return (-1);	// source1 register
	if (add_reg(bytecode,arg3,16)<0) return (-1);	// source2 register
	return (0);
}

int opcode_addi(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x20000000; 				// op
	if (add_reg(bytecode,arg1,16)<0) return (-1);	// destination register
	if (add_reg(bytecode,arg2,21)<0) return (-1);	// source1 register
	if (add_imi(bytecode,atoi(arg3))) return (-1);	// constant
	return (0);
}

int opcode_andi(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x30000000; 				// op
	if (add_reg(bytecode,arg1,16)<0) return (-1); 	// destination register
	if (add_reg(bytecode,arg2,21)<0) return (-1);	// source1 register
	if (add_imi(bytecode,atoi(arg3))) return (-1);	// constant
	return (0);
}

int opcode_blez(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x18000000; 				// op
	if (add_reg(bytecode,arg1,21)<0) return (-1);	// register1
	if (add_lbl(offset,bytecode,arg2)) return (-1); // jump
	return (0);
}

int opcode_bne(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x14000000; 				// op
	if (add_reg(bytecode,arg1,21)<0) return (-1); 	// register1
	if (add_reg(bytecode,arg2,16)<0) return (-1);	// register2
	if (add_lbl(offset,bytecode,arg3)) return (-1); // jump
	return (0);
}

int opcode_srl(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x2; 					// op
	if (add_reg(bytecode,arg1,11)<0) return (-1);   // destination register
	if (add_reg(bytecode,arg2,16)<0) return (-1);   // source1 register
	if (add_sht(bytecode,atoi(arg3))<0) return (-1);// shift
	return(0);
}

int opcode_sll(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0; 					// op
	if (add_reg(bytecode,arg1,11)<0) return (-1);	// destination register
	if (add_reg(bytecode,arg2,16)<0) return (-1); 	// source1 register
	if (add_sht(bytecode,atoi(arg3))<0) return (-1);// shift
	return(0);
}

int opcode_jr(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x8; 					// op
	if (add_reg(bytecode,arg1,21)<0) return (-1);	// source register
	return(0);
}

int opcode_jal(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x0C000000; 					// op
	if (add_text_addr(bytecode, arg1)<0) return (-1);// find and add address
	return(0);
}

const char *opcode_str[] = {"nop", "add", "addi", "andi", "blez", "bne", "srl", "sll", "jal", "jr"};
opcode_function opcode_func[] = {&opcode_nop, &opcode_add, &opcode_addi, &opcode_andi, &opcode_blez, &opcode_bne, &opcode_srl, &opcode_sll, &opcode_jal, &opcode_jr};

/* a function to print the state of the machine */
int print_registers() {
  int i;
  printf("registers:\n");
  for (i = 0; i < MAX_REGISTER; i++) {
    printf(" %d: %d\n", i, registers[i]);
  }
  printf(" Program Counter: 0x%08x\n", pc);
  return (0);
}

/* function to execute bytecode */
int exec_bytecode() {
  printf("EXECUTING PROGRAM ...\n");
  int instruction; // used to store each bytecode instruction
  int count = 1; // makes sure only instructions are executed and not memory addresses
  pc = ADDR_TEXT; // set program counter to the start of our program
  int pcIndex = TEXT_POS(pc); // convert pc to index
  while (pc != ADDR_POS(text[MAX_PROG_LEN - 1])){ // while pc has not reached last memory address
    for (int i = 0; i < MAX_PROG_LEN; i++){
      instruction = text[i + count]; 
      if (instruction == 0x00000000){ // if operation is NOP
        break;
      }
      else{
        find_type(instruction); 
        pcIndex ++; 
        pc = ADDR_POS(pcIndex); // converting pc index back to memory address
        count ++;
      }
    }
  }
  print_registers(); // print out the state of registers at the end of execution
  printf("... DONE!\n");
  return (0);
}

void find_type(int instruction){ // type = R, J or I
  char strInstruction[20];
  char type[20];
  sprintf(strInstruction, "%d", instruction); // converts the bitcode instruction to a string
  if ((strInstruction[2] == '0') && (strInstruction[3] != 'c')){ // if instruction starts with 0x0
    type = 'R';
    find_r_operation(strInstruction[]);
    
  }
  else if ((strInstruction[2] == '0') && (strInstruction[3] == 'c')){ // if instruction starts with 0x0c
    type = 'J';
    find_j_operation(strInstruction[]);
  }  
  else{
    type = 'I';
    find_i_operation(strInstruction[]);
  }
 }

void find_r_operation(char strInstruction[]){
  if (strInstruction[9] == '2'){ // if instruction ends in 0x2
    operation[] = 'srl'; 
  } else if (strInstruction[9] == '8'){ // if instruction ends in 0x8
    operation[] = 'jr';
  }
  if (strInstruction[9] == '0'){
    if (strInstruction[8] == '2'){ // if instruction ends in 0x20
      operation[] = 'add';
    }
    else{ // if operation ends in 0x0
      operation[] = 'sll';
    }
  }
  execute_instruction(strInstruction, operation[]);
}

void find_j_operation(char strInstruction[]){
  char operation[20] = 'jal'; 
  execute_instruction(strInstruction, operation[]);
}

void find_i_operation(char strInstruction[]){
  if (strInstruction[9] == '2'){ // if instruction starts with 0x20
    char operation[] = 'addi';
  }
  else if (strInstruction[9] == '3'){ // if instruction starts with 0x30
    char operation[] = 'andi';
  }
  if (strInstruction[8] == '1'){
    if (strInstruction[9] == '8'){ // if instruction starts with 0x18
      char operation[] = 'blez';
    } 
    else{ // if instruction starts with 0x14
      char operation[] = 'bne';
    }
  }
  execute_instruction(strInstruction, operation[]);


void execute_instruction(char strInstruction[], char operation[]){
  int binary[] = hex_to_binary(strInstruction[]); // converts string of bytecode instructions to binary
  int r1Binary = binary[1]; // first register in binary
  int r1 = binary_to_dec(r1Binary); // first register in decimal 
  int r2Binary = binary[2];
  int r2 = binary_to_dec(r2Binary);
  int r3Binary = binary[3];
  int r3 = binary_to_dec(r3Binary)

  if operation == 'add'{ //r execution
    registers[r3] = registers[r1] + registers[r2]; 
  }
  else if operation = 'sll'{ 
    registers[r3] = registers[r1] << r2; // left shift
  }
  else if operation = 'srl'{
    registers[r3] = registers[r1] >> r2; // right shift
  }

  if operation = 'addi'{ // i execution
    registers[r2] = registers[r1] + r3;
  }
  else if operation = 'andi'{
    registers[r2] = registers[r1] && r3;
  }
}

int binary_to_dec(int binary){ // converts binary to decimal
    int decimal = 0;
    int base = 1; 
    int remainder;
    int t = binary;
    while(t > 0){
        remainder = t % 10;
        decimal = decimal + remainder * base;
        t = t / 10;
        base = base * 2;
    }
    return decimal;
}

int hex_to_binary(char instruction[]){ // converts hex to binary
	long int count = 0;
  int binaryArray[10]; 
	while(instruction[count])
	{
		switch(instruction[count])
		{
			case '0' : n[count] = 0000; // adding 4 bits to binary array based on hex value
				break;
			case '1' : n[count] = 0001;
				break;
			case '2' : n[count] = 0010;
				break;
			case '3' : n[count] = 0011;
				break;
			case '4' : n[count] = 0100;
				break;
			case '5' : n[count] = 0101;
				break;
			case '6' : n[count] = 0110;
				break;
			case '7' : n[count] = 0111;
				break;
			case '8' : n[count] = 1000;
				break;
			case '9' : n[count] = 1001;
				break;
			case 'A' : n[count] = 1010;
				break;
			case 'B' : n[count] = 1011;
				break;
			case 'C' : n[count] = 1100;
				break;
			case 'D' : n[count] = 1101;
				break;
			case 'E' : n[count] = 1110;
				break;
			case 'F' : n[count] = 1111;
				break;
			case 'a' : n[count] = 1010;
				break;
			case 'b' : n[count] = 1011;
				break;
			case 'c' : n[count] = 1100;
				break;
			case 'd' : n[count] = 1101;
				break;
			case 'e' : n[count] = 1110;
				break;
			case 'f' : n[count] = 1111;
				break;
			default : n[count] = ("\nError");
		}
		count++;
	}
	return binaryArray;
}  

/*function to create bytecode */
int make_bytecode() {
  unsigned int
      bytecode; // holds the bytecode for each converted program instruction
  int i, j = 0;    // instruction counter (equivalent to program line)

  char label[MAX_ARG_LEN + 1];
  char opcode[MAX_ARG_LEN + 1];
  char arg1[MAX_ARG_LEN + 1];
  char arg2[MAX_ARG_LEN + 1];
  char arg3[MAX_ARG_LEN + 1];

  printf("ASSEMBLING PROGRAM ...\n");
  while (j < prog_len) {
    memset(label, 0, sizeof(label));
    memset(opcode, 0, sizeof(opcode));
    memset(arg1, 0, sizeof(arg1));
    memset(arg2, 0, sizeof(arg2));
    memset(arg3, 0, sizeof(arg3));

    bytecode = 0;

    if (strchr(&prog[j][0], ':')) { // check if the line contains a label
      if (sscanf(
              &prog[j][0],
              "%" XSTR(MAX_ARG_LEN) "[^:]: %" XSTR(MAX_ARG_LEN) "s %" XSTR(
                  MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s",
              label, opcode, arg1, arg2,
              arg3) < 2) { // parse the line with label
        printf("parse error line %d\n", j);
        return (-1);
      }
    } else {
      if (sscanf(&prog[j][0],
                 "%" XSTR(MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s %" XSTR(
                     MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s",
                 opcode, arg1, arg2,
                 arg3) < 1) { // parse the line without label
        printf("parse error line %d\n", j);
        return (-1);
      }
    }

    for (i=0; i<MAX_OPCODE; i++){
        if (!strcmp(opcode, opcode_str[i]) && ((*opcode_func[i]) != NULL))
        {
            if ((*opcode_func[i])(j, &bytecode, opcode, arg1, arg2, arg3) < 0)
            {
                printf("ERROR: line %d opcode error (assembly: %s %s %s %s)\n", j, opcode, arg1, arg2, arg3);
                return (-1);
            }
            else
            {
                printf("0x%08x 0x%08x\n", ADDR_TEXT + 4 * j, bytecode);
                text[j] = bytecode;
                break;
            }
        }
        if (i == (MAX_OPCODE - 1))
        {
            printf("ERROR: line %d unknown opcode\n", j);
            return (-1);
        }
    }

    j++;
  }
  printf("... DONE!\n");
  return (0);
}

/* loading the program into memory */
int load_program(char *filename) {
  int j = 0;
  FILE *f;

  printf("LOADING PROGRAM %s ...\n", filename);

  f = fopen(filename, "r");
  if (f == NULL) {
      printf("ERROR: Cannot open program %s...\n", filename);
      return -1;
  }
  while (fgets(&prog[prog_len][0], MAX_LINE_LEN, f) != NULL) {
    prog_len++;
  }

  printf("PROGRAM:\n");
  for (j = 0; j < prog_len; j++) {
    printf("%d: %s", j, &prog[j][0]);
  }

  return (0);
}
