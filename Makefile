
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

CFLAGS += -Wall -std=c99
CXXFLAGS += -Wall -Weffc++ -Woverloaded-virtual -Wsign-promo -Werror
LDFLAGS += -lm -lev -lprotobuf -lsqlite3

ifeq ($(uname_S),Linux)
  LDFLAGS += -lpthread
endif

ifneq ($(NDEBUG),1)
	CXXFLAGS += -g -DDEBUG
	CFLAGS += -g -DDEBUG
endif

all: harq-http

SRC=$(sort $(wildcard src/*.cpp))
OBJ=$(patsubst %.cpp,%.o,$(SRC))
OBJ+=src/http_parser.o

harq-http: $(OBJ) 
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJ)

rebuild_pb:
	protoc -Isrc --cpp_out=src src/http.proto
	mv src/http.pb.cc src/http.pb.cpp
	protoc -Isrc --cpp_out=src src/wire.proto
	mv src/wire.pb.cc src/wire.pb.cpp

clean:
	-rm harq-http
	-rm src/*.o

distclean: clean
	-rm vendor/*.a
	cd vendor/leveldb; make clean

vendor/libleveldb.a:
	cd vendor/leveldb; make && cp libleveldb.a ..

dep:
	: > depend
	for i in $(SRC); do $(CC) $(CXXFLAGS) -MM -MT $${i%.cpp}.o $$i >> depend; done

.PHONY: clean distclean dep

-include depend
