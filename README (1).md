# Digital Logic Circuit Simulator

A console application that models and evaluates digital logic
circuits using object-oriented programming. It supports primitive logic
gates, custom gate-level circuits, truth tables, adders, and a simple
ALU.

## Features

-   **Logic gates:** AND, OR, NOT, XOR, NAND, NOR, XNOR, and buffer
    (BUF).
-   **Object-oriented design:** abstract `Node` base class, derived
    input and gate classes, inheritance, and runtime polymorphism
    through `compute()`.
-   **Circuit construction:** create circuits programmatically or build
    a custom circuit through the interactive menu.

-   **Truth tables:** automatically generates truth tables for circuits.
-   **Arithmetic circuits:** half adder, full adder, and configurable
    N-bit ripple-carry adder.
-   **ALU:** configurable N-bit ALU supporting ADD, SUB, AND, and XOR,
    with carry and zero flags.
-   **Self-verification:** exhaustively checks the 4-bit adder and ALU
    against native C++ arithmetic.

## Requirements

-   A C++ compiler support (for example, GCC or MinGW-w64).
-   A terminal to run the interactive program.

## Build

Save the source file as `logic_simulator.cpp`, then compile it:

``` bash
g++ -std=c++17 -O2 -Wall -Wextra -o logic_simulator logic_simulator.cpp
```

On Windows with MinGW/MSYS2, run the equivalent command in the UCRT64
terminal or another terminal where `g++` is available.

## Run

Start the interactive menu:

``` bash
./logic_simulator
```

Run the non-interactive demonstration:

``` bash
./logic_simulator --demo
```

On Windows, the executable can be started as:

``` powershell
.\logic_simulator.exe
```

or:

``` powershell
.\logic_simulator.exe --demo
```

The program waits for Enter before closing when it reaches the end of
`main()`.

## Main menu

    Option Function
  -------- ---------------------------------------------
         1 Calculate with user-provided bit strings
         2 List supported operations
         3 Print the truth table of one logic gate
         4 Build and evaluate a custom circuit
         5 Evaluate a half adder
         6 Evaluate a full adder
         7 Run an N-bit ripple-carry adder (1--8 bits)
         8 Run an N-bit ALU (1--8 bits)
         9 Run the full demonstration
        10 Run self-verification
         0 Exit

### Bit calculator

The bit calculator accepts bit strings of 1 to 16 bits. It supports:

-   Two-input logic: AND, OR, XOR, NAND, NOR, XNOR
-   One-input logic: NOT, BUF
-   Arithmetic: ADD and SUB

For two bit strings of different lengths, the shorter string is padded
with leading zeros.

### ALU operation select

The ALU uses the select lines `S1 S0` as follows:

  S1 S0   Operation
  ------- -----------
  00      ADD
  01      SUB
  10      AND
  11      XOR

Subtraction is implemented using two's complement:

``` text
A - B = A + (B XOR sub) + sub
```

For subtraction, a carry flag of `1` means no borrow (`A >= B`). The ALU
also produces a `ZERO` flag, which is `1` when the result is zero.

## Building a custom circuit

Choose menu option `4`.

1.  Enter unique input names separated by spaces, for example:

    ``` text
    A B C
    ```

2.  Define gates one per line using this format:

    ``` text
    gate_name = GATE_TYPE source1 [source2]
    ```

    Example:

    ``` text
    g1 = AND A B
    g2 = NOT C
    g3 = XOR g1 g2
    DONE
    ```

    Sources must be input nodes or gates that have already been defined.
    Gate names are case-insensitive; node identifiers are
    case-sensitive.

3.  Enter one or more output node names, for example:

    ``` text
    g3
    ```

4.  Use the custom-circuit menu to print its truth table, evaluate it
    with chosen input values, or display its evaluation order.

For the example above, the circuit implements:

``` text
OUT = (A AND B) XOR (NOT C)
```


During evaluation, the simulator walks through the resulting order. It
gathers each node's input values and calls its virtual `compute()`
method. This ensures that source values are ready before a dependent
gate is evaluated.

**Complexity:** topological sorting takes `O(V + E)` time, where `V` is
the number of nodes and `E` is the number of dependency edges. The
queue, indegree map, and adjacency data require `O(V + E)` storage
overall.

### Simple dependency example

For the connections `A → G1`, `G1 → G2`, and `G2 → OUT`, the evaluation
order is:

``` text
A → G1 → G2
```

A feedback loop such as `G1 → G2 → G1` prevents a complete topological
ordering and is reported as a cycle.

## Project structure

The uploaded source is a single C++ file. Its main components are:

  -----------------------------------------------------------------------
  Component                           Responsibility
  ----------------------------------- -----------------------------------
  `CircuitError`                      Reports circuit construction and
                                      evaluation errors

  `Node`                              Abstract base class for circuit
                                      nodes

  `InputNode`                         Represents a primary input

  `Gate`, `UnaryGate`, `BinaryGate`   Base classes for logic gates

  `NotGate`, `AndGate`, etc.          Implement individual gate
                                      operations

  `makeGate()`                        Creates a gate from a text name

  `Circuit`                           Owns nodes, connects them, sorts
                                      dependencies, and evaluates outputs

  `halfAdder()`, `fullAdder()`,       Reusable gate-level building blocks
  `mux2()`                            

  `buildHalfAdder()`,                 Construct ready-made adder circuits
  `buildFullAdder()`                  

  `buildRippleAdder()`                Constructs an N-bit ripple-carry
                                      adder

  `buildAlu()`                        Constructs the N-bit ALU

  `verifyAll()`                       Runs exhaustive checks for the
                                      4-bit adder and ALU

  `demo()`                            Runs demonstrations of the
                                      supported circuits

  `interactiveMenu()`                 Provides the console user interface
  -----------------------------------------------------------------------

## Self-verification

Choose menu option `10`, or run option `9` to include verification at
the end of the demonstration.

The verification checks:

-   **4-bit adder:** all 16 × 16 input pairs and both carry-in values,
    for 512 cases.
-   **4-bit ALU:** all 16 × 16 input pairs and four operations, for
    1,024 cases.

The program compares circuit outputs and flags against native C++
arithmetic. A successful run prints an `[OK] Verified` message.

## Notes and current limitations

-   The interactive N-bit adder and ALU accept word sizes from 1 to 8
    bits.
-   The bit calculator accepts bit strings up to 16 bits.
-   Truth-table generation in the custom-circuit menu is limited to
    circuits with at most 10 inputs.
-   The implementation evaluates combinational dependencies. A feedback
    loop is detected and reported; this simulator does not model clocked
    sequential behavior or register state.
-   The ALU's `COUT` output is gated for arithmetic operations; for AND
    and XOR, it is not an arithmetic carry result.


