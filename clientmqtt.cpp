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

#include "clientmqtt.hpp"
#include "radioutil.hpp"
#include <string.h>
#include <stdio.h>
#ifndef ARDUINO
 #include <wchar.h>
#endif
#include <stdlib.h>
#include <locale.h>


ClientMqttSn::ClientMqttSn()
{
  strcpy(m_szclient_id, "CL") ;  

  m_willmessage[0] = '\0' ;
  m_willmessagesize = 0 ;
  m_willtopic[0] = '\0' ;
  m_willtopicsize = 0 ;
  m_willtopicqos = 0;

  m_sleep_duration = 0 ;
  
  m_fnconnected = NULL ;
  m_fndisconnected = NULL ;
  m_fngatewayinfo = NULL ;
  m_fnpublished = NULL ;
  m_fnregister = NULL;
  m_fnmessage = NULL ;
  m_fnsubscribed = NULL ;
}

ClientMqttSn::~ClientMqttSn()
{

}


void ClientMqttSn::received_puback(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len != 5) return ;
  bool bsuccess = false;
  
  uint16_t topicid = (data[0] << 8) | data[1] ; // Assuming MSB is first
  uint16_t messageid = (data[2] << 8) | data[3] ; // Assuming MSB is first
  uint8_t returncode = data[4] ;

  DPRINT("PUBACK: {topicid = %u, messageid = %u, returncode = %u}\n", topicid, messageid, returncode) ;

  // not for this client if the connection address is different
  if (!m_client_connection.address_match(sender_address)) return ; 
  m_client_connection.update_activity() ;
  if (m_client_connection.get_activity() == MqttConnection::Activity::publishing)
    m_client_connection.set_activity(MqttConnection::Activity::none) ;
    
  switch(returncode){
  case MQTT_RETURN_ACCEPTED:
    DPRINT("PUBACK: {return code = Accepted}\n") ;
    bsuccess = true ;
    break ;
  case MQTT_RETURN_CONGESTION:
    DPRINT("PUBACK: {return code = Congestion}\n") ;
    break ;
  case MQTT_RETURN_INVALID_TOPIC:
    DPRINT("PUBACK: {return code = Invalid Topic}\n") ;
    break ;
  case MQTT_RETURN_NOT_SUPPORTED:
    DPRINT("PUBACK: {return code = Not Supported}\n") ;
    break ;
  default:
    DPRINT("PUBACK: {return code = %u}\n", returncode) ;
  }    
  if (m_fnpublished) (*m_fnpublished)(bsuccess, returncode, topicid, messageid, m_client_connection.get_gwid());

}

void ClientMqttSn::received_pubrec(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (m_client_connection.get_activity() != MqttConnection::Activity::publishing)
    return ; // client is not expecting a pubrec

  if (!m_client_connection.is_connected()) return ;

  // Check that this is coming from the expected gateway
  if (!m_client_connection.address_match(sender_address)) return ; 

  // Note the server activity and reset timers
  m_client_connection.update_activity() ;

  // Is this a QoS == 2? To Do: Implement check

  // Check the length, does it match expected PUBREC length?
  if (len != 2) return ;
    
  uint16_t messageid = (data[0] << 8) | data[1] ; // Assuming MSB is first
  DPRINT("PUBREC {messageid = %u}\n", messageid) ;
  writemqtt(&m_client_connection, MQTT_PUBREL, data, 2) ;
}

void ClientMqttSn::received_pubrel(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len != 2) return ; // Invalid PUBREL message length
  
  if (m_client_connection.get_activity() != MqttConnection::Activity::publishing)
    return ; // unexpected

  if (!m_client_connection.is_connected()) return ;

  // Check that this is coming from the expected gateway
  if (!m_client_connection.address_match(sender_address)) return ;

  // Note the server activity and reset timers
  m_client_connection.update_activity() ;
  uint16_t messageid = (data[0] << 8) | data[1] ;
  DPRINT("PUBREL: {messageid = %u}\n", messageid) ;

  if (m_client_connection.get_pubsub_messageid() != messageid){
    DPRINT("PUBREL: unexpected messageid from server. Expecting %u, but received %u\n", m_client_connection.get_pubsub_messageid(), messageid) ;
    // Ignore this, but it needs debugging in protocol
  }
  if(writemqtt(&m_client_connection, MQTT_PUBCOMP, data, 2))
    m_client_connection.set_activity(MqttConnection::Activity::none) ;
}

void ClientMqttSn::received_pubcomp(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len != 2) return ; // Invalid PUBCOMP message length
  uint16_t messageid = (data[0] << 8) | data[1] ; // Assuming MSB is first
  DPRINT("PUBCOMP {messageid = %u}\n", messageid) ;

  if (m_client_connection.get_activity() != MqttConnection::Activity::publishing)
    return ; // client is not expecting a pubcomp

  // not for this client if the connection address is different
  if (!m_client_connection.address_match(sender_address)) return ; 

  // Note the server activity and reset timers
  m_client_connection.update_activity() ;

  // Reset the connection activity
  m_client_connection.set_activity(MqttConnection::Activity::none) ;

}

void ClientMqttSn::received_suback(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len < 5) return ;

  uint16_t topicid = (data[1] << 8) | data[2] ; // Assuming MSB is first
  uint16_t messageid = (data[3] << 8) | data[4] ; // Assuming MSB is first

  // Check connection status, are we connected, otherwise ignore
  if (!m_client_connection.is_connected()) return ;
  // Verify the source address is our connected gateway
  if (!m_client_connection.address_match(sender_address)) return ; 

  pthread_mutex_lock(&m_mqttlock) ;

  m_client_connection.update_activity() ; // Reset timers
  
  DPRINT("SUBACK: {topicid: %u, messageid: %u}\n", topicid, messageid) ;

  MqttTopic *t = NULL ;

  switch(data[5]){
  case MQTT_RETURN_ACCEPTED:
    DPRINT("SUBACK: {return code = Accepted}\n") ;
    if ( (t=m_client_connection.topics.get_topic(topicid)) ){
      // Topic exists already which it should be complete the registration
      if (!t->is_complete()){
	DPRINT("Topic %u already exists but not completed reg, completing registration now\n", topicid) ;
	// Complete the topic anyway
	t->set_message_id(messageid) ;
	t->complete(topicid) ;
      }
      // Set subscription flag
      t->set_subscribed(true) ;
    }else if (! (t=m_client_connection.topics.complete_topic(messageid, topicid))){
      // Topic completion may not work if the topicid was already registered or
      // previously subscribed
      EPRINT("SUBACK: Client cannot complete topic ID %u for mid %u\n", topicid, messageid) ;
    }else{
      DPRINT("SUBACK: Topic %s completed and registered with ID %u\n", t->get_topic(), t->get_id()) ;
    }

    break ;
  case MQTT_RETURN_CONGESTION:
    DPRINT("SUBACK: {return code = Congestion}\n") ;
    break ;
  case MQTT_RETURN_INVALID_TOPIC:
    DPRINT("SUBACK: {return code = Invalid Topic}\n") ;
    break ;
  case MQTT_RETURN_NOT_SUPPORTED:
    DPRINT("SUBACK: {return code = Not Supported}\n") ;
    break ;
  default:
    DPRINT("SUBACK: {return code = %u}\n", data[5]) ;
  }

  m_client_connection.set_activity(MqttConnection::Activity::none) ;
  pthread_mutex_unlock(&m_mqttlock) ;
  if (m_fnsubscribed) (*m_fnsubscribed)(data[5] == MQTT_RETURN_ACCEPTED,
					data[5], topicid, messageid, m_client_connection.get_gwid());
}

void ClientMqttSn::received_unsubscribe(uint8_t *sender_address, uint8_t *data, uint8_t len)
{

}

void ClientMqttSn::received_unsuback(uint8_t *sender_address, uint8_t *data, uint8_t len)
{

}

void ClientMqttSn::received_publish(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len < 6) return ; // not long enough to be a publish
  uint16_t topicid = (data[1] << 8) | data[2] ; // Assuming MSB is first
  uint16_t messageid = (data[3] << 8) | data[4] ; // Assuming MSB is first
  uint8_t qos = data[0] & FLAG_QOSN1 ;
  uint8_t topic_type = data[0] & (FLAG_DEFINED_TOPIC_ID | FLAG_SHORT_TOPIC_NAME);
  uint8_t payload[PACKET_DRIVER_MAX_PAYLOAD] ;
  int payload_len = len-5 ;
  
  memcpy(payload, data+5, payload_len) ;

  m_buff[0] = data[1] ; // replicate topic id 
  m_buff[1] = data[2] ; // replicate topic id 
  m_buff[2] = data[3] ; // replicate message id
  m_buff[3] = data[4] ; // replicate message id

  DPRINT("PUBLISH: {Flags = %X, QoS = %u, Topic ID = %u, Mess ID = %u}\n",
	 data[0], qos, topicid, messageid) ;

  // Check connection status, are we connected, otherwise ignore
  if (!m_client_connection.is_connected()) return ;
  // Verify the source address is our connected gateway
  if (!m_client_connection.address_match(sender_address)) return ; 

  m_client_connection.set_pub_entities(topicid,
				       messageid,
				       topic_type,
				       qos,
				       payload_len,
				       payload,
				       data[0] & FLAG_RETAIN);

  // Search and get the topic from ID
  MqttTopic *t = NULL ;
  const char *sztopic = NULL ;
  char szshort[3] ;
  switch(topic_type){
  case FLAG_NORMAL_TOPIC_ID:
    DPRINT("PUBLISH: Searching normal topic IDs\n") ;
    t=m_client_connection.topics.get_topic(topicid);
    break ;
  case FLAG_DEFINED_TOPIC_ID:
    DPRINT("PUBLISH: Searching predefined topic IDs\n") ;
    t = m_predefined_topics.get_topic(topicid);
    break;
  case FLAG_SHORT_TOPIC_NAME:
    szshort[0] = topicid >> 8;
    szshort[1] = topicid & 0x00FF ;
    szshort[2] = '\0';
    sztopic = szshort ;
    DPRINT("Client received short topic publish for %s\n", szshort) ;    
    break;
  default:
    // Unknown or not implemented
    m_buff[4] = MQTT_RETURN_NOT_SUPPORTED;
    writemqtt(&m_client_connection, MQTT_PUBACK, m_buff, 5) ;
    return ;
  }
  if (topic_type != FLAG_SHORT_TOPIC_NAME){
    if (t){
      sztopic = t->get_topic() ;
      DPRINT("Client received publish for topic %s, ID %u, Message ID %u\n", sztopic, topicid, messageid) ;
    }else{
      m_buff[4] = MQTT_RETURN_INVALID_TOPIC ;
      writemqtt(&m_client_connection, MQTT_PUBACK, m_buff, 5) ;
      return ;
    }
  }
  
  // tell client of message
  // bool success, uint8_t return, const char* topic, uint8_t* payload, uint8_t payloadlen, uint8_t gwid
  if (m_fnmessage) (*m_fnmessage)(true, MQTT_RETURN_ACCEPTED, sztopic, payload, payload_len, m_client_connection.get_gwid());
  
  if (qos == FLAG_QOS0 || qos == FLAG_QOS1){

    if (qos == FLAG_QOS1){
      m_buff[4] = MQTT_RETURN_ACCEPTED ;
      writemqtt(&m_client_connection, MQTT_PUBACK, m_buff, 5);
    }
    m_client_connection.set_activity(MqttConnection::Activity::none);
    return ;
  }
  
  // QoS 2 requires further orchestration
  if (writemqtt(&m_client_connection, MQTT_PUBREC, m_buff+2, 2))
    m_client_connection.set_activity(MqttConnection::Activity::publishing);
  
}

void ClientMqttSn::received_register(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len < 4) return ;
  uint16_t topicid = (data[0] << 8) | data[1] ; // Assuming MSB is first
  uint16_t messageid = (data[2] << 8) | data[3] ; // Assuming MSB is first
  char sztopic[PACKET_DRIVER_MAX_PAYLOAD - MQTT_REGISTER_HDR_LEN +1] ;
  if (len - 4 > PACKET_DRIVER_MAX_PAYLOAD - MQTT_REGISTER_HDR_LEN) return ; // overflow
  memcpy(sztopic, data+4, len-4) ;
  sztopic[len-4] = '\0';


  // Check connection status, are we connected, otherwise ignore
  if (!m_client_connection.is_connected()) return ;
  // Verify the source address is our connected gateway
  if (!m_client_connection.address_match(sender_address)) return ; 

  pthread_mutex_lock(&m_mqttlock) ;

  m_client_connection.update_activity() ; // Reset timers
  
  DPRINT("REGISTER: {topicid: %u, messageid: %u, topic %s}\n", topicid, messageid, sztopic) ;

  MqttTopic *t = NULL;
  if (!(t=m_client_connection.topics.create_topic(sztopic, topicid))){
    EPRINT("Server error, cannot create topic %s, possible memory error or topic exists\n", sztopic) ;
    return ; // Stop
  }
  
  uint8_t response[5] ;
  response[0] = topicid >> 8 ; // Write topicid MSB first
  response[1] = topicid & 0x00FF ;
  response[2] = data[2] ; // Echo back the messageid received
  response[3] = data[3] ; // Echo back the messageid received
  response[4] = MQTT_RETURN_ACCEPTED ;
  writemqtt(&m_client_connection, MQTT_REGACK, response, 5) ;
  pthread_mutex_unlock(&m_mqttlock) ;

  // Call the client callback to inform of new topic
  // Implicitly acceped, returns zero for message ID as client didn't request
  if (m_fnregister) (*m_fnregister)(true, MQTT_RETURN_ACCEPTED, topicid, 0, m_client_connection.get_gwid());

}

void ClientMqttSn::received_regack(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  if (len != 5) return ;
  bool bsuccess = false ;
  
  uint16_t topicid = (data[0] << 8) | data[1] ; // Assuming MSB is first
  uint16_t messageid = (data[2] << 8) | data[3] ; // Assuming MSB is first
  uint8_t returncode = data[4] ;

  DPRINT("REGACK: {topicid = %u, messageid = %u, returncode = %u}\n", topicid, messageid, returncode) ;

  // not for this client if the connection address is different
  if (!m_client_connection.address_match(sender_address)) return ; 
  m_client_connection.update_activity() ;
  m_client_connection.set_activity(MqttConnection::Activity::none) ;
  switch(returncode){
  case MQTT_RETURN_ACCEPTED:
    DPRINT("REGACK: {return code = Accepted}\n") ;
    if (!m_client_connection.topics.complete_topic(messageid, topicid)){
      DPRINT("Cannot complete topic %u with messageid %u\n", topicid, messageid) ;
    }else{
      bsuccess = true ;
    }
    break ;
  case MQTT_RETURN_CONGESTION:
    DPRINT("REGACK: {return code = Congestion}\n") ;
    m_client_connection.topics.del_topic_by_messageid(messageid) ;
    break ;
  case MQTT_RETURN_INVALID_TOPIC:
    DPRINT("REGACK: {return code = Invalid Topic}\n") ;
    m_client_connection.topics.del_topic_by_messageid(messageid) ;
    break ;
  case MQTT_RETURN_NOT_SUPPORTED:
    DPRINT("REGACK: {return code = Not Supported}\n") ;
    m_client_connection.topics.del_topic_by_messageid(messageid) ;
    break ;
  default:
    DPRINT("REGACK: {return code = %u}\n", returncode) ;
    m_client_connection.topics.del_topic_by_messageid(messageid) ;
  }
  if (m_fnregister) (*m_fnregister)(bsuccess, returncode, topicid, messageid, m_client_connection.get_gwid());
}

void ClientMqttSn::received_pingresp(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  DPRINT("PINGRESP\n") ;
#ifndef ARDUINO
  pthread_mutex_lock(&m_mqttlock) ;
#endif
  MqttGwInfo *gw = get_gateway_address(sender_address);
  if (gw){
    gw->update_activity() ;

    if (gw->get_gwid() == m_client_connection.get_gwid()){
      // Ping received from connected gateway
      m_client_connection.update_activity() ;
    }
  }
#ifndef ARDUINO
  pthread_mutex_unlock(&m_mqttlock) ;
#endif
}

void ClientMqttSn::received_pingreq(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  // regardless of client or gateway just send the ACK
  #ifdef DEBUG
  if (len > 0){ //contains a client id
    char szclientid[PACKET_DRIVER_MAX_PAYLOAD - MQTT_CONNECT_HDR_LEN +1];
    memcpy(szclientid, data, len);
    szclientid[len] = '\0' ;
    DPRINT("PINGREQ: {clientid = %s}\n", szclientid) ;
  }else{
    DPRINT("PINGREQ\n") ;
  }
  #endif
 
  addrwritemqtt(sender_address, MQTT_PINGRESP, NULL, 0) ;
 
}

void ClientMqttSn::received_advertised(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  uint16_t duration = (data[1] << 8) | data[2] ; // Assuming MSB is first
  DPRINT("ADVERTISED: {gw = %u, duration = %u}\n", data[0], duration) ;

#ifndef ARDUINO
  pthread_mutex_lock(&m_mqttlock) ;
#endif
  // Call update gateway. This returns false if gateway is not known
  if (!update_gateway(sender_address, data[0], duration)){

    // New gateway
    bool ret = add_gateway(sender_address, data[0], duration);
    if (!ret){
      DPRINT("Cannot add gateway %u\n", data[0]) ;
    }else{
      if (m_fngatewayinfo) (*m_fngatewayinfo)(true, data[0]) ;
    }
  }

  // If client is connected to this gateway then update client connection activity
  if (m_client_connection.is_connected() &&
      m_client_connection.get_gwid() == data[0]){
    m_client_connection.update_activity() ;
  }
#ifndef ARDUINO
  pthread_mutex_unlock(&m_mqttlock) ;
#endif
}

bool ClientMqttSn::add_gateway(uint8_t *gateway_address, uint8_t gwid, uint16_t ad_duration, bool perm)
{
  for (uint8_t i=0; i < MQTT_MAX_GATEWAYS; i++){
    if (!m_gwinfo[i].is_allocated() || !m_gwinfo[i].is_active()){
      m_gwinfo[i].reset() ; // Clear gateway
      m_gwinfo[i].set_address(gateway_address, m_pDriver->get_address_len()) ;
      m_gwinfo[i].set_allocated(true) ;
      m_gwinfo[i].update_activity() ;
      m_gwinfo[i].set_gwid(gwid) ;
      m_gwinfo[i].set_permanent(perm) ;
      
      // Do not callback as user should be aware of a manual gateway

      if (ad_duration > 0){
        m_gwinfo[i].advertised(ad_duration) ; 
      }
      return true ;
    }
  }
  return false ;
}

bool ClientMqttSn::update_gateway(uint8_t *gateway_address, uint8_t gwid, uint16_t ad_duration)
{
  for (uint8_t i=0; i < MQTT_MAX_GATEWAYS; i++){
    if (m_gwinfo[i].is_allocated() && m_gwinfo[i].get_gwid() == gwid){
      m_gwinfo[i].set_address(gateway_address, m_pDriver->get_address_len()) ;
      m_gwinfo[i].update_activity() ;

      // Only set new duration if not zero. Retain original advertised state
      if (ad_duration > 0){
        m_gwinfo[i].advertised(ad_duration) ; 
      }

      return true ;
    }
  }
  return false;
}

bool ClientMqttSn::del_gateway(uint8_t gwid)
{
  for (uint8_t i=0; i < MQTT_MAX_GATEWAYS; i++){
    if (m_gwinfo[i].get_gwid() == gwid){
      m_gwinfo[i].reset() ; // Clear all attributes. Can be reused
      return true ;
    }
  }
  return false;
}


void ClientMqttSn::received_gwinfo(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  DPRINT("GWINFO: {gw = %u, address = ", data[0]) ;
  for (uint8_t j=0; j < len - 1; j++) DPRINT("%X", data[j+1]) ;
  DPRINT("}\n") ;
  
  bool gw_updated = false  ;
#ifndef ARDUINO
  pthread_mutex_lock(&m_mqttlock) ;
#endif
  // Reset activity if searching was requested
  if (m_client_connection.get_activity() == MqttConnection::Activity::searching){
    m_client_connection.set_activity(MqttConnection::Activity::none) ;
  }
  
  if (len == m_pDriver->get_address_len()+1) // Was the address populated in GWINFO?
    gw_updated = update_gateway(data+1, data[0], 0);
  else
    gw_updated = update_gateway(sender_address, data[0], 0);
  
  if (!gw_updated){
    // Insert new gateway. Overwrite old or expired gateways
    if (len == m_pDriver->get_address_len()+1){ // Was the address populated in GWINFO?
      add_gateway(data+1, data[0], 0);
    }else{ // No address, but the sender address can be used
      add_gateway(sender_address, data[0], 0);
    }
    if (m_fngatewayinfo) (*m_fngatewayinfo)(true, data[0]) ;
  }
#ifndef ARDUINO
  pthread_mutex_unlock(&m_mqttlock) ;
#endif

  // It's possible that the gateway cannot be saved if there's already a full list
  // of gateways.
  // Why is the client requesting the info though??? Only needs one GW so not an error
  
}

void ClientMqttSn::received_connack(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  bool bsuccess = false ;
#ifndef ARDUINO
  pthread_mutex_lock(&m_mqttlock) ;
#endif
  // Was this client connecting?
  if (m_client_connection.get_state() != MqttConnection::State::connecting){
#ifndef ARDUINO
    pthread_mutex_unlock(&m_mqttlock) ;
#endif
    return ; // not enabled and expected
  }
  
  m_client_connection.update_activity() ;
  // TO DO - confirm that the gateway sending this matches the address expected
  // not a major fault but is worth a check

  if (m_client_connection.get_activity() == MqttConnection::Activity::willtopic){
    EPRINT("Connection complete, but will topic or message not processed\n") ;
  }

  // TO DO: Needs to handle the errors properly and inform client of error
  // so correct behaviour can follow.
  switch(data[0]){
  case MQTT_RETURN_ACCEPTED:
    DPRINT("CONNACK: {return code = Accepted}\n") ;
    m_client_connection.set_state(MqttConnection::State::connected);
    bsuccess = true ;
    break ;
  case MQTT_RETURN_CONGESTION:
    DPRINT("CONNACK: {return code = Congestion}\n") ;
    m_client_connection.set_state(MqttConnection::State::disconnected); // Cannot connect
    break ;
  case MQTT_RETURN_INVALID_TOPIC:
    DPRINT("CONNACK: {return code = Invalid Topic}\n") ;
    // Can this still count as a connection?
    m_client_connection.set_state(MqttConnection::State::disconnected); // Don't allow?
    break ;
  case MQTT_RETURN_NOT_SUPPORTED:
    DPRINT("CONNACK: {return code = Not Supported}\n") ;
    m_client_connection.set_state(MqttConnection::State::disconnected); // Cannot connect
    break ;
  default:
    DPRINT("CONNACK: {return code = %u}\n", data[0]) ;
    // ? Are we connected ?
    m_client_connection.set_state(MqttConnection::State::disconnected); // Cannot connect
  }
  
  if (m_fnconnected) (*m_fnconnected) (bsuccess, data[0], m_client_connection.get_gwid()) ;
    
  m_client_connection.set_activity(MqttConnection::Activity::none) ;
    
#ifndef ARDUINO
  pthread_mutex_unlock(&m_mqttlock) ;
#endif
}

void ClientMqttSn::received_willtopicreq(uint8_t *sender_address, uint8_t *data, uint8_t len)
{

  // Check that this is coming from the expected gateway
  if (!m_client_connection.address_match(sender_address)) return ; 

  // Only clients need to respond to this
  DPRINT("WILLTOPICREQ\n") ;
  if (m_client_connection.get_state() != MqttConnection::State::connecting) return ; // Unexpected
  if (m_willtopicsize == 0){
    // No topic set
    writemqtt(&m_client_connection, MQTT_WILLTOPIC, NULL, 0) ;
  }else{
    m_buff[0] = 0 ;
    switch(m_willtopicqos){
    case 0:
      m_buff[0] = FLAG_QOS0 ;
      break ;
    case 1:
      m_buff[0] = FLAG_QOS1 ;
      break ;
    case 2:
    default: // Ignore other values and set to max QOS
      m_buff[0] = FLAG_QOS2 ;
    }
    // Any overflow of size should have been checked so shouldn't need to check again here.
    memcpy(m_buff+1, m_willtopic, m_willtopicsize) ;
    writemqtt(&m_client_connection, MQTT_WILLTOPIC, m_buff, m_willtopicsize+1) ;
  }
  m_client_connection.set_activity(MqttConnection::Activity::willtopic) ;
  m_client_connection.update_activity() ;
  
}

void ClientMqttSn::received_willmsgreq(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  // Check that this is coming from the expected gateway
  if (!m_client_connection.address_match(sender_address)) return ; 

  // Only clients need to respond to this
  DPRINT("WILLMSGREQ\n") ;
  if (m_client_connection.get_state() != MqttConnection::State::connecting) return ; // Unexpected

  if (m_willmessagesize == 0){
    // No topic set
    writemqtt(&m_client_connection, MQTT_WILLMSG, NULL, 0) ;
  }else{

    // Any overflow of size should have been checked so shouldn't need to check again here.
    writemqtt(&m_client_connection, MQTT_WILLMSG, (uint8_t*)m_willmessage, m_willmessagesize) ;
  }
  m_client_connection.set_activity(MqttConnection::Activity::willmessage) ;
  m_client_connection.update_activity() ;
  
}

void ClientMqttSn::received_disconnect(uint8_t *sender_address, uint8_t *data, uint8_t len)
{
  // Disconnect request from server to client
  // probably due to an error
  DPRINT("DISCONNECT\n") ;
  if (m_sleep_duration)
    m_client_connection.set_state(MqttConnection::State::asleep) ;
  else
    m_client_connection.set_state(MqttConnection::State::disconnected) ;
  m_client_connection.topics.free_topics() ; // client always forgets topics
  MqttGwInfo *gwi = get_gateway_address(sender_address);
  uint8_t gwid = gwi?gwi->get_gwid():0;

  if (m_fndisconnected) (*m_fndisconnected) (m_sleep_duration?true:false, m_sleep_duration, gwid) ;
}

void ClientMqttSn::set_client_id(const char *szclientid)
{
  strncpy(m_szclient_id, szclientid, m_pDriver->get_payload_width() - MQTT_CONNECT_HDR_LEN) ;
}

const char* ClientMqttSn::get_client_id()
{
  return m_szclient_id ;
}

void ClientMqttSn::initialise(uint8_t address_len, uint8_t *broadcast, uint8_t *address)
{
  // Reset will attributes
  m_willtopic[0] = '\0' ;
  m_willtopicsize = 0 ;
  m_willtopicqos = 0 ;
  m_willmessage[0] = '\0' ;
  m_willmessagesize = 0;
  
  MqttSnEmbed::initialise(address_len, broadcast, address) ;
}

bool ClientMqttSn::manage_gw_connection()
{
  // Has connection been lost or does the connection need a keep-alive
  // ping to maintain connection?
  
  if (m_client_connection.lost_contact()){
    DPRINT("Client lost connection to gateway %u\n", m_client_connection.get_gwid()) ;
    // Close connection. Take down connection
    m_client_connection.set_state(MqttConnection::State::disconnected) ;
    m_client_connection.set_activity(MqttConnection::Activity::none) ;
    m_client_connection.topics.free_topics() ; // clear all topics
    // Disable the gateway in the client register
    MqttGwInfo *gw = get_gateway(m_client_connection.get_gwid()) ;
    if (gw){
      gw->set_active(false);
    }
    // Lost gateway, inform user through callback
    if (m_fngatewayinfo) (*m_fngatewayinfo)(false, gw->get_gwid()) ;
    if (m_fndisconnected) (*m_fndisconnected)(false, 0, gw->get_gwid()) ;
    return false;
    
  }else{
    // Another ping due?      
    if (m_client_connection.send_another_ping()){
      DPRINT("Sending a ping to %u\n", m_client_connection.get_gwid()) ;
      bool r = ping(m_client_connection.get_gwid()) ;
      if (!r) DPRINT("Ping to gw failed\n") ;
    }
  }
  return true ;
}

bool ClientMqttSn::manage_connections()
{
  switch (m_client_connection.get_state()){
  case MqttConnection::State::connected:
    // Manage connection to gateway. Ensure gateway is still there and connection should be open
    // Function returns false if gateway lost - no need to manage connection
    if (manage_gw_connection()){
      if (m_client_connection.get_activity() != MqttConnection::Activity::none){
        // If connected client is doing anything then manage the connection
        if (!manage_pending_message(m_client_connection)){
          m_client_connection.set_activity(MqttConnection::Activity::none) ;
        }
      }
    }

    break;
  case MqttConnection::State::connecting:
    if (!manage_pending_message(m_client_connection)){
      m_client_connection.set_state(MqttConnection::State::disconnected) ;
      // Failed to connect
      if (m_fnconnected) (*m_fnconnected) (false, MQTT_RETURN_ACCEPTED, m_client_connection.get_gwid()) ;
    }
    break;
  case MqttConnection::State::disconnecting:
    if (!manage_pending_message(m_client_connection)){
      m_client_connection.set_state(MqttConnection::State::disconnected) ;
      // Disconnect timed out. Inform through callback that the connection should be closed anyway
      if (m_fndisconnected) (*m_fndisconnected)(false, 0, m_client_connection.get_gwid()) ;
    }
    break ;
  case MqttConnection::State::disconnected:
    // Retry searches if no response.
    if (m_client_connection.get_activity() == MqttConnection::Activity::searching){
      if (!manage_pending_message(m_client_connection)){
        // No search response, stop searching
        m_client_connection.set_activity(MqttConnection::Activity::none);
      }
    }
      
    break ;
  case MqttConnection::State::asleep:
    break ;
  default:
    break ; // unhandled connection state
  }
  
  // TO DO - Issue search if no gateways. Currently managed by APP
  
  return dispatch_queue() ;
}

bool ClientMqttSn::searchgw(uint8_t radius)
{
  if (addrwritemqtt(m_pDriver->get_broadcast(), MQTT_SEARCHGW, &radius, 1)){
    // Cache the message if disconnected. This changes the connection to use
    // the broadcast address for resending. 
    // Note that no caching is used if connection is in anyother state than disconnected. 
    // This is more efficient and prevents overwrite of cache for other 'connected' comms
    if (m_client_connection.get_state() == MqttConnection::State::disconnected){
      m_client_connection.set_address(m_pDriver->get_broadcast(), m_pDriver->get_address_len());
      m_client_connection.set_cache(MQTT_SEARCHGW, &radius, 1) ;
      m_client_connection.set_activity(MqttConnection::Activity::searching) ;
    }
    return true ;
  }

  return false ;
}
#ifndef ARDUINO
uint16_t ClientMqttSn::register_topic(const wchar_t *topic)
{
  char sztopic[PACKET_DRIVER_MAX_PAYLOAD - MQTT_REGISTER_HDR_LEN +1] ;
   
  wchar_to_utf8(topic, sztopic, (unsigned)(m_pDriver->get_payload_width() - MQTT_REGISTER_HDR_LEN));

  return register_topic(sztopic) ;
}
#endif
uint16_t ClientMqttSn::register_topic(const char *topic)
{
  uint16_t len = strlen(topic) ; // len excluding terminator

  if (m_client_connection.is_connected()){
    // Reject topic if too long for payload
    if (len > m_pDriver->get_payload_width() - MQTT_REGISTER_HDR_LEN) return 0;
#ifndef ARDUINO
    pthread_mutex_lock(&m_mqttlock) ;
#endif
    uint16_t mid = m_client_connection.get_new_messageid() ;
    // Register the topic. Return value is zero if topic is new
    MqttTopic *t = m_client_connection.topics.reg_topic(topic, mid) ;
    if(t->get_id() > 0){
      if (t->is_complete()){
	DPRINT("reg_topic returned an existing & complete topic ID %u\n", t->get_id()) ;
#ifndef ARDUINO
	pthread_mutex_unlock(&m_mqttlock) ;
#endif
	return 0 ; // already exists
      }else{
	DPRINT("Topic ID %u already exists but is incomplete, attempting with new mid %u\n", t->get_id(), mid) ;
	t->set_message_id(mid) ; // Set new message id to complete
      }
    }else{
      DPRINT("reg_topic has registered a new topic %u\n", mid) ;
    }

#ifndef ARDUINO
    pthread_mutex_unlock(&m_mqttlock) ;
#endif
    m_buff[0] = 0 ;
    m_buff[1] = 0 ; // topic ID set to zero
    m_buff[2] = mid >> 8 ; // MSB
    m_buff[3] = mid & 0x00FF;
    memcpy(m_buff+4, topic, len) ;
    
    if (writemqtt(&m_client_connection, MQTT_REGISTER, m_buff, 4+len)){
      // Cache the message
      m_client_connection.set_activity(MqttConnection::Activity::registering) ;
      return mid; //return the message id
    }
  }
  // Connection timed out or not connected
  return 0 ;
}

bool ClientMqttSn::subscribe(uint8_t qos, const char *sztopic, bool bshorttopic)
{
  if (!m_client_connection.is_connected()) return false ;

  if (qos > 2) return false ; // Invalid QoS
  uint8_t topic_len = strlen(sztopic) ;
  if (topic_len > m_pDriver->get_payload_width() - MQTT_SUBSCRIBE_HDR_LEN){
    EPRINT("Topic %s is too long to fit in subscription message\n", sztopic) ;
    return false ;
  }
  if (bshorttopic){
    if (topic_len < 2) return false ;
    uint16_t topicid = (sztopic[0] << 8) | sztopic[1] ;
    return subscribe(qos, topicid, FLAG_SHORT_TOPIC_NAME);
  }

  m_buff[0] = (qos==0?FLAG_QOS0:0) |
              (qos==1?FLAG_QOS1:0) |
              (qos==2?FLAG_QOS2:0) ;
  uint16_t mid = m_client_connection.get_new_messageid() ;
  m_buff[1] = mid >> 8 ;
  m_buff[2] = mid & 0x00FF ;

  memcpy (m_buff+3, sztopic, topic_len) ;

  MqttTopic *t = m_client_connection.topics.reg_topic(sztopic, mid) ;
  if (t->get_id() > 0 && t->is_subscribed()){
    DPRINT("INFO: Subscription to topic %s is already registered with an ID %u\n", sztopic, t->get_id());
    return false;
  }
  
  if (writemqtt(&m_client_connection, MQTT_SUBSCRIBE, m_buff, topic_len+3)){
    m_client_connection.set_activity(MqttConnection::Activity::subscribing);
    // Update cached version to set the DUP flag
    m_buff[0] |= FLAG_DUP;
    m_client_connection.set_cache(MQTT_PUBLISH, m_buff, topic_len+3) ;
    return true ;
  }
  return false ;
}

bool ClientMqttSn::subscribe(uint8_t qos, uint16_t topicid, uint8_t topictype)
{
  if (!m_client_connection.is_connected()) return false ;
  if (qos > 2 || (topictype != FLAG_DEFINED_TOPIC_ID && topictype != FLAG_SHORT_TOPIC_NAME)) return false ;

  // Check if topic exists for defined topic
  if (topictype == FLAG_DEFINED_TOPIC_ID && !m_predefined_topics.get_topic(topicid)){
    EPRINT("Topic ID %u is not a predefined topic id for subscription\n", topicid) ;
    return false;
  }

  m_buff[0] = 
    (qos==0?FLAG_QOS0:0) |
    (qos==1?FLAG_QOS1:0) |
    (qos==2?FLAG_QOS2:0) | topictype;
  uint16_t mid = m_client_connection.get_new_messageid() ;
  m_buff[1] = mid >> 8 ;
  m_buff[2] = mid & 0x00FF ;
  m_buff[3] = topicid >> 8 ;
  m_buff[4] = topicid & 0x00FF ;
  
  if (writemqtt(&m_client_connection, MQTT_SUBSCRIBE, m_buff, 5)){
    if (qos > 0) m_client_connection.set_activity(MqttConnection::Activity::subscribing);
    // Update cached version to set the DUP flag
    m_buff[0] |= FLAG_DUP;
    m_client_connection.set_cache(MQTT_PUBLISH, m_buff, 5) ;
    return true ;
  }
  return false ;
}

bool ClientMqttSn::ping(uint8_t gwid)
{
  // Client to Gateway ping
  size_t clientid_len = strlen(m_szclient_id) ;
  MqttGwInfo *gw = get_gateway(gwid) ;
  if (!gw) return false ; // no gateway known

  // Record when the ping was attempted, note that this doesn't care
  // if it worked
  if (m_client_connection.get_gwid() == gwid)
    m_client_connection.reset_ping() ;
  
  if (addrwritemqtt(gw->get_address(), MQTT_PINGREQ, (uint8_t *)m_szclient_id, clientid_len))
    return true ;

  return false ;
}

bool ClientMqttSn::disconnect(uint16_t sleep_duration)
{
  uint8_t len = 0 ;
  // Already disconnected or other issue closed the connection
  if (m_client_connection.is_disconnected()) return false ;
  if (m_client_connection.is_asleep()) return false ;

  if (sleep_duration > 0){
    m_buff[0] = sleep_duration >> 8 ; //MSB set first
    m_buff[1] = sleep_duration & 0x00FF ;
    len = 2 ;
  }
  m_sleep_duration = sleep_duration ; // store the duration

  if (writemqtt(&m_client_connection, MQTT_DISCONNECT, m_buff, len)){
    m_client_connection.set_state(MqttConnection::State::disconnecting) ;
    return true ;
  }
  
  return false ; // failed to send the diconnect
}

bool ClientMqttSn::publish_noqos(uint8_t gwid, const char* sztopic, const uint8_t *payload, uint8_t payload_len, bool retain)
{
  uint16_t topicid = 0;
  // This will send Qos -1 messages with a short topic
  if (strlen(sztopic) != 2) return false ; // must be 2 bytes
  topicid = (sztopic[0] << 8) | sztopic[1] ;
  return publish_noqos(gwid,
		       topicid,
		       FLAG_SHORT_TOPIC_NAME,
		       payload, payload_len, retain) ;
}

bool ClientMqttSn::publish_noqos(uint8_t gwid, uint16_t topicid, uint8_t topictype, const uint8_t *payload, uint8_t payload_len, bool retain)
{
  m_buff[0] = (retain?FLAG_RETAIN:0) | FLAG_QOSN1 | topictype ;
  m_buff[1] = topicid >> 8 ;
  m_buff[2] = topicid & 0x00FF ;
  m_buff[3] = 0 ;
  m_buff[4] = 0 ;
  uint8_t len = payload_len + MQTT_PUBLISH_HDR_LEN ;
  if (payload_len > (m_pDriver->get_payload_width() - MQTT_PUBLISH_HDR_LEN)){
    EPRINT("Payload of %u bytes is too long for publish\n", payload_len) ;
    return false ;
  }
  memcpy(m_buff+MQTT_PUBLISH_HDR_LEN,payload, payload_len);

  DPRINT("NOQOS - topicid %u, topictype %u, flags %X\n",topicid,topictype,m_buff[0]) ;

  MqttGwInfo *gw = get_gateway(gwid) ;
  if (!gw) return false ;

  if(topictype == FLAG_SHORT_TOPIC_NAME ||
     topictype == FLAG_DEFINED_TOPIC_ID){
    if (!addrwritemqtt(gw->get_address(), MQTT_PUBLISH, m_buff, len)){
      EPRINT("Failed to send QoS -1 message\n") ;
      return false ;
    }
  }else if (topictype == FLAG_NORMAL_TOPIC_ID){
    EPRINT("QoS -1 cannot support normal topic IDs\n") ;
    return false ;
  }else{
    EPRINT("QoS -1 unknown topic type") ;
    return false ;
  }
  return true ;
}

bool ClientMqttSn::publish(uint8_t qos, const char *sztopic, const uint8_t *payload, uint8_t payload_len, bool retain)
{
  uint16_t topicid = 0;
  // This will send messages with a short topic
  if (strlen(sztopic) != 2) return false ; // must be 2 bytes
  topicid = (sztopic[0] << 8) | sztopic[1] ;
  return publish(qos,
		 topicid,
		 FLAG_SHORT_TOPIC_NAME,
		 payload, payload_len, retain) ;
}

bool ClientMqttSn::publish(uint8_t qos, uint16_t topicid, uint16_t topictype, const uint8_t *payload, uint8_t payload_len, bool retain)
{
  // This publish call will not handle -1 QoS messages
  if (!m_client_connection.is_connected()) return false ;

  if (qos > 2) return false ; // Invalid QoS
  
  m_buff[0] = (retain?FLAG_RETAIN:0) |
    (qos==0?FLAG_QOS0:0) |
    (qos==1?FLAG_QOS1:0) |
    (qos==2?FLAG_QOS2:0) |
    topictype;
  m_buff[1] = topicid >> 8 ;
  m_buff[2] = topicid & 0x00FF ;
  uint16_t mid = m_client_connection.get_new_messageid() ;
  m_buff[3] = mid >> 8 ;
  m_buff[4] = mid & 0x00FF ;
  uint8_t len = payload_len + 5 ;
  if (payload_len > (m_pDriver->get_payload_width() - MQTT_PUBLISH_HDR_LEN)){
    EPRINT("Payload of %u bytes is too long for publish\n", payload_len) ;
    return false ;
  }
  memcpy(m_buff+5,payload, payload_len);
  
  if (writemqtt(&m_client_connection, MQTT_PUBLISH, m_buff, len)){
    if (qos > 0) m_client_connection.set_activity(MqttConnection::Activity::publishing);
    // keep a copy for retries. Set the DUP flag for retries
    m_buff[0] |= FLAG_DUP;
    m_client_connection.set_cache(MQTT_PUBLISH, m_buff, len) ;
    return true ;
  }

  return false ;
}

bool ClientMqttSn::connect(uint8_t gwid, bool will, bool clean, uint16_t keepalive)
{
  MqttGwInfo *gw ;
  m_buff[0] = (will?FLAG_WILL:0) | (clean?FLAG_CLEANSESSION:0) ;
  m_buff[1] = MQTT_PROTOCOL ;
  m_buff[2] = keepalive >> 8 ; // MSB set first
  m_buff[3] = keepalive & 0x00FF ;

#ifndef ARDUINO
  pthread_mutex_lock(&m_mqttlock) ;
#endif
  if (!(gw = get_gateway(gwid))){
#ifndef ARDUINO
    pthread_mutex_unlock(&m_mqttlock) ;
#endif
    EPRINT("Gateway ID unknown") ;
    return false ;
  }
  // Copy connection details
  m_client_connection.set_state(MqttConnection::State::disconnected) ;
  m_client_connection.topics.free_topics() ; // clear all topics
  m_client_connection.set_gwid(gwid) ;
  m_client_connection.set_address(gw->get_address(), m_pDriver->get_address_len()) ;
#ifndef ARDUINO
  pthread_mutex_unlock(&m_mqttlock) ;
#endif
  
  uint8_t len = strlen(m_szclient_id) ;
  if (len > m_pDriver->get_payload_width() - MQTT_CONNECT_HDR_LEN){
    len = m_pDriver->get_payload_width() - MQTT_CONNECT_HDR_LEN ; // Client ID is too long. Truncate
  }
  memcpy(m_buff+4, m_szclient_id, len) ; // do not copy /0 terminator

#if DEBUG
  char addrdbg[(PACKET_DRIVER_MAX_ADDRESS_LEN*2)+1];
  addr_to_straddr(gw->get_address(), addrdbg, m_pDriver->get_address_len()) ;
  DPRINT("Connecting to gateway %d at address %s\n", gwid, addrdbg) ;
#endif
  if (writemqtt(&m_client_connection, MQTT_CONNECT, m_buff, 4+len)){
    // Record the start of the connection
    m_client_connection.set_state(MqttConnection::State::connecting) ;
    if(will)
      m_client_connection.set_activity(MqttConnection::Activity::willtopic) ;
    else
      m_client_connection.set_activity(MqttConnection::Activity::none) ;
    // Hold keep alive info on gateway list and in the connection parameters
    m_client_connection.duration = keepalive ; // keep alive timer for connection
    return true ;
  }

  return false ;
}

bool ClientMqttSn::is_gateway_valid(uint8_t gwid)
{
  for (unsigned int i=0; i < MQTT_MAX_GATEWAYS;i++){
    if (m_gwinfo[i].is_allocated() && m_gwinfo[i].is_active() && m_gwinfo[i].get_gwid() == gwid){
      // Check if it has expired
      return m_gwinfo[i].is_active();
    }
  }
  return false ; // No gateway with this id found
}

void ClientMqttSn::print_gw_table()
{
  for (unsigned int i=0; i < MQTT_MAX_GATEWAYS;i++){
    if (m_gwinfo[i].is_allocated()){
      printf("GWID: %u, Permanent: %s, Active: %s, Advertising: %s, Advertising Duration: %u\n", 
	     m_gwinfo[i].get_gwid(),
	     m_gwinfo[i].is_permanent()?"yes":"no",
	     m_gwinfo[i].is_active()?"yes":"no",
	     m_gwinfo[i].is_advertising()?"yes":"no",
	     m_gwinfo[i].advertising_duration());
    }
  }
}

bool ClientMqttSn::is_disconnected()
{
  return m_client_connection.is_disconnected() ;
}

bool ClientMqttSn::is_connected(uint8_t gwid)
{
  return m_client_connection.is_connected() && 
    (m_client_connection.get_gwid() == gwid) ;
}

bool ClientMqttSn::is_connected()
{
  return m_client_connection.is_connected() ;
}

void ClientMqttSn::set_willtopic(const char *topic, uint8_t qos)
{
  if (topic == NULL){
    m_willtopic[0] = '\0' ; // Clear the topic
    m_willtopicsize = 0;
  }else{
    strncpy(m_willtopic, topic, m_pDriver->get_payload_width() - MQTT_WILLTOPIC_HDR_LEN);
    m_willtopic[m_pDriver->get_payload_width() - MQTT_WILLTOPIC_HDR_LEN] = '\0'; //paranoia
    m_willtopicsize = strlen(m_willtopic) ;
  }
  m_willtopicqos = qos ;
}

#ifndef ARDUINO
void ClientMqttSn::set_willtopic(const wchar_t *topic, uint8_t qos)
{
  if (topic == NULL){
    m_willtopic[0] = '\0' ; // Clear the topic
    m_willtopicsize = 0;
    m_willtopicqos = qos ;
  }else{
    size_t len = wcslen(topic) ;
    if ((uint8_t)len > m_pDriver->get_payload_width() - MQTT_WILLTOPIC_HDR_LEN){
      EPRINT("Will topic too long for payload\n") ;
      return ;
    }

    size_t ret = wchar_to_utf8(topic, m_willtopic, m_pDriver->get_payload_width() - MQTT_WILLTOPIC_HDR_LEN);
    
    m_willtopicsize = ret ;
    m_willtopicqos = qos ;
  }
}
#endif
#ifndef ARDUINO
void ClientMqttSn::set_willmessage(const wchar_t *message)
{
  size_t maxlen = m_pDriver->get_payload_width() - MQTT_WILLMSG_HDR_LEN ;

  if (wcslen(message) > maxlen){
    EPRINT("Will message too long for payload, truncating message\n") ;
  }
  
  wchar_to_utf8(message, (char*)m_willmessage, maxlen) ;
}
#endif
void ClientMqttSn::set_willmessage(const uint8_t *message, uint8_t len)
{
  if (message == NULL || len == 0){
    m_willmessagesize = 0 ;
    return ;
  }
  
  if (len > m_pDriver->get_payload_width() - MQTT_WILLMSG_HDR_LEN)
    return ; //WILL message too long for payload

  memcpy(m_willmessage, message, len) ;

  m_willmessagesize = len ;
}

MqttGwInfo* ClientMqttSn::get_available_gateway()
{
  if (m_client_connection.is_connected()){
    uint8_t gwid = m_client_connection.get_gwid();
    return get_gateway(gwid) ;
  }

  for (unsigned int i=0; i < MQTT_MAX_GATEWAYS;i++){
    if (m_gwinfo[i].is_allocated() && m_gwinfo[i].is_active()){
      // return the first known gateway
      DPRINT("Seaching for gateways found active GW %u\n", m_gwinfo[i].get_gwid());
      return &(m_gwinfo[i]) ;
    }
  }
  return NULL ;
}

MqttGwInfo* ClientMqttSn::get_gateway_address(uint8_t *gwaddress)
{
  for (unsigned int i=0; i < MQTT_MAX_GATEWAYS;i++){
    if (m_gwinfo[i].is_allocated() && m_gwinfo[i].is_active()){
      if (m_gwinfo[i].match(gwaddress)){
	return &(m_gwinfo[i]) ;
      }
    }
  }
  return NULL ;
}

MqttGwInfo* ClientMqttSn::get_gateway(uint8_t gwid)
{
  for (unsigned int i=0; i < MQTT_MAX_GATEWAYS;i++){
    if (m_gwinfo[i].is_allocated() && m_gwinfo[i].is_active() && m_gwinfo[i].get_gwid() == gwid){
      return &(m_gwinfo[i]) ;
    }
  }
  return NULL ;
}

// TO DO: Maybe have a similar function but parameter of gwid
uint8_t *ClientMqttSn::get_gateway_address()
{
  for (unsigned int i=0; i < MQTT_MAX_GATEWAYS;i++){
    if (m_gwinfo[i].is_allocated() && m_gwinfo[i].is_active()){
      // return the first known gateway
      return m_gwinfo[i].get_address() ;
    }
  }
  return NULL ; // No gateways are known
}

bool ClientMqttSn::get_known_gateway(uint8_t *gwid)
{
  if (m_client_connection.is_connected()){
    *gwid = m_client_connection.get_gwid();
    return true ;
  }

  MqttGwInfo *gw = get_available_gateway();

  if (!gw) return false;
  *gwid = gw->get_gwid() ;

  return true ;
}
