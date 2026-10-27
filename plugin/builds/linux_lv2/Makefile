ifndef CONFIG
  CONFIG=Release
endif

all: binary ttl_generator lv2

binary:
	$(MAKE) -f Makefile.binary CONFIG=$(CONFIG) DEBCXXFLAGS="$(DEBCXXFLAGS)" DEBLDFLAGS="$(DEBLDFLAGS)"

ttl_generator:
	$(MAKE) -f Makefile.ttl_generator CONFIG=$(CONFIG)

lv2: ttl_generator binary
	./lv2_ttl_generator build/Heartbeat.so
	mkdir -p Heartbeat.lv2
	cp build/Heartbeat.so Heartbeat.lv2
	mv *.ttl Heartbeat.lv2

clean:
	$(MAKE) clean CONFIG=$(CONFIG) -f Makefile.binary
	$(MAKE) clean CONFIG=$(CONFIG) -f Makefile.ttl_generator
	rm -rf build
	rm -rf Heartbeat.lv2
