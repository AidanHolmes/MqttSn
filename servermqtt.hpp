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

#ifndef __SERVER_MQTTSN_EMBED
#define __SERVER_MQTTSN_EMBED

#include "mqttsnembed.hpp"
#include "mqttconnection.hpp"
#include "mqtttopic.hpp"
#include <time.h>
#include <mosquitto.h>
#include <pthread.h>

class ServerMqttSn : public MqttSnEmbed{
public:
  ServerMqttSn();
  ~ServerMqttSn() ;

  //////////////////////////////////////
  // Setup
  
  // Powers up and configures addresses. Goes into listen
  // mode
  void initialise(uint8_t address_len, uint8_t *broadcast, uint8_t *address) ;

  // Gateways can define how often the advertise is sent
  void set_advertise_interval(uint16_t t) ;
  
  ///////////////////////////////////////
  // Settings
  
  // Set this for gateways. Defaults to zero
  void set_gateway_id(const uint8_t gwid){m_gwid = gwid;}
  uint8_t get_gateway_id(){return m_gwid;}

  // Server call
  // Create a pre-defined topic. 2 options to add wide char or UTF8 string
  bool create_predefined_topic(uint16_t topicid, const char *name) ;
  bool create_predefined_topic(uint16_t topicid, const wchar_t *name) ;
  
  //////////////////////////////////////
  // MQTT messages
  
  // Send an advertise message to broadcast address
  // duration, in sec, until next advertise is broadcast
  // Returns false if connection couldn't be made due to timeout (only applies if using acks on pipes)
  bool advertise(uint16_t duration) ;

  // Ping for use by a gateway to check a client is alive
  // Returns false if client is not connected or ping fails (with ACK)
  bool ping(const char *szclientid) ;

  // Server call
  // Register a topic to a client. Returns false if failed
  bool register_topic(MqttConnection *con, MqttTopic *t);

  // Handles connections to gateways or to clients. Dispatches queued messages
  // Will return false if a queued message cannot be dispatched.
  bool manage_connections() ;

  void lock_mosquitto(){pthread_mutex_lock(&m_mosquittolock) ;}
  void unlock_mosquitto(){pthread_mutex_unlock(&m_mosquittolock) ;}

protected:

  static void gateway_message_callback(struct mosquitto *m,
				       void *data,
				       const struct mosquitto_message message) ;
  
  static void gateway_subscribe_callback(struct mosquitto *m,
					 void *data,
					 int mid,
					 int qoscount,
					 const int *grantedqos);
  
  static void gateway_publish_callback(struct mosquitto *m,
				       void *data,
				       int mid);

  static void gateway_disconnect_callback(struct mosquitto *m,
					  void *data,
					  int res);

  static void gateway_connect_callback(struct mosquitto *m,
				       void *data,
				       int res);


  // Connection state handling for clients
  void connection_watchdog(MqttConnection *p);
  void manage_client_connection(MqttConnection *p);
  void complete_client_connection(MqttConnection *p) ;

  void received_searchgw(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_connect(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_willtopic(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_willmsg(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_pingresp(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_pingreq(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_disconnect(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_register(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_regack(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_publish(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_pubrel(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_subscribe(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_suback(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_unsubscribe(uint8_t *sender_address, uint8_t *data, uint8_t len) ;
  void received_unsuback(uint8_t *sender_address, uint8_t *data, uint8_t len) ;

  // Searches for a client connection using the client ID
  // Only returns connected clients 
  MqttConnection* search_connection(const char *szclientid);
  // Searches all cached connections connected and disconnected
  // Returns NULL if no matches found
  MqttConnection* search_cached_connection(const char *szclientid);
  // Searches for connections by address. Only returns connected
  // clients. Returns NULL if no connected clients can be found
  MqttConnection* search_connection_address(const uint8_t *clientaddr);
  // Searches for connections by address regardless of connection state.
  // Returns NULL if no connections can be found
  MqttConnection* search_cached_connection_address(const uint8_t *clientaddr);
  // Creates a new connection and appends to end of client connection list
  MqttConnection* new_connection();
  // Get the connection for a specified mosquitto connection.
  // Returns NULL if the message id cannot be found.
  MqttConnection* search_mosquitto_id(int mid) ;
  // Removes a connection from the connection cache.
  void delete_connection(const char *szclientid);
  
  // Gateway function to write PUBLISH messages to the MQTT server
  // Requires a connection to extract the registered topic
  // Returns false if the MQTT server cannot be processed
  bool server_publish(MqttConnection *con);

  // Gateway function to send a WILL to the
  // MQTT server
  void send_will(MqttConnection *con) ;

  MqttConnection *m_connection_head ;

  // Gateway connection attributes
  MqttTopicCollection m_predefined_topics ;
  struct mosquitto *m_pmosquitto ;
  time_t m_last_advertised ;
  uint16_t m_advertise_interval ;
  bool m_mosquitto_initialised ;
  uint8_t m_gwid;
  bool m_broker_connected ;
  bool m_register_all ;
  
  pthread_mutex_t m_mosquittolock ;
};


#endif
