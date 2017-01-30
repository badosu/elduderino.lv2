BUNDLE = elduderino.lv2
INSTALL_DIR = /usr/local/lib/lv2
CFLAGS = -shared -fPIC -DPIC

$(BUNDLE): manifest.ttl elduderino.ttl elduderino.so
	rm -rf $(BUNDLE)
	mkdir $(BUNDLE)
	cp $^ $(BUNDLE)

all: $(BUNDLE)

elduderino.so: elduderino.c
	gcc $< -o $@ $(CFLAGS)

clean:
	rm -rf $(BUNDLE) *.so *.moc.cpp

install: $(BUNDLE)
	sudo mkdir -p $(INSTALL_DIR)
	sudo rm -rf $(INSTALL_DIR)/$(BUNDLE)
	sudo cp -R $(BUNDLE) $(INSTALL_DIR)

uninstall:
	rm -rf $(INSTALL_DIR)/$(BUNDLE)

run: install
	jalv http://lv2plug.in/plugins/elduderino
