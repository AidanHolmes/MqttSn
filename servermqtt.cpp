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

#include "servermqtt.hpp"
#include "radioutil.hpp"
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#include <locale.h>

ServerMqttSn::ServerMqttSn()
{
  pthread_mutexattr_t attr ;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) ;
  pthread_mutex_init(&m_mosquittolock, &attr) ;

  m_gwid = 0 ;

  m_connection_head = NULL ;

  m_last_advertised = 0 ;
  m_advertise_interval = 1500 ;
  m_pmosquitto = NULL ;

  m_mosquitto_initialised = false ;
  m_broker_connected = false ;
}

ServerMqttSn::~ServerMqttSn()
{
  if (m_mosquitto_initialised){
    mosquitto_lib_cleanup() ;
  }
  pthread_mutex_destroy(&m_mosquittolock) ;
}

void ServerMqttSn::set_advertise_interval(uint16_t t)
{
  m_advertise_interval = t ;
}

void ServerMqttSn::received_publish(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len < 6) return ; // not long enough to be a publish
  uint16_t topicid = (data[1] << 8) | data[2] ; // Assuming MSB is first
  uint16_t messageid = (data[3] << 8) | data[4] ; // Assuming MSB is first
  uint8_t qos = data[0] & FLAG_QOSN1 ;
  uint8_t topic_type = data[0] & (FLAG_DEFINED_TOPIC_ID | FLAG_SHORT_TOPIC_NAME);
  uint8_t payload[PACKET_DRIVER_MAX_PAYLOAD] ;
  int payload_len = len-5 ;
  int ret = 0;
  memcpy(payload, data+5, payload_len) ;

  uint8_t buff[5] ; // Response buffer
  buff[0] = data[1] ; // replicate topic id 
  buff[1] = data[2] ; // replicate topic id 
  buff[2] = data[3] ; // replicate message id
  buff[3] = data[4] ; // replicate message id

  int mid = 0 ;
  
  DPRINT("PUBLISH: {Flags = %X, QoS = %d, Topic ID = %u, Mess ID = %u}\n",
	 data[0], qos, topicid, messageid) ;

  if (!m_mosquitto_initialised){
    EPRINT("PUBLISH: Gateway is not connected to the Mosquitto server to process Publish\n") ;
    buff[4] = MQTT_RETURN_CONGESTION;
    if(addrwritemqtt(sender_address, MQTT_PUBACK, buff, 5)){
      DPRINT("PUBLISH: Sending MQTT_PUBACK to client for message ID = %u\n",
	     messageid) ;
    }else{
      EPRINT("PUBLISH: Failed to send MQTT_PUBACK to client for message ID = %u\n",
	     messageid) ;
    }
    return ;
  }
  
  if (qos == FLAG_QOSN1){
    // Cannot process normal topic IDs
    // An error shouldn't really be sent for -1 QoS messages, but
    // it cannot hurt as the client is likely to just ignore
    if (topic_type == FLAG_NORMAL_TOPIC_ID){
      EPRINT("PUBLISH: Client sent normal topic ID %u for -1 QoS message\n", topicid);
      buff[4] = MQTT_RETURN_INVALID_TOPIC ;
      if (addrwritemqtt(sender_address, MQTT_PUBACK, buff, 5)){
	DPRINT("PUBLISH: Sending MQTT_PUBACK to client for message ID = %u\n",
	       messageid) ;
      }else{
	EPRINT("PUBLISH: Failed to send MQTT_PUBACK to client for message ID = %u\n",
	       messageid) ;
      }
      return ;
    }
    // Just publish and forget for QoS -1
    if (topic_type == FLAG_SHORT_TOPIC_NAME){
      char szshort[3] ;
      szshort[0] = (char)(buff[0]);
      szshort[1] = (char)(buff[1]) ;
      szshort[2] = '\0';
      // Publish with QoS 1 to server
      ret = mosquitto_publish(m_pmosquitto,
			      &mid,
			      szshort,
			      payload_len,
			      payload, 1,
			      data[0] & FLAG_RETAIN) ;
    }else if(topic_type == FLAG_DEFINED_TOPIC_ID){
      MqttTopic *t = m_predefined_topics.get_topic(topicid);
      if (!t){
	EPRINT("PUBLISH: Cannot find topic %u in the pre-defined list\n", topicid) ;
	buff[4] = MQTT_RETURN_INVALID_TOPIC ;
	if (addrwritemqtt(sender_address, MQTT_PUBACK, buff, 5)){
	  DPRINT("PUBLISH: Sending MQTT_PUBACK to client for message ID = %u\n",
		 messageid) ;
	}else{
	  EPRINT("PUBLISH: Failed to send MQTT_PUBACK to client for message ID = %u\n",
		 messageid) ;
	}
	return ;
      }
      // Publish with QoS 1 to server
      ret = mosquitto_publish(m_pmosquitto,
			      &mid,
			      t->get_topic(),
			      payload_len,
			      payload, 1,
			      data[0] & FLAG_RETAIN) ;	
    }
    if (ret != MOSQ_ERR_SUCCESS)
      EPRINT("PUBLISH: Mosquitto QoS -1 publish failed with code %d\n", ret) ;
    return ;    
  }

  pthread_mutex_lock(&m_mosquittolock) ;
  // Not a QoS -1 message, search for connection
  MqttConnection *con = search_connection_address(sender_address);
  if (!con){
    EPRINT("PUBLISH: No registered connection for client\n") ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return;
  }
  
  if (!server_publish(con, messageid, topicid, topic_type, payload, payload_len, qos, data[0] & FLAG_RETAIN)){
    EPRINT("PUBLISH: server_publish failed\n") ;
  }
  
  pthread_mutex_unlock(&m_mosquittolock) ;
}

bool ServerMqttSn::server_publish(MqttConnection *con, uint16_t messageid, uint16_t topicid,
				  uint8_t topic_type, uint8_t *payload, uint8_t len,
				  uint8_t qos, bool retain)
{
  int mid = 0, ret = 0 ;
  uint8_t buff[5] ; // Response buffer
  if (!con){
    EPRINT("PUBLISH: No registered connection for client\n") ;
    return false;
  }

  pthread_mutex_lock(&m_mosquittolock) ;
  buff[0] = topicid >> 8 ; // replicate topic id 
  buff[1] = (topicid & 0x00FF) ; // replicate topic id 
  buff[2] = messageid >> 8 ; // replicate message id
  buff[3] = (messageid & 0x00FF) ; // replicate message id

  const char *ptopic = NULL ;
  char szshort[3] ;
  MqttTopic *topic = NULL ;
  switch(topic_type){
  case FLAG_SHORT_TOPIC_NAME:
    szshort[0] = (char)(buff[0]);
    szshort[1] = (char)(buff[1]) ;
    szshort[2] = '\0';
    ptopic = szshort ;
    break;
  case FLAG_DEFINED_TOPIC_ID:
    topic = m_predefined_topics.get_topic(topicid);
    if (!topic){
      EPRINT("PUBLISH: Predefined topic id %u unrecognised\n", topicid);
      buff[4] = MQTT_RETURN_INVALID_TOPIC ;
      if (writemqtt(con, MQTT_PUBACK, buff, 5)){
	DPRINT("PUBLISH: Sending MQTT_PUBACK to client %s for message ID = %u\n",
	       con->get_client_id(),
	       messageid) ;
      }else{
	EPRINT("PUBLISH: Failed to send MQTT_PUBACK to client %s for message ID = %u\n",
	       con->get_client_id(),
	       messageid) ;
      }
      pthread_mutex_unlock(&m_mosquittolock) ;
      return false;
    }
    
    ptopic = topic->get_topic() ;
    break;
  case FLAG_NORMAL_TOPIC_ID:
    topic = con->topics.get_topic(topicid) ;
    if (!topic || topicid == 0){
      EPRINT("PUBLISH: Client topic id %d unknown to gateway\n", topicid) ;
      buff[4] = MQTT_RETURN_INVALID_TOPIC ;
      if (writemqtt(con, MQTT_PUBACK, buff, 5)){
	DPRINT("PUBLISH: Sending MQTT_PUBACK to client %s for message ID = %u\n",
	       con->get_client_id(), messageid) ;
      }else{
	EPRINT("PUBLISH: Failed to send MQTT_PUBACK to client %s for message ID = %u\n",
	       con->get_client_id(), messageid) ;
      }
      pthread_mutex_unlock(&m_mosquittolock) ;
      return false;
    }
    
    ptopic = topic->get_topic() ;
    break;
  default:
    pthread_mutex_unlock(&m_mosquittolock) ;
    EPRINT("PUBLISH: Unknown topic type\n") ;
    return false ;
  }

  MqttMessage *m = con->messages.add_message(MqttMessage::Activity::publishing);
  if (!m){ // cannot allocate a message, server is out of space
    EPRINT("PUBLISH: Cannot create new message, returning congestion error\n") ;
    buff[4] = MQTT_RETURN_CONGESTION ;
    if (writemqtt(con, MQTT_PUBACK, buff, 5)){
      DPRINT("PUBLISH: Sending MQTT_PUBACK to client %s for message ID = %u\n",
	     con->get_client_id(), messageid) ;
    }else{
      EPRINT("PUBLISH: Failed to send MQTT_PUBACK to client %s for message ID = %u\n",
	     con->get_client_id(), messageid) ;
    }
    pthread_mutex_unlock(&m_mosquittolock) ;
    return false ;
  }

  // Lock the publish and recording of MID 
  ret = mosquitto_publish(m_pmosquitto,
			  &mid,
			  ptopic,
			  len,
			  payload,
			  1,
			  retain) ;
  if (ret != MOSQ_ERR_SUCCESS){
    m->set_inactive() ;
    EPRINT("PUBLISH: Mosquitto failed %d, params - Topic: %s, len %u, retain %s\n", ret, ptopic, len, retain?"yes":"no");
    buff[4] = MQTT_RETURN_CONGESTION ;
    if (writemqtt(con, MQTT_PUBACK, buff, 5)){
      DPRINT("PUBLISH: Sending MQTT_PUBACK to client %s for message ID = %u\n",
	     con->get_client_id(), messageid) ;
    }else{
      EPRINT("PUBLISH: Failed to send MQTT_PUBACK to client %s for message ID = %u\n",
	     con->get_client_id(), messageid) ;
    }
    pthread_mutex_unlock(&m_mosquittolock) ;
    return false ;
  }
  m->set_mosquitto_mid(mid) ;
  m->set_qos(qos) ;
  m->set_topic_id(topicid) ;
  m->set_message_id(messageid, true) ;
  m->set_topic_type(topic_type) ;
  pthread_mutex_unlock(&m_mosquittolock) ;
  
  return true ;
}

void ServerMqttSn::received_pubrel(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len != 2) return ; // Invalid PUBREL message length
  uint16_t messageid = (data[0] << 8) | data[1] ; // Assuming MSB is first
  DPRINT("PUBREL {messageid = %u}\n", messageid) ;

  pthread_mutex_lock(&m_mosquittolock) ;
  MqttConnection *con = search_connection_address(sender_address);
  if (!con){
    EPRINT("PUBREL: No registered connection for client\n") ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return;
  }
  MqttMessage *m = con->messages.get_message(messageid,true) ;
  if (!m){
    EPRINT("PUBREL: received unknown message ID %u\n", messageid) ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }

  m->set_inactive() ; // Complete the message
  
  con->update_activity() ;
  if (writemqtt(con, MQTT_PUBCOMP, data, 2)){
    DPRINT("PUBREL: Sending MQTT_PUBCOMP to client %s for message ID %u\n",
	   con->get_client_id(), messageid) ;
  }else{
    EPRINT("PUBREL: Failed to send MQTT_PUBCOMP to client %s for message ID %u\n",
	   con->get_client_id(), messageid) ;
  }
  pthread_mutex_unlock(&m_mosquittolock) ;  
}

void ServerMqttSn::received_puback(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  uint16_t topicid = (data[0] << 8) | data[1] ;
  uint16_t messageid = (data[2] << 8) | data[3] ;
  uint8_t returncode = data[4] ;

  DPRINT("PUBACK: {topicid = %u, messageid = %u, returncode = %u}\n", topicid, messageid, returncode) ;

  pthread_mutex_lock(&m_mosquittolock) ;
  MqttConnection *con = search_connection_address(sender_address);
  if (!con){
    EPRINT("PUBACK: No registered connection for client\n") ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }
  MqttMessage *m = con->messages.get_message(messageid) ;
  if (!m){
    EPRINT("PUBACK: received unknown message ID %u\n", messageid) ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }

  m->set_inactive() ;
  
  con->update_activity() ;
  if (returncode != MQTT_RETURN_ACCEPTED){
    EPRINT("PUBACK: {return error code = %u}\n", returncode) ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }

  if (m->get_topic_id() != topicid){
    // Not the publish we were waiting for?
    EPRINT("PUBACK: client confirmed completion of topic %u with message id %u, but topic %u with message id %u expected\n", m->get_topic_id(), m->get_message_id(), topicid, messageid) ;
    // Accept anyway, need to debug protocol
  }  
  pthread_mutex_unlock(&m_mosquittolock) ;
}

void ServerMqttSn::received_pubrec(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len != 2) return ; // wrong length

  pthread_mutex_lock(&m_mosquittolock) ;
  MqttConnection *con = search_connection_address(sender_address);
  if (!con){
    EPRINT("PUBREC: No registered connection for client\n") ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }
  
  uint16_t messageid = (data[0] << 8) | data[1] ; // Assuming MSB is first

  con->update_activity() ;
  MqttMessage *m = con->messages.get_message(messageid) ;
  if (!m){
    EPRINT("PUBREC: received unknown message ID %u\n", messageid) ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }
  
  DPRINT("PUBREC: {messageid = %u}\n", messageid) ;

  // Replace active message with new response
  m->reset_message() ; // clear old timers and sent status
  m->set_message(MQTT_PUBREL, data, 2) ;
  m->set_activity(MqttMessage::Activity::publishing) ;
  pthread_mutex_unlock(&m_mosquittolock) ;
}

void ServerMqttSn::received_pubcomp(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len != 2) return ; // wrong length

  pthread_mutex_lock(&m_mosquittolock) ;
  MqttConnection *con = search_connection_address(sender_address);
  if (!con){
    EPRINT("PUBCOMP: No registered connection for client\n") ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }
  uint16_t messageid = (data[0] << 8) | data[1] ; // Assuming MSB is first
  con->update_activity() ;
  MqttMessage *m = con->messages.get_message(messageid) ;
  if (!m){
    EPRINT("PUBCOMP: received unknown message ID %u\n", messageid) ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }

  DPRINT("PUBCOMP: {messageid = %u}\n", messageid) ;

  m->set_inactive() ;
  con->update_activity() ;
  pthread_mutex_unlock(&m_mosquittolock) ;
}

void ServerMqttSn::received_subscribe(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len < 3) return ;
  uint16_t messageid = (data[1] << 8) | data[2] ; // Assuming MSB is first
  uint8_t qos = data[0] & FLAG_QOSN1 ;
  uint8_t topic_type = data[0] & (FLAG_DEFINED_TOPIC_ID | FLAG_SHORT_TOPIC_NAME);
  uint8_t buff[PACKET_DRIVER_MAX_PAYLOAD] ;
  char sztopic[PACKET_DRIVER_MAX_PAYLOAD - MQTT_SUBSCRIBE_HDR_LEN + 1];
  int mid = 0, ret = 0;
  uint16_t topicid = 0;
  MqttTopic *t = NULL ;
  
  buff[0] = data[0] ;
  buff[1] = 0 ;
  buff[2] = 0;
  buff[3] = data[1] ; // Message ID
  buff[4] = data[2] ; // Message ID
  
  pthread_mutex_lock(&m_mosquittolock) ;
  MqttConnection *con = search_connection_address(sender_address);
  if (!con){
    EPRINT("SUBSCRIBE: No registered connection for client\n") ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }
  con->update_activity() ;

  if (!m_mosquitto_initialised){
    EPRINT("SUBSCRIBE: Gateway is not connected to the Mosquitto server to process Subscribe\n") ;
    buff[5] = MQTT_RETURN_CONGESTION;
    if (writemqtt(con, MQTT_SUBACK, buff, 5)){
      DPRINT("SUBSCRIBE: Sending MQTT_SUBACK to client %s for message ID %u\n",
	     con->get_client_id(), messageid) ;
    }else{
      EPRINT("SUBSCRIBE: Failed to send MQTT_SUBACK to client %s for message ID %u\n",
	     con->get_client_id(), messageid) ;
    }
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }

  switch(topic_type){
  case FLAG_NORMAL_TOPIC_ID:
  case FLAG_SHORT_TOPIC_NAME:
    // Topic is contained in remaining bytes of data
    memcpy(sztopic, data+3, len - 3);
    sztopic[len-3] = '\0' ;
    // Add topic if new, otherwise returns existing topic
    if (!(t=con->topics.get_topic(sztopic))){
      t = con->topics.add_topic(sztopic, messageid) ;
      if (!t){
	EPRINT("SUBSCRIBE: Something went wrong adding the topic for client subscription\n");
	pthread_mutex_unlock(&m_mosquittolock) ;
	return ;
      }
    }
    if (topic_type == FLAG_SHORT_TOPIC_NAME) t->set_short_topic(true);

    break;
  case FLAG_DEFINED_TOPIC_ID:
    topicid = (data[3] << 8) | data[4] ;
    t = m_predefined_topics.get_topic(topicid) ;
    if (!t){
      EPRINT("SUBSCRIBE: Topic %u unknown for predefined client subscription\n", topicid) ;
      // Invalid topic, send client SUBACK with error
      buff[5] = MQTT_RETURN_INVALID_TOPIC ;
      if (writemqtt(con, MQTT_SUBACK, buff, 6)){
	DPRINT("SUBSCRIBE: Sending MQTT_SUBACK to client %s for message ID %u\n",
	       con->get_client_id(), messageid) ;
      }else{
	EPRINT("SUBSCRIBE: Failed to send MQTT_SUBACK to client %s for message ID %u\n",
	       con->get_client_id(), messageid) ;
      }
      pthread_mutex_unlock(&m_mosquittolock) ;
      return ;
    }
    // Reference the predefined topic
    break;
  default:
    // Shouldn't be possible to reach here
    EPRINT("SUBSCRIBE: Unknown topic type %u from client\n", topic_type) ;
    buff[5] = MQTT_RETURN_INVALID_TOPIC ;
    if (writemqtt(con, MQTT_SUBACK, buff, 6)){
      DPRINT("SUBSCRIBE: Sending MQTT_SUBACK to client %s for message ID %u\n",
	     con->get_client_id(), messageid) ;
    }else{
      EPRINT("SUBSCRIBE: Failed to send MQTT_SUBACK to client %s for message ID %u\n",
	     con->get_client_id(), messageid) ;
    }
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }

  if(t->is_subscribed()){
    topicid = t->get_id();
    buff[1] = topicid >> 8 ;
    buff[2] = topicid & 0x00FF ;
    buff[5] = MQTT_RETURN_ACCEPTED;
    if (writemqtt(con, MQTT_SUBACK, buff, 6)){
      DPRINT("SUBSCRIBE: Sending MQTT_SUBACK to client %s for message ID %u\n",
	     con->get_client_id(), messageid) ;
    }else{
      EPRINT("SUBSCRIBE: Failed to send MQTT_SUBACK to client %s for message ID %u\n",
	     con->get_client_id(), messageid) ;
    }
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }

  t->set_qos(qos) ;
  t->set_subscribed(true) ;
  
  // Subscribe using QoS 1 to server.
  // TO DO - may need a config setting for all mosquitto calls 
  ret = mosquitto_subscribe(m_pmosquitto,
			    &mid,
			    t->get_topic(),
			    1);
  // SUBACK handled through mosquitto call-back
  if (ret != MOSQ_ERR_SUCCESS){
    EPRINT("SUBSCRIBE: Mosquitto subscribe failed with code %d\n",ret);
    t->set_subscribed(false) ; // remove subscription due to error
    if (ret == MOSQ_ERR_INVAL){
      buff[5] = MQTT_RETURN_INVALID_TOPIC;
    }else{
      buff[5] = MQTT_RETURN_CONGESTION;
    }
#ifdef DEBUG
    DPRINT("SUBSCRIBE: Error from mosquitto. Sending following SUBACK: [") ;
    for (int di=0; di < 6; di++)
      DPRINT("%02X ", buff[di]);
    DPRINT("]\n") ;
#endif
    if (writemqtt(con, MQTT_SUBACK, buff, 6)){
      DPRINT("SUBSCRIBE: Sending MQTT_SUBACK to client %s for message ID %u\n",
	     con->get_client_id(), messageid) ;
    }else{
      EPRINT("SUBSCRIBE: Failed to send MQTT_SUBACK to client %s for message ID %u\n",
	     con->get_client_id(), messageid) ;
    }
  }else{
    // Don't set a topic ID if topic is a wildcard
    //    con->set_sub_entities(t->is_wildcard()?0:t->get_id(), messageid, qos) ;
    MqttMessage *m = con->messages.add_message(MqttMessage::Activity::subscribing) ;
    if (!m){
      buff[5] = MQTT_RETURN_CONGESTION;
      if (writemqtt(con, MQTT_SUBACK, buff, 6)){
	DPRINT("SUBSCRIBE: Sending MQTT_SUBACK to client %s for message ID %u\n",
	       con->get_client_id(), messageid) ;
      }else{
	EPRINT("SUBSCRIBE: Failed to send MQTT_SUBACK to client %s for message ID %u\n",
	       con->get_client_id(), messageid) ;
      }
    }else{
      m->set_topic_id(t->get_id()) ;
      m->set_topic_type(topic_type) ;
      m->set_message_id(messageid,true) ;
      m->set_qos(qos) ;
      m->set_mosquitto_mid(mid) ;
      m->one_shot(true);
    }
  }
  pthread_mutex_unlock(&m_mosquittolock) ;
  return ;
}

void ServerMqttSn::received_suback(uint8_t *sender_address, uint8_t *data, uint8_t len)
{

}

void ServerMqttSn::received_unsubscribe(uint8_t *sender_address, uint8_t *data, uint8_t len)
{

}

void ServerMqttSn::received_unsuback(uint8_t *sender_address, uint8_t *data, uint8_t len)
{

}


void ServerMqttSn::received_register(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len < 4) return ;
  uint16_t topicid = (data[0] << 8) | data[1] ; // Assuming MSB is first
  uint16_t messageid = (data[2] << 8) | data[3] ; // Assuming MSB is first
  char sztopic[PACKET_DRIVER_MAX_PAYLOAD - MQTT_REGISTER_HDR_LEN +1] ;
  if (len - 4 > PACKET_DRIVER_MAX_PAYLOAD - MQTT_REGISTER_HDR_LEN) return ; // overflow
  memcpy(sztopic, data+4, len-4) ;
  sztopic[len-4] = '\0';

  DPRINT("REGISTER: {topicid: %u, messageid: %u, topic %s}\n", topicid, messageid, sztopic) ;
  pthread_mutex_lock(&m_mosquittolock) ;
  MqttConnection *con = search_connection_address(sender_address) ;
  if (!con){
    EPRINT("REGISTER: Cannot find a connection for the client\n") ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }
  con->update_activity() ;

  MqttTopic *t = con->topics.add_topic(sztopic, messageid) ;
  topicid = t->get_id();
  uint8_t response[5] ;
  response[0] = topicid >> 8 ; // Write topicid MSB first
  response[1] = topicid & 0x00FF ;
  response[2] = data[2] ; // Echo back the messageid received
  response[3] = data[3] ; // Echo back the messageid received
  response[4] = MQTT_RETURN_ACCEPTED ;
  if (writemqtt(con, MQTT_REGACK, response, 5)){
    DPRINT("REGISTER: Sending MQTT_REGACK to client %s for message ID %u\n",
	   con->get_client_id(), messageid) ;
  }else{
    EPRINT("REGISTER: Failed to send MQTT_REGACK to client %s for message ID %u\n",
	   con->get_client_id(), messageid) ;
  }
  pthread_mutex_unlock(&m_mosquittolock) ;
}

void ServerMqttSn::received_regack(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len != 5) return ;
  
  uint16_t topicid = (data[0] << 8) | data[1] ; // Assuming MSB is first
  uint16_t messageid = (data[2] << 8) | data[3] ; // Assuming MSB is first
  uint8_t returncode = data[4] ;

  DPRINT("REGACK: {topicid = %u, messageid = %u, returncode = %u}\n", topicid, messageid, returncode) ;

  pthread_mutex_lock(&m_mosquittolock) ;
  MqttConnection *con = search_connection_address(sender_address) ;
  if (!con){
    EPRINT("REGACK: No registered connection for client\n") ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }
  con->update_activity() ;
  MqttMessage *m = con->messages.get_message(messageid) ;
  if (!m){
    EPRINT("REGACK: received unknown message ID %u\n", messageid) ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }
  m->set_inactive() ; // Message complete

  // TO DO: Return code is ignored, do something sensible with it
  
  if (con->is_connected() && con->get_send_topics()){
    // Iterate to next topic to send
    MqttTopic *t = NULL ;
    for(t=con->topics.get_next_topic(); t && (t->is_wildcard() || t->is_short_topic()); t=con->topics.get_next_topic());
    if (t){
      if (!register_topic(con, t)){
	EPRINT("REGACK: Failed to register topic id %u, name %s\n",
	       t->get_id(), t->get_topic()) ;
      }
    }else{
      // Finished all topics
      // Stop the connection trying to send again
      con->set_send_topics(false);
    }
  }
  pthread_mutex_unlock(&m_mosquittolock) ;
}

void ServerMqttSn::received_pingresp(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  pthread_mutex_lock(&m_mosquittolock) ;
  MqttConnection *con = search_connection_address(sender_address) ;
  if (con){
    // just update the last activity timestamp
    con->update_activity() ;
  }
  
  pthread_mutex_unlock(&m_mosquittolock) ;
}

void ServerMqttSn::received_pingreq(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  // Only respond to connected clients
  pthread_mutex_lock(&m_mosquittolock) ;
  MqttConnection *con = search_connection_address(sender_address) ;
  if (con){
    con->update_activity() ;
    if (!writemqtt(con, MQTT_PINGRESP, NULL, 0)){
      EPRINT("PINGREG: Failed to send ping response (IO if not ACKs enabled)\n") ;
    }
  }
  
  pthread_mutex_unlock(&m_mosquittolock) ; 
}

void ServerMqttSn::received_searchgw(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  uint8_t buff[1] ;

  DPRINT("SEARCHGW: {radius = %u, broker connected %s}\n", data[0], m_broker_connected?"yes":"no") ;

  // Ignore radius value. This is a gw so respond with
  // a broadcast message back.
  if (m_broker_connected){
    buff[0] = m_gwid ;
    pthread_mutex_lock(&m_mosquittolock) ;
    if (!addrwritemqtt(sender_address, MQTT_GWINFO, buff, 1)){
      EPRINT("SEARCHGW: Failed to send GWINFO") ;
    }
    pthread_mutex_unlock(&m_mosquittolock) ;
  }

}

MqttConnection* ServerMqttSn::search_connection(const char *szclientid)
{
  MqttConnection *p = NULL ;
  for(p = m_connection_head; p != NULL; p=p->next){
    if (!p->is_disconnected()){
      if (p->client_id_match(szclientid)) break ;
    }
  }
  return p ;
}

MqttConnection* ServerMqttSn::search_mosquitto_id(int mid, MqttMessage **pm)
{
  MqttConnection *p = NULL ;
  MqttMessage *m = NULL ;
  for(p = m_connection_head; p != NULL; p=p->next){
    if (p->is_connected()){
      if ((m=p->messages.get_mos_message(mid))){
	if (pm) *pm = m ;
	break ;
      }
    }
  }
  return p ;
}

MqttConnection* ServerMqttSn::search_cached_connection(const char *szclientid)
{
  MqttConnection *p = NULL ;
  for(p = m_connection_head; p != NULL; p=p->next){
    if (p->client_id_match(szclientid)) break ;
  }
  return p ;
}

MqttConnection* ServerMqttSn::search_connection_address(const uint8_t *clientaddr)
{
  MqttConnection *p = NULL ;

  for(p = m_connection_head; p != NULL; p=p->next){
    if (!p->is_disconnected() && p->address_match(clientaddr)){
      return p ;
    }
  }
  return NULL ; // Cannot find
}

MqttConnection* ServerMqttSn::search_cached_connection_address(const uint8_t *clientaddr)
{
  MqttConnection *p = NULL ;

  for(p = m_connection_head; p != NULL; p=p->next){
    if (p->address_match(clientaddr)){
      return p ;
    }
  }
  return NULL ; // Cannot find
}

MqttConnection* ServerMqttSn::new_connection()
{
  MqttConnection *p = NULL, *prev = NULL ;

  p = new MqttConnection() ;

  // Set the head if first record
  if (m_connection_head == NULL) m_connection_head = p ;
  else{
    // Append connection to end of connection list
    for (prev = m_connection_head; prev->next; prev = prev->next){
      
    }
    p->prev = prev ;
    prev->next = p ;
  }

  return p ;
}

void ServerMqttSn::delete_connection(const char *szclientid)
{
  MqttConnection *p = NULL, *prev = NULL, *next = NULL ;

  // Search for all client id instances and remove
  while ((p = search_connection(szclientid))){
    prev = p->prev ;
    next = p->next ;
    delete p ;
    if (!prev) m_connection_head = NULL ; // this was the head
    else prev->next = next ; // Connect the head and tail records
  }
}

void ServerMqttSn::received_connect(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  char szClientID[PACKET_DRIVER_MAX_PAYLOAD - MQTT_CONNECT_HDR_LEN +1] ;
  if (len < 5 || len > (4 + (m_pDriver->get_payload_width() - MQTT_CONNECT_HDR_LEN))) return ; // invalid data length
  
  memcpy(szClientID, data+4, len - 4) ; // copy identifier
  szClientID[len-4] = '\0' ; // Create null terminated string

  DPRINT("CONNECT: {flags = %02X, protocol = %02X, duration = %u, client ID = %s}\n", data[0], data[1], (data[2] << 8) | data[3], szClientID) ;

  if (data[1] != MQTT_PROTOCOL){
    EPRINT("CONNECT: Invalid protocol ID in CONNECT from client %s\n", szClientID);
    return ;
  }

  pthread_mutex_lock(&m_mosquittolock) ;

  MqttConnection *con = search_cached_connection(szClientID) ;
  if (!con){
#if DEBUG
    char addrdbg[(PACKET_DRIVER_MAX_ADDRESS_LEN*2)+1];
    addr_to_straddr(sender_address, addrdbg, m_pDriver->get_address_len()) ;    
    DPRINT("CONNECT: Creating a new connection for %s at address %s\n", szClientID, addrdbg) ;
#endif
    con = new_connection() ;
  }
  if (!con){
    EPRINT("CONNECT: Cannot create a new connection record for client %s\n", szClientID) ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ; // something went wrong with the allocation
  }

  con->set_state(MqttConnection::State::connecting);
  con->sleep_duration = 0 ;
  con->asleep_from = 0 ;
  con->update_activity() ; // update activity from client
  con->set_client_id(szClientID) ;
  con->duration = (data[2] << 8) | data[3] ; // MSB assumed
  con->set_address(sender_address, m_pDriver->get_address_len()) ;
  con->set_send_topics(false) ;

  // Remove any historic messages
  con->messages.clear_queue() ;

  // If clean flag is set then remove all topics and will data
  if (((FLAG_CLEANSESSION & data[0]) > 0)){
    con->topics.free_topics() ;
    con->set_will_topic(NULL, 0, false);
    con->set_will_message(NULL, 0) ;
  }else{
    // resume old connection and send topics after connection setup
    con->set_send_topics(true) ;
  }
    
  // If WILL if flagged then set the flags for the message and topic
  bool will = ((FLAG_WILL & data[0]) > 0) ;

  if (will){
    // Start with will topic request
    MqttMessage *m = con->messages.add_message(MqttMessage::Activity::willtopic) ;
    if (!m){
      EPRINT("CONNET: Connection cannot create a new message for will topic\n") ;
      pthread_mutex_unlock(&m_mosquittolock) ;
      return ;
    }
    // Add message to allow retries
    m->set_message(MQTT_WILLTOPICREQ, NULL, 0);
  }else{
    // No need for WILL setup, just CONNACK
    uint8_t buff[1] ;
    buff[0] = MQTT_RETURN_ACCEPTED ;
    if (writemqtt(con, MQTT_CONNACK, buff, 1)){
      DPRINT("CONNECT: Sending MQTT_CONNACK to client %s\n", con->get_client_id());
    }else{
      EPRINT("CONNECT: Failed to send MQTT_CONNACK to client %s\n", con->get_client_id()) ;
    }
    con->set_state(MqttConnection::State::connected) ;
  }
  pthread_mutex_unlock(&m_mosquittolock) ;
}

void ServerMqttSn::complete_client_connection(MqttConnection *p)
{
  if (!p) return ;
  p->update_activity() ;
  p->set_state(MqttConnection::State::connected) ;
  p->topics.iterate_first_topic();
  MqttTopic *t = NULL ;
  
  for(t=p->topics.get_curr_topic(); t && (t->is_wildcard() || t->is_short_topic()); t=p->topics.get_next_topic());
  if (t){
    // Topics are set on the connection
    if (!register_topic(p, t)){
      EPRINT("Complete client connection: Failed to send client topic id %u, name %s\n",
	     t->get_id(), t->get_topic()) ;
    }
  }
}

void ServerMqttSn::received_willtopic(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  char utf8[PACKET_DRIVER_MAX_PAYLOAD - MQTT_WILLTOPIC_HDR_LEN+1] ;

  pthread_mutex_lock(&m_mosquittolock) ;
  MqttConnection *con = search_connection_address(sender_address) ;
  uint8_t buff[1] ;
  if (!con){
    EPRINT("WILLTOPIC: could not find the client connection\n") ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }
  
  con->update_activity() ; // update known client activity
  if (con->get_state() != MqttConnection::State::connecting){
    // WILLTOPIC is only used during connection setup. This is out of sequence
    EPRINT("WILLTOPIC: Out of sequence WILLTOPIC received\n") ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }
  // Get message as the active message in queu
  MqttMessage *m = con->messages.get_active_message() ;
  if (!m){
    EPRINT("WILLTOPIC: Cannot find active connection message\n") ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }

  if (len == 0){
    // Client indicated a will but didn't send one
    // Complete connection and leave will unset
    con->set_will_topic(NULL,0,false);
    EPRINT("WILLTOPIC: received zero len topic\n") ; 
    buff[0] = MQTT_RETURN_ACCEPTED ;
    con->set_state(MqttConnection::State::connected) ;
    if (writemqtt(con, MQTT_CONNACK, buff, 1)){
      DPRINT("WILLTOPIC: sending MQTT_CONNACK to client %s\n",
	     con->get_client_id()) ;
    }else{
      DPRINT("WILLTOPIC: failed to send MQTT_CONNACK to client %s\n",
	     con->get_client_id()) ;
    }
    m->set_inactive() ; // Complete message
  }else{
    memcpy(utf8, data+1, len-1) ;
    utf8[len-1] = '\0' ;
    
    uint8_t qos = 0;
    bool retain = (data[0] & FLAG_RETAIN) ;
    if (data[0] & FLAG_QOS1) qos = 1;
    else if (data[0] & FLAG_QOS2) qos =2 ;
    
    DPRINT("WILLTOPIC: {QOS = %u, Topic = %s, Retain = %s}\n", qos, utf8, retain?"Yes":"No") ;

    if (!con->set_will_topic(utf8, qos, retain)){
      EPRINT("WILLTOPIC: Failed to set will topic for connection!\n") ;
    }
    // Reuse active message
    m->reset_message() ;
    m->set_activity(MqttMessage::Activity::willmessage);
    m->set_message(MQTT_WILLMSGREQ, NULL, 0) ;
  }
  pthread_mutex_unlock(&m_mosquittolock) ;
}

void ServerMqttSn::received_willmsg(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  pthread_mutex_lock(&m_mosquittolock) ;
  MqttConnection *con = search_connection_address(sender_address) ;
  uint8_t buff[1] ;
  if (!con){
    EPRINT("WILLMSG: could not find the client connection\n") ;
    buff[0] = MQTT_RETURN_CONGESTION ; // There is no "who are you?" response so this will do
    if (writemqtt(con, MQTT_CONNACK, buff, 1)){
      DPRINT("WILLMSG: sending MQTT_CONNACK to client %s\n",
	     con->get_client_id()) ;
    }else{
      EPRINT("WILLMSG: failed to send MQTT_CONNACK to client %s\n",
	     con->get_client_id()) ;
    }
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }

  con->update_activity() ; // update known client activity
  if (con->get_state() != MqttConnection::State::connecting){
    // WILLMSG is only used during connection setup. This is out of sequence
    EPRINT("WILLMSG: Out of sequence WILLMSG received\n") ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }
  // Connection messages should be the only active messages
  MqttMessage *m = con->messages.get_active_message() ;
  if (!m){
    EPRINT("WILLMSG: Cannot find active connection message\n");
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }
  m->set_inactive() ; // Complete message 

  if (!con->set_will_message(data, len)){
    EPRINT("WILLMSG: Failed to set the will message for connection!\n") ;
  }

  // Client sent final will message
  buff[0] = MQTT_RETURN_ACCEPTED ;
  if (writemqtt(con, MQTT_CONNACK, buff, 1)){
    DPRINT("WILLMSG: Sending MQTT_CONNACK to client %s\n",
	   con->get_client_id());
  }else{
    EPRINT("WILLMSG: failed to send MQTT_CONNACK to client %s\n",
	   con->get_client_id());
  } 
  con->set_state(MqttConnection::State::connected) ;

  pthread_mutex_unlock(&m_mosquittolock) ;
}

void ServerMqttSn::received_disconnect(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  // Disconnect request from client
  pthread_mutex_lock(&m_mosquittolock) ;
  MqttConnection *con = search_connection_address(sender_address);
  if (!con){
    DPRINT("DISCONNECT: Disconnect received from unknown client. Ignoring\n") ;
    pthread_mutex_unlock(&m_mosquittolock) ;
    return ;
  }
  time_t time_now = time(NULL) ;
  if (len == 2){
    // Contains a duration
    con->sleep_duration = (data[0] << 8) | data[1] ; // MSB assumed
    DPRINT("DISCONNECT: {sleeping for %u sec}\n", con->sleep_duration) ;
    con->asleep_from = time_now ;
    con->set_state(MqttConnection::State::asleep) ;
  }else{
    DPRINT("DISCONNECT: {}\n") ;
    con->set_state(MqttConnection::State::disconnected) ;
  }
  con->update_activity() ;

  if (writemqtt(con, MQTT_DISCONNECT, NULL, 0)){
    DPRINT("DISCONNECT: sending MQTT_DISCONNECT to client %s\n",
	   con->get_client_id()) ;
  }else{
    EPRINT("DISCONNECT: failed to send MQTT_DISCONNECT to client %s\n",
	   con->get_client_id()) ;
  }
  pthread_mutex_unlock(&m_mosquittolock) ;
}

void ServerMqttSn::initialise(uint8_t address_len, uint8_t *broadcast, uint8_t *address)
{
  MqttSnEmbed::initialise(address_len, broadcast, address);

  pthread_mutexattr_t attr ;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) ;
  if (pthread_mutex_init(&m_mosquittolock, &attr) != 0){
    EPRINT("Init: Mosquitto mutex creation failed\n") ;
    return ;
  }
  
  char szgw[PACKET_DRIVER_MAX_PAYLOAD - MQTT_CONNECT_HDR_LEN+1];
  if (!m_mosquitto_initialised) mosquitto_lib_init();
  m_mosquitto_initialised = true ;

  snprintf(szgw, m_pDriver->get_payload_width() - MQTT_CONNECT_HDR_LEN, "Gateway %u", m_gwid) ;
  m_pmosquitto = mosquitto_new(szgw, false, this) ;
  if (!m_pmosquitto){
    EPRINT("Init: Cannot create a new mosquitto instance\n") ;
  }else{

    // TO DO: add in config for the mosquitto server
    int ret = mosquitto_connect_async(m_pmosquitto, "localhost", 1883, 60) ;
    if (ret != MOSQ_ERR_SUCCESS){
      EPRINT("Init: Cannot connect to mosquitto broker\n") ;
    }

    ret = mosquitto_loop_start(m_pmosquitto) ;
    if (ret != MOSQ_ERR_SUCCESS){
      EPRINT("Init: Cannot start mosquitto loop\n") ;
    }

#ifdef DEBUG
    int major, minor, revision ;
    mosquitto_lib_version(&major, &minor, &revision) ;
    
    DPRINT("Init: Mosquitto server connected %d.%d.%d\n", major, minor, revision) ;
    m_broker_connected = true ; // GDB hack
#endif
  
    mosquitto_message_callback_set(m_pmosquitto, gateway_message_callback) ;
    mosquitto_connect_callback_set(m_pmosquitto, gateway_connect_callback) ;
    mosquitto_disconnect_callback_set(m_pmosquitto, gateway_disconnect_callback);
    mosquitto_publish_callback_set(m_pmosquitto, gateway_publish_callback) ;
    mosquitto_subscribe_callback_set(m_pmosquitto, gateway_subscribe_callback) ;
  }
}

void ServerMqttSn::gateway_message_callback(struct mosquitto *m,
					    void *data,
					    const struct mosquitto_message *message)
{
  DPRINT("MESSAGE CALLBACK: Received mosquitto message for topic %s at qos %d, with retain %s\n", message->topic, message->qos, message->retain?"true":"false") ;

  /* mosquitto message struct
  int mid;
  char *topic;
  void *payload;
  int payloadlen;
  int qos;
  bool retain;
  */

  if (data == NULL) return ;
  ServerMqttSn *gateway = (ServerMqttSn*)data ;

  MqttTopic *t = NULL;
  MqttConnection *p = NULL ;

  if (message->payloadlen > (gateway->m_pDriver->get_payload_width() - MQTT_PUBLISH_HDR_LEN)){
    EPRINT("MESSAGE CALLBACK: Payload of %u bytes is too long for publish\n", message->payloadlen);
    return ;
  }

  bool bfound = false ;
  gateway->lock_mosquitto() ; // Using topic iterators, lock section
  for(p = gateway->m_connection_head; p != NULL; p=p->next){
    if (p->is_connected()){
      bfound = false ;
      p->topics.iterate_first_topic();
      t=p->topics.get_curr_topic();
      // Iterate the registered topics
      while (t){
	if (t->is_subscribed() && t->match(message->topic)){
	  gateway->do_publish_topic(p, t, message->topic, t->is_short_topic()?FLAG_SHORT_TOPIC_NAME:FLAG_NORMAL_TOPIC_ID, message->payload, message->payloadlen, message->retain) ;
	  bfound = true ;
	  break ; //found a match, continue to next connection
	}
	t=p->topics.get_next_topic();
      }
      if (!bfound){
	gateway->m_predefined_topics.iterate_first_topic() ;
	t=gateway->m_predefined_topics.get_curr_topic();
	// Iterate the predefined topics
	while (t){
	  if (t->is_subscribed() && t->match(message->topic)){
	    gateway->do_publish_topic(p, t, message->topic, t->is_short_topic()?FLAG_SHORT_TOPIC_NAME:FLAG_DEFINED_TOPIC_ID, message->payload, message->payloadlen, message->retain) ;
	    break ; //found a match, continue to next connection
	  }
	  t=p->topics.get_next_topic();
	}
      }
    }
  }
  gateway->unlock_mosquitto() ;
}

void ServerMqttSn::do_publish_topic(MqttConnection *con,
				    MqttTopic *t,
				    const char *sztopic,
				    uint8_t topic_type,
				    void *payload,
				    uint8_t payloadlen,
				    bool retain)
{
  uint8_t buff[PACKET_DRIVER_MAX_PAYLOAD] ;
  uint8_t qos = t->get_qos() ;
  if (t->is_wildcard()){
    // Create new topic ID or use existing
    if (!(t = con->topics.get_topic(sztopic))){
      // doesn't exist as registered topic. Create new topic and register
      if (!(t = con->topics.add_topic(sztopic))) return;
      t->set_qos(qos) ;
      
      // Queue a registration topic
      register_topic(con, t); 
    }
  }

  MqttMessage *m = con->messages.add_message(MqttMessage::Activity::publishing) ;
  if (!m){
    EPRINT("MESSAGE CALLBACK: Cannot allocate a new message for publish of topic %s\n", sztopic) ;
    return ;
  }
  
  buff[0] = (retain?FLAG_RETAIN:0) | qos | topic_type;
  uint16_t topicid = t->get_id() ;
  if (topic_type == FLAG_SHORT_TOPIC_NAME){
    const char *szshort = t->get_topic() ;
    buff[1] = szshort[0] ;
    buff[2] = szshort[1] ;
  }else{
    buff[1] = topicid >> 8;
    buff[2] = topicid & 0x00FF ;
  }
  uint16_t mid = m->get_message_id() ;
  buff[3] = mid >> 8 ;
  buff[4] = mid & 0x00FF ;
  uint8_t len = payloadlen + 5;

  memcpy(buff+5, payload, payloadlen) ;

  m->set_message(MQTT_PUBLISH, buff, len) ;
  if (qos != FLAG_QOS0){
    m->set_topic_id(topicid) ;
    m->set_topic_type(FLAG_NORMAL_TOPIC_ID) ;
    m->set_qos(qos) ;
  }else{
    // QoS zero doesn't require an ACK and is a one shot message
    m->one_shot(true) ;
  }
}

void ServerMqttSn::gateway_subscribe_callback(struct mosquitto *m,
					      void *data,
					      int mid,
					      int qoscount,
					      const int *grantedqos)
{
  if (data == NULL) return ;
  ServerMqttSn *gateway = (ServerMqttSn*)data ;

  gateway->lock_mosquitto() ;
  MqttMessage *mess = NULL ;
  MqttConnection *con = gateway->search_mosquitto_id(mid, &mess) ;

  if (!con){
    EPRINT("SUBSCRIBE CALLBACK: Cannot find Mosquitto ID %d in any connection for subscription\n", mid) ;
    gateway->unlock_mosquitto();
    return ;
  }

  uint8_t buff[6] ;
  uint16_t topicid = mess->get_topic_id() ;
  uint16_t messageid = mess->get_message_id() ;
  buff[0] = mess->get_qos();
  buff[1] = topicid >> 8 ;
  buff[2] = topicid & 0x00FF ;
  buff[3] = messageid >> 8 ;
  buff[4] = messageid & 0x00FF ;
  buff[5] = MQTT_RETURN_ACCEPTED ;
  mess->set_message(MQTT_SUBACK, buff, 6) ;

  gateway->unlock_mosquitto();
}

void ServerMqttSn::gateway_publish_callback(struct mosquitto *m,
						  void *data,
						  int mid)
{
  if (data == NULL) return ;
  
  ServerMqttSn *gateway = (ServerMqttSn*)data ;
  // Lock until publish has completed and the mid logged
  gateway->lock_mosquitto() ;
  MqttMessage *mess = NULL ;
  MqttConnection *con = gateway->search_mosquitto_id(mid, &mess) ;

  if (!con){
    EPRINT("PUBLISH CALLBACK: Cannot find Mosquitto ID %d in any connection. Could be a QoS -1 message\n", mid) ;
    gateway->unlock_mosquitto();
    return ;
  }

  uint8_t buff[5] ; // Response buffer
  uint16_t topicid = mess->get_topic_id() ;
  uint16_t messageid = mess->get_message_id() ;
  buff[0] = topicid >> 8 ; // replicate topic id 
  buff[1] = topicid & 0x00FF ; // replicate topic id 
  buff[2] = messageid >> 8 ; // replicate message id
  buff[3] = messageid & 0x00FF ; // replicate message id

  switch(mess->get_qos()){
  case FLAG_QOS0:
    mess->set_inactive() ;
    break;
  case FLAG_QOS1:
    mess->set_inactive() ;
    buff[4] = MQTT_RETURN_ACCEPTED ;
    if (gateway->writemqtt(con, MQTT_PUBACK, buff, 5)){
      DPRINT("PUBLISH CALLBACK: Sending MQTT_PUBACK to client %s, for message ID %u\n", con->get_client_id(), messageid) ;
    }else{
      EPRINT("PUBLISH CALLBACK: Failed to send MQTT_PUBACK to client %s, for message ID %u\n", con->get_client_id(), messageid) ;
    }
    break ;
  case FLAG_QOS2:
    mess->set_message(MQTT_PUBREC, buff+2, 2) ;
    break ;
  default:
    mess->set_inactive() ;
    EPRINT("PUBLISH CALLBACK: Invalid QoS %d\n", mess->get_qos()) ;
  }
  gateway->unlock_mosquitto();
}

void ServerMqttSn::gateway_disconnect_callback(struct mosquitto *m,
						    void *data,
						    int res)
{
  ServerMqttSn *gateway = (ServerMqttSn*)data ;
  DPRINT("DISCONNECT CALLBACK: Mosquitto disconnect: %d\n", res) ;
  gateway->lock_mosquitto();
  gateway->m_broker_connected = false ;
  gateway->unlock_mosquitto();
}

void ServerMqttSn::gateway_connect_callback(struct mosquitto *m,
						 void *data,
						 int res)
{
  ServerMqttSn *gateway = (ServerMqttSn*)data ;
  DPRINT("CONNECT CALLBACK: Mosquitto connect: %d\n", res) ;
  // Gateway connected to the broker
  gateway->lock_mosquitto();
  if (res == 0) gateway->m_broker_connected = true ;
  gateway->unlock_mosquitto();
}

void ServerMqttSn::send_will(MqttConnection *con)
{
  if (!m_mosquitto_initialised) return ; // cannot process

  int mid = 0 ;
  pthread_mutex_lock(&m_mosquittolock) ;
  if (strlen(con->get_will_topic()) > 0){
    int ret = mosquitto_publish(m_pmosquitto,
				&mid,
				con->get_will_topic(),
				con->get_will_message_len(),
				con->get_will_message(),
				con->get_will_qos(),
				con->get_will_retain()) ;
    if (ret != MOSQ_ERR_SUCCESS){
      EPRINT("Sending WILL: Mosquitto publish failed with code %d\n", ret);
    }
  }
  pthread_mutex_unlock(&m_mosquittolock) ;
}

bool ServerMqttSn::manage_connections()
{
  MqttConnection *con = NULL ;
  MqttMessage *m = NULL ;
  pthread_mutex_lock(&m_mosquittolock) ;
  
  for(con = m_connection_head; con != NULL; con=con->next){
    switch(con->get_state()){
    case MqttConnection::State::connected:
    case MqttConnection::State::connecting:

      if (con->lost_contact()){
	// Client is not a sleeping client and is also
	// inactive
	con->set_state(MqttConnection::State::disconnected) ;
	// Attempt to send a disconnect
	DPRINT("MANAGE CONNECTION: Disconnecting lost client: %s\n", con->get_client_id()) ;
	if (!writemqtt(con, MQTT_DISCONNECT, NULL, 0)){
	  EPRINT("MANAGE CONNECTION: IO failed to send MQTT_DISCONNECT to client %s\n", con->get_client_id()) ;
	}
	// Remove all pending messages
	con->messages.clear_queue() ;
	// Send the client will
	send_will(con) ;
      }else{
	m=con->messages.get_active_message();
	if (m && m->has_content()){
	  // There's an active message to process
	  if (!m->is_sending()){
	    // Write the message to client
	    DPRINT("MANAGE CONNECTION: Sending MQTT message %s, Message ID %u, length %u to client %s\n",
		   mqtt_code_str(m->get_message_type()),
		   m->get_message_id(),
		   m->get_message_len(),
		   con->get_client_id());
	    if(writemqtt(con,
			 m->get_message_type(),
			 m->get_message(), m->get_message_len())){
	      m->sending() ; // Acknowledge message is sending
	    }else{
	      EPRINT("MANAGE CONNECTION: IO failure - writemqtt failed for message %s, Message ID %u to client %s\n",
		     mqtt_code_str(m->get_message_type()),
		     m->get_message_id(),
		     con->get_client_id());
	    }
	  }else{ // Already sending the active message, check retries
	    if (m->has_expired(m_Tretry)){
	      if (m->has_failed(m_Nretry)){
		// Connection has failed retry attempts
		if (m->get_activity() == MqttMessage::Activity::willtopic ||
		    m->get_activity() == MqttMessage::Activity::willmessage){
		  // Couldn't complete a connection
		  con->set_state(MqttConnection::State::disconnected) ;
		  // Disconnected so clear any pending messages in queue
		  con->messages.clear_queue() ;
		}
		DPRINT("MANAGE CONNECTION: Message failed to deliver %s, Message ID %u, length %u to client %s\n",
		       mqtt_code_str(m->get_message_type()),
		       m->get_message_id(),
		       m->get_message_len(),
		       con->get_client_id());
		// Set this message to inactive and process the next message
		m->set_inactive();
	      }else{
		// Write the message again
		DPRINT("MANAGE CONNECTION: Resending MQTT message %s, Message ID %u, length %u, to client %s\n",
		       mqtt_code_str(m->get_message_type()),
		       m->get_message_id(),
		       m->get_message_len(),
		       con->get_client_id());
		if (!writemqtt(con,
			       m->get_message_type(),
			       m->get_message(),
			       m->get_message_len())){
		  EPRINT("MANAGE CONNECTION: IO failed to send message %s, message ID %u, to client %s\n",
			 mqtt_code_str(m->get_message_type()),
			 m->get_message_id(),
			 con->get_client_id());
		}
	      }
	    }
	  }
	}else{
	  // No active message to send

	  // New connection requires the delivery
	  // of topics due to dirty reconnect
	  if (con->get_send_topics()){
	    complete_client_connection(con) ;
	  }    
	}
      }

      break;
    case MqttConnection::State::disconnected:
      break;
    case MqttConnection::State::asleep:
      break;
    default:
      break;
    }
  }
  
  if (m_broker_connected){
    // Send Advertise messages
    time_t now = time(NULL) ;
    if (m_last_advertised+m_advertise_interval < now){
      DPRINT("MANAGE CONNECTION: Sending Advertised\n") ;
      advertise(m_advertise_interval) ;
      m_last_advertised = now ;
    }
  }

  pthread_mutex_unlock(&m_mosquittolock) ;

  return dispatch_queue() ;
}

bool ServerMqttSn::advertise(uint16_t duration)
{
  uint8_t buff[3] ;
  buff[0] = m_gwid ;
  buff[1] = duration >> 8 ; // Is this MSB first or LSB first?
  buff[2] = duration & 0x00FF;

  pthread_mutex_lock(&m_mosquittolock) ;
  if (addrwritemqtt(m_pDriver->get_broadcast(), MQTT_ADVERTISE, buff, 3)){
    pthread_mutex_unlock(&m_mosquittolock) ;
    return true ;
  }
  EPRINT("Advertise cannot send due to an error\n") ;
  
  pthread_mutex_unlock(&m_mosquittolock) ;
  return false ;
}

bool ServerMqttSn::register_topic(MqttConnection *con, MqttTopic *t)
{
  uint8_t buff[PACKET_DRIVER_MAX_PAYLOAD] ;
  // Gateway call to client
  pthread_mutex_lock(&m_mosquittolock) ;
  if (!con->is_connected()){
    pthread_mutex_unlock(&m_mosquittolock) ;
    return false ; // not connected
  }
  MqttMessage *m = con->messages.add_message(MqttMessage::Activity::registering) ;
  if (!m){
    pthread_mutex_unlock(&m_mosquittolock) ;
    return false ;
  }
  
  uint16_t mid = m->get_message_id();
  uint16_t topicid = t->get_id() ;
  buff[0] = topicid >> 8;
  buff[1] = topicid & 0x00FF ;
  buff[2] = mid >> 8;
  buff[3] = mid & 0x00FF ;
  const char *sz = t->get_topic() ;
  size_t len = strlen(sz) ;
  memcpy(buff+4, sz, len) ;
  m->set_message(MQTT_REGISTER, buff, 4+len) ;
  if (con->get_send_topics()){
    m->set_activity(MqttMessage::Activity::registeringall) ;
  }

  pthread_mutex_unlock(&m_mosquittolock) ;
  return true ;
}

bool ServerMqttSn::ping(const char *szclientid)
{
  pthread_mutex_lock(&m_mosquittolock) ;
  MqttConnection *con = search_connection(szclientid) ;
  if (!con) return false ; // cannot ping unknown client

  // Record when the ping was attempted, note that this doesn't care
  // if it worked
  con->reset_ping() ;

  if (writemqtt(con, MQTT_PINGREQ, NULL, 0)){
    pthread_mutex_unlock(&m_mosquittolock) ;
    return true ;
  }
  EPRINT("Failed to send ping to client %s\n", szclientid) ;
  pthread_mutex_unlock(&m_mosquittolock) ;
  return false ; // failed to send the ping
}


