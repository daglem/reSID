## Process this file with automake to create Makefile.in

lib_LIBRARIES = libmos6581.a

libmos6581_a_SOURCES = sid.cc voice.cc wave.cc envelope.cc filter.cc pot.cc sample_PST.cc sample_PS_.cc sample_P_T.cc sample__ST.cc

pkginclude_HEADERS = siddefs.h sid.h voice.h wave.h envelope.h filter.h pot.h

noinst_DATA = data/sample_PST.freq0800 data/sample_PS_.freq1000 data/sample_P_T.freq0800 data/sample__ST.freq0800

noinst_SCRIPTS = samp2src.pl

EXTRA_DIST = $(noinst_DATA) $(noinst_SCRIPTS)

sample_PST.cc : data/sample_PST.freq0800
	$(PERL) ./samp2src.pl $* $< $@
sample_PS_.cc : data/sample_PS_.freq1000
	$(PERL) ./samp2src.pl $* $< $@
sample_P_T.cc : data/sample_P_T.freq0800
	$(PERL) ./samp2src.pl $* $< $@
sample__ST.cc : data/sample__ST.freq0800
	$(PERL) ./samp2src.pl $* $< $@

maintainer-clean-libLIBRARIES:
	rm -f sample_PST.cc sample_PS_.cc sample_P_T.cc sample__ST.cc
