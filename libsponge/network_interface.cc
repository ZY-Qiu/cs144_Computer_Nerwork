#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    EthernetAddress mac = map(next_hop_ip);

    if (ethernetEqual(mac, {0, 0, 0, 0, 0, 0})) {
        // queue the datagram
        // use ip address to search the map
        auto it = this->_arp_pending_dgram.find(next_hop_ip);
        if (it == this->_arp_pending_dgram.end()) {
            queue<InternetDatagram> q;
            q.push(dgram);
            this->_arp_pending_dgram.insert(make_pair(next_hop_ip, q));
        } else {
            it->second.push(dgram);
        }
        // broadcast an arp request, but if another packet with the same treget ip sent ARP in 5 seconds, wait
        auto iterator = this->_arp_pending_ip.find(next_hop_ip);
        if (iterator != this->_arp_pending_ip.end()) {
            // another packet already sent the broadcast
            if (this->_time - iterator->second > ARPTTL) {
                // last sent time over 5 seconds, resend ARP and queue the sending
                iterator->second = this->_time;
                send_arp(next_hop_ip);
            }
            // else wait
        } else {
            // queue the sending and send the ARP
            this->_arp_pending_ip.insert(make_pair(next_hop_ip, this->_time));
            send_arp(next_hop_ip);
        }
    } else {
        // create the ethernet frame, send the packet
        EthernetFrame frame;
        frame.header().type = EthernetHeader::TYPE_IPv4;
        frame.header().src = this->_ethernet_address;
        frame.header().dst = mac;
        //  set the payload to be the serialized datagram
        frame.payload() = std::move(dgram.serialize());
        this->_frames_out.push(frame);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // ignore any frame not destined for this NIC
    if (frame.header().dst == this->_ethernet_address || frame.header().dst == ETHERNET_BROADCAST) {
        if (frame.header().type == EthernetHeader::TYPE_IPv4) {
            InternetDatagram dgram;
            if (dgram.parse(frame.payload()) == ParseResult::NoError) {
                return dgram;
            }
        }
        if (frame.header().type == EthernetHeader::TYPE_ARP) {
            ARPMessage amsg;
            if (amsg.parse(frame.payload()) == ParseResult::NoError) {
                // remember the mapping between the sender’s IP address and Ethernet address for 30 seconds
                uint32_t ip = amsg.sender_ip_address;
                EthernetAddress mac = amsg.sender_ethernet_address;
                arp_map_item ami(mac, this->_time);
                auto it = this->_map.find(ip);
                if (it == this->_map.end()) {
                    this->_map.insert(make_pair(ip, ami));
                } else {
                    it->second = ami;
                }
                // remove any ARP pending for the ip in this ARP packet, and send out the queued datagram
                auto ite = this->_arp_pending_ip.find(ip);
                // has packet queued for this ip address
                if (ite != this->_arp_pending_ip.end()) {
                    // erase the ip ARP pending
                    this->_arp_pending_ip.erase(ite);
                    // send out the datagram queued here
                    auto iter = this->_arp_pending_dgram.find(ip);
                    if (iter != this->_arp_pending_dgram.end()) {
                        while (!iter->second.empty()) {
                            // send_datagram(iter->second.front(), ip);
                            // create the ethernet frame, send the packet
                            EthernetFrame f;
                            f.header().type = EthernetHeader::TYPE_IPv4;
                            f.header().src = this->_ethernet_address;
                            f.header().dst = mac;  // sender ethernet address
                            //  set the payload to be the serialized datagram
                            f.payload() = std::move(iter->second.front().serialize());
                            this->_frames_out.push(f);
                            iter->second.pop();
                        }
                        // sent out all queued datagram, erase the entry in the map
                        this->_arp_pending_dgram.erase(iter);
                    }
                }
                // if this is an ARP request for the exact ip address of this NIC, send back an ARP reply
                if (amsg.opcode == ARPMessage::OPCODE_REQUEST &&
                    amsg.target_ip_address == this->_ip_address.ipv4_numeric()) {
                    ARPMessage reply;
                    reply.opcode = ARPMessage::OPCODE_REPLY;
                    reply.sender_ethernet_address = this->_ethernet_address;
                    reply.sender_ip_address = this->_ip_address.ipv4_numeric();
                    reply.target_ethernet_address = amsg.sender_ethernet_address;
                    reply.target_ip_address = amsg.sender_ip_address;
                    EthernetFrame arpFrame;
                    arpFrame.header().src = this->_ethernet_address;
                    arpFrame.header().dst = amsg.sender_ethernet_address;
                    arpFrame.header().type = EthernetHeader::TYPE_ARP;
                    arpFrame.payload() = std::move(reply.serialize());
                    // send back
                    this->_frames_out.push(arpFrame);
                }
            }
        }
    }
    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    this->_time += ms_since_last_tick;
    // Already expire any IP-to-Ethernet mappings that have expired in the map function.
    // May triger retransmission of ARP request if have not received reply in 5 seconds;
}

EthernetAddress NetworkInterface::map(const uint32_t &ip_address) {
    auto it = this->_map.find(ip_address);
    if (it == this->_map.end())
        return {0, 0, 0, 0, 0, 0};
    else {
        if (this->_time - it->second.time_stamp > TTL) {
            this->_map.erase(it);
            return {0, 0, 0, 0, 0, 0};
        }
        return it->second.mac_addr;
    }
}

void NetworkInterface::send_arp(const uint32_t &ip_address) {
    ARPMessage msg;  // serve as the payload of an Ethernet frame when serialized
    msg.opcode = ARPMessage::OPCODE_REQUEST;
    msg.sender_ethernet_address = this->_ethernet_address;
    msg.sender_ip_address = this->_ip_address.ipv4_numeric();
    msg.target_ethernet_address = {0, 0, 0, 0, 0, 0};  // set to 0 because unknown
    msg.target_ip_address = ip_address;

    EthernetFrame arp_frame;
    arp_frame.header().type = EthernetHeader::TYPE_ARP;
    arp_frame.header().src = this->_ethernet_address;
    arp_frame.header().dst = ETHERNET_BROADCAST;
    arp_frame.payload() = std::move(msg.serialize());

    // send the arp_frame
    this->_frames_out.push(arp_frame);
}