LDFLAGS?=-lboost_context

benchmark: pingpang createalot
	echo FINISHED

libaco.a:
	$(CC) -g -O3 -Wall -Werror  ../libaco/acosw.S ../libaco/aco.c -c -fPIE
	ar rcs libaco.a acosw.o aco.o

pingpang: libaco.a
	$(CXX) $(CXXFLAGS) pingpang.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o pingpang_fcontext -DUSE_FCONTEXT=1 $(LDFLAGS)
	$(CXX) $(CXXFLAGS) pingpang.cpp libaco.a -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o pingpang_libaco -DUSE_FCONTEXT=0 -DUSE_LIBACO=1
	$(CXX) $(CXXFLAGS) pingpang.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o pingpang_native -DUSE_FCONTEXT=0
	time ./pingpang_fcontext
	time ./pingpang_libaco
	time ./pingpang_native

createalot: libaco.a
	$(CXX) $(CXXFLAGS) createalot.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o createalot_fcontext -DUSE_FCONTEXT=1 $(LDFLAGS)
	$(CXX) $(CXXFLAGS) createalot.cpp libaco.a -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o createalot_libaco -DUSE_FCONTEXT=0 -DUSE_LIBACO=1
	$(CXX) $(CXXFLAGS) createalot.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o createalot_native -DUSE_FCONTEXT=0
	time ./createalot_fcontext
	time ./createalot_libaco
	time ./createalot_native

clean:
	rm -f pingpang_fcontext pingpang_libaco pingpang_native pingpang_sjlj createalot_fcontext createalot_libaco createalot_native createalot_sjlj
