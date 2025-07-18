SRCDIR = src
BUILDDIR = build
OUTPUTEXE = login

CXX = g++
CXXFLAGS = -m64 -fno-strict-aliasing -pedantic -Wno-unused-parameter -Wall -Wextra -pthread --std=c++11
LFLAGS = -Wl,-t -lcrypto

DEBUG ?= 0
ifneq ($(DEBUG), 0)
	CXXFLAGS += -g -O0
else
	CXXFLAGS += -O2
endif

HEADERS = $(SRCDIR)/common.hh $(SRCDIR)/login.hh

$(BUILDDIR)/$(OUTPUTEXE): $(BUILDDIR)/common.obj $(BUILDDIR)/connections.obj $(BUILDDIR)/crypto.obj $(BUILDDIR)/login.obj $(BUILDDIR)/query.obj
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LFLAGS)

$(BUILDDIR)/common.obj: $(SRCDIR)/common.cc $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/connections.obj: $(SRCDIR)/connections.cc $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/crypto.obj: $(SRCDIR)/crypto.cc $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/login.obj: $(SRCDIR)/login.cc $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/query.obj: $(SRCDIR)/query.cc $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

.PHONY: clean

clean:
	@rm -rf $(BUILDDIR)

