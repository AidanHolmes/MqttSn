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

MqttConnection::MqttConnection(){
  next = NULL ;
  prev = NULL ;
  duration = 0 ; // keep alive timer
  m_gwid = 0 ;
  m_lastactivity = 0 ;
  sleep_duration = 0 ;
  asleep_from = 0 ;
  m_last_ping =0;
  m_messageid = 0;
  m_address_len = 0 ;
  m_state = State::disconnected ;
  m_activity = Activity::none ;
  m_szclientid[0] = '\0' ;
  m_attempts = 0;
  m_lasttry = 0 ;
  m_message_cache_len = 0 ;
  m_message_cache_id = 0 ;
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

bool MqttConnection::state_timeout(uint16_t timeout){
  if((timeout+m_lasttry) < TIMENOW){m_attempts++; m_lasttry = TIMENOW; return true;}
  else return false ;
}

uint16_t MqttConnection::get_new_messageid()
{
  if(m_messageid == 0xFFFF)
    m_messageid=0;
  return ++m_messageid;
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

void MqttConnection::set_cache(uint8_t messageid, const uint8_t *message, uint8_t len){
  memcpy(m_message_cache, message, len) ;
  m_message_cache_len = len ;
  m_message_cache_id = messageid ;
}

void MqttConnection::set_reg_entities(uint16_t messageid)
{
  m_tmpmessageid = messageid ;
}


void MqttConnection::set_pub_entities(uint16_t topicid,
				      uint16_t messageid,
				      uint8_t topictype,
				      int qos, int len,
				      const uint8_t *payload, bool retain)
{
  m_tmptopicid = topicid ;
  m_tmptopictype = topictype ;
  m_tmpmessageid = messageid ;
  m_tmpqos = qos ;
  m_tmpmessagelen = len ;
  memcpy(m_tmppubmessage, payload, len) ;
  m_tmpretain = retain ;
}

void MqttConnection::set_sub_entities(uint16_t topicid,
				      uint8_t topictype,
				      uint16_t messageid,
				      int qos)
{
  m_tmptopicid = topicid;
  m_tmptopictype = topictype ;
  m_tmpmessageid = messageid ;
  m_tmpqos = qos ;
}


uint8_t MqttConnection::get_pubsub_topic_type()
{
  return m_tmptopictype ;
}

uint16_t MqttConnection::get_pubsub_topicid()
{
  return m_tmptopicid ;
}

uint16_t MqttConnection::get_pubsub_messageid()
{
  return m_tmpmessageid ;
}

int MqttConnection::get_pubsub_qos()
{
  return m_tmpqos ;
}

int MqttConnection::get_pub_payload_len()
{
  return m_tmpmessagelen ;
}

const uint8_t* MqttConnection::get_pub_payload()
{
  return m_tmppubmessage ;
}

bool MqttConnection::get_pub_retain()
{
  return m_tmpretain ;
}

void MqttConnection::set_mosquitto_mid(int mid)
{
  m_tmpmosmid = mid ;
}

int MqttConnection::get_mosquitto_mid()
{
  return m_tmpmosmid ;
}

bool MqttConnection::set_will_topic(char *topic, uint8_t qos, bool retain)
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

bool MqttConnection::set_will_message(uint8_t *message, uint8_t len)
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







 
