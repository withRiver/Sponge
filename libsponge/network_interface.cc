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

    if (_arp_table.count(next_hop_ip)) {
        EthernetFrame frame;
        frame.header() = { _arp_table[next_hop_ip].eth_addr, 
                           _ethernet_address, 
                           EthernetHeader::TYPE_IPv4};
        frame.payload() = dgram.serialize();
        _frames_out.push(frame);
    } else {
        if (!_waiting_arp_response_ip_addr.count(next_hop_ip)) {
            ARPMessage arp_request;
            arp_request.opcode = ARPMessage::OPCODE_REQUEST;
            arp_request.sender_ethernet_address = _ethernet_address;
            arp_request.sender_ip_address = _ip_address.ipv4_numeric();
            arp_request.target_ethernet_address = {};
            arp_request.target_ip_address = next_hop_ip;

            EthernetFrame frame;
            frame.header() = { ETHERNET_BROADCAST,
                               _ethernet_address, 
                               EthernetHeader::TYPE_ARP};
            frame.payload() = arp_request.serialize();
            _frames_out.push(frame);

            _waiting_arp_response_ip_addr[next_hop_ip] = _ARP_RESPONSE_DEFAULT_TTL;
        }

        _waiting_arp_internet_datagrams.push_back({next_hop, dgram});
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) {
        return nullopt;
    }

    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram datagram;
        if (datagram.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        }
        return datagram;
    } 
    else if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_msg;
        if (arp_msg.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        }
        const uint32_t &src_ip_addr = arp_msg.sender_ip_address;
        const uint32_t &dst_ip_addr = arp_msg.target_ip_address;
        const EthernetAddress &src_eth_addr = arp_msg.sender_ethernet_address;
        const EthernetAddress &dst_eth_addr = arp_msg.target_ethernet_address;       

        bool is_valid_arp_request = 
            arp_msg.opcode == ARPMessage::OPCODE_REQUEST && dst_ip_addr == _ip_address.ipv4_numeric();
        bool is_valid_arp_response = 
            arp_msg.opcode == ARPMessage::OPCODE_REPLY && dst_eth_addr == _ethernet_address;

        if (is_valid_arp_request) {
            ARPMessage arp_reply;
            arp_reply.opcode = ARPMessage::OPCODE_REPLY;
            arp_reply.sender_ethernet_address = _ethernet_address;
            arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
            arp_reply.target_ethernet_address = src_eth_addr;
            arp_reply.target_ip_address = src_ip_addr;

            EthernetFrame out_frame;
            out_frame.header() = {src_eth_addr, _ethernet_address, EthernetHeader::TYPE_ARP};
            out_frame.payload() = arp_reply.serialize();
            _frames_out.push(out_frame);
        }

        if (is_valid_arp_request || is_valid_arp_response) {
            _arp_table[src_ip_addr] = {src_eth_addr, _ARP_ENTRY_DEFAULT_TTL};
            for (auto iter = _waiting_arp_internet_datagrams.begin()
            ; iter != _waiting_arp_internet_datagrams.end()
            ;) {
                if (iter->first.ipv4_numeric() == src_ip_addr) {
                    send_datagram(iter->second, iter->first);
                    iter = _waiting_arp_internet_datagrams.erase(iter);
                } else {
                    ++iter;
                }
            }
            _waiting_arp_response_ip_addr.erase(src_ip_addr);
        }
    }
    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    for (auto iter = _arp_table.begin(); iter != _arp_table.end(); ) {
        if (iter->second.ttl <= ms_since_last_tick) iter = _arp_table.erase(iter);
        else {
            iter->second.ttl -= ms_since_last_tick;
            ++iter;
        }
    } 

    for (auto iter = _waiting_arp_response_ip_addr.begin(); iter != _waiting_arp_response_ip_addr.end(); /* nop */) {
            // 如果 ARP 等待队列中的 ARP 请求过期
            if (iter->second <= ms_since_last_tick) {
                // 重新发送 ARP 请求
                ARPMessage arp_request;
                arp_request.opcode = ARPMessage::OPCODE_REQUEST;
                arp_request.sender_ethernet_address = _ethernet_address;
                arp_request.sender_ip_address = _ip_address.ipv4_numeric();
                arp_request.target_ethernet_address = {/* 这里应该置为空*/};
                arp_request.target_ip_address = iter->first;

                EthernetFrame eth_frame;
                eth_frame.header() = {/* dst  */ ETHERNET_BROADCAST,
                                    /* src  */ _ethernet_address,
                                    /* type */ EthernetHeader::TYPE_ARP};
                eth_frame.payload() = arp_request.serialize();
                _frames_out.push(eth_frame);

                iter->second = _ARP_RESPONSE_DEFAULT_TTL;
            } else {
                iter->second -= ms_since_last_tick;
                ++iter;
            }
        }    
}
