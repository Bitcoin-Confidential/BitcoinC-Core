.PHONY: FORCE
all: FORCE
	$(MAKE) -C .. bitcoinc_qt test_bitcoinc_qt
clean: FORCE
	$(MAKE) -C .. bitcoinc_qt_clean test_bitcoinc_qt_clean
check: FORCE
	$(MAKE) -C .. test_bitcoinc_qt_check
bitcoin-qt bitcoin-qt.exe: FORCE
	 $(MAKE) -C .. bitcoinc_qt
