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

#include "mqttsnembed.hpp"

#include <string.h>
#include <stdio.h>
#ifndef ARDUINO
 #include <wchar.h>
#endif
#include <stdlib.h>
#include <locale.h>

MqttSnEmbed::MqttSnEmbed()
{
  m_queue_head = 0 ;

  m_Tretry = 1; // sec
  m_Nretry = 5 ; // attempts

  m_pDriver = NULL ;
}

MqttSnEmbed::~MqttSnEmbed()
{

}

bool MqttSnEmbed::create_predefined_topic(uint16_t topicid, const char *name)
{
  if ((uint8_t)strlen(name) > m_pDriver->get_payload_width() - MQTT_REGISTER_HDR_LEN){
    EPRINT("Pre-defined topic %s too long\n", name) ;
    return false ;
  }
  return m_predefined_topics.create_topic(name, topicid) != NULL ;
}

#ifndef ARDUINO
bool MqttSnEmbed::create_predefined_topic(uint16_t topicid, const wchar_t *name)
{
  char sztopic[PACKET_DRIVER_MAX_PAYLOAD - MQTT_REGISTER_HDR_LEN] ;
  
  wchar_to_utf8(name,
		sztopic,
		m_pDriver->get_payload_width() - MQTT_REGISTER_HDR_LEN);
  
  return m_predefined_topics.create_topic(sztopic, topicid) != NULL ;
}
#endif

void MqttSnEmbed::set_retry_attributes(uint16_t Tretry, uint16_t Nretry)
{
  m_Tretry = Tretry ;
  m_Nretry = Tretry ;
}

bool MqttSnEmbed::m_fn_packet_received(void *pContext, uint8_t *sender_addr, uint8_t *packet)
{
  // Read the MQTT-SN payload
  uint8_t length = packet[0] ;
  uint8_t messageid = packet[1] ;
  uint8_t *mqtt_payload = packet+MQTT_HDR_LEN ;

  // Queue the message and exit
  ((MqttSnEmbed *)(pContext))->queue_received(sender_addr, messageid, mqtt_payload, length-MQTT_HDR_LEN) ;
  return true ;
}


bool MqttSnEmbed::initialise(uint8_t address_len, uint8_t *broadcast, uint8_t *address)
{
  // Assertion errors - driver and GPIO are not set so stop initialisation
  if (!m_pDriver){
    EPRINT("Driver not set\n") ;
    return false;
  }

#ifndef ARDUINO
  pthread_mutexattr_t attr ;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) ;
  if (pthread_mutex_init(&m_mqttlock, &attr) != 0){
    EPRINT("MQTT mutex creation failed\n") ;
    return false;
  }
#endif
  
  m_pDriver->set_callback_context(this) ;
  m_pDriver->set_data_received_callback(&MqttSnEmbed::m_fn_packet_received) ;

  if (address_len > PACKET_DRIVER_MAX_ADDRESS_LEN){
    EPRINT("Address too long") ;
    address_len = PACKET_DRIVER_MAX_ADDRESS_LEN; // fix to max length, but not really fixing the programming error
  }
  
  if (!m_pDriver->initialise(address, broadcast, address_len)){
    EPRINT("Failed to initialise driver\n") ;
    return false ;
  }
  return true ;
}

void MqttSnEmbed::shutdown()
{
  if (!m_pDriver->shutdown())
    EPRINT("GPIO error, cannot shutdown\n") ;

}

void MqttSnEmbed::queue_received(const uint8_t *addr,
				uint8_t messageid,
				const uint8_t *data,
				uint8_t len)
{
  // Len is too long, could be invalid or corrupt data packet
  // This function has to keep in mind that any old packet could be picked up
  if (len > PACKET_DRIVER_MAX_PAYLOAD) return ;
  if (messageid < MQTT_WILLMSGRESP) return ;
#ifndef ARDUINO
  pthread_mutex_lock(&m_mqttlock) ;
#endif
  m_queue_head++ ;
  if (m_queue_head >= MQTT_MAX_QUEUE) m_queue_head = 0 ;

  // Regardless of what's in the queue, either overwrite
  // an old entry or set a new one. 
  m_queue[m_queue_head].set = true ;
  m_queue[m_queue_head].messageid = messageid ;
  memcpy((void*)m_queue[m_queue_head].address, addr, m_pDriver->get_address_len()) ;
  if (len > 0 && data != NULL)
    memcpy((void*)m_queue[m_queue_head].message_data, data, len) ;
  m_queue[m_queue_head].message_len = len ;

#ifndef ARDUINO
  pthread_mutex_unlock(&m_mqttlock) ;
#endif
}

bool MqttSnEmbed::dispatch_queue()
{
  uint8_t queue_ptr = m_queue_head ;

#ifndef ARDUINO
  pthread_mutex_lock(&m_mqttlock) ;
#endif

  do{ 
    if (m_queue[queue_ptr].set){
      switch(m_queue[queue_ptr].messageid){
      case MQTT_ADVERTISE:
	// Gateway message received
	received_advertised((uint8_t*)m_queue[queue_ptr].address,
			    (uint8_t*)m_queue[queue_ptr].message_data,
			    m_queue[queue_ptr].message_len) ;
	break;
      case MQTT_SEARCHGW:
	// Message from client to gateway
	received_searchgw((uint8_t*)m_queue[queue_ptr].address,
			  (uint8_t*)m_queue[queue_ptr].message_data,
			  m_queue[queue_ptr].message_len) ;
	break;
      case MQTT_GWINFO:
	// Sent by gateways, although clients can also respond (not implemented)
	received_gwinfo((uint8_t*)m_queue[queue_ptr].address,
			(uint8_t*)m_queue[queue_ptr].message_data,
			m_queue[queue_ptr].message_len) ;
	
	break ;
      case MQTT_CONNECT:
	received_connect((uint8_t*)m_queue[queue_ptr].address,
			 (uint8_t*)m_queue[queue_ptr].message_data,
			 m_queue[queue_ptr].message_len) ;

	break ;
      case MQTT_CONNACK:
	received_connack((uint8_t*)m_queue[queue_ptr].address,
			 (uint8_t*)m_queue[queue_ptr].message_data,
			 m_queue[queue_ptr].message_len) ;

	break ;
      case MQTT_WILLTOPICREQ:
	received_willtopicreq((uint8_t*)m_queue[queue_ptr].address,
			      (uint8_t*)m_queue[queue_ptr].message_data,
			      m_queue[queue_ptr].message_len) ;

	break ;
      case MQTT_WILLTOPIC:
	received_willtopic((uint8_t*)m_queue[queue_ptr].address,
			   (uint8_t*)m_queue[queue_ptr].message_data,
			   m_queue[queue_ptr].message_len) ;

	break ;
      case MQTT_WILLMSGREQ:
	received_willmsgreq((uint8_t*)m_queue[queue_ptr].address,
			    (uint8_t*)m_queue[queue_ptr].message_data,
			    m_queue[queue_ptr].message_len) ;

	break;
      case MQTT_WILLMSG:
	received_willmsg((uint8_t*)m_queue[queue_ptr].address,
			 (uint8_t*)m_queue[queue_ptr].message_data,
			 m_queue[queue_ptr].message_len) ;

	break ;
      case MQTT_PINGREQ:
	received_pingreq((uint8_t*)m_queue[queue_ptr].address,
			 (uint8_t*)m_queue[queue_ptr].message_data,
			 m_queue[queue_ptr].message_len) ;
	break ;
      case MQTT_PINGRESP:
	received_pingresp((uint8_t*)m_queue[queue_ptr].address,
			  (uint8_t*)m_queue[queue_ptr].message_data,
			  m_queue[queue_ptr].message_len) ;
	break ;
      case MQTT_DISCONNECT:
	received_disconnect((uint8_t*)m_queue[queue_ptr].address,
			    (uint8_t*)m_queue[queue_ptr].message_data,
			    m_queue[queue_ptr].message_len) ;
	break;
      case MQTT_REGISTER:
	received_register((uint8_t*)m_queue[queue_ptr].address,
			  (uint8_t*)m_queue[queue_ptr].message_data,
			  m_queue[queue_ptr].message_len) ;
	break;
      case MQTT_REGACK:
	received_regack((uint8_t*)m_queue[queue_ptr].address,
			(uint8_t*)m_queue[queue_ptr].message_data,
			m_queue[queue_ptr].message_len) ;
	break ;
      case MQTT_PUBLISH:
	received_publish((uint8_t*)m_queue[queue_ptr].address,
			 (uint8_t*)m_queue[queue_ptr].message_data,
			 m_queue[queue_ptr].message_len) ;
	break ;
      case MQTT_PUBACK:
	received_puback((uint8_t*)m_queue[queue_ptr].address,
			(uint8_t*)m_queue[queue_ptr].message_data,
			m_queue[queue_ptr].message_len) ;
	break ;
      case MQTT_PUBREC:
	received_pubrec((uint8_t*)m_queue[queue_ptr].address,
			(uint8_t*)m_queue[queue_ptr].message_data,
			m_queue[queue_ptr].message_len) ;
	break ;
      case MQTT_PUBREL:
	received_pubrel((uint8_t*)m_queue[queue_ptr].address,
			(uint8_t*)m_queue[queue_ptr].message_data,
			m_queue[queue_ptr].message_len) ;
	break ;
      case MQTT_PUBCOMP:
	received_pubcomp((uint8_t*)m_queue[queue_ptr].address,
			 (uint8_t*)m_queue[queue_ptr].message_data,
			 m_queue[queue_ptr].message_len) ;
	break ;
      case MQTT_SUBSCRIBE:
	received_subscribe((uint8_t*)m_queue[queue_ptr].address,
			   (uint8_t*)m_queue[queue_ptr].message_data,
			   m_queue[queue_ptr].message_len) ;
	break ;
      case MQTT_SUBACK:
	received_suback((uint8_t*)m_queue[queue_ptr].address,
			(uint8_t*)m_queue[queue_ptr].message_data,
			m_queue[queue_ptr].message_len) ;
	break ;
      case MQTT_UNSUBSCRIBE:
	received_unsubscribe((uint8_t*)m_queue[queue_ptr].address,
			     (uint8_t*)m_queue[queue_ptr].message_data,
			     m_queue[queue_ptr].message_len) ;
	break ;
      case MQTT_UNSUBACK:
	received_unsuback((uint8_t*)m_queue[queue_ptr].address,
			  (uint8_t*)m_queue[queue_ptr].message_data,
			  m_queue[queue_ptr].message_len) ;
	break ;

      default:
	// Not expected message.
	// This is not a 1.2 MQTT message
	received_unknown(m_queue[queue_ptr].messageid,
			 (uint8_t*)m_queue[queue_ptr].address,
			 (uint8_t*)m_queue[queue_ptr].message_data,
			 m_queue[queue_ptr].message_len) ;
      }
    }

    m_queue[queue_ptr].set = false ;
    queue_ptr++;
    if (queue_ptr >= MQTT_MAX_QUEUE) queue_ptr = 0 ;
  }while(queue_ptr != m_queue_head);
#ifndef ARDUINO
    pthread_mutex_unlock(&m_mqttlock) ;
#endif
  return true ;
}

bool MqttSnEmbed::writemqtt(MqttConnection *con,
			   uint8_t messageid,
			   const uint8_t *buff, uint8_t len)
{
  return addrwritemqtt(con->get_address(),messageid,buff,len);
}

bool MqttSnEmbed::addrwritemqtt(const uint8_t *address,
			       uint8_t messageid,
			       const uint8_t *buff,
			       uint8_t len)
{
  uint8_t send_buff[PACKET_DRIVER_MAX_PAYLOAD] ;
  // includes the length field and message type
  uint8_t payload_len = len+MQTT_HDR_LEN;

  send_buff[0] = payload_len ;
  send_buff[1] = messageid ;
  if (buff != NULL && len > 0)
    memcpy(send_buff+MQTT_HDR_LEN, buff, len) ;

  bool ret = m_pDriver->send(address, send_buff, payload_len) ;
  return ret;
}

#ifndef ARDUINO
size_t MqttSnEmbed::wchar_to_utf8(const wchar_t *wstr, char *outstr, const size_t maxbytes)
{
  size_t len = wcslen(wstr) ;
  if (len > maxbytes){
    EPRINT("Conversion to UTF8 too long\n");
    return 0;
  }

  char *curlocale = setlocale(LC_CTYPE, NULL);
  
  if (!setlocale(LC_CTYPE, "en_GB.UTF-8")){
    EPRINT("Cannot set UTF local to en_GB\n") ;
    return 0 ;
  }
    
  size_t ret = wcstombs(outstr, wstr, maxbytes) ;

  setlocale(LC_CTYPE, curlocale) ; // reset locale

  if (ret < 0){
    EPRINT("Failed to convert wide string to UTF8\n") ;
  }
  
  return ret ;
}
#endif
#ifndef ARDUINO
size_t utf8_to_wchar(const char *str, wchar_t *outstr, const size_t maxbytes)
{
  char *curloc = setlocale(LC_CTYPE, NULL);
  size_t ret = 0;

  if (!setlocale(LC_CTYPE, "en_GB.UTF-8")){
    EPRINT("Cannot set UTF8 locale en_GB\n") ;
    return 0;
  }

  ret = mbstowcs(outstr, str, maxbytes) ;

  setlocale(LC_CTYPE, curloc) ; // reset locale
  
  if (ret < 0){
    EPRINT("Failed to convert utf8 to wide string\n") ;
  }
  
  return ret ;  
}

#endif

