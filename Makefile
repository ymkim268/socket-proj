# Makefile for EE450 Socket Programming Project, Fall 2016
#
# you need to have nums.csv file in same directory as client
# Reference Material 
# - Beej's Guide to Network Programming
#---------------------------------------------------------------
# make all
#	Compiles all files and creates executables
# make serverA
#	Runs server A
# make serverB
#	Runs server B
# make serverC
#	Runs server C
# make aws
#	Runs AWS
# ./client <function>
#	Starts the client performs <function> on nums.csv file
#---------------------------------------------------------------
# <function>
# min, max, sum, sos, sort
#---------------------------------------------------------------
all:
	gcc -Wall reduction.c helper.c servera.c -o servera
	gcc -Wall servera.c reduction.c helper.c -o servera
	gcc -Wall serverb.c reduction.c helper.c -o serverb
	gcc -Wall serverc.c reduction.c helper.c -o serverc
	gcc -Wall aws.c reduction.c helper.c -o aws
	gcc -Wall client.c reduction.c helper.c -o client

.PHONY: servera serverb serverc aws

servera:
	./servera
serverb:
	./serverb
serverc:
	./serverc
aws:
	./aws
	
clean:
	-rm serverc
	-rm serverb
	-rm servera
	-rm aws
	-rm client


