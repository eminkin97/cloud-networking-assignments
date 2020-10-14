all: vec ls

vec: main.c monitor_neighbors.h
	gcc -pthread -o vec_router main.c monitor_neighbors.h

ls: main.c
	gcc -pthread -o ls_router main.c
	
ms: manager_send.c
	gcc -pthread -o ls_router manager_send.c

.PHONY: clean
clean:
	rm *.o vec_router ls_router
