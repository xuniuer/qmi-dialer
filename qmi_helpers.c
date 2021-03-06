#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <endian.h>
#include <stdlib.h>
#include <unistd.h>

#include "qmi_dialer.h"
#include "qmi_hdrs.h"
#include "qmi_shared.h"
#include "qmi_device.h"

void create_qmi_request(uint8_t *buf, uint8_t service, uint8_t client_id, 
        uint16_t transaction_id, uint16_t message_id){
    qmux_hdr_t *qmux_hdr = (qmux_hdr_t*) buf;

    if(service == QMI_SERVICE_CTL)
        //-1 is for removing the preamble
        qmux_hdr->length = htole16(sizeof(qmux_hdr_t) - 1 +
                sizeof(qmi_hdr_ctl_t));
    else
        qmux_hdr->length = htole16(sizeof(qmux_hdr_t) - 1 +
                sizeof(qmi_hdr_gen_t));

    qmux_hdr->type = QMUX_IF_TYPE;
    //Messages are send from the control point
    qmux_hdr->control_flags = 0;
    //Which service I want to request something from. Remember that CTL is 0
    qmux_hdr->service_type = service;
    qmux_hdr->client_id = client_id;

    //Can I somehow do this more elegant?
    if(service == QMI_SERVICE_CTL){
        qmi_hdr_ctl_t *qmi_hdr = (qmi_hdr_ctl_t*) (qmux_hdr+1);
        //Always sends request (message type 0, only flag)
        qmi_hdr->control_flags = 0;
        //Internal transaction sequence number (one message exchange)
        qmi_hdr->transaction_id = transaction_id;
        //Type of message
        qmi_hdr->message_id = htole16(message_id);
        qmi_hdr->length = 0;
    } else {
        qmi_hdr_gen_t *qmi_hdr = (qmi_hdr_gen_t*) (qmux_hdr+1);
        qmi_hdr->control_flags = 0;
        qmi_hdr->transaction_id = htole16(transaction_id);
        qmi_hdr->message_id = htole16(message_id);
        qmi_hdr->length = 0;
    }
}

//Assume only one TLV parameter for now
void add_tlv(uint8_t *buf, uint8_t type, uint16_t length, void *value){
    qmux_hdr_t *qmux_hdr = (qmux_hdr_t*) buf;
    qmi_tlv_t *tlv;

    assert(le16toh(qmux_hdr->length) + length + sizeof(qmi_tlv_t) <
            QMI_DEFAULT_BUF_SIZE);

    //+1 is to compensate or the mark, which is now part of message
    tlv = (qmi_tlv_t*) (buf + le16toh(qmux_hdr->length) + 1);
    tlv->type = type;
    tlv->length = htole16(length);
    memcpy(tlv + 1, value, length);

    //Update the length of thw qmux and qmi headers
    qmux_hdr->length += htole16(sizeof(qmi_tlv_t) + length);

    //Updte QMI service length
    if(qmux_hdr->service_type == QMI_SERVICE_CTL){
        qmi_hdr_ctl_t *qmi_hdr = (qmi_hdr_ctl_t*) (qmux_hdr+1);
        qmi_hdr->length += htole16(sizeof(qmi_tlv_t) + length);
    } else {
        qmi_hdr_gen_t *qmi_hdr = (qmi_hdr_gen_t*) (qmux_hdr+1);
        qmi_hdr->length += htole16(sizeof(qmi_tlv_t) + length);
    }
}

void parse_qmi(uint8_t *buf){
    int i, j;
    uint8_t *tlv_val = NULL;
    qmux_hdr_t *qmux_hdr = (qmux_hdr_t*) buf;
    qmi_tlv_t *tlv = NULL;
    uint16_t tlv_length = 0;

    QMID_DEBUG_PRINT(stderr, "Complete message: ");
    //When I call this function, a messages is either ready to be sent or has
    //been received. All values are in little endian
    for(i=0; i < le16toh(qmux_hdr->length); i++)
        fprintf(stderr, "%.2x:", buf[i]);

    //I need the last byte, since I have added the marker to the qmux header
    //(and this byte is not included in length)
    fprintf(stderr, "%.2x\n", buf[i]);

    QMID_DEBUG_PRINT(stderr, "QMUX:\n");
    QMID_DEBUG_PRINT(stderr, "\tlength: %u\n", le16toh(qmux_hdr->length));
    QMID_DEBUG_PRINT(stderr, "\tflags: 0x%.2x\n", qmux_hdr->control_flags);
    QMID_DEBUG_PRINT(stderr, "\tservice: 0x%.2x\n", qmux_hdr->service_type);
    QMID_DEBUG_PRINT(stderr, "\tclient id: %u\n", qmux_hdr->client_id);

    if(qmux_hdr->service_type == QMI_SERVICE_CTL){
        qmi_hdr_ctl_t *qmi_hdr = (qmi_hdr_ctl_t*) (qmux_hdr+1);
        QMID_DEBUG_PRINT(stderr, "QMI (control):\n");
        QMID_DEBUG_PRINT(stderr, "\tflags: %u\n", qmi_hdr->control_flags >> 1);
        QMID_DEBUG_PRINT(stderr, "\ttransaction id: %u\n", qmi_hdr->transaction_id);
        QMID_DEBUG_PRINT(stderr, "\tmessage type: 0x%.2x\n", le16toh(qmi_hdr->message_id));
        QMID_DEBUG_PRINT(stderr, "\tlength: %u %x\n", le16toh(qmi_hdr->length), le16toh(qmi_hdr->length));
        tlv = (qmi_tlv_t *) (qmi_hdr+1);
        tlv_length = le16toh(qmi_hdr->length);
    } else {
        qmi_hdr_gen_t *qmi_hdr = (qmi_hdr_gen_t*) (qmux_hdr+1);
        QMID_DEBUG_PRINT(stderr, "QMI (service):\n");
        QMID_DEBUG_PRINT(stderr, "\tflags: %u\n", qmi_hdr->control_flags >> 1);
        QMID_DEBUG_PRINT(stderr, "\ttransaction id: %u\n", le16toh(qmi_hdr->transaction_id));
        QMID_DEBUG_PRINT(stderr, "\tmessage type: 0x%.2x\n", le16toh(qmi_hdr->message_id));
        QMID_DEBUG_PRINT(stderr, "\tlength: %u\n", le16toh(qmi_hdr->length));
        tlv = (qmi_tlv_t *) (qmi_hdr+1);
        tlv_length = le16toh(qmi_hdr->length);
    }

    i=0;
    while(i<tlv_length){
        tlv_val = (uint8_t*) (tlv+1);
        QMID_DEBUG_PRINT(stderr, "TLV:\n");
        QMID_DEBUG_PRINT(stderr, "\ttype: 0x%.2x\n", tlv->type);
        QMID_DEBUG_PRINT(stderr, "\tlen: %u\n", le16toh(tlv->length));
        QMID_DEBUG_PRINT(stderr, "\tvalue: ");
        
        for(j=0; j<le16toh(tlv->length)-1; j++)
            fprintf(stderr, "%.2x:", tlv_val[j]);
        fprintf(stderr, "%.2x", tlv_val[j]);

        fprintf(stderr, "\n");
        i += sizeof(qmi_tlv_t) + le16toh(tlv->length);

        if(i==tlv_length)
            break;
        else
            tlv = (qmi_tlv_t*) (((uint8_t*) (tlv+1)) + le16toh(tlv->length));
    }
}

ssize_t qmi_helpers_write(int32_t qmi_fd, uint8_t *buf, ssize_t len){
    return write(qmi_fd, buf, len);
}

int qmi_helpers_set_link(char *ifname, uint8_t up){
    char *ip_link_fmt = "ip link set dev";
    //22 is strlen(ip link set dev X down\0"
    char ip_link_cmd[22+IFNAMSIZ];
    
    if(up)
        snprintf(ip_link_cmd, sizeof(ip_link_cmd), "%s %s up",
                 ip_link_fmt, ifname);
    else
        snprintf(ip_link_cmd, sizeof(ip_link_cmd), "%s %s down",
                 ip_link_fmt, ifname);

    //if(qmid_verbose_logging >= QMID_LOG_LEVEL_1)
    //    fprintf(stderr, "Will run command %s\n", ip_link_cmd);

    return system(ip_link_cmd);
}
