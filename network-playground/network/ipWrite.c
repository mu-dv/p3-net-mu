/**
 * @file ipWrite.c
 * @provides ipWrite
 *
 */
/* Author: Drew Vanderwiel, Jiayi Xin  */
/* Class:  COSC4300         */
/* Date:   11/19/2016       */

#include <xinu.h>
#include <network.h>
#include <ether.h>
#include <arp.h>


/**
 * Send an IPv4 packet
 * @param payload  pointer to the raw payload
 * @param dataLen  Length of the payload in bytes
 * @param proto    Protocol of IPv4 service
 * @return OK for success, SYSERR for syntax error
 */
syscall ipWrite(void *payload, ushort dataLen, uchar proto, uchar *ipAddr)
{
    int i;
    struct ipPack ipPkg;
    uchar  *dstHwAddr;
    
    if (payload == NULL || ipAddr == NULL)
        return SYSERR;
    
    /*
    The ipWrite function should take as parameters a pointer to the payload, 
    a payload length, an IPv4 type field, and a destination IP address. 
    This function builds the IPv4 header, and passes the packet down to the 
    netWrite function with a destination MAC address.
    */
    
    if (SYSERR == arpResolve(ipAddr, dstHwAddr))
        return SYSERR;
    
    // Version 5, IHL size 5 * (4 byte words) = 20
    ipPkg.ipHdr.ver_ihl = 0x45;
    ipPkg.ipHdr.tos = IPv4_TOS_ROUTINE;
    ipPkg.ipHdr.len = htons(IPv4_HDR_LEN + dataLen);
    ipPkg.ipHdr.id = 0; // **** TODO: Dynamically get an ID depending on protocol *****
    ipPkg.ipHdr.flags_froff = 0;
    ipPkg.ipHdr.ttl = IPv4_TTL;
    ipPkg.ipHdr.proto = proto;
    ipPkg.ipHdr.chksum = 0x0000;
    
     // Source protocol addr (ours)
    for (i = 0; i < IP_ADDR_LEN; i++)
        ipPkg.ipHdr.src[i] = net.ipAddr[i];
    
    // Dest protocol addr (requester's)
    for (i = 0; i < IP_ADDR_LEN; i++)
        ipPkg.ipHdr.dst[i] = ipAddr[i];
    
    // Unnecessary? Nah
    ipPkg.ipHdr.chksum = checksum((void *) &ipPkg.ipHdr, IPv4_HDR_LEN);
    
    ipPkg.payload = (uchar *) payload;
    ipPkg.dataLen = dataLen;
    
    return netWrite(&ipPkg, dstHwAddr);
}
