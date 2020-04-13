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

#ifndef __MQTT_CONNECTION
#define __MQTT_CONNECTION

#include "mqttparams.hpp"
#ifdef ARDUINO
 #include <TimeLib.h>
 #include <arduino.h>
 #define TIMENOW now()
#else
 #include <time.h>
 #define TIMENOW time(NULL)
#endif
#include "mqtttopic.hpp"

class MqttConnection{
public:
  enum State{
    disconnected, connected, asleep, // states
    connecting, disconnecting // transition states
  };
  enum Activity{
    none, willtopic, willmessage, registering, registeringall, publishing, subscribing, searching
  };
  
  MqttConnection() ;
  void update_activity(); // received activity from client or server
  bool send_another_ping() ;
  void reset_ping(){m_last_ping = TIMENOW ;}
  bool is_asleep(){
    return m_state == State::asleep ;
  }

  void set_client_id(const char *sz){strncpy(m_szclientid, sz, PACKET_DRIVER_MAX_PAYLOAD - MQTT_CONNECT_HDR_LEN);}
  bool client_id_match(const char *sz){return (strcmp(sz, m_szclientid) == 0);}
  const char* get_client_id(){return m_szclientid;}
  State get_state(){return m_state;}
  void set_state(State s){m_state = s;m_attempts=0;m_lasttry=TIMENOW;}
  Activity get_activity(){return m_activity;}
  void set_activity(Activity a){m_activity = a;m_attempts=0;m_lasttry=TIMENOW;}
  bool state_timeout(uint16_t timeout);
  uint16_t state_timeout_count(){return m_attempts;}
  bool is_connected(){
    return m_state == State::connected ;
  }
  bool is_disconnected(){
    return m_state == State::disconnected ;
  }


  // This is not a thread safe call. 
  uint16_t get_new_messageid() ;
  
  // Give 5 retries before failing. This mutliplies the time assuming that
  // all pings will be sent timely
  bool lost_contact();

  // Compare address of connection with addr. Returns true if matches
  bool address_match(const uint8_t *addr) ;

  // Set the connection address
  void set_address(const uint8_t *addr, uint8_t len) ;
  // Return the address set in the connection. NULL if not set
  const uint8_t* get_address(){return m_connect_address;}

  // Write a packet to cache against the connection.
  // This will be retried on connection failure
  void set_cache(uint8_t messageid, const uint8_t *message, uint8_t len) ;

  // Read the cache data
  const uint8_t* get_cache(){return m_message_cache;}

  // Read cache size
  uint8_t get_cache_len(){return m_message_cache_len;}

  // Read the cached message ID
  uint8_t get_cache_id(){return m_message_cache_id;}

  // Set the temporary parameters for a publish. This caches
  // the entries.
  void set_pub_entities(uint16_t topicid,
			uint16_t messageid,
			uint8_t topictype,
			int qos, int len, uint8_t *payload, bool retain) ;

  void set_mosquitto_mid(int mid) ;
  
  uint16_t get_pub_topicid();
  uint16_t get_pub_messageid() ;
  int get_pub_qos();
  int get_pub_payload_len();
  const uint8_t* get_pub_payload();
  bool get_pub_retain();
  uint8_t get_pub_topic_type() ;
  int get_mosquitto_mid() ;

  void set_send_topics(bool b){m_sendtopics = b;}
  bool get_send_topics(){return m_sendtopics ;}
  
  void set_gwid(uint8_t gwid){m_gwid = gwid;}
  uint8_t get_gwid(){return m_gwid;}
  MqttConnection *next; // linked list of connections (gw only)
  MqttConnection *prev ; // linked list of connections (gw only)
  //bool enabled ; // This record is valid
  //bool disconnected ; // client issued a disconnect if true
  //bool connection_complete ; // protocol complete
  //bool prompt_will_topic ; // waiting for will topic
  //bool prompt_will_message ; // waiting for will message
  uint16_t duration ; // keep alive duration
  time_t asleep_from ;
  uint16_t sleep_duration ;
  MqttTopicCollection topics ; // Public collection of topics
  
  bool set_will_topic(char *topic, uint8_t qos, bool retain) ;
  bool set_will_message(uint8_t *message, uint8_t len) ;
  bool get_will_retain() ;
  char* get_will_topic() ;
  uint8_t* get_will_message();
  size_t get_will_message_len();
  uint8_t get_will_qos();
  
protected:
  uint8_t m_gwid ; // gw id for client connections
  time_t m_last_ping ;
  time_t m_lastactivity ; // when did we last hear from the client (sec)
  uint16_t m_messageid ;
  char m_szclientid[PACKET_DRIVER_MAX_PAYLOAD - MQTT_CONNECT_HDR_LEN+1] ; // client id for gw
  uint8_t m_connect_address[PACKET_DRIVER_MAX_ADDRESS_LEN] ; // client or gw address
  uint8_t m_address_len ;
  State m_state ;
  Activity m_activity ;
  uint8_t m_message_cache[PACKET_DRIVER_MAX_PAYLOAD] ;
  uint8_t m_message_cache_len ;
  uint8_t m_message_cache_id ;

  // Connection retry attributes
  time_t m_lasttry ;
  uint16_t m_attempts ;

  // Publish entities
  uint16_t m_tmptopicid ;
  uint16_t m_tmpmessageid ;
  int m_tmpqos ;
  int m_tmpmessagelen ;
  uint8_t m_tmppubmessage[PACKET_DRIVER_MAX_PAYLOAD+1] ;  // +1 really needed?
  bool m_tmpretain ;
  uint8_t m_tmptopictype ;
  int m_tmpmosmid ;

  // Will
  char m_willtopic[PACKET_DRIVER_MAX_PAYLOAD - MQTT_WILLTOPIC_HDR_LEN+1] ;
  uint8_t m_willmessage[PACKET_DRIVER_MAX_PAYLOAD - MQTT_WILLMSG_HDR_LEN] ;
  size_t m_willtopicsize ;
  size_t m_willmessagesize ;
  uint8_t m_willtopicqos ;
  bool m_willtopicretain ;

  // Reconnect to dirty conn
  bool m_sendtopics ;

};



#endif
