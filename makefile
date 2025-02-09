# Makefile for C64 Markdown Editor

# Compiler and tools
CC = cl65
CFLAGS = -t c128 -O --codesize 200

# Program details
PROGRAM = markdown
SRC = markdown.c
PRG = $(PROGRAM).prg
D71 = $(PROGRAM).d71

# Default target
all: $(D71)

# Compile C source to PRG
$(PRG): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

# Create bootable disk image (using D71 for C128)
$(D71): $(PRG)
	c1541 -format "markdown,01" d71 $(D71)
	c1541 -attach $(D71) -write $(PRG) "markdown"

# Clean build artifacts
clean:
	rm -f $(PRG) $(D71)

.PHONY: all clean