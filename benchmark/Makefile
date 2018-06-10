benchmark: pingpang createalot
	echo FINISHED

pingpang:
	$(CXX) pingpang.cpp -std=c++17 -lboost_context-mt -flto -march=native -o pingpang_fcontext
	$(CXX) pingpang.cpp -std=c++17 -flto -march=native -o pingpang_native -DUSE_FCONTEXT=0
	time ./pingpang_fcontext
	time ./pingpang_native

createalot:
	$(CXX) createalot.cpp -std=c++17 -lboost_context-mt -flto -march=native -o createalot_fcontext
	$(CXX) createalot.cpp -std=c++17 -flto -march=native -o createalot_native -DUSE_FCONTEXT=0
	time ./createalot_fcontext
	time ./createalot_native

clean:
	rm -f pingpang_fcontext pingpang_native createalot_fcontext createalot_native
