//   Copyright 2020 Aidan Holmes
//
// This file is part of MQTT-SN-EMBED library for embedded devices.
//
// MQTT-SN-EMBED is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// MQTT_SN_EMBED is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with MQTT-SN-EMBED.  If not, see <https://www.gnu.org/licenses/>.

// Use RF24 Driver
#include "RF24Driver.hpp"
#include "clientmqtt.hpp"
#include "wpihardware.hpp"
#include "spihardware.hpp"
#include "radioutil.hpp"
#include "command.hpp"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/select.h>

#define ADDR_WIDTH 5

wPi pi ;
ClientMqttSn *pradio = NULL;
RF24Driver *pdrv = NULL ;

int opt_irq = 0,
  opt_ce = 0,
  opt_gw = 0,
  opt_channel = 0,
  opt_cname = 0,
  opt_speed = 1,
  opt_ack = 0;

void data_received(bool success,
		   uint8_t returncode,
		   const char* sztopic,
		   uint8_t *payload,
		   uint8_t payloadlen,
		   uint8_t gwid)
{
  if (success){
    printf("Publish received: Topic [%s] Payload [", sztopic) ;
    for (uint8_t i=0; i < payloadlen; i++){
      printf(" %X", payload[i]) ;
    }
    printf(" - '");
    for (uint8_t i=0; i < payloadlen; i++){
      printf("%c", payload[i]) ;
    }
    printf("']\n") ;
  }
}

bool inputAvailable()
{
  struct timeval tv;
  fd_set fds;
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
  return (FD_ISSET(0, &fds));
}

void siginterrupt(int sig)
{
  printf("\nExiting and resetting radio\n") ;
  pi.output(opt_ce, IHardwareGPIO::low) ;
  if (pradio){
    pradio->shutdown();
    pdrv->reset_rf24() ;
  }
  exit(EXIT_SUCCESS) ;
}

void connect(char params[][30], int count)
{
  uint8_t gw = 0;
  bool will = false ;
  bool clean = true ;
  
  if (!pradio->get_known_gateway(&gw)){
    printf("Cannot connect, no known gateway\n") ;
    return;
  }

  if (count >= 2){
    // Will set
    will = true ;
    pradio->set_willtopic(params[0], 3);
    pradio->set_willmessage((uint8_t *)params[1], strlen(params[1])) ;
  }else{
    pradio->set_willtopic((char *)NULL,0) ;
    pradio->set_willmessage(NULL,0) ;
  }

  if (count == 3 && params[2][0] == 'n'){
    clean = false ;
  }
  
  if (!pradio->connect(gw, will, clean, 30)){
    printf("Connecting to gateway %u\n", gw) ;
  }
}

void search(char params[][30], int count)
{
  if(pradio->searchgw(1)){
    printf("Sending gateway search...\n");
  }else{
    printf("Failed to send search!\n");
  }
}
void disconnect(char params[][30], int count)
{
  uint16_t  sleep_duration = 0 ;
  for (int i=0; i <count; i++){
    printf("PARAM %d: %s\n", i, params[i]);
  }
  if (count > 0){
    sleep_duration = (uint16_t)atoi(params[0]);
  }
  if (pradio->disconnect(sleep_duration)){
    printf("Disconnecting from server, sleeping %u\n", sleep_duration);
  }else{
    printf("Failed to disconnect from server\n");
  }
}

void rf24_state(char params[][30], int count)
{
  print_state(pdrv) ;
}

void gwinfo (char params[][30], int count)
{
  pradio->print_gw_table();
}

void addgw (char params[][30], int count)
{
  uint8_t rf24address[ADDR_WIDTH] ;

  if (count < 2) return ;
  
  if (!straddr_to_addr(params[1], rf24address, ADDR_WIDTH)){
    fprintf(stderr, "Invalid address\n") ;
    return;
  }

  uint8_t gwid = atoi(params[0]) ;

  if (pradio->add_gateway(rf24address, gwid, 0, true)){
    printf("Added new gateway %u, address %s\n", gwid, params[1]) ;
  }else{
    fprintf(stderr, "Failed to add perm gateway %u with address %s\n", gwid, params[1]) ;
  }
}

void publish(char params[][30], int count)
{
  if (count < 3){
    printf("publish qos topic|topicid value [retain]\n") ;
    return ;
  }

  bool b_strtopic = false, b_retain = false; 

  uint8_t gwid = 0;
  if (!pradio->get_known_gateway(&gwid)){
    fprintf(stderr, "No known gateway to connect to\n") ;
    return ;
  }

  int qos = atoi(params[0]);
  for (char *p = params[1]; *p; p++){
    if (!(*p >= '0' && *p <= '9')){
      b_strtopic = true ;
      break;
    }
  }
  if (strlen(params[1]) != 2 && b_strtopic){
    fprintf(stderr, "Incorrect length of short topic\n");
  }

  if (count >= 4 && strcasecmp(params[3], "retain") == 0) b_retain = true ;
  printf("Publishing with QoS: %d, topic: %s, retaining: %s\n", qos, params[1], b_retain?"yes":"no");
      
  if (qos == -1){
    if (b_strtopic){
      pradio->publish_noqos(gwid, params[1], (uint8_t *)params[2], strlen(params[2]), b_retain);
    }else{
      pradio->publish_noqos(gwid, atoi(params[1]), FLAG_DEFINED_TOPIC_ID, (uint8_t *)params[2], strlen(params[2]), b_retain);
    }
  }else{
    if (b_strtopic){
      pradio->publish(qos, params[1], (uint8_t *)params[2], strlen(params[2]), b_retain);
    }else{
      pradio->publish(qos, (uint16_t)atoi(params[1]), FLAG_NORMAL_TOPIC_ID, (uint8_t *)params[2], strlen(params[2]), b_retain);
    }
  }
}
void subscribe(char params[][30], int count)
{
  bool b_numtopic = false;
  
  if (count < 2){
    printf("subscribe qos topic|topicid\n") ;
    return ;
  }

  uint8_t gwid = 0;
  if (!pradio->get_known_gateway(&gwid)){
    fprintf(stderr, "No known gateway to connect to\n") ;
    return ;
  }
    
  int qos = atoi(params[0]);
  for (char *p = params[1]; *p; p++){
    if (*p >= '0' && *p <= '9'){
      b_numtopic = true ;
      break;
    }
  }

  if (b_numtopic){
    pradio->subscribe((uint8_t)qos, (uint16_t)atoi(params[1]));
  }else{
    pradio->subscribe((uint8_t)qos, params[1], false);
  }
}

void register_topic(char params[][30], int count)
{
  if (count < 1){
    printf("register topicname\n") ;
    return ;
  }

  uint16_t ret = pradio->register_topic(params[0]);
  if (ret != 0){
    printf("%s already registered with ID: %u\n", params[0], ret) ;
  }  
}

int main(int argc, char **argv)
{
  const char usage[] = "Usage: %s -c ce -i irq -a address -b address [-n clientname] [-o channel] [-s 250|1|2] [-x]\n" ;
  const char optlist[] = "i:c:o:a:b:s:n:x" ;
  int opt = 0 ;
  uint8_t rf24address[ADDR_WIDTH] ;
  uint8_t rf24broadcast[ADDR_WIDTH] ;
  bool baddr = false;
  bool bbroad = false ;
  char szclientid[PACKET_DRIVER_MAX_PAYLOAD - MQTT_CONNECT_HDR_LEN+1] ;
  Command commands[] = {Command("connect",&connect,true),
			Command("disconnect",NULL,true),
			Command("topic",NULL,true),
			Command("search",&search,true),
			Command("rf24", &rf24_state, true),
			Command("disconnect", &disconnect, true),
			Command("gwinfo", &gwinfo, true),
			Command("addgw", &addgw, true),
			Command("publish", &publish, true),
			Command("register", &register_topic, true),
                        Command("subscribe", &subscribe, true)} ;
  
  struct sigaction siginthandle ;

  ClientMqttSn mqtt ;
  RF24Driver drv ;

  pdrv = &drv ;
  pradio = &mqtt;

  siginthandle.sa_handler = siginterrupt ;
  sigemptyset(&siginthandle.sa_mask) ;
  siginthandle.sa_flags = 0 ;

  if (sigaction(SIGINT, &siginthandle, NULL) < 0){
    fprintf(stderr,"Failed to set signal handler\n") ;
    return EXIT_FAILURE ;
  }

  while ((opt = getopt(argc, argv, optlist)) != -1) {
    switch (opt) {
    case 'x': //ack
      opt_ack = 1 ;
      break ;
    case 'i': // IRQ pin
      opt_irq = atoi(optarg) ;
      break ;
    case 'c': // CE pin
      opt_ce = atoi(optarg) ;
      break ;
    case 'o': // channel
      opt_channel = atoi(optarg) ;
      break;
    case 's': // speed
      opt_speed = atoi(optarg) ;
      break ;
    case 'a': // unicast address
      if (!straddr_to_addr(optarg, rf24address, ADDR_WIDTH)){
	fprintf(stderr, "Invalid address\n") ;
	return EXIT_FAILURE ;
      }
      baddr = true ;
      break;
    case 'b': // broadcast address
      if (!straddr_to_addr(optarg, rf24broadcast, ADDR_WIDTH)){
	fprintf(stderr, "Invalid address\n") ;
	return EXIT_FAILURE ;
      }
      bbroad = true ;
      break;
    case 'n': // client name
      strncpy(szclientid, optarg, PACKET_DRIVER_MAX_PAYLOAD - MQTT_CONNECT_HDR_LEN) ;
      opt_cname = 1 ;
      break;
    default: // ? opt
      fprintf(stderr, usage, argv[0]);
      exit(EXIT_FAILURE);
    }
  }
  
  if (!opt_ce || !opt_irq || !bbroad || !baddr){
    fprintf(stderr, usage, argv[0]);
    exit(EXIT_FAILURE);
  }

  switch (opt_speed){
  case 1:
    opt_speed = RF24_1MBPS ;
    break ;
  case 2:
    opt_speed = RF24_2MBPS ;
    break ;
  case 250:
    opt_speed = RF24_250KBPS ;
    break ;
  default:
    fprintf(stderr, "Invalid speed option. Use 250, 1 or 2\n") ;
    return EXIT_FAILURE ;
  }

  // Create spidev instance. wiringPi interface doesn't seem to work
  // The SPIDEV code has more configuration to handle devices. 
  spiHw spi ;

  // Pi has only one bus available on the user pins. 
  // Two devices 0,0 and 0,1 are available (CS0 & CS1). 
  if (!spi.spiopen(0,0)){ // init SPI
    fprintf(stderr, "Cannot Open SPI\n") ;
    return 1;
  }

  spi.setCSHigh(false) ;
  spi.setMode(0) ;

  // 1 KHz = 1000 Hz
  // 1 MHz = 1000 KHz
  spi.setSpeed(6000000) ;
  drv.set_spi(&spi) ;
  drv.set_timer(&pi);

  if (!drv.set_gpio(&pi, opt_ce, opt_irq)){
    fprintf(stderr, "Failed to initialise GPIO\n") ;
    return 1 ;
  }
  
  drv.reset_rf24();
  mqtt.set_driver(&drv) ;

  if (opt_cname)
    mqtt.set_client_id(szclientid);
  else
    mqtt.set_client_id("CL") ;

  mqtt.initialise(ADDR_WIDTH, rf24broadcast, rf24address) ;
  // Link layer specific options
  drv.set_channel(opt_channel) ; // 2.400GHz + channel MHz
  drv.set_data_rate(opt_speed) ; 
  
  int command_count = sizeof(commands) / sizeof(Command);
  const int max_params = 10 ;
  const int max_param_text = 30 ;
  char parameters[max_params][max_param_text];
  int param_count = 0;
  int param_index = 0;
  Command *pFound = NULL ;

  mqtt.set_callback_message(&data_received) ;

  // Working loop
  for ( ; ; ){
    while (inputAvailable()){
      char c;
      read(STDIN_FILENO, &c, 1);
      switch(c){
      case ' ':
      case '\t':
	if (pFound){
	  if (param_index > 0){
	    parameters[param_count][param_index] = '\0';
	    if (param_count < max_params ){
	      param_count++ ;
	    }
	  }
	}
	param_index = 0 ;
	break;
      case '\n':
	for (int i=0; i < command_count; i++){
	  commands[i].reset();
	}
	if (pFound){
	  if (param_index > 0){
	    parameters[param_count][param_index] = '\0';
	    param_count++ ;
	  }
	  pFound->cmd(parameters, param_count) ;
	}
	
	param_count = 0 ;
	param_index = 0 ;
	pFound = NULL;
	break;
      case '\r':
	break;
      default:
	if (pFound){
	  if (param_index < max_param_text-1){
	    parameters[param_count][param_index++] = c ;
	  }
	}else{
	  for (int i=0; i < command_count; i++){
	    if(commands[i].found(c)){
	      pFound = &commands[i] ;
	    }
	  }
	}
	break;
      }
      
    }
    
    mqtt.manage_connections() ;
    pi.milliSleep(5) ; // 5ms wait
  }

  drv.reset_rf24();

  return 0 ;
}
