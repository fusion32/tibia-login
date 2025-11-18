SRCDIR = src
BUILDDIR = build
OUTPUTEXE = login

CXX = g++
CXXFLAGS = -m64 -fno-strict-aliasing -Wno-deprecated-declarations -pedantic -Wall -Wextra -pthread --std=c++11
LFLAGS = -Wl,-t -lcrypto

DEBUG ?= 0
ifneq ($(DEBUG), 0)
 CXXFLAGS += -g -Og -DENABLE_ASSERTIONS=1
else
 CXXFLAGS += -O2
endif

$(BUILDDIR)/$(OUTPUTEXE): $(BUILDDIR)/crypto.obj $(BUILDDIR)/connections.obj $(BUILDDIR)/main.obj $(BUILDDIR)/query.obj $(BUILDDIR)/status.obj
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LFLAGS)

$(BUILDDIR)/crypto.obj: $(SRCDIR)/crypto.cc $(SRCDIR)/common.hh
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/connections.obj: $(SRCDIR)/connections.cc $(SRCDIR)/common.hh
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/main.obj: $(SRCDIR)/main.cc $(SRCDIR)/common.hh
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/status.obj: $(SRCDIR)/status.cc $(SRCDIR)/common.hh
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/query.obj: $(SRCDIR)/query.cc $(SRCDIR)/common.hh
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

.PHONY: clean

clean:
	@rm -rf $(BUILDDIR)

