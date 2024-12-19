CXX = g++
CFLAGS = -g -Wall -std=c++17

test_predictor: test_predictor.cc vp.h tage.h
	$(CXX) $(CFLAGS) -o test_predictor test_predictor.cc tage.h

clean:
	rm -f test_predictor *.o