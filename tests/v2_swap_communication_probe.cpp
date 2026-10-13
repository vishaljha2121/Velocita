// Qualification helper only. Never a production gateway, signer, or voter.
// Native Aeron plus production Unix signer protocol, synthetic key/state only.
#include "velocita/aeron_runtime.hpp"
#include "velocita/v2/swap_signer_service.hpp"
#include "v2_swap_signer_test_support.hpp"
#include "FragmentAssembler.h"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#ifndef VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE
#error "communication probe requires synthetic fixture compilation"
#endif

namespace {
using namespace swap_signer_test;
constexpr unsigned gateway_uid=29001, signer_uid=29002;
constexpr auto address="100.72.127.102";
using Steady=std::chrono::steady_clock;

std::string hex(std::span<const std::byte> bytes){
    constexpr char digits[]="0123456789abcdef";std::string result;
    for(auto b:bytes){auto v=std::to_integer<unsigned>(b);result+=digits[v>>4];result+=digits[v&15];}return result;
}
std::array<std::byte,33> public_from_hex(const std::string& text){
    require(text.size()==66,"Aeron public key length");std::array<std::byte,33> key{};
    auto nibble=[](char c)->unsigned{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("nonhex public key");};
    for(unsigned n=0;n<33;++n)key[n]=std::byte((nibble(text[2*n])<<4)|nibble(text[2*n+1]));
    return key;
}
swap_signer::Request request(){
    swap_signer::Request r;r.operation=swap_signer::Operation::sign;r.effect=EffectId{1};r.sequence=1;r.minimum_index=10;r.sign_by_ms=20100;
    r.handle_size=handle.size();std::copy(handle.begin(),handle.end(),r.handle.begin());r.payload_size=body.size();std::copy(body_bytes().begin(),body_bytes().end(),r.payload.begin());return r;
}

// Real UDP publications, subscriptions and embedded native media drivers.
// No node identity, authentication credential, Raft command, or venue message.
struct Exchange {
    velocita::AeronRuntime runtime;
    std::shared_ptr<aeron::Subscription> sub;
    std::shared_ptr<aeron::Publication> pub;
    explicit Exchange(const std::filesystem::path& root):runtime((root/"aeron").string()){}
std::string exchange(bool signer,const std::string& nonce,const std::string& own){
    auto channel=[](unsigned port){return std::string("aeron:udp?endpoint=")+address+":"+std::to_string(port);};
    sub=runtime.add_subscription(channel(signer?45491:45492),197731,std::chrono::seconds(5));
    pub=runtime.add_publication(channel(signer?45492:45491),197731,std::chrono::seconds(5));
    std::string received;bool offered=false;
    aeron::FragmentAssembler assembler([&](aeron::concurrent::AtomicBuffer& buffer,aeron::util::index_t offset,aeron::util::index_t length,aeron::Header&){
        require(length>0&&length<128,"Aeron fixture frame bound");
        const std::string value(reinterpret_cast<char*>(buffer.buffer()+offset),static_cast<std::size_t>(length));
        require(value.starts_with(nonce+"|"),"Aeron run nonce matches");
        require(received.empty()||received==value,"Aeron payload stable");received=value;
    });
    const auto deadline=Steady::now()+std::chrono::seconds(10);
    // Both peers acknowledge actual receive, preventing publication offer from
    // being mistaken for a delivered roundtrip. Distinct packet kinds are used.
    bool peer_ack=false,ack_offered=false;
    aeron::FragmentAssembler messages([&](aeron::concurrent::AtomicBuffer& b,aeron::util::index_t o,aeron::util::index_t n,aeron::Header& h){
        require(n>0&&n<128,"Aeron frame bound");
        std::string value(reinterpret_cast<char*>(b.buffer()+o),static_cast<std::size_t>(n));
        if(value==nonce+"|ack")peer_ack=true;else assembler.handler()(b,o,n,h);
    });
    const std::string ack=nonce+"|ack";
    while(Steady::now()<deadline){
        if(!offered)offered=velocita::AeronRuntime::offer(*pub,std::as_bytes(std::span(own.data(),own.size())))>=0;
        sub->poll(messages.handler(),10);
        if(!received.empty()&&!ack_offered)ack_offered=velocita::AeronRuntime::offer(*pub,std::as_bytes(std::span(ack.data(),ack.size())))>=0;
        if(offered&&ack_offered&&peer_ack&&!received.empty())return received;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Aeron nonce roundtrip deadline");
}
};
void wait_socket(const std::filesystem::path& path){
    const auto until=Steady::now()+std::chrono::seconds(4);
    while(Steady::now()<until){if(std::filesystem::is_socket(path))return;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    throw std::runtime_error("synthetic signer socket deadline");
}
void signer(const std::filesystem::path& root,const std::string& nonce){
    require(swap_signer::restrict_process(signer_uid,gateway_uid),"production signer UID restriction");
    Rig r;require(bool(r.key.protected_key),"synthetic fixture key");
    Exchange channel(root);
    {
        swap_signer::Server server;require(server.listen(root/"sign.sock",gateway_uid),"cross-UID protected socket listen");
        require(channel.exchange(true,nonce,nonce+"|"+hex(r.key.public_key))==nonce+"|gateway","Aeron gateway nonce received");
        require(server.serve_once(*r.session,4000)&&r.key.protected_key->signature_attempts()==1,"cross-UID sign once");
        require(server.serve_once(*r.session,4000)&&r.key.protected_key->signature_attempts()==1,"cross-UID retrieve without signing");
        require(!server.serve_once(*r.session,4000)&&r.key.protected_key->signature_attempts()==1,"wrong-server UID sends no request");
        require(server.serve_once(*r.session,4000)&&r.key.protected_key->signature_attempts()==1,"wrong public pin cannot repeat signing");
    }
    swap_signer::Server denied;require(denied.listen(root/"deny.sock",signer_uid),"peer denial fixture listen");
    require(!denied.serve_once(*r.session,4000)&&denied.peer_refusals()==1&&r.key.protected_key->signature_attempts()==1,"wrong gateway UID refused before signing");
}
void gateway(const std::filesystem::path& root,const std::filesystem::path& signer_root,const std::string& nonce){
    Exchange channel(root);const auto peer=channel.exchange(false,nonce,nonce+"|gateway");
    auto public_key=public_from_hex(peer.substr(nonce.size()+1));
    const auto socket=signer_root/"sign.sock";wait_socket(socket);
    swap_signer::Client client(socket,signer_uid,public_key,4000);auto req=request();
    const auto first=client.request(req);require(first.code==swap_signer::ReplyCode::signed_once&&!first.stamp.empty(),"cross-UID signature stamp verified");
    req.operation=swap_signer::Operation::retrieve;const auto second=client.request(req);
    require(second.code==swap_signer::ReplyCode::retrieved&&second.stamp.view()==first.stamp.view(),"cross-UID identical persisted stamp retrieved");
    swap_signer::Client wrong_uid(socket,gateway_uid,public_key,4000);
    require(wrong_uid.request(req).stamp.empty(),"client rejects wrong signer UID");
    auto wrong_key=public_key;wrong_key.back()^=std::byte{1};swap_signer::Client wrong_pin(socket,signer_uid,wrong_key,4000);
    require(wrong_pin.request(req).stamp.empty(),"client rejects wrong signer public key");
    wait_socket(signer_root/"deny.sock");swap_signer::Client denied(signer_root/"deny.sock",signer_uid,public_key,4000);
    require(denied.request(req).stamp.empty(),"server rejects unauthorized gateway UID");
}
}
int main(int argc,char** argv){
    try{
        require(argc==4,"usage: communication-probe signer|gateway RUN_ID NONCE");
        const std::string role=argv[1],run=argv[2],nonce=argv[3];
        require((role=="signer"||role=="gateway")&&run.size()==16&&run.find_first_not_of("0123456789abcdef")==std::string::npos,"fixed role and run identifier");
        require(nonce.size()==32&&nonce.find_first_not_of("0123456789abcdef")==std::string::npos,"fixed hexadecimal nonce");
        const bool is_signer=role=="signer";const auto uid=is_signer?signer_uid:gateway_uid;const auto gid=is_signer?signer_uid:gateway_uid;
        require(::getuid()==uid&&::geteuid()==uid&&::getgid()==gid&&::getegid()==gid,"permanent role UID/GID exact");
        std::vector<gid_t> groups(static_cast<std::size_t>(::getgroups(0,nullptr)));require(::getgroups(groups.size(),groups.data())>=0,"groups readable");
        require(std::all_of(groups.begin(),groups.end(),[&](auto group){return group==gid||(!is_signer&&group==signer_uid);}),"no unapproved supplementary groups");
        if(!is_signer)require(std::find(groups.begin(),groups.end(),signer_uid)!=groups.end(),"gateway has socket group");
        const auto root=std::filesystem::path("/run/velocita-comm-"+run+"-"+role);
        const auto signer_root=std::filesystem::path("/run/velocita-comm-"+run+"-signer");
        require(::setenv("HOME",root.c_str(),1)==0,"synthetic fixture HOME confined");
        if(is_signer)signer(root,nonce);else gateway(root,signer_root,nonce);
        std::cout<<"{\"role\":\""<<role<<"\",\"run\":\""<<run<<"\",\"nonce\":\""<<nonce<<"\",\"uid\":"<<uid<<",\"gid\":"<<gid<<",\"aeron_local_transport_passed\":true,\"native_signer_ipc_passed\":true,\"authenticated_voter_reads\":false,\"live_admitted\":false}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"communication probe failed: "<<e.what()<<'\n';return 1;}
}
