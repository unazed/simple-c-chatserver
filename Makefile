.DEFAULT_GOAL := compile

compile:
	g++ -g -Wall -Wno-class-memaccess -o confserver confserver.cc
	g++ -g -Wall -Wno-class-memaccess -o confclient confclient.cc
