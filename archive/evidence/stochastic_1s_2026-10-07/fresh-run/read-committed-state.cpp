// Bounded diagnostic: only request_state, never submit or request_authority.
#include "velocita/aeron_runtime.hpp"
#include "velocita/v2/aeron_node_transport.hpp"
#include "velocita/v2/gateway_link.hpp"
#include "velocita/v2/node_files.hpp"
#include <chrono>
#include <iostream>
#include <map>
#include <thread>
#include <unistd.h>
using namespace velocita::v2;
Tick now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main() {
 try {
  auto secret=load_secret_set("/run/velocita-secrets/caller-13.key");
  if(!secret.secrets) throw std::runtime_error("key unavailable");
  auto epoch=advance_session_epoch_file("/state/gateway.session");
  if(epoch.error!=SessionEpochError::none) throw std::runtime_error("session unavailable");
  std::array<std::byte,16> cluster{},process{};
  std::string hex="c8a6a0a78fbf49a099b222124a5d557f";
  for(std::size_t i=0;i<16;++i) cluster[i]=std::byte(std::stoul(hex.substr(i*2,2),nullptr,16));
  if(getentropy(process.data(),process.size())) throw std::runtime_error("entropy unavailable");
  std::map<std::int32_t,Endpoint> nodes{{1,{"100.72.127.102",44301}},{2,{"100.84.151.116",44302}},{3,{"100.85.36.22",44303}}};
  velocita::AeronRuntime aeron("/dev/shm/velocita-state-diagnostic");
  AeronClientTransport transport(aeron,nodes,{"100.72.127.102",44413});
  std::vector<ClientRoute> routes;
  for(int id=1;id<=3;++id) {
   auto ingress=AuthKey::derive(secret.secrets->current,{AuthRole::ingress,13,id},cluster);
   auto response=AuthKey::derive(secret.secrets->current,{AuthRole::response,id,13},cluster);
   if(!ingress||!response) throw std::runtime_error("route refused");
   std::vector<AuthKey> responses;responses.push_back(std::move(*response));
   routes.push_back({id,std::move(*ingress),std::move(responses)});
  }
  GatewayLink link(epoch.epoch,process,std::move(routes),transport);
  if(!link.request_state(now_ms(),15000)) throw std::runtime_error("read refused");
  for(;;) {
   auto outcome=link.pump(now_ms());
   if(outcome.status==LinkStatus::pending) {std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
   if(outcome.status!=LinkStatus::done) throw std::runtime_error("read did not complete");
   const auto& state=link.state();
   std::cout<<"applied_index="<<outcome.reading.applied_index<<" orders="<<state.order_count<<" effects="<<state.outbox_effect_count<<'\n';
   for(std::size_t i=0;i<state.order_count;++i) {auto& x=state.orders[i];std::cout<<"order id="<<x.cluster_order_id.value()<<" effect="<<x.effect_id.value()<<" state="<<int(x.state)<<" evidence_version="<<x.last_evidence_state_version<<'\n';}
   for(std::size_t i=0;i<state.outbox_effect_count;++i) {auto& x=state.outbox_effects[i];std::cout<<"effect id="<<x.effect_id.value()<<" order="<<x.cluster_order_id.value()<<" state="<<int(x.state)<<" operation="<<int(x.operation)<<'\n';}
   for(std::size_t i=0;i<state.risk_account_count;++i) {auto& x=state.risk_accounts[i];std::cout<<"risk account="<<x.account_id.value()<<" position="<<x.position_quantity.atoms()<<" reserved="<<x.reserved_quantity.atoms()<<" margin="<<x.reserved_margin.atoms()<<'\n';}
   return 0;
  }
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
