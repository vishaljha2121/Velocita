#include "velocita/v2/jupiter_route_decode.hpp"
#include <iostream>
using namespace velocita::v2::truemarkets;
int main(){const std::array<std::uint8_t,35> bytes{229,23,203,151,122,227,173,42,1,0,0,0,156,100,0,1,64,66,15,0,0,0,0,0,180,153,142,0,0,0,0,0,244,1,20}; auto r=decode_jupiter_direct_route_data(bytes); if(r.status!=JupiterRouteStatus::unsupported)return 1; std::cout<<"PASS: qualified decoder refuses fresh route variant 156\n";}
