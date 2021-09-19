LDFLAGS?=-lboost_context

benchmark: pingpong createalot
	echo FINISHED

libaco.a:
	$(CC) -g -O3 -Wall -Werror  ../libaco/acosw.S ../libaco/aco.c -c -fPIE
	ar rcs libaco.a acosw.o aco.o

pingpong: libaco.a
	$(CXX) $(CXXFLAGS) pingpong.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o pingpong_fcontext -DUSE_FCONTEXT=1 $(LDFLAGS)
	$(CXX) $(CXXFLAGS) pingpong.cpp libaco.a -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o pingpong_libaco -DUSE_FCONTEXT=0 -DUSE_LIBACO=1
	$(CXX) $(CXXFLAGS) pingpong.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o pingpong_native -DUSE_FCONTEXT=0
	time ./pingpong_fcontext
	time ./pingpong_libaco
	time ./pingpong_native

createalot: libaco.a
	$(CXX) $(CXXFLAGS) createalot.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o createalot_fcontext -DUSE_FCONTEXT=1 $(LDFLAGS)
	$(CXX) $(CXXFLAGS) createalot.cpp libaco.a -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o createalot_libaco -DUSE_FCONTEXT=0 -DUSE_LIBACO=1
	$(CXX) $(CXXFLAGS) createalot.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o createalot_native -DUSE_FCONTEXT=0
	time ./createalot_fcontext
	time ./createalot_libaco
	time ./createalot_native

clean:
	rm -f pingpong_fcontext pingpong_libaco pingpong_native pingpong_sjlj createalot_fcontext createalot_libaco createalot_native createalot_sjlj
