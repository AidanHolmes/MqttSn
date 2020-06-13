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

// Uses RF24 driver for connection
#include <stdio.h>
#include <signal.h>
#include "RF24Driver.hpp"
#include "wpihardware.hpp"
#include "spihardware.hpp"
#include "clientmqtt.hpp"
#include "radioutil.hpp"

#define ADDR_WIDTH 5

void print_ard_state(NordicRF24 *pRadio);
void con_callback(bool success, uint8_t return_code, uint8_t gwid);
void gwinfo_callback(bool available, uint8_t gwid);
void register_callback(bool success, uint8_t return_code, uint16_t topic_id, uint16_t message_id, uint8_t gwid);
void dis_callback(bool sleeping, uint16_t sleep_duration, uint8_t gwid);
void data_callback(bool success,
		   uint8_t returncode,
		   const char* sztopic,
		   uint8_t *payload,
		   uint8_t payloadlen,
		   uint8_t gwid);


struct sigaction siginthandle ;

wPi pi ;
spiHw spi;

ClientMqttSn mqtt ;
RF24Driver drv ;

const int pin_ce = 27; // Pi
//const int pin_ce = PA1; //STM32
//const int pin_ce = 2; // Teensy
const uint8_t pin_irq = 17; // Pi
//const uint8_t pin_irq = PB0; //STM32
//const uint8_t pin_irq = 15; // Teensy
const uint8_t pin_cs = 0 ; // Pi
//const uint8_t pin_cs = PA4 ; // STM32
//const uint8_t pin_cs = 4 ; // Teensy
const int opt_channel = 0;
const int opt_ack = 0;
const bool opt_block = false ;
const int opt_speed = RF24_1MBPS ; // macro options available in library header
uint8_t opt_rf24broadcast[ADDR_WIDTH] ;
uint8_t opt_rf24address[ADDR_WIDTH];
const bool opt_listen = true ;

void setup() {

  // put your setup code here, to run once:
  if (!spi.spiopen(0, pin_cs)){
    printf("failed to open SPI\n") ;
    exit(0);
  }
  spi.setMode(0) ;
  spi.setSpeed(1000000);
  spi.setBitOrder(false);
  spi.setCSHigh(false);

  drv.set_spi(&spi) ;
  drv.set_timer(&pi);
  drv.set_gpio(&pi, pin_ce, pin_irq);

  drv.reset_rf24();
  mqtt.set_driver(&drv) ;
  pi.milliSleep(500) ; // Allow raido to power up and stablise

  straddr_to_addr("C0C0C0C0C0", opt_rf24broadcast, ADDR_WIDTH);
  straddr_to_addr("A1A1A1A1A1", opt_rf24address, ADDR_WIDTH);

  mqtt.set_client_id("AutoPi");
  mqtt.initialise(ADDR_WIDTH, opt_rf24broadcast, opt_rf24address);
  drv.set_channel(opt_channel) ; // 2.400GHz + channel MHz
  drv.set_data_rate(opt_speed) ;

  mqtt.set_callback_connected(&con_callback) ;
  mqtt.set_callback_gwinfo(&gwinfo_callback) ;
  mqtt.set_callback_disconnected(&dis_callback) ;
  mqtt.set_callback_message(&data_callback) ;
  mqtt.set_callback_register(&register_callback) ;

  mqtt.set_willtopic("client/pi2", 1, true) ;
  mqtt.set_willmessage((uint8_t*)"offline", 7) ;

  //  Serial.begin(9600);
  //while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
    //}
  printf("Starting...\r\n");
  print_ard_state(&drv) ;

}


void print_ard_state(NordicRF24 *pRadio)
{
  int i=0 ;
  uint8_t addr_width = pRadio->get_address_width() ;
  uint8_t address[5], w=0 ;

  printf("Data Ready Interrupt: ");
  printf(pRadio->use_interrupt_data_ready()?"true\r\n":"false\r\n") ;
  printf("Data Sent Interrupt: ");
  printf(pRadio->use_interrupt_data_sent()?"true\r\n":"false\r\n") ;
  printf("Max Retry Interrupt: ");
  printf(pRadio->use_interrupt_max_retry()?"true\r\n":"false\r\n") ;
  printf("CRC Enabled: ");
  printf(pRadio->is_crc_enabled()?"true\r\n":"false\r\n") ;
  printf("Is Powered Up: ");
  printf(pRadio->is_powered_up()?"true\r\n":"false\r\n") ;
  printf("Is Receiver: ");
  printf(pRadio->is_receiver()?"true\r\n":"false\r\n") ;
  printf("2 uint8_t CRC: ");
  printf(pRadio->is_2_byte_crc()?"true\r\n":"false\r\n") ;
  printf("Address Width: ");
  printf("%u",addr_width);
  printf("\r\n");
  printf("Retry Delay: ");
  printf("%u",pRadio->get_retry_delay()) ;
  printf("\r\n");
  printf("Retry Count: ");
  printf("%u",pRadio->get_retry_count()) ;
  printf("\r\n");
  printf("Channel: ");
  printf("%u",pRadio->get_channel()) ;
  printf("\r\n");
  printf("Power Level: ");
  printf("%u",pRadio->get_power_level());
  printf("\r\n");
  printf("Data Rate: ");
  printf("%u",pRadio->get_data_rate());
  printf("\r\n");
  printf("Continuous Carrier: ");
  printf(pRadio->is_continuous_carrier_transmit()?"true\r\n":"false\r\n") ;
  printf("Dynamic Payloads: ");
  printf(pRadio->dynamic_payloads_enabled()?"true\r\n":"false\r\n") ;
  printf("Payload ACK: ");
  printf(pRadio->payload_ack_enabled()?"true\r\n":"false\r\n") ;
  printf("TX No ACK: ");
  printf(pRadio->tx_noack_cmd_enabled()?"true\r\n":"false\r\n") ;

  for (i=0; i < RF24_PIPES;i++){
    printf("Pipe ");
    printf("%d",i);
    printf(" Enabled: ");
    printf(pRadio->is_pipe_enabled(i)?"true\r\n":"false\r\n") ;
    printf("Pipe ");
    printf("%d",i);
    printf(" ACK: ");
    printf(pRadio->is_pipe_ack(i)?"true\r\n":"false\r\n") ;
    pRadio->get_rx_address(i, address, &addr_width) ;
    printf("Pipe ") ;
    printf("%u",i);
    printf(" Address: [") ;
    // Print backwards so MSB is printed first
    for (int j=addr_width-1; j >= 0; j--){
      printf("%02X",address[j]) ;
    }
    printf("]\r\n");

    pRadio->get_payload_width(i, &w) ;
    printf("Pipe %d Payload Width: %d\r\n", i,w) ;
    printf("Pipe %d Dynamic Payloads: %s\r\n\r\n", i, pRadio->is_dynamic_payload(i)?"true\r\n":"false\r\n") ;
  }

  pRadio->get_tx_address(address,&addr_width) ;
  printf("Transmit Address: [") ;
  for (int j=addr_width-1; j >= 0; j--){
    printf("%02X",address[j]) ;
  }
  printf("]\r\n");

}

bool bconnecting = false;
unsigned long last_time = 0;
unsigned long last_search = 0;
const unsigned long search_frequency = 10 ;
const unsigned long message_frequency = 10 ; // seconds
uint16_t willtopic_mid = 0;
uint16_t willtopic_id = 0;

void con_callback(bool success, uint8_t return_code, uint8_t gwid)
{
  uint16_t mid = 0 ;
  bconnecting = false ;
  if (success){
    printf("Connected to gateway ") ;
  }else{
    printf("Failed to connect to gateway error ") ;
    printf("%02X",return_code) ;
    printf(" for gateway ") ;
  }
  printf("%u",gwid);
  printf("\r\n");

  if (success){
    willtopic_mid = mqtt.register_topic("client/pi2");
    if (willtopic_mid == 0){
      printf("Failed to register topic client/pi2\r\n") ;
    }else{
      printf("MID ") ;
      printf("%u",willtopic_mid);
      printf( " - register client/pi2\r\n") ;
    }

    if (!(mid=mqtt.subscribe(1, "#"))){
      printf("Failed to subscribe to #\r\n") ;
    }else{
      printf("MID ") ;
      printf("%u",mid) ;
      printf(" - subscribe to #\r\n") ;
    }
  }
}

void gwinfo_callback(bool available, uint8_t gwid)
{
  if (available){
    printf("Available gateway ") ;
  }else{
    printf("Unavailable gateway ") ;
  }
  printf("%u",gwid) ;
  printf("\r\n") ;
}

void register_callback(bool success, uint8_t return_code, uint16_t topic_id, uint16_t message_id, uint8_t gwid)
{
  // Message ID will be zero for new registrations matching a wildcard topic
  if(success){
    printf("Registered a new topic ") ;
    printf("%u",topic_id);
    printf(" message ID ") ;
    printf("%u",message_id) ;
    printf("\r\n") ;
    if (message_id > 0 && message_id == willtopic_mid){
      if (mqtt.publish(2, topic_id, FLAG_NORMAL_TOPIC_ID, (uint8_t *)"Online", 6, false)){
	willtopic_id = topic_id ;
      }else{
	printf("Failed to publish topic to server\r\n") ;
      }
    }    
  }else{
    printf("Cannot register topic ");
    printf("%u",topic_id);
  }
  printf("\r\n") ;
}

void dis_callback(bool sleeping, uint16_t sleep_duration, uint8_t gwid)
{
  if (gwid > 0){
    mqtt.connect(gwid, false, true, 30) ;
    printf("Disconnected, attempting to reconnect to gateway ") ;
    printf("%u",gwid);
    printf("\r\n");
    bconnecting = false ;
  }
}
void data_callback(bool success,
		   uint8_t returncode,
		   const char* sztopic,
		   uint8_t *payload,
		   uint8_t payloadlen,
		   uint8_t gwid)
{
  if (success){
    printf("Publish received: Topic [") ;
    printf(sztopic);
    printf("] Payload [") ;
    for (uint8_t i=0; i < payloadlen; i++){
      printf("%02X",payload[i]) ;
    }
    printf(" - '");
    for (uint8_t i=0; i < payloadlen; i++){
      printf("%c",(char)payload[i]) ;
    }
    printf("']\n") ;
  }
}

void loop() {
  unsigned long t = time(NULL) ;
  uint8_t gwid = 0;

  if (!mqtt.is_connected() && !bconnecting){
    if (mqtt.get_known_gateway(&gwid)){
      mqtt.connect(gwid, true, true, 30) ;
      printf("Connecting to gateway ") ;
      printf("%u",gwid);
      printf("\r\n");
      bconnecting = true ;
    }else{
      if (last_search + search_frequency < t) {
	printf("Searching...") ;
	mqtt.searchgw(1) ;
	last_search = t ;
      }
    }
  }

  if (mqtt.is_connected() && mqtt.get_known_gateway(&gwid)){
    // Connected to server
    if (last_time + message_frequency < t) {
      last_time = t ;
      printf(".") ;
      if (willtopic_id > 0)
	mqtt.publish(2, willtopic_id, FLAG_NORMAL_TOPIC_ID, (uint8_t *)"Online", 6, false);
    }
  }
  mqtt.manage_connections() ;
  pi.milliSleep(10) ;
}

void siginterrupt(int sig)
{
  printf("\nExiting and resetting radio\n") ;
  pi.output(pin_ce, IHardwareGPIO::low) ;
  mqtt.shutdown();
  drv.reset_rf24() ;
  
  exit(EXIT_SUCCESS) ;
}

int main()
{
  siginthandle.sa_handler = siginterrupt ;
  sigemptyset(&siginthandle.sa_mask) ;
  siginthandle.sa_flags = 0 ;

  if (sigaction(SIGINT, &siginthandle, NULL) < 0){
    fprintf(stderr,"Failed to set signal handler\n") ;
    return EXIT_FAILURE ;
  }

  setup();
  for ( ; ; ) loop() ;
}
