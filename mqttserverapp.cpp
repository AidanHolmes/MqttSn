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

#include "RF24Driver.hpp"
#include "servermqtt.hpp"
#include "wpihardware.hpp"
#include "spihardware.hpp"
#include "radioutil.hpp"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>

#define ADDR_WIDTH 5

wPi pi ;
ServerMqttSn *pradio = NULL;
RF24Driver *pdrv = NULL ;

int opt_irq = 0,
  opt_ce = 0,
  opt_channel = 0,
  opt_cname = 0,
  opt_speed = 1,
  opt_ack = 0;

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

int main(int argc, char **argv)
{
  const char usage[] = "Usage: %s -c ce -i irq -a address -b address [-o channel] [-s 250|1|2] [-x]\n" ;
  const char optlist[] = "i:c:o:a:b:s:x" ;
  int opt = 0 ;
  uint8_t rf24address[ADDR_WIDTH] ;
  uint8_t rf24broadcast[ADDR_WIDTH] ;
  bool baddr = false;
  bool bbroad = false ;
  
  struct sigaction siginthandle ;

  RF24Driver drv ;
  ServerMqttSn mqtt ;

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
  drv.set_timer(&pi) ;

  if (!drv.set_gpio(&pi, opt_ce, opt_irq)){
    fprintf(stderr, "Failed to initialise GPIO\n") ;
    return 1 ;
  }
  
  drv.reset_rf24();

  mqtt.set_driver(&drv) ;
  
  mqtt.set_gateway_id(88) ;

  mqtt.initialise(ADDR_WIDTH, rf24broadcast, rf24address) ;
  mqtt.set_advertise_interval(400);

  // optional driver overrides
  // initialise will set some values, override here to change
  drv.set_channel(opt_channel) ; // 2.400GHz + channel MHz
  drv.set_data_rate(opt_speed) ; 

  //print_state(&drv) ;
  
  // Working loop
  for ( ; ; ){
    mqtt.manage_connections() ;
    pi.milliSleep(5); // 5ms wait
  }

  drv.reset_rf24();

  return 0 ;
}
