LDFLAGS?=-lboost_context-mt

benchmark: pingpang createalot
	echo FINISHED

pingpang:
	$(CXX) $(CXXFLAGS) pingpang.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o pingpang_fcontext -DUSE_FCONTEXT=1 $(LDFLAGS)
	$(CXX) $(CXXFLAGS) pingpang.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o pingpang_native -DUSE_FCONTEXT=0
	$(CXX) $(CXXFLAGS) pingpang.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o pingpang_sjlj -DUSE_FCONTEXT=0 -DUSE_UCONTEXT=0
	time ./pingpang_fcontext
	time ./pingpang_native
	time ./pingpang_sjlj || true

createalot:
	$(CXX) $(CXXFLAGS) createalot.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o createalot_fcontext -DUSE_FCONTEXT=1 $(LDFLAGS)
	$(CXX) $(CXXFLAGS) createalot.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o createalot_native -DUSE_FCONTEXT=0
	$(CXX) $(CXXFLAGS) createalot.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o createalot_sjlj -DUSE_FCONTEXT=0 -DUSE_UCONTEXT=0
	time ./createalot_fcontext
	time ./createalot_native
	time ./createalot_sjlj || true

clean:
	rm -f pingpang_fcontext pingpang_native pingpang_sjlj createalot_fcontext createalot_native createalot_sjlj
