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

#ifndef __MQTT_TOPIC_H
#define __MQTT_TOPIC_H

#include <stdint.h>
#include "mqttparams.hpp"
#ifdef ARDUINO
 #include <TimeLib.h>
 #include <arduino.h>
 #define TIMENOW now()
#else
 #include <time.h>
 #define TIMENOW time(NULL)
#endif

class MqttTopic{
public:
  MqttTopic();
  MqttTopic(uint16_t topic, uint16_t mid, const char *sztopic);

  bool match(const char *sztopic) ;
  void set_topic(uint16_t topic, uint16_t messageid, const char *sztopic) ;
  void reset() ;
  void set_subscribed(bool sub){m_issubscribed = sub;}
  bool is_subscribed(){return m_issubscribed;}
  bool registration_expired(){return (m_registered_at + m_timeout) < TIMENOW;}
  bool is_head(){return !m_prev;}
  uint16_t get_id(){return m_topicid;}
  uint16_t get_message_id(){return m_messageid;}
  void set_message_id(uint16_t mid){m_messageid = mid;}
  const char *get_topic(){return m_sztopic;}
  MqttTopic *next(){return m_next;}
  MqttTopic *prev(){return m_prev;}
  bool is_complete(){return m_acknowledged;}
  bool is_wildcard(){return m_iswildcard;}
  void complete(uint16_t tid);
  void set_predefined(bool predefined){m_predefined = predefined;}
  bool is_predefined(){return m_predefined;}
  void set_qos(uint8_t qos){m_topicqos = qos;}
  uint8_t get_qos(){return m_topicqos;}
  void unlink(){if (!is_head())m_prev->m_next = m_next;}
  void link_head(MqttTopic *topic){if (m_prev)m_prev->m_next = topic;m_prev = topic;} // adds topic ahead
  void link_tail(MqttTopic *topic){if (m_next)m_next->m_prev = topic;m_next = topic;} // adds topic after
  void set_short_topic(bool bset){m_isshort = bset;}
  bool is_short_topic(){return m_isshort;}
protected:
  MqttTopic *m_next ;
  MqttTopic *m_prev ;
  char m_sztopic[PACKET_DRIVER_MAX_PAYLOAD - MQTT_WILLMSG_HDR_LEN+1];
  uint16_t m_topicid ;
  uint16_t m_messageid ; // used by clients
  bool m_acknowledged ;
  time_t m_registered_at ;
  uint16_t m_timeout ;
  bool m_predefined;
  bool m_iswildcard;
  bool m_issubscribed;
  uint8_t m_topicqos ;
  bool m_isshort;
};

class MqttTopicCollection{
public:
  MqttTopicCollection() ;
  ~MqttTopicCollection() ;
  
  // Client connection will register for a new topic
  // topic remains incomplete until confirmed by server and client calls complete_topic
  MqttTopic* reg_topic(const char *sztopic, uint16_t messageid) ;
  
  // Server adds the topic. a call to complete_topic is not required when a
  // server adds a topic.
  // Will return a new Topic or if the topic already exists, the existing Topic object
  MqttTopic* add_topic(const char *sztopic, uint16_t messageid=0) ;

  // Add a topic to the collection with a specific topic ID
  // returns NULL if the topic ID has already been allocated
  MqttTopic* create_topic(const char *sztopic, uint16_t topicid, bool predefined = false) ;
  
  // Client call to complete topic and update topicid. Returns NULL if not found
  // Returns the completed topic
  MqttTopic* complete_topic(uint16_t messageid, uint16_t topicid) ;
  bool del_topic(uint16_t id) ;
  void del_topic(MqttTopic *t);
  bool del_topic_by_messageid(uint16_t messageid) ;
  void free_topics() ; // delete all topics and free mem
  void iterate_first_topic() ;
  MqttTopic* get_next_topic() ;
  MqttTopic* get_curr_topic() ;
  MqttTopic* get_topic(uint16_t topicid) ;
  MqttTopic* get_topic(const char *sztopic);
  
protected:
  MqttTopic *topics ;
  MqttTopic *m_topic_iterator ;
  
};


#endif
