build:
	mkdir -p bin
	gcc app/*.c -o bin/http-server.exe -Wall
	./bin/http-server.exe --directory files
	# ./bin/http-server.exe

tpool:
	mkdir -p bin
	gcc test/tpool.c app/tpool.c -o bin/tpool.exe -Wall
	./bin/tpool.exe

run:
	./bin/http-server.exe

clean:
	rm -rf bin
