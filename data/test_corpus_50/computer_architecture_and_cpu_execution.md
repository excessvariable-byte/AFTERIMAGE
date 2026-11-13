# Computer Organization and CPU Architecture

## Heading: Computer Organization and Execution
This document covers the fundamental principles of how a central processing unit (CPU) operates, how a computer executes instructions, and the memory structures that support computation.

### 1. How a Computer Executes Instructions: The Instruction Cycle
Every instruction processed by a digital computer undergoes the classical **Fetch-Decode-Execute Cycle**:
1. **Instruction Fetch (IF)**: The Program Counter (PC) holds the address of the next machine instruction in memory. The instruction is fetched via the system bus into the Instruction Register (IR). The PC is incremented.
2. **Instruction Decode (ID)**: The control unit decodes the opcode in the IR to determine the operation and required operands.
3. **Operand Fetch**: Any required data values are loaded from registers or memory into internal CPU registers.
4. **Execute (EX)**: The Arithmetic Logic Unit (ALU) performs the designated operation (addition, logical AND, branch comparison, etc.).
5. **Write Back (WB)**: The result is written back to the destination register or main memory.

```
       +------------------+
       |   Fetch from PC  | ---> Program Counter (PC)
       +--------+---------+
                |
                v
       +------------------+
       | Decode in IR     | ---> Instruction Register (IR)
       +--------+---------+
                |
                v
       +------------------+
       | Execute in ALU   | ---> Arithmetic Logic Unit (ALU)
       +--------+---------+
                |
                v
       +------------------+
       | Store Result     | ---> Register File / Memory
       +------------------+
```

### 2. Different Types of CPU Architectures: RISC vs. CISC
CPUs are broadly classified according to their instruction set architecture (ISA):
- **RISC (Reduced Instruction Set Computer)**: Emphasizes a small, highly optimized set of fixed-length instructions that execute in a single cycle. Features load-store architecture where memory is accessed only via explicit LOAD and STORE instructions. Examples include ARM, MIPS, and RISC-V.
- **CISC (Complex Instruction Set Computer)**: Features a large set of variable-length instructions, many capable of performing multi-step operations or direct memory manipulation in a single instruction. Examples include x86 and x86-64.
- **VLIW & Superscalar Architectures**: Modern CPUs use superscalar pipelines to execute multiple instructions per clock cycle out-of-order.

### 3. Difference Between RAM and ROM
Primary memory is divided into two distinct functional types:
- **RAM (Random Access Memory)**: Volatile read-write memory used to store active program code, operating system structures, and runtime data. Loses all contents when power is removed. Divided into SRAM (fast, static cache) and DRAM (dynamic main memory requiring periodic refresh).
- **ROM (Read-Only Memory)**: Non-volatile storage containing permanent bootstrap code, BIOS/UEFI firmware, and microcode that persists across reboots. Data is written during manufacture or flashed permanently.
