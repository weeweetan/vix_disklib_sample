INCLUDEDIR=../../../include
LIBDIR=../../../lib64

all: vix-disklib-sample

clean:
	$(RM) -f vix-disklib-sample

vix-disklib-sample: vixDiskLibSample.cpp
	$(CXX) -o $@ -I$(INCLUDEDIR) -L$(LIBDIR) $? -ldl -lvixDiskLib
