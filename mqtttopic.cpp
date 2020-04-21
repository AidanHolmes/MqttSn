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

#include "mqtttopic.hpp"
#include <stdio.h>
#include <string.h>

#ifdef DEBUG
#define DPRINT(x,...) fprintf(stdout,x,##__VA_ARGS__)
#define EPRINT(x,...) fprintf(stderr,x,##__VA_ARGS__)
#else
#define DPRINT(x,...)
#define EPRINT(x,...)
#endif

void MqttTopic::set_topic(uint16_t topic, uint16_t messageid, const char *sztopic)
{
  m_topicid = topic ;
  strncpy(m_sztopic, sztopic, PACKET_DRIVER_MAX_PAYLOAD - MQTT_REGISTER_HDR_LEN) ;
  m_iswildcard = false ;
  for (char *c = m_sztopic; *c ; c++){
    if (*c == '#' || *c == '+') m_iswildcard = true ;
  }  
  m_messageid = messageid ;
  m_registered_at = TIMENOW ;
}

bool MqttTopic::match(const char* sztopic)
{
  const char *p = sztopic ;
  for (char *c = m_sztopic; *c ; c++){
    switch(*c){
    case '+':
      // Skip level
      for ( ; *p && *p != '/'; p++) ;
      if (*p == '/') p++ ;
      break;
    case '#':
      // Assumed to be final wildcard to match remaining
      return true ;      
    default:
      break;
    }
    if (*c != *p) return false ;
  }

  // All of m_sztopic has been parsed
  if (*p) return false ; // Still more characters in sztopic
  return true ; // match
}

void MqttTopic::reset()
{
  m_next = NULL;
  m_prev = NULL;
  m_sztopic[0] = '\0' ;
  m_topicid = 0;
  m_messageid = 0;
  m_acknowledged = false ;
  m_registered_at = 0 ;
  m_timeout = 5 ;
  m_predefined = false ;
  m_iswildcard = false ;
  m_issubscribed = false ;
  m_topicqos = 0;
}

MqttTopicCollection::MqttTopicCollection()
{
  m_topic_iterator = NULL ;
  topics = NULL ;
}

MqttTopicCollection::~MqttTopicCollection()
{
  free_topics() ;
}

MqttTopic* MqttTopicCollection::reg_topic(const char *sztopic, uint16_t messageid)
{
  MqttTopic *p = NULL, *insert_at = NULL ;
  if (!topics){
    // No topics in collection.
    // Create the head topic. No index set
    topics = new MqttTopic(0, messageid, sztopic) ;
    return topics;
  }
  for (p = topics; p; p = p->next()){
    if (strcmp(p->get_topic(), sztopic) == 0){
      // topic exists
      DPRINT("Topic %s already exists for collection\n", sztopic) ;
      return p ;
    }
    insert_at = p ; // Save last valid topic pointer
  }

  p = new MqttTopic(0, messageid, sztopic) ;
  if (p)
    insert_at->link_tail(p);
  return p ;
}

MqttTopic* MqttTopicCollection::complete_topic(uint16_t messageid, uint16_t topicid)
{
  MqttTopic *p = NULL ;
  if (!topics) return p ; // no topics
  for (p = topics; p; p = p->next()){
    if (p->get_message_id() == messageid && !p->is_complete()){
      p->complete(topicid) ;
      return p ;
    }
  }
  // Cannot find an incomplete topic that needs completing
  return p ;
}

// Used for predefined topics although servers capture unique predefined topics in another
// topic collection so these are redundant
MqttTopic* MqttTopicCollection::create_topic(const char *sztopic, uint16_t topicid, bool predefined)
{
  MqttTopic *p = NULL, *insert_at = NULL ;
  uint16_t available_id = 0 ;

  // To Do: Verify that the topic name doesn't contain wildcards for
  // pre-defined topics
  if (!topics){
    // No topics in collection.
    // Create the head topic.
    topics = new MqttTopic(topicid, 0, sztopic) ;
    topics->set_predefined(predefined) ;
    return topics;
  }

  for (p = topics; p; p = p->next()){
    if (p->get_id() == topicid){
      // topic exists
      DPRINT("Topic ID %u already exists for collection\n", topicid) ;
      return NULL ;
    }
    insert_at = p ; // Save last valid topic pointer
  }
  // If collection was to run a long time with creation and deletion of topics then
  // the ID count will overflow! Overflows in 18 hours if requested every second
  // TO DO: Better implementation of ID assignment and improved data structure
  p = new MqttTopic(topicid, 0, sztopic) ;
  p->set_predefined(predefined) ;
  p->complete(topicid) ; // server completes the topic
  insert_at->link_tail(p);
  return p ;
}

// Server call to add a topic. Used for subscriptions
MqttTopic* MqttTopicCollection::add_topic(const char *sztopic, uint16_t messageid)
{
  MqttTopic *p = NULL, *insert_at = NULL ;
  uint16_t available_id = 0 ;
  if (!topics){
    // No topics in collection.
    // Create the head topic. Always index 1
    topics = new MqttTopic(1, messageid, sztopic) ;
    topics->complete(1) ;
    return topics;
  }
  for (p = topics; p; p = p->next()){
    if (strcmp(p->get_topic(), sztopic) == 0){
      // topic exists
      DPRINT("Topic %s already exists for collection\n", sztopic) ;
      return p ;
    }
    // Add 1 to be unique
    if (p->get_id() > available_id) available_id = p->get_id() + 1 ;
    insert_at = p ; // Save last valid topic pointer
  }
  // If collection was to run a long time with creation and deletion of topics then
  // the ID count will overflow! Overflows in 18 hours if requested every second
  // TO DO: Better implementation of ID assignment and improved data structure
  p = new MqttTopic(available_id, messageid, sztopic) ;
  p->complete(available_id) ; // server completes the topic
  insert_at->link_tail(p);
  return p ;
}

bool MqttTopicCollection::del_topic_by_messageid(uint16_t messageid)
{
  MqttTopic *p = NULL ;
  for (p=topics;p;p = p->next()){
    if (p->get_message_id() == messageid){
      if (p->is_head()){
	topics = p->next() ;
      }else{
	p->unlink() ;
      }
      delete p ;
      return true ;
    }
  }
  return false ;
}

bool MqttTopicCollection::del_topic(uint16_t id)
{
  MqttTopic *p = NULL ;
  for (p=topics;p;p = p->next()){
    if (p->get_id() == id){
      if (p->is_head()){
	topics = p->next() ;
      }else{
	p->unlink() ;
      }
      delete p ;
      return true ;
    }
  }
  return false ;
}

void MqttTopicCollection::free_topics()
{
  MqttTopic *p = topics,*delme = NULL ;

  while(p){
    // Cannot unlink the head
    if (!p->is_head()){
      p->unlink() ;
      delme = p;
      p = p->next() ;
      delete delme ;
    }else{
      p = p->next() ;
    }
  }
  if (topics){
    delete topics ;
    topics = NULL ;
  }
  m_topic_iterator = NULL ;
}
 
void MqttTopicCollection::iterate_first_topic()
{
  m_topic_iterator = topics ;
}

MqttTopic* MqttTopicCollection::get_topic(uint16_t topicid)
{
  if (!topics){
    DPRINT("No topics in collection to find from get_topic");
    return NULL ;
  }
  for (MqttTopic *it = topics; it; it = it->next()){
    if (it->get_id() == topicid) return it ;
  }
  return NULL ;
}

MqttTopic* MqttTopicCollection::get_next_topic()
{
  if (!m_topic_iterator) return NULL ;
  m_topic_iterator = m_topic_iterator->next() ;

  return m_topic_iterator ;
}
 
MqttTopic* MqttTopicCollection::get_curr_topic()
{
  return m_topic_iterator ;
}
