ACLOCAL_AMFLAGS = -I build-aux/m4

include_HEADERS = include/recursive_shared_mutex.h

librsm_la_SOURCES = lib/recursive_shared_mutex.cpp \
	$(include_HEADERS)

librsm_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
librsm_la_CPPFLAGS = $(AM_CPPFLAGS)
librsm_la_CXXFLAGS = $(AM_CXXFLAGS) -I$(top_srcdir)/include

lib_LTLIBRARIES = librsm.la

CLEANFILES = lib/*.gcda lib/*.gcno

exp_test_files =

if ENABLE_EXPERIMENTAL
librsm_exp_la_SOURCES = lib/experimental/exp_recursive_shared_mutex.cpp \
	lib/experimental/exp_recursive_shared_mutex.h

librsm_exp_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
librsm_exp_la_CPPFLAGS = $(AM_CPPFLAGS)
librsm_exp_la_CXXFLAGS = $(AM_CXXFLAGS)

lib_LTLIBRARIES += librsm_exp.la

exp_test_files += \
	test/experimental/exp_rsm_promotion_tests.cpp \
	test/experimental/exp_rsm_simple_tests.cpp \
	test/experimental/exp_rsm_starvation_tests.cpp \
	$(librsm_exp_la_SOURCES)

CLEANFILES += lib/experimental/*.gcda lib/experimental/*.gcno
endif

if ENABLE_TESTS
bin_PROGRAMS = test/test_rsm
TEST_BINARY = test/test_rsm$(EXEEXT)

test_test_rsm_SOURCES = test/test_cxx_rsm.cpp \
	test/rsm_promotion_tests.cpp \
	test/rsm_simple_tests.cpp \
	test/rsm_starvation_tests.cpp \
	test/test_cxx_rsm.h \
	test/timer.cpp \
	test/timer.h \
	$(librsm_la_SOURCES) \
	$(exp_test_files)

test_test_rsm_CPPFLAGS = $(AM_CPPFLAGS) $(BOOST_CPPFLAGS) $(TESTDEFS)
test_test_rsm_CXXFLAGS = $(AM_CXXFLAGS) -I$(top_srcdir)/include -pthread
if ENABLE_EXPERIMENTAL
test_test_rsm_CXXFLAGS += -I$(top_srcdir)/lib/experimental -I$(top_srcdir)/test
endif
test_test_rsm_LDFLAGS = -static $(LIBTOOL_APP_LDFLAGS)
test_test_rsm_LDADD = $(BOOST_LIBS)

CLEAN_RSM_TEST = test/*.gcda test/*.gcno

CLEANFILES += $(CLEAN_RSM_TEST)

rsm_test: $(TEST_BINARY)

rsm_test_check: $(TEST_BINARY) FORCE
	$(MAKE) check-TESTS TESTS=$^

rsm_test_clean : FORCE
	rm -f $(CLEAN_RSM_TEST) $(test_test_rsm_OBJECTS) $(TEST_BINARY)
endif

dist_noinst_SCRIPTS = autogen.sh
