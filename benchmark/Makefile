LDFLAGS?=-lboost_context-mt

benchmark: pingpang createalot
	echo FINISHED

pingpang:
	$(CXX) $(CXXFLAGS) pingpang.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o pingpang_fcontext -DUSE_FCONTEXT=1 $(LDFLAGS)
	$(CXX) $(CXXFLAGS) pingpang.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o pingpang_native -DUSE_FCONTEXT=0
	time ./pingpang_fcontext
	time ./pingpang_native

createalot:
	$(CXX) $(CXXFLAGS) createalot.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o createalot_fcontext -DUSE_FCONTEXT=1 $(LDFLAGS)
	$(CXX) $(CXXFLAGS) createalot.cpp -std=c++17 -flto -march=native -DNDEBUG=1 -O3 -o createalot_native -DUSE_FCONTEXT=0
	time ./createalot_fcontext
	time ./createalot_native

clean:
	rm -f pingpang_fcontext pingpang_native createalot_fcontext createalot_native
