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

#ifndef __CLIENT_MQTTSN_EMBED
#define __CLIENT_MQTTSN_EMBED

#include "mqttsnembed.hpp"
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

// Callback - bool success, uint8_t return_code, uint8_t gwid
#define MQTTCONCALLBACK(fn) void (*fn)(bool, uint8_t, uint8_t)
// Callback - bool sleeping, uint16_t sleep_duration, uint8_t gwid
#define MQTTDISCALLBACK(fn) void (*fn)(bool, uint16_t, uint8_t)
// Callback - bool active, uint8_t gwid
#define MQTTGWCALLBACK(fn) void (*fn)(bool, uint8_t)
// Callback for publish - bool success, uint8_t return_code, uint16_t topic_id, uint16_t message_id, uint8_t gwid
#define MQTTPUBCALLBACK(fn) void (*fn)(bool, uint8_t, uint16_t, uint16_t, uint8_t)
// Callback for registration - bool success, uint8_t return_code, uint16_t topic_id, uint16_t message_id, uint8_t gwid
#define MQTTREGCALLBACK(fn) void (*fn)(bool, uint8_t, uint16_t, uint16_t, uint8_t)

class ClientMqttSn : public MqttSnEmbed{
public:
  ClientMqttSn();
  ~ClientMqttSn() ;

  //////////////////////////////////////
  // Setup
  
  // Powers up and configures addresses. Goes into listen
  // mode
  void initialise(uint8_t address_len, uint8_t *broadcast, uint8_t *address) ;

  ///////////////////////////////////////
  // Settings
  
  // Set a client ID. This can be up to MAX_MQTT_CLIENTID.
  // String must be null terminated.
  // Will throw MqttOutOfRange if string is over MAX_MQTT_CLIENTID
  void set_client_id(const char *szclientid) ;
  const char* get_client_id() ; // Returns pointer to client identfier

  //////////////////////////////////////
  // MQTT messages
  
  // Send a request for a gateway. Gateway responds with gwinfo data
  // Returns false if connection couldn't be made due to timeout (only applies if using acks on pipes)
  bool searchgw(uint8_t radius) ;

  // Throws MqttOutOfRange
  // Returns false if connection couldn't be made due to timeout or
  // no known gateway to connect to. Timeouts only detected if using ACKS on pipes
  bool connect(uint8_t gwid, bool will, bool clean, uint16_t duration) ; 
  //bool connect_expired(uint16_t retry_time) ; // has the connect request expired?
  //bool connect_max_retry(bool reset); // Has exceeded retry counter?
  bool is_connected(uint8_t gwid) ; // are we connected to this gw?
  bool is_connected() ; // are we connected to any gateway?
  bool is_disconnected() ; // are we disconnected to any gateway?
#ifndef ARDUINO
  void set_willtopic(const wchar_t *topic, uint8_t qos) ;
  void set_willmessage(const wchar_t *message) ;
#endif
  void set_willtopic(const char *topic, uint8_t qos) ;
  void set_willmessage(const uint8_t *message, uint8_t len) ;
  // TO DO: Protocol also allows update of will messages during connection to server
  
  // Disconnect. Optional sleep duration can be set. If zero then
  // no sleep timer will be set
  // Returns false if disconnect cannot be sent (ACK enabled)
  bool disconnect(uint16_t sleep_duration = 0) ;

  // Publishes a -1 QoS message which do not require a connection.
  // This call publishes short topics (2 bytes).
  // Requires a known gateway (requires manual specification of GW)
  bool publish_noqos(uint8_t gwid,
		     const char* sztopic,
		     const uint8_t *payload,
		     uint8_t payload_len,
		     bool retain) ;

  // Publish a -1 QoS message. Topic ID must relate to a pre-defined
  // topic.
  bool publish_noqos(uint8_t gwid,
		     uint16_t topicid,
		     uint8_t topictype,
		     const uint8_t *payload,
		     uint8_t payload_len,
		     bool retain);

  // Publish for connected clients. Doesn't support -1 QoS
  // Sets a 2 letter short topic
  // Returns false if cannot send message or client disconnected
  bool publish(uint8_t qos,
	       const char* sztopic,
	       const uint8_t *payload,
	       uint8_t payload_len,
	       bool retain);
  
  // Publish for connected clients. Doesn't support -1 QoS
  // Supports topic ids on the connection or permanent on server
  // Returns false if message cannot be sent or client not connected
  bool publish(uint8_t qos,
	       uint16_t topicid,
	       uint16_t topictype,
	       const uint8_t *payload,
	       uint8_t payload_len,
	       bool retain);
  
  // Ping for use by a client to check a gateway is alive
  // Returns false if gateway is unknown or transmit failed (with ACK)
  bool ping(uint8_t gw);

  // Register a topic with the server. Returns the topic id to use
  // when publishing messages. Returns 0 if the register fails.
  #ifndef ARDUINO
  uint16_t register_topic(const wchar_t *topic) ;
  #endif
  uint16_t register_topic(const char *topic) ;

  // Handles connections to gateways or to clients. Dispatches queued messages
  // Will return false if a queued message cannot be dispatched.
  bool manage_connections() ;

  // Returns true if known gateway exists. Sets gwid to known gateway handle
  // returns false if no known gateways exist
  bool get_known_gateway(uint8_t *gwid) ;

  // Check gateway handle. If gateway has been lost then this will return false,
  // otherwise it will be true
  bool is_gateway_valid(uint8_t gwid);

  void print_gw_table() ;

  bool add_gateway(uint8_t *gateway_address, uint8_t gwid, uint16_t ad_duration, bool perm=false);

  void set_callback_connected(MQTTCONCALLBACK(fn)){m_fnconnected = fn;}
  void set_callback_disconnected(MQTTDISCALLBACK(fn)){m_fndisconnected = fn;}
  void set_callback_gwinfo(MQTTGWCALLBACK(fn)){m_fngatewayinfo = fn ;}
  void set_callback_published(MQTTPUBCALLBACK(fn)){m_fnpublished = fn ;}
  void set_callback_register(MQTTREGCALLBACK(fn)){m_fnregister = fn ;}
protected:

  // Connection state handling for clients
  bool manage_gw_connection() ;

  virtual void received_advertised(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_gwinfo(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_connack(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_willtopicreq(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_willmsgreq(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_pingresp(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_pingreq(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_disconnect(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_register(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_regack(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_puback(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_pubrec(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_pubcomp(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_subscribe(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_suback(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_unsubscribe(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  virtual void received_unsuback(uint8_t *sender_address, uint8_t *data, uint8_t len) ;

  MqttGwInfo* get_gateway(uint8_t gwid);
  MqttGwInfo* get_gateway_address(uint8_t *gwaddress) ;
  MqttGwInfo* get_available_gateway();
  uint8_t *get_gateway_address();
  bool update_gateway(uint8_t *gateway_address, uint8_t gwid, uint16_t ad_duration);
  bool del_gateway(uint8_t gwid) ;

  char m_szclient_id[PACKET_DRIVER_MAX_PAYLOAD - MQTT_CONNECT_HDR_LEN +1] ; // Client ID
  MqttGwInfo m_gwinfo[MQTT_MAX_GATEWAYS] ;

  // Connection attributes for a client
  MqttConnection m_client_connection ;
  char m_willtopic[PACKET_DRIVER_MAX_PAYLOAD - MQTT_WILLTOPIC_HDR_LEN +1] ;
  uint8_t m_willmessage[PACKET_DRIVER_MAX_PAYLOAD - MQTT_WILLMSG_HDR_LEN] ;
  size_t m_willtopicsize ;
  size_t m_willmessagesize ;
  uint8_t m_willtopicqos ;
  uint16_t m_sleep_duration ;
  
  // Callback functions
  MQTTCONCALLBACK(m_fnconnected);
  MQTTDISCALLBACK(m_fndisconnected) ;
  MQTTGWCALLBACK(m_fngatewayinfo) ;
  MQTTPUBCALLBACK(m_fnpublished) ;
  MQTTREGCALLBACK(m_fnregister) ;
};


#endif
