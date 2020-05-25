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

#include "mqttconnection.hpp"
#include <stdio.h>

void MqttMessage::reset()
{
  m_active = false ;
  m_message_cache_typeid = 0;
  m_message_cache_len = 0;
  m_messageid = 0;
  m_external_message = false ;
  m_topicid = 0;
  m_topictype = 0;
  m_mosmid = 0;
  m_state = Activity::none ;
  m_lasttry = 0 ;
  m_attempts = 0 ;
  m_sent = false ;
  m_oneshot = false ;
  m_message_set = false ;
}
void MqttMessage::sending()
{
  // A one shot message will not attempt a retry
  if (m_oneshot){
    m_active =false ;
  }else{
    m_sent = true;
    m_lasttry=TIMENOW;
    m_attempts=1;
    // Set DUP flag for repeat messages
    if (m_attempts == 1 &&
	(m_message_cache_typeid == MQTT_SUBSCRIBE ||
	 m_message_cache_typeid == MQTT_PUBLISH)){
      if (m_message_cache_len > 0) m_message_cache[0] |= FLAG_DUP ;
    }
  }
}

void MqttMessage::set_message(uint8_t messagetypeid, const uint8_t *message, uint8_t len)
{
  m_message_set = true ;
  m_message_cache_typeid = messagetypeid ;
  if(message){
    memcpy(m_message_cache, message, len) ;
  }else{
    len = 0;
  }
  m_message_cache_len = len ;
}

bool MqttMessage::has_expired(time_t timeout)
{
  if((timeout+m_lasttry) < TIMENOW){
    m_attempts++;
    m_lasttry = TIMENOW;
    return true;
  }
  
  return false ;
}


MqttMessageCollection::MqttMessageCollection()
{
  m_lastmessageid = 0;
  m_queuehead = 0;
  m_queuetail = 0;
}

uint16_t MqttMessageCollection::get_new_messageid()
{
  if(m_lastmessageid == 0xFFFF)
    m_lastmessageid=0;
  m_lastmessageid++ ;
  return m_lastmessageid;
}

MqttMessage* MqttMessageCollection::add_message(MqttMessage::Activity state)
{
  uint16_t messpos = m_queuetail ;
  do{
    if (!m_messages[messpos].is_active()){
      // This message is inactive
      m_messages[messpos].reset() ;
      m_messages[messpos].set_active() ;
      m_messages[messpos].set_message_id(get_new_messageid()) ;
      m_messages[messpos].set_activity(state);
      m_queuetail = messpos ;
      return &(m_messages[messpos]);
    }
    messpos++ ;
    if (messpos == MQTT_MESSAGES_INFLIGHT) messpos = 0;
  }while(messpos != m_queuetail);

  return NULL ;
}

MqttMessage* MqttMessageCollection::get_message(uint16_t messageid, bool externalid)
{
  // Search for the message ID. This will also return inactive messages that match
  for(int i=0; i < MQTT_MESSAGES_INFLIGHT; i++){
    if (m_messages[i].get_message_id() == messageid && m_messages[i].is_external() == externalid){
      return &(m_messages[i]) ;
    }
  }
  return NULL ; // No match
}

MqttMessage* MqttMessageCollection::get_mos_message(int messageid)
{
  // Search for the message ID. This will also return inactive messages that match
  for(int i=0; i < MQTT_MESSAGES_INFLIGHT; i++){
    if (m_messages[i].get_mosquitto_mid() == messageid){
      return &(m_messages[i]) ;
    }
  }
  return NULL ; // No match
  
}

MqttMessage* MqttMessageCollection::get_active_message()
{
  uint16_t mpos = m_queuehead ;
  do{
    if (m_messages[mpos].is_active()){
      m_queuehead = mpos ; // update head
      return &(m_messages[mpos]) ;
    }
    mpos++ ;
    if (mpos == MQTT_MESSAGES_INFLIGHT) mpos = 0;
  }while (mpos != m_queuehead) ;
  m_queuehead = 0;
  m_queuetail = 0 ;
  return NULL ; // No active messages
}

void MqttMessageCollection::clear_queue()
{
  for(int i=0; i < MQTT_MESSAGES_INFLIGHT; i++){
    m_messages[i].reset();
  }
  m_queuehead = 0;
  m_queuetail = 0;
}



MqttConnection::MqttConnection(){
  next = NULL ;
  prev = NULL ;
  duration = 0 ; // keep alive timer
  m_gwid = 0 ;
  m_lastactivity = 0 ;
  sleep_duration = 0 ;
  asleep_from = 0 ;
  m_last_ping =0;
  m_address_len = 0 ;
  m_state = State::disconnected ;
  m_szclientid[0] = '\0' ;
  m_willtopicretain = false ;
  m_willtopicqos = 0 ;
  m_willtopicsize = 0 ;
  m_willmessagesize = 0 ;
  m_willtopic[0] = '\0' ;
  m_sendtopics = false ;
}

void MqttConnection::update_activity()
{
  // received activity from client or server
  m_lastactivity = TIMENOW ;
  reset_ping() ;
}

bool MqttConnection::send_another_ping()
{
  return ((m_last_ping + (duration)) < TIMENOW) ;
}

bool MqttConnection::lost_contact()
{
  // Give 5 retries before failing. This mutliplies the time assuming that
  // all pings will be sent timely
  return ((m_lastactivity + (duration * 5)) < TIMENOW) ;
}

bool MqttConnection::address_match(const uint8_t *addr)
{
  for (uint8_t a=0; a < m_address_len; a++){
    if (m_connect_address[a] != addr[a]) return false ;
  }
  return true ;
}

void MqttConnection::set_address(const uint8_t *addr, uint8_t len)
{
  memcpy(m_connect_address, addr, len) ;
  m_address_len = len ;
}


bool MqttConnection::set_will_topic(const char *topic, uint8_t qos, bool retain)
{
  size_t len = 0;
  if (!topic || (len=strlen(topic)) == 0){
    m_willtopic[0] = '\0' ;
    m_willtopicsize = 0 ;
    m_willmessage[0] = '\0';
    m_willmessagesize = 0;
  }else{
    if (len > PACKET_DRIVER_MAX_PAYLOAD - MQTT_WILLTOPIC_HDR_LEN) return false ;
    
    memcpy(m_willtopic, topic, len) ;
    m_willtopic[len] = '\0' ;
    m_willtopicsize = len ;
  }
  m_willtopicqos = qos ;
  m_willtopicretain = retain ;
  return true ;
}

bool MqttConnection::set_will_message(const uint8_t *message, uint8_t len)
{
  if (!message || len == 0){
    m_willmessagesize = 0;
  }else{
    if (len > PACKET_DRIVER_MAX_PAYLOAD - MQTT_WILLMSG_HDR_LEN)
      return false ;
  
    memcpy(m_willmessage, message, len) ;
    m_willmessagesize = len ;
  }
  return true ;  
}

bool MqttConnection::get_will_retain()
{
  return m_willtopicretain;
}

char *MqttConnection::get_will_topic()
{
  return m_willtopic ;
}

size_t MqttConnection::get_will_message_len()
{
  return m_willmessagesize ;
}

uint8_t *MqttConnection::get_will_message()
{
  return m_willmessage ;
}

uint8_t MqttConnection::get_will_qos()
{
  return m_willtopicqos ;
}







 
