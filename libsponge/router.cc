#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    _routing_table.emplace_back(route_prefix, prefix_length, next_hop, interface_num);
}

// finally not used
uint8_t Router::leading_zero(uint32_t n) {
    if (n == 0) return 32;
    int count = 0;
    if (n <= 0x0000FFFF) { count += 16; n <<= 16; }
    if (n <= 0x00FFFFFF) { count += 8; n <<= 8; }
    if (n <= 0x0FFFFFFF) { count += 4; n <<= 4; }
    if (n <= 0x3FFFFFFF) { count += 2; n <<= 2; }
    if (n <= 0x7FFFFFFF) { count += 1; }
    return count;
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    uint32_t ip_addr = dgram.header().dst;
    uint8_t max_match = 0;
    std::optional<Route> chosen_route;
    for(auto& route : _routing_table) {
        if (route.prefix_length == 0
        || (route.route_prefix ^ ip_addr) >> (32 - route.prefix_length) == 0) {
            if (!chosen_route.has_value() || route.prefix_length > max_match) {
                max_match = route.prefix_length;
                chosen_route = route;
            }
        }
    }

    if (!chosen_route.has_value()) {
        return;
    }

    if (dgram.header().ttl <= 1) {
        return;
    } 

    dgram.header().ttl -= 1;

    size_t interface_num = chosen_route.value().interface_num;
    std::optional<Address> next_hop = chosen_route.value().next_hop;
    if (next_hop.has_value()) 
        interface(interface_num).send_datagram(dgram, next_hop.value());
    else {
        interface(interface_num).send_datagram(dgram, Address::from_ipv4_numeric(ip_addr));
    }
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
