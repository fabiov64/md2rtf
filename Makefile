# Makefile per md2rtf — macOS (clang)

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
	@echo "Installato in /usr/local/bin/$(TARGET)"

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	@echo "Rimosso /usr/local/bin/$(TARGET)"

# Genera un documento di prova e lo apre in TextEdit
test: $(TARGET)
	@printf '# Titolo Principale\n\nQuesto è un paragrafo con **grassetto**, *corsivo* e `codice`.\n\n## Sezione 2\n\n- Voce uno\n- Voce due\n- Voce tre\n\n### Blocco di codice\n\n```c\nint main(void) {\n    return 0;\n}\n```\n\n> Una citazione di esempio.\n\n| Colonna A | Colonna B |\n|-----------|----------|\n| Alpha     | Beta      |\n| Gamma     | Delta     |\n\n---\n\nFine documento.\n' > _test_input.md
	./$(TARGET) _test_input.md
	open _test_input.rtf
