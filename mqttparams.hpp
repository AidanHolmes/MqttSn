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

#include "RF24Driver.hpp" // Change header inclusion for different driver

#ifndef __MQTT_PARAMS_H
#define __MQTT_PARAMS_H

// Complier or client code can define the max queue and gateway
// to tune memory usage
#ifndef MQTT_MAX_QUEUE
#define MQTT_MAX_QUEUE 20
#endif
#ifndef MQTT_MAX_GATEWAYS
#define MQTT_MAX_GATEWAYS 5
#endif
#ifndef MQTT_MESSAGES_INFLIGHT
#define MQTT_MESSAGES_INFLIGHT 5
#endif

#define MQTT_PROTOCOL 0x01

#ifndef _BV
#define _BV(x) 1 << x
#endif

#define FLAG_DUP _BV(7)
#define FLAG_RETAIN _BV(4)
#define FLAG_WILL _BV(3)
#define FLAG_CLEANSESSION _BV(2)
#define FLAG_QOS0 0
#define FLAG_QOS1 _BV(5)
#define FLAG_QOS2 _BV(6)
#define FLAG_QOSN1 (_BV(5) | _BV(6))
#define FLAG_NORMAL_TOPIC_ID 0
#define FLAG_DEFINED_TOPIC_ID _BV(0)
#define FLAG_SHORT_TOPIC_NAME _BV(1)

#define MQTT_RETURN_ACCEPTED 0x00
#define MQTT_RETURN_CONGESTION 0x01
#define MQTT_RETURN_INVALID_TOPIC 0x02
#define MQTT_RETURN_NOT_SUPPORTED 0x03
// Unofficial codes - used in callback functions
#define MQTT_RETURN_MSG_FAILURE 0xFF

#define MQTT_ADVERTISE 0x00
#define MQTT_GWINFO 0x02
#define MQTT_CONNECT 0x04
#define MQTT_WILLTOPICREQ 0x06
#define MQTT_WILLMSGREQ 0x08
#define MQTT_REGISTER 0x0A
#define MQTT_PUBLISH 0x0C
#define MQTT_PUBCOMP 0x0E
#define MQTT_PUBREL 0x10
#define MQTT_SUBSCRIBE 0x12
#define MQTT_UNSUBSCRIBE 0x14
#define MQTT_PINGREQ 0x16
#define MQTT_DISCONNECT 0x18
#define MQTT_WILLTOPICUPD 0x1A
#define MQTT_WILLMSGUPD 0x1C
#define MQTT_SEARCHGW 0x01
#define MQTT_CONNACK 0x05
#define MQTT_WILLTOPIC 0x07
#define MQTT_WILLMSG 0x09
#define MQTT_REGACK 0x0B
#define MQTT_PUBACK 0x0D
#define MQTT_PUBREC 0x0F
#define MQTT_SUBACK 0x13
#define MQTT_UNSUBACK 0x15
#define MQTT_PINGRESP 0x17
#define MQTT_WILLTOPICRESP 0x1B
#define MQTT_WILLMSGRESP 0x1D

// MQTT size macros - driver agnostic
#define MQTT_HDR_LEN 2
#define MQTT_HDR_FLAGS_LEN 1
#define MQTT_HDR_TOPICID_LEN 2
#define MQTT_HDR_MSGID_LEN 2
#define MQTT_HDR_PROTOCOLID_LEN 1
#define MQTT_HDR_DURATION_LEN 2

#define MQTT_CONNECT_HDR_LEN (MQTT_HDR_LEN + MQTT_HDR_FLAGS_LEN + MQTT_HDR_PROTOCOLID_LEN + MQTT_HDR_DURATION_LEN)
#define MQTT_REGISTER_HDR_LEN (MQTT_HDR_LEN + MQTT_HDR_TOPICID_LEN + MQTT_HDR_MSGID_LEN)
#define MQTT_WILLTOPIC_HDR_LEN (MQTT_HDR_LEN + MQTT_HDR_FLAGS_LEN)
#define MQTT_PUBLISH_HDR_LEN (MQTT_HDR_LEN + MQTT_HDR_FLAGS_LEN + MQTT_HDR_TOPICID_LEN + MQTT_HDR_MSGID_LEN)
#define MQTT_WILLMSG_HDR_LEN (MQTT_HDR_LEN)
#define MQTT_SUBSCRIBE_HDR_LEN (MQTT_HDR_LEN + MQTT_HDR_FLAGS_LEN + MQTT_HDR_MSGID_LEN)

#ifdef DEBUG
#define DPRINT(x,...) fprintf(stdout,x,##__VA_ARGS__)
#define EPRINT(x,...) fprintf(stderr,x,##__VA_ARGS__)
#else
#define DPRINT(x,...)
#define EPRINT(x,...)
#endif


#endif
