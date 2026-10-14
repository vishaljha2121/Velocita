#include "velocita/v2/swap_runtime_https.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <iostream>
#include <unistd.h>
using namespace velocita::v2;
namespace {
Digest256 hash(std::string_view s){return sha256_digest(std::as_bytes(std::span(s.data(),s.size())));}
struct Clock:SwapFinalizedReadClock{std::uint64_t value{101};std::uint64_t now_ms()noexcept override{return value;}};
}
int main(int argc,char**argv){
    if(argc!=5)return 2;const std::string_view mode=argv[4];Clock clock;
    if(mode.starts_with("rpc")){
        auto port=SwapRuntimeFinalizedHttps::local_fixture(argv[1],argv[2],500);if(!port)return 3;
        std::string body="{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getTransaction\",\"params\":[\"1GMkH3brNXiNNs1tiFZHu4yZSRrzJwxi5wB9bHFtMinfCXNnR1adh8Vo8NTheK4evneedH4qmvjeqcBBNAefgS\",{\"encoding\":\"base64\",\"commitment\":\"finalized\",\"maxSupportedTransactionVersion\":0}]}";
        if(mode=="rpc-method")body.replace(body.find("getTransaction"),14,"sendTransaction");
        if(mode=="rpc-commitment")body.replace(body.find("finalized"),9,"confirmed");
        if(mode=="rpc-signature")body.replace(body.find("1GMk"),4,"0GMk");
        if(mode=="rpc-id")body.replace(body.find("\"id\":1"),6,"\"id\":0");
        if(mode=="rpc-extra")body.insert(body.size()-1,",\"extra\":true");
        if(mode.starts_with("rpc-account"))body="{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getAccountInfo\",\"params\":[\"G6EBADumzmaYZCPZ6Jfvh2r8Xx3hcSxVLs4f5Zc4y7Xq\",{\"encoding\":\"base64\",\"commitment\":\"finalized\"}]}";
        if(mode=="rpc-account-commitment")body.replace(body.find("finalized"),9,"confirmed");
        if(mode=="rpc-account-encoding")body.replace(body.find("\"base64\""),8,"\"jsonParsed\"");
        if(mode=="rpc-account-key")body.replace(body.find("G6EB"),4,"0GEB");
        const auto result=port->request(body,mode=="rpc-timeout"?501:500);
        std::cout<<"complete="<<result.transport_complete<<" status="<<result.http_status<<" size="<<result.size<<" provider="<<!port->provider_identity().is_zero()<<'\n';return 0;
    }
    SwapRuntimeBinding b;b.allocation={hash("cluster"),hash("catalog"),hash("custody"),AccountId{2},InstrumentId{2},PolicyId{901},PolicyVersion{1},RiskPolicyVersion{1}};
    b.authority_domain=hash("authority");b.account_session={hash("account-assets"),hash("session")};b.owner_wallet.fill(std::byte{7});
    const auto x=*parse_digest256("6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296");b.signer_public_key[0]=std::byte{2};std::copy(x.bytes.begin(),x.bytes.end(),b.signer_public_key.begin()+1);
    constexpr std::string_view socket="/fixture/signer.sock";std::copy(socket.begin(),socket.end(),b.socket.begin());b.socket_size=socket.size();
    b.gateway_uid=::getuid();b.signer_uid=b.gateway_uid+1;b.gateway_caller=CallerId{22};b.signer_caller=CallerId{23};b.ipc_timeout_ms=500;
    SwapRuntimeCode code{};auto credentials=SwapGatewayCredentials::load({b,hash("fixture-token"),1000},SwapInheritedSecretFd(std::stoi(argv[3])),100,code);if(!credentials)return 4;
    SwapSubmissionIdentity id;id.effect=EffectId{1};id.payload=hash("payload");id.handle_size=3;id.handle[0]=std::byte{'a'};id.handle[1]=std::byte{'b'};id.handle[2]=std::byte{'c'};id.handle_digest=sha256_digest(std::span(id.handle).first(3));
    truemarkets::SwapRestRequest request;constexpr std::string_view path="/v1/gateway/orders/abc/execute",body="{\"auth_type\":\"api_key\",\"signatures\":[\"fixture\"]}";
    std::copy(path.begin(),path.end(),request.path.begin());request.path_size=path.size();std::copy(body.begin(),body.end(),request.body.begin());request.body_size=body.size();id.execution_request=hash(body);
    if(mode=="wrong-path")request.path[19]='X';
    if(mode=="large-body")request.body_size=1025;
    if(mode=="large-path")request.path_size=161;
    if(mode=="large-handle")id.handle_size=129;
    if(mode=="bad-hash")id.execution_request=hash("other");
    auto port=SwapRuntimeExecutionHttps::local_fixture(*credentials,clock,id,request,argv[1],argv[2],500);if(!port)return 5;
    auto sent=id;if(mode=="wrong-identity")sent.effect=EffectId{2};
    std::string sent_body(body);if(mode=="wrong-body")sent_body.back()=' ';
    if(mode=="expired")clock.value=1000;
    port->execute(sent,std::as_bytes(std::span(sent_body.data(),sent_body.size())));
    port->execute(id,std::as_bytes(std::span(body.data(),body.size())));
    const auto&r=port->last_result();std::cout<<"complete="<<(r.status==transport::SwapHttpsStatus::complete)<<" local="<<(r.status==transport::SwapHttpsStatus::local_refusal)<<" status="<<r.http_status<<" used="<<port->used()<<'\n';return 0;
}
