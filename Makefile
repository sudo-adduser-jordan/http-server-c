all: win

win:
	gcc app/*.c -o bin/http-server.exe -Wall -lz -lwsock32 -lWs2_32
	./bin/http-server.exe

linux:
	# mkdir -p bin
	gcc app/*.c -o bin/http-server.exe -Wall -lz
	# ./bin/http-server.exe --directory /home/user1/Downloads/codecrafters-http-server-c/
	./bin/http-server.exe

clean:
	rm -rf bin
