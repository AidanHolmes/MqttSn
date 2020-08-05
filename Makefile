DEBUG=-DDEBUG
HWLIBS = ../hardware
DRIVER = ../NordicRF24
CXX = g++
CC = g++
CXXFLAGS= -std=c++11 -Wall -I$(HWLIBS) -I$(DRIVER) $(DEBUG) -g
CFLAGS = $(CXXFLAGS)
LIBS = -lwiringPi -lpihw -lrf24 -lpthread
LDFLAGS = -L$(HWLIBS) -L$(DRIVER)

SRCS_LIB = clientmqtt.cpp mqttsnembed.cpp mqttconnection.cpp mqtttopic.cpp servermqtt.cpp
H_LIB = $(SRCS_LIB:.cpp=.hpp)
OBJS_LIB = $(SRCS_LIB:.cpp=.o)

SRCS_AUTOMQTTCLIENT = autoclient.cpp clientmqtt.cpp mqttsnembed.cpp mqttconnection.cpp mqtttopic.cpp
OBJS_AUTOMQTTCLIENT = $(SRCS_AUTOMQTTCLIENT:.cpp=.o) 

SRCS_MQTTCLIENT = mqttclientapp.cpp clientmqtt.cpp mqttsnembed.cpp mqttconnection.cpp mqtttopic.cpp command.cpp
OBJS_MQTTCLIENT = $(SRCS_MQTTCLIENT:.cpp=.o) 

SRCS_MQTTSERVER = mqttserverapp.cpp servermqtt.cpp mqttsnembed.cpp mqttconnection.cpp mqtttopic.cpp
OBJS_MQTTSERVER = $(SRCS_MQTTSERVER:.cpp=.o) 

MQTTAUTOCLIENTEXE = mqttautoclient
MQTTSERVEREXE = mqttsnserver
MQTTCLIENTEXE = mqttsnclient
ARCHIVE = libmqttsn.a

.PHONY: all
all: $(MQTTSERVEREXE) $(MQTTCLIENTEXE) $(MQTTAUTOCLIENTEXE) $(ARCHIVE)

$(MQTTSERVEREXE): $(OBJS_MQTTSERVER) $(OBJS_CMD) libhw librf24
	$(CXX) $(LDFLAGS) $(OBJS_MQTTSERVER) $(OBJS_CMD) -lmosquitto $(LIBS) -o $@

$(MQTTCLIENTEXE): $(OBJS_MQTTCLIENT) $(OBJS_CMD) libhw librf24
		$(CXX) $(LDFLAGS) $(OBJS_MQTTCLIENT) $(OBJS_CMD) $(LIBS) -o $@

$(MQTTAUTOCLIENTEXE): $(OBJS_AUTOMQTTCLIENT) $(OBJS_CMD) libhw librf24
		$(CXX) $(LDFLAGS) $(OBJS_AUTOMQTTCLIENT) $(OBJS_CMD) $(LIBS) -o $@

$(ARCHIVE): $(OBJS_LIB)
	ar r $@ $?

$(OBJS_LIB): $(H_LIB)

.PHONY: libhw
libhw:
	$(MAKE) libpihw.a -C $(HWLIBS)

.PHONY: librf24
librf24:
	$(MAKE) librf24.a -C $(DRIVER)

.PHONY: clean
clean:
	rm -f *.o $(MQTTSERVEREXE) $(MQTTAUTOCLIENTEXE) $(ARCHIVE) $(MQTTCLIENTEXE)
