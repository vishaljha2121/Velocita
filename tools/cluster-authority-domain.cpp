#include "velocita/v2/digest.hpp"
#include "velocita/v2/node_config.hpp"
#include "velocita/v2/static_identity.hpp"
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

namespace velocita::cluster_domain {
using namespace v2;
struct Domain{Digest256 manifest,authority;std::int32_t node{};};
std::optional<Domain> domain(std::string_view text,std::string& why){
    const auto parsed=parse_node_config(text);
    if(!parsed.config){why="line "+std::to_string(parsed.error.line)+" key "+parsed.error.key+" code "+std::to_string(static_cast<int>(parsed.error.code));return {};}
    std::array<std::byte,kMaxStaticIdentityBytes> encoded{};
    const auto result=encode_static_identity(parsed.config->static_identity(),encoded);if(result.error!=StaticIdentityCodecError::none)return {};
    VerifiedStaticIdentity verified;
    if(VerifiedStaticIdentity::decode(std::span(encoded).first(result.bytes_written),verified)!=StaticIdentityCodecError::none)return {};
    return Domain{verified.manifest_digest(),verified.authority_domain(),verified.value().local_node_id};
}
int run(int argc,char** argv){
    if(argc<2){std::cerr<<"REFUSED one or more node configs required\n";return 1;}
    std::optional<Domain> first;
    for(int n=1;n<argc;++n){
        std::ifstream in(argv[n]);std::stringstream text;text<<in.rdbuf();
        std::string why="unreadable or unencodable";const auto d=in?domain(text.str(),why):std::nullopt;
        if(!d){std::cerr<<"REFUSED config "<<argv[n]<<": "<<why<<'\n';return 1;}
        if(first && (d->manifest!=first->manifest || d->authority!=first->authority)){std::cerr<<"REFUSED configs disagree on the cluster identity\n";return 1;}
        if(!first)first=d;
        std::cout<<"node="<<d->node<<" manifest_digest="<<to_hex(d->manifest)<<" authority_domain="<<to_hex(d->authority)<<'\n';
    }
    return 0;
}
}
#ifndef VELOCITA_CLUSTER_DOMAIN_TEST
int main(int argc,char** argv){return velocita::cluster_domain::run(argc,argv);}
#endif
