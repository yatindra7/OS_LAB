CC=gcc
FLAGS=-Wall

build:
	make main producer consumer
.PHONY: build

main: main.c macros.h facebook_combined.txt
	$(CC) $(FLAGS) main.c -o main

producer: producer.c macros.h facebook_combined.txt
	$(CC) $(FLAGS) producer.c -o producer

consumer: consumer.c macros.h facebook_combined.txt
	$(CC) $(FLAGS) consumer.c -o consumer

clean:
	rm main producer consumer
.PHONY: clean