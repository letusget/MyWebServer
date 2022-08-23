# 编译有bug
target=webserver_1
src=sem.o condition.o http_conn.o main.o 
$(target):$(src)
	g++ $(src) -o $(target) -pthread
main.o:main.cpp
	g++ -c main.cpp -o main.o 
sem.o:sem.cpp
	g++ -c sem.cpp -pthread
condition.o:condition.cpp
	g++ -c condition.cpp -o condition.o
http_conn.o:http_conn.cpp
	g++ -c http_conn.cpp -o http_conn.o

clean:
	rm $(src)
