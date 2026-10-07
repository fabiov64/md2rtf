# Makefile for md2rtf — macOS (clang)

CC      = clang
CFLAGS  = -Wall -Wextra -O2 -std=c11 -pedantic
TARGET  = md2rtf
SRC     = md2rtf.c

.PHONY: all clean install uninstall test

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET) *.o

install: $(TARGET)
	install -d /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	@echo "Installed to /usr/local/bin/$(TARGET)"

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	@echo "Removed /usr/local/bin/$(TARGET)"

# Build a sample document and open it in TextEdit
test: $(TARGET)
	@printf '# Main Title\n\nThis is a paragraph with **bold**, *italic* and `code`.\n\n## Section 2\n\n- Item one\n- Item two\n- Item three\n\n### Code block\n\n```c\nint main(void) {\n    return 0;\n}\n```\n\n> A sample blockquote.\n\n| Column A | Column B |\n|----------|----------|\n| Alpha    | Beta     |\n| Gamma    | Delta    |\n\n---\n\nEnd of document.\n' > _test_input.md
	./$(TARGET) _test_input.md
	open _test_input.rtf
