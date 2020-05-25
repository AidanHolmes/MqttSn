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

class MqttMessage{
public:
  enum Activity{
    none, willtopic, willmessage, registering, registeringall, publishing, subscribing, searching, disconnecting
  };

  MqttMessage(){reset();}
  void reset() ;
  void set_active(){m_active = true ;}
  void set_inactive(){m_active = false;}
  bool is_active(){return m_active;}
  
  void set_activity(Activity connection_state){m_state = connection_state;}
  Activity get_activity(){return m_state;}
  
  // Write a packet to cache against the connection.
  void set_message(uint8_t messagetypeid, const uint8_t *message, uint8_t len) ;

  // Read the cache data
  const uint8_t* get_message(){return m_message_cache;}

  uint8_t get_message_type(){return m_message_cache_typeid;}
  
  // Read cache size
  uint8_t get_message_len(){return m_message_cache_len;}

  bool has_content(){return m_message_set;}

  void set_message_id(uint16_t messageid, bool isexternal=false){m_messageid = messageid;m_external_message = isexternal;}
  uint16_t get_message_id(){return m_messageid;}
  bool is_external(){return m_external_message;}

  void set_qos(uint8_t qos){m_qos = qos ;}
  uint8_t get_qos(){return m_qos ;}
  
  void set_topic_id(uint16_t topicid){m_topicid = topicid;}
  uint16_t get_topic_id(){return m_topicid;}

  void set_topic_type(uint8_t topictype){m_topictype = topictype;}
  uint8_t get_topic_type(){return m_topictype ;}

  void set_mosquitto_mid(int mid){m_mosmid = mid ;}
  int get_mosquitto_mid(){return m_mosmid;}

  bool state_timeout(uint16_t timeout);
  uint16_t state_timeout_count(){return m_attempts;}

  bool has_expired(time_t timeout);
  bool has_failed(uint16_t max_attempts){return m_attempts >= max_attempts;}
  // Retain message, but reset the sent and attempt status
  void reset_message(){m_attempts = 0 ; m_sent = false;}

  // If the message is being sent from the queue then call this to
  // set flags and increment attempts
  void sending();
  bool is_sending(){return m_sent;}

  void one_shot(bool bset){m_oneshot = bset;}
  
protected:
  bool m_active ; // Is this in-use or free to hold another connection?
  uint8_t m_message_cache_typeid ; // MQTT message header ID
  uint8_t m_message_cache[PACKET_DRIVER_MAX_PAYLOAD] ;
  uint8_t m_message_cache_len ;
  
  uint16_t m_messageid ;
  bool m_external_message ;
  uint16_t m_topicid ;
  uint8_t m_topictype ;
  uint8_t m_qos ;
  int m_mosmid ;
  Activity m_state ;

  bool m_sent ;
  bool m_oneshot;
  bool m_message_set ;
  
  // Connection retry attributes
  time_t m_lasttry ;
  uint16_t m_attempts ;
  
private:

};

class MqttMessageCollection{
public:
  MqttMessageCollection() ;
  // Create an empty message. Will allocate a messageid, reset the message and set a state
  MqttMessage* add_message(MqttMessage::Activity state);
  MqttMessage* get_message(uint16_t messageid, bool externalid=false);
  MqttMessage* get_mos_message(int messageid) ;
  MqttMessage* get_active_message() ;
  void clear_queue() ;
  
  
protected:
  uint16_t get_new_messageid();
  
  MqttMessage m_messages[MQTT_MESSAGES_INFLIGHT] ;
  uint16_t m_lastmessageid ;
  uint16_t m_queuehead ;
  uint16_t m_queuetail ;
};

class MqttConnection{
public:
  enum State{
    disconnected, connected, asleep, // states
    connecting // transition states
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
  void set_state(State s){m_state = s;}
  bool is_connected(){
    return m_state == State::connected ;
  }
  bool is_disconnected(){
    return m_state == State::disconnected ;
  }


  // Give 5 retries before failing. This mutliplies the time assuming that
  // all pings will be sent timely
  bool lost_contact();

  // Compare address of connection with addr. Returns true if matches
  bool address_match(const uint8_t *addr) ;

  // Set the connection address
  void set_address(const uint8_t *addr, uint8_t len) ;
  // Return the address set in the connection. NULL if not set
  const uint8_t* get_address(){return m_connect_address;}

  void set_send_topics(bool b){m_sendtopics = b;}
  bool get_send_topics(){return m_sendtopics ;}
  
  void set_gwid(uint8_t gwid){m_gwid = gwid;}
  uint8_t get_gwid(){return m_gwid;}
  MqttConnection *next; // linked list of connections (gw only)
  MqttConnection *prev ; // linked list of connections (gw only)
  uint16_t duration ; // keep alive duration
  time_t asleep_from ;
  uint16_t sleep_duration ;
  MqttTopicCollection topics ; // Public collection of topics
  MqttMessageCollection messages;
  
  bool set_will_topic(const char *topic, uint8_t qos, bool retain) ;
  bool set_will_message(const uint8_t *message, uint8_t len) ;
  bool get_will_retain() ;
  char* get_will_topic() ;
  uint8_t* get_will_message();
  size_t get_will_message_len();
  uint8_t get_will_qos();
  
protected:
  uint8_t m_gwid ; // gw id for client connections
  time_t m_last_ping ;
  time_t m_lastactivity ; // when did we last hear from the client (sec)
  char m_szclientid[PACKET_DRIVER_MAX_PAYLOAD - MQTT_CONNECT_HDR_LEN+1] ; // client id for gw
  uint8_t m_connect_address[PACKET_DRIVER_MAX_ADDRESS_LEN] ; // client or gw address
  uint8_t m_address_len ;
  State m_state ;

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
