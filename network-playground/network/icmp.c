/**
 * @file icmp.c
 * @provides icmpRecv, icmpHandleRequest, and icmpHandleReply
 *
 */
/* Author: Drew Vanderwiel, Jiayi Xin  */
/* Class:  COSC4300         */
/* Date:   10/29/2016       */

#include <xinu.h>
#include <network.h>
#include <ether.h>
#include <icmp.h>
#include <arp.h>

/* Global ICMP table definition */
struct icmpTblEntry icmpTbl[ICMP_TBL_LEN];


/**
 * Initialize the ICMP table
 * @return OK for success, SYSERR for syntax error
 */
syscall icmpInit(void)
{
    int i, j;
    
    /* Initialize the ICMP table entries */
    for (i = 0; i < ICMP_TBL_LEN; i++)
    {
        icmpTbl[i].pid = ICMP_TBL_INIT_PID;
        icmpTbl[i].sema = semcreate(1);
        icmpTbl[i].flag = ICMP_ENTRY_INVALID;
        icmpTbl[i].ttl = 0;
        icmpTbl[i].seqNum = 0;
        
        for (j = 0; j < IPv4_ADDR_LEN; j++)
            icmpTbl[i].ipAddr[j] = 0;
    }
    
    return OK;
}


/**
 * Receive and filter ICMP Packets
 * @param ipPkt   received IPv4 packet
 * @param srcAddr Sender MAC address
 * @return OK for success, SYSERR for syntax error
 */
syscall icmpRecv(struct ipgram *ipPkt, uchar *srcAddr)
{
    int i, eqFlag;
    struct icmpPkt *pkt;
    ushort origChksum, calChksum;
     
    if (ipPkt == NULL || srcAddr == NULL)
        return SYSERR;
    
    // TODO start after the ip packets options, at its data
    pkt = (struct icmpPkt *) ipPkt->opts;
    
    
    // Screen out packets with bad ICMP headers
    if ( ntohs(ipPkt->len) < (ICMP_HEADER_LEN + IPv4_HDR_LEN) ||
        (pkt->type != ICMP_ECHO_RQST_T &&
         pkt->type != ICMP_ECHO_RPLY_T) ||
         pkt->code != ICMP_ECHO_RQST_C)
        return SYSERR;
    
    
    // Screen out packets with a bad ICMP checksums
    origChksum = pkt->chksum;
    pkt->chksum = 0;
    calChksum = checksum((void *) pkt, ICMP_HEADER_LEN);
    
    
    if (calChksum != origChksum)
        return SYSERR;
    
    // Handle the ICMP packet
    if ( pkt->type == ICMP_ECHO_RQST_T)
    {
        return icmpHandleRequest(ipPkt, srcAddr);
    }
    else if ( pkt->type == ICMP_ECHO_RPLY_T )
    {
        return icmpHandleReply(ipPkt);
    }
    
    return OK;
}


/**
 * Handle ICMP Echo request Packets
 * @param ipPkt   received IPv4 packet
 * @param srcAddr Sender MAC address
 * @return OK for success, SYSERR for syntax error
 */
syscall icmpHandleRequest(struct ipgram *ipPkt, uchar *srcAddr)
{
    int i;
    struct ethergram    *egram = NULL;
    struct ipgram       *ipP = NULL;
    struct icmpPkt      *icmpPRecvd = NULL;
    struct icmpPkt      *icmpP = NULL;
    ulong               icmpDataLen, pktSize = 0;
    char                *buf = NULL; 
    
    /* Debug: uncomment to test ping times */
    //sleep(10);
    
    if (ntohs(ipPkt->len) < ETHER_MINPAYLOAD)
    {
        pktSize = (ulong) (ETH_HEADER_LEN + ETHER_MINPAYLOAD);
    }
    else if (ntohs(ipPkt->len) < ETH_MTU)
    {
        pktSize = (ulong) (ETH_HEADER_LEN + ntohs(ipPkt->len));
    }
    // The ip packet is too long to send over ethernet
    else
    {
        return SYSERR;
    }
    
    
    buf = (char *) malloc(pktSize);
    
    if (buf == NULL)
        return SYSERR;
    
    // Zero out the packet buffer
    bzero(buf, pktSize);
    
    
    /* Set up Ethergram header */
    egram = (struct ethergram *) buf;
    
    for (i = 0; i < ETH_ADDR_LEN; i++)
        egram->dst[i] = srcAddr[i];
    
    for (i = 0; i < ETH_ADDR_LEN; i++)
        egram->src[i] = arp.hwAddr[i];
    
    egram->type = htons(ETYPE_IPv4);
    
    
    /* Set up IPv4 header */
    ipP = (struct ipgram *) &egram->data;
    
    // Version 5, IHL size 5 * (4 byte words) = 20
    ipP->ver_ihl = 0x45;
    ipP->tos = IPv4_TOS_ROUTINE;
    
    icmpDataLen = ntohs(ipPkt->len) - IPv4_HDR_LEN - ICMP_HEADER_LEN;
    ipP->len = htons(IPv4_HDR_LEN + ICMP_HEADER_LEN + icmpDataLen);
    
    // Get id from request packet
    ipP->id = ipPkt->id;
    ipP->flags_froff = 0;
    ipP->ttl = IPv4_TTL;
    ipP->proto = IPv4_PROTO_ICMP;
    ipP->chksum = 0;
    
    // Source protocol addr (ours)
    for (i = 0; i < IPv4_ADDR_LEN; i++)
        ipP->src[i] = arp.ipAddr[i];
    
    // Dest protocol addr (requester's)
    for (i = 0; i < IPv4_ADDR_LEN; i++)
        ipP->dst[i] = ipPkt->src[i];
    
    // Calculate the IP header checksum
    ipP->chksum = checksum((void *) ipP, IPv4_HDR_LEN);
    
    
    /* Set up ICMP header */
    icmpPRecvd = (struct icmpPkt *) &ipPkt->opts;
    icmpP = (struct icmpPkt *) &ipP->opts;
    
    icmpP->type = ICMP_ECHO_RPLY_T;
    icmpP->code = ICMP_ECHO_RPLY_C;
    icmpP->chksum = 0;
    icmpP->id = icmpPRecvd->id;
    icmpP->seqNum = icmpPRecvd->seqNum;
    
    // Copy the received data into the data field of the ICMP reply
    for (i = 0; i < icmpDataLen; i++)
    {
        icmpP->data[i] = icmpPRecvd->data[i];
    }
    
    // Calculate the ICMP header checksum
    icmpP->chksum = checksum((void *) icmpP, ICMP_HEADER_LEN);
    
    
    /* Send packet */
    write(ETH0, (uchar *)buf, ICMP_PKTSIZE);
    
    free((void *) buf);
    return OK;
}


/**
 * Handle ICMP Echo reply Packets
 * @param ipPkt   received IPv4 packet
 * @return OK for success, SYSERR for syntax error
 */
syscall icmpHandleReply(struct ipgram *ipPkt)
{
    int i, ipEqual = 0;
    ushort id;
    ushort seqNum;
    struct icmpPkt      *icmpPRecvd = NULL;
    message msg;
    
    icmpPRecvd = (struct icmpPkt *) &ipPkt->opts;
    id = ntohs(icmpPRecvd->id);
    seqNum = ntohs(icmpPRecvd->seqNum);
    
    
    // Make sure the id is within the table's range
    if (id < ICMP_TBL_LEN)
    {
        // Grab semaphore
        wait(icmpTbl[id].sema);
        
        // Is the ICMP table entry valid and does
        // it match the reply we got?
        if ( ICMP_RQST_SENT     == icmpTbl[id].flag &&
             ICMP_TBL_INIT_PID  != icmpTbl[id].pid &&
             seqNum             == icmpTbl[id].seqNum )
        {
            // Check if the IP address matches
            ipEqual = 1;
            for (i = 0; i < IPv4_ADDR_LEN; i++)
            {
                if (icmpTbl[id].ipAddr[i] != ipPkt->src[i])
                {
                    ipEqual = 0;
                    break;
                }
            }
            
            // Set the flag and the ttl field, since we got an ICMP reply
            if (ipEqual)
            {
                icmpTbl[id].flag = ICMP_GOT_RPLY;
                icmpTbl[id].ttl = ipPkt->ttl;
                uchar4ToUlong(ipPkt->data, &icmpTbl[id].recvdTime, BIG_ENDIAN);
                icmpTbl[id].recvdTime = ntohs(icmpTbl[id].recvdTime);
                
                printf("recvdTime %d\n", icmpTbl[id].recvdTime);
                icmpTbl[id].recvdBytes = ntohs(ipPkt->len);
            }
        }
        // Give back the semaphore
        signal(icmpTbl[id].sema);
    }
    
    
    // If this is true, we have received a good ICMP reply
    if (ipEqual)
    {
        // Send a message to the waiting process
        msg = (message) ntohs(ipPkt->len);
        send(icmpTbl[id].pid, msg);
    }
    
    return OK;
}


/**
 * Converts a ulong to an array of 4 uchars
 * @param buf   uchar array
 * @param num   the number to convert
 * @param flag  flag for indicating whether the value should be stored
 *              little endian or big endian
 * @return OK for success, SYSERR for syntax error
 */
syscall ulongToUchar4(uchar *buf, ulong num, int flag)
{
    if (buf == NULL)
        return SYSERR;
    
    if (flag == BIG_ENDIAN)
    {
        buf[3] = (uchar) ((num & 0xFF000000) >> 24);
        buf[2] = (uchar) ((num & 0x00FF0000) >> 16);
        buf[1] = (uchar) ((num & 0x0000FF00) >> 8);
        buf[0] = (uchar) (num & 0x000000FF);
    }
    else if (flag == LITTLE_ENDIAN)
    {
        buf[0] = (uchar) ((num & 0xFF000000) >> 24);
        buf[1] = (uchar) ((num & 0x00FF0000) >> 16);
        buf[2] = (uchar) ((num & 0x0000FF00) >> 8);
        buf[3] = (uchar) (num & 0x000000FF);
    }
    else
    {
        return SYSERR;
    }
    
    return OK;
}


/**
 * Converts an array of 4 uchars to a ulong
 * @param buf   uchar array to convert
 * @param num   a pointer to the ulong
 * @param flag  flag for indicating whether the value should be stored
 *              little endian or big endian
 * @return OK for success, SYSERR for syntax error
 */
syscall uchar4ToUlong(uchar *buf, ulong *num, int flag)
{
    if (buf == NULL || num == NULL)
        return SYSERR;
    
    if (flag == BIG_ENDIAN)
    {
        *num = (buf[3] << 24) | (buf[2] << 16) |
               (buf[1] << 8)  |  buf[0];
    }
    else if (flag == LITTLE_ENDIAN)
    {
        *num = (buf[0] << 24) | (buf[1] << 16) |
               (buf[2] << 8)  |  buf[3];
    }
    else
    {
        return SYSERR;
    }
    
    return OK;
}

