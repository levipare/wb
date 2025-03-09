CFLAGS = --std=c99 $(shell pkg-config --cflags gtk4 gtk4-layer-shell-0)
LDFLAGS = $(shell pkg-config --libs gtk4 gtk4-layer-shell-0)
TARGET = wb
SRC = wb.c
INSTALL_DIR = /usr/local/bin

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

install: $(TARGET)
	sudo install -m 755 $(TARGET) $(INSTALL_DIR)

clean:
	rm -f $(TARGET)

uninstall:
	sudo rm -f $(INSTALL_DIR)/$(TARGET)

.PHONY: clean install uninstall
