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

#ifndef __MQTTSN_EMBED
#define __MQTTSN_EMBED

#include "mqttparams.hpp"
#include "mqttconnection.hpp"
#include "mqtttopic.hpp"

#ifdef ARDUINO
 #include <TimeLib.h>
 #include <arduino.h>
 #define TIMENOW now()
#else
 #include <time.h>
 #define TIMENOW time(NULL)
#endif

#define MQTT_RETURN_ACCEPTED 0x00
#define MQTT_RETURN_CONGESTION 0x01
#define MQTT_RETURN_INVALID_TOPIC 0x02
#define MQTT_RETURN_NOT_SUPPORTED 0x03

class MqttMessageQueue{
public:
  MqttMessageQueue(){
    set = false ;
    address_len = 0 ;
    message_len = 0 ;
    messageid = 0;
  }
  bool set ;
  uint8_t messageid ;
  uint8_t address[PACKET_DRIVER_MAX_ADDRESS_LEN];
  uint8_t address_len ;
  uint8_t message_data[PACKET_DRIVER_MAX_PAYLOAD] ;
  uint8_t message_len ;
};


class MqttGwInfo{
public:
  MqttGwInfo(){
    reset();
  }
  
  void reset(){
    m_allocated=false;
    m_permanent = false;
    m_gwid=0;
    m_address_length=0;
    m_ad_time = 0 ;
    m_ad_duration = 0 ;
    m_advertising = false ;
    m_lastactivity = 0 ;
    m_active = false ;
  }    
  
  bool is_advertising(){
    return m_advertising;
  }
  
  void advertised(uint16_t t){
    m_advertising = true ;
    m_active = true ;
    m_ad_time = TIMENOW ;
    m_lastactivity = m_ad_time ; // update activity as well
    m_ad_duration = t ;
  
  }
  void set_permanent(bool perm){m_permanent = perm;}
  bool is_permanent(){return m_permanent ;}

  void update_activity(){
    m_lastactivity = TIMENOW;
    m_active = true ;
  }
  bool is_active(){
    if (m_permanent) return true ; // always active

    if (m_advertising){
      // Add 1 min grace
      return TIMENOW < m_lastactivity + m_ad_duration + 60;
    } 

    return m_active ;
  }
  void set_active(bool active){m_active = active ;}

  bool match(uint8_t *addr){
    for (uint8_t addri=0; addri < m_address_length; addri++){
      if (m_address[addri] != addr[addri])
	return false;
    }
    return true ;
  }

  void set_address(uint8_t *address, uint8_t len){
    m_address_length = len ;
    memcpy(m_address, address, m_address_length) ;
  }
  uint8_t *get_address(){return m_address;}
  
  uint16_t advertising_duration(){return m_ad_duration;}

  bool is_allocated(){return m_allocated;}
  void set_allocated(bool alloc){m_allocated = alloc;}

  uint8_t get_gwid(){return m_gwid;}
  void set_gwid(uint8_t gwid){m_gwid = gwid;}
  
protected:
  bool m_active ;
  uint8_t m_address[PACKET_DRIVER_MAX_ADDRESS_LEN] ;
  uint8_t m_address_length ;
  uint8_t m_gwid;
  time_t m_ad_time ;
  uint16_t m_ad_duration ;
  time_t m_lastactivity ;
  bool m_permanent ;
  bool m_allocated ;
  bool m_advertising ;
};

class MqttSnEmbed{
public:
  MqttSnEmbed();
  ~MqttSnEmbed() ;

#ifndef ARDUINO
  size_t wchar_to_utf8(const wchar_t *wstr, char *outstr, const size_t maxbytes) ;
  size_t utf8_to_wchar(const char *str, wchar_t *outstr, const size_t maxbytes) ; 
#endif
  void set_driver(IPacketDriver *pdriver){m_pDriver = pdriver;}

  // Create a pre-defined topic. 2 options to add wide char or UTF8 string
  // Applies to server or client connections
  bool create_predefined_topic(uint16_t topicid, const char *name) ;
#ifndef ARDUINO
  bool create_predefined_topic(uint16_t topicid, const wchar_t *name) ;
#endif
  
  // Powers up and configures addresses. Goes into listen
  // mode
  bool initialise(uint8_t address_len, uint8_t *broadcast, uint8_t *address) ;

  // Set the retry attributes. This affects all future connections
  void set_retry_attributes(uint16_t Tretry, uint16_t Nretry) ;

  // Powers down the radio. Call initialise to power up again
  void shutdown() ;

protected:

  // send all queued responses
  // returns false if a message cannot be sent.
  // Recommend retrying later if it fails (only applies if using acks on pipes)
  bool dispatch_queue();

  // Handle all MQTT messages with functions that can be overridden in base class
  static PACKETRECEIVEDCALLBACK(m_fn_packet_received);
  virtual void received_unknown(uint8_t id, uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_advertised(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_searchgw(uint8_t *sender_address, uint8_t *data, uint8_t len){} 
  virtual void received_gwinfo(uint8_t *sender_address, uint8_t *data, uint8_t len){} 
  virtual void received_connect(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_connack(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_willtopicreq(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_willtopic(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_willmsgreq(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_willmsg(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_pingresp(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_pingreq(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_disconnect(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_register(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_regack(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_publish(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_puback(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_pubrec(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_pubrel(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_pubcomp(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_subscribe(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_suback(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_unsubscribe(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  virtual void received_unsuback(uint8_t *sender_address, uint8_t *data, uint8_t len){}
  
  // Queue the data received until dispatch is called.
  // Overwrites old queue messages without error if not dispacted quickly
  void queue_received(const uint8_t *addr,
		      uint8_t messageid,
		      const uint8_t *data,
		      uint8_t len) ;

  // Creates header and body. Writes to address
  // Throws MqttIOErr or MqttOutOfRange exceptions
  // Returns false if connection failed max retries
  bool addrwritemqtt(const uint8_t *address,
		     uint8_t messageid,
		     const uint8_t *buff,
		     uint8_t len);

  bool writemqtt(MqttConnection *con, uint8_t messageid, const uint8_t *buff, uint8_t len);
  void listen_mode() ;
  void send_mode() ;

  MqttTopicCollection m_predefined_topics ;

  volatile MqttMessageQueue m_queue[MQTT_MAX_QUEUE] ;
  uint8_t m_queue_head ;

  time_t m_Tretry ;
  uint16_t m_Nretry ;

  IPacketDriver *m_pDriver ;

#ifndef ARDUINO
  pthread_mutex_t m_mqttlock ;
#endif
};


#endif
