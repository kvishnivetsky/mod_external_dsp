include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_external_dsp

mod_LTLIBRARIES = mod_external_dsp.la
mod_external_dsp_la_SOURCES  = mod_external_dsp.cpp
mod_external_dsp_la_CFLAGS   = $(AM_CFLAGS) -DINTEGER_SAMPLES
mod_external_dsp_la_CXXFLAGS = -DINTEGER_SAMPLES
mod_external_dsp_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_external_dsp_la_LDFLAGS  = -avoid-version -module -no-undefined -shared
